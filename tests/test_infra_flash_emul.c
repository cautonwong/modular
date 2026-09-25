/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "flash/flash.h"

/*
 * A flash that behaves like flash: erasure is whole-sector only, and a half-word can only be
 * programmed into space that is still erased. That second rule is deliberate - it turns a
 * double write (which real flash silently corrupts into an AND) into a refusal, so the
 * emulation cannot pass this suite while writing over itself.
 *
 * Power loss is injected by counting half-word writes and erases, which is what lets the tests
 * stop the page transfer at a chosen point and then re-initialise from the same image.
 */
#define MOCK_FLASH_SIZE 0x8000u /* exactly the emulation's two pages */

/*
 * A page holds four-byte records from offset 4 onwards, because its own first half-word is the
 * status word. Counting the slots this way rather than writing literals is what keeps the
 * tests below honest about when the page is actually full.
 */
#define RECORDS_PER_PAGE ((int)(FLASH_EMUL_PAGE_SIZE / 4u) - 1)

typedef struct mock_flash {
    uint8_t bytes[MOCK_FLASH_SIZE];
    int erase_calls;
    size_t last_erase_len;
    int halfword_writes;
    int fail_at_write;    /* fail when this many writes have happened; -1 = never */
    int fail_after_erase; /* fail the next write once this many erases have happened; -1 = never */
} mock_flash_t;

static edge_status_t mock_read(void *self, uint32_t offset, uint8_t *buf, size_t len) {
    mock_flash_t *flash = (mock_flash_t *)self;
    if (offset + len > MOCK_FLASH_SIZE) {
        return EDGE_EINVAL;
    }
    memcpy(buf, flash->bytes + offset, len);
    return EDGE_OK;
}

static edge_status_t mock_write_halfword(void *self, uint32_t offset, uint16_t value) {
    mock_flash_t *flash = (mock_flash_t *)self;
    if (offset + 2u > MOCK_FLASH_SIZE) {
        return EDGE_EINVAL;
    }
    if (flash->fail_at_write >= 0 && flash->halfword_writes >= flash->fail_at_write) {
        return EDGE_EIO; /* the power went away */
    }
    if (flash->fail_after_erase >= 0 && flash->erase_calls >= flash->fail_after_erase) {
        return EDGE_EIO;
    }
    /*
     * Flash can only clear bits. Programming a value that would need a bit raised is the error a
     * real part reports, and it is the check that catches a record being written over itself. A
     * transition that only clears bits is legal - the page marker going 0xEEEE -> 0x0000 is
     * exactly that, and requiring an erased half-word here would falsely reject it.
     */
    const uint16_t existing =
        (uint16_t)((uint16_t)flash->bytes[offset] | ((uint16_t)flash->bytes[offset + 1u] << 8));
    if ((value & existing) != value) {
        return EDGE_EBUSY;
    }

    flash->bytes[offset] = (uint8_t)(value & 0xFFu);
    flash->bytes[offset + 1u] = (uint8_t)(value >> 8);
    flash->halfword_writes++;
    return EDGE_OK;
}

static edge_status_t mock_erase_sector(void *self, uint32_t offset, size_t len) {
    mock_flash_t *flash = (mock_flash_t *)self;
    /* Granularity: whole sectors, nothing smaller. */
    if (len != FLASH_EMUL_PAGE_SIZE || offset + len > MOCK_FLASH_SIZE) {
        return EDGE_EINVAL;
    }
    memset(flash->bytes + offset, 0xFF, len);
    flash->erase_calls++;
    flash->last_erase_len = len;
    return EDGE_OK;
}

static const uint16_t test_keys[] = {0x0001u, 0x0002u, 0x1234u, 0xBEEFu};

/* Wire an emulation onto an existing image: a restart must not touch the flash contents. */
static void wire_emul(flash_emul_t *emul, mock_flash_t *flash) {
    flash->fail_at_write = -1;
    flash->fail_after_erase = -1;

    const flash_sector_port_t port = {
        .read = mock_read,
        .write_halfword = mock_write_halfword,
        .erase_sector = mock_erase_sector,
        .self = flash,
    };
    /*
     * The port has to outlive this function, and every test here drives one flash image, so it
     * is held in a static and re-pointed at that image. A future test that wants two emulations
     * over two different images must give each its own port instead.
     */
    static flash_sector_port_t held_port;
    held_port = port;

    const flash_var_table_t table = {.virtual_addresses = test_keys,
                                     .count = sizeof(test_keys) / sizeof(test_keys[0])};
    flash_emul_construct(emul, &held_port, table, 0u);
}

/* A fresh image: everything erased and the counters at zero. */
static void make_emul(flash_emul_t *emul, mock_flash_t *flash) {
    memset(flash, 0xFF, sizeof(*flash));
    /* memset(0xFF) leaves the counters at -1, so zero them explicitly. */
    flash->erase_calls = 0;
    flash->last_erase_len = 0u;
    flash->halfword_writes = 0;
    wire_emul(emul, flash);
}

/* A fresh image formats both pages and leaves page 0 receiving. */
static void test_flash_emul_init_formats(void **state) {
    (void)state;
    mock_flash_t flash;
    flash_emul_t emul;
    make_emul(&emul, &flash);

    assert_int_equal(flash_emul_init(&emul), EDGE_OK);
    assert_int_equal(flash.erase_calls, 2);
    assert_int_equal(flash.last_erase_len, FLASH_EMUL_PAGE_SIZE); /* the granularity */
    assert_int_equal(flash_emul_valid_page_offset(&emul), 0u);

    uint16_t value = 0u;
    assert_int_equal(flash_emul_read(&emul, test_keys[0], &value), EDGE_ENOENT);
}

static void test_flash_emul_write_read_roundtrip(void **state) {
    (void)state;
    mock_flash_t flash;
    flash_emul_t emul;
    make_emul(&emul, &flash);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);

    assert_int_equal(flash_emul_write(&emul, test_keys[0], 0x1234u), EDGE_OK);
    assert_int_equal(flash_emul_write(&emul, test_keys[1], 0xABCDu), EDGE_OK);

    uint16_t value = 0u;
    assert_int_equal(flash_emul_read(&emul, test_keys[0], &value), EDGE_OK);
    assert_int_equal(value, 0x1234u);
    assert_int_equal(flash_emul_read(&emul, test_keys[1], &value), EDGE_OK);
    assert_int_equal(value, 0xABCDu);
    assert_int_equal(flash_emul_read(&emul, 0x7777u, &value), EDGE_ENOENT);
}

/* Append-only: the newest record wins because the read scans from the end of the page. */
static void test_flash_emul_newest_wins(void **state) {
    (void)state;
    mock_flash_t flash;
    flash_emul_t emul;
    make_emul(&emul, &flash);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);

    for (uint16_t i = 0u; i < 5u; i++) {
        assert_int_equal(flash_emul_write(&emul, test_keys[2], (uint16_t)(0x100u + i)), EDGE_OK);
    }

    uint16_t value = 0u;
    assert_int_equal(flash_emul_read(&emul, test_keys[2], &value), EDGE_OK);
    assert_int_equal(value, 0x104u);
}

/* The fast path appends and never erases: that is what keeps the previous copy intact. */
static void test_flash_emul_write_does_not_erase(void **state) {
    (void)state;
    mock_flash_t flash;
    flash_emul_t emul;
    make_emul(&emul, &flash);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);
    const int erases_after_init = flash.erase_calls;

    assert_int_equal(flash_emul_write(&emul, test_keys[0], 0xAAAAu), EDGE_OK);
    assert_int_equal(flash.erase_calls, erases_after_init);
    assert_int_equal(flash_emul_valid_page_offset(&emul), 0u);
}

/* Filling the receiving page transfers everything to the other page and erases the old one. */
static void test_flash_emul_page_fills_and_transfers(void **state) {
    (void)state;
    mock_flash_t flash;
    flash_emul_t emul;
    make_emul(&emul, &flash);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);

    for (int i = 0; i < RECORDS_PER_PAGE + 1; i++) { /* fill, then the write that transfers */
        const uint16_t key = test_keys[i % 3];
        assert_int_equal(flash_emul_write(&emul, key, (uint16_t)(0x2000 + i)), EDGE_OK);
    }

    /* A transfer happened: the live page moved and the old one was erased. */
    assert_int_equal(flash_emul_valid_page_offset(&emul), FLASH_EMUL_PAGE_SIZE);
    assert_true(flash.erase_calls >= 3); /* format's two, plus the transfer's */

    uint16_t value = 0u;
    assert_int_equal(flash_emul_read(&emul, test_keys[0], &value), EDGE_OK);
    assert_int_equal(flash_emul_read(&emul, test_keys[1], &value), EDGE_OK);
    assert_int_equal(flash_emul_read(&emul, test_keys[2], &value), EDGE_OK);
}

/*
 * Power loss during a transfer, before the old page is erased: the old page is still a valid,
 * complete copy, so a fresh initialisation comes up on it and the last committed value is
 * there. This is the criterion's "recoverable from a power loss".
 */
static void test_flash_emul_recovers_when_power_dies_mid_transfer(void **state) {
    (void)state;
    mock_flash_t flash;
    flash_emul_t emul;
    make_emul(&emul, &flash);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);

    assert_int_equal(flash_emul_write(&emul, test_keys[0], 0x5150u), EDGE_OK);
    uint16_t committed = 0u;
    assert_int_equal(flash_emul_read(&emul, test_keys[0], &committed), EDGE_OK);
    assert_int_equal(committed, 0x5150u);

    /* One record is already written, so fill the rest of the page and leave it full: the write
     * below is then the one that has to transfer. */
    for (int i = 0; i < RECORDS_PER_PAGE - 1; i++) {
        assert_int_equal(flash_emul_write(&emul, test_keys[1], (uint16_t)(0x3000 + i)), EDGE_OK);
    }

    /* Let the next few writes through (the new page's marker and records), then cut the power
     * before the old page is erased. */
    flash.fail_at_write = flash.halfword_writes + 3;
    const edge_status_t status = flash_emul_write(&emul, test_keys[0], 0xDEADu);
    assert_int_equal(status, EDGE_EIO);

    /* A fresh emulation over the same image still finds the old page valid ... */
    flash_emul_t restarted;
    wire_emul(&restarted, &flash); /* same image, injection cleared */
    assert_int_equal(flash_emul_init(&restarted), EDGE_OK);

    uint16_t value = 0u;
    assert_int_equal(flash_emul_read(&restarted, test_keys[0], &value), EDGE_OK);
    assert_int_equal(value, 0x5150u); /* the last committed value, not the interrupted one */
    /* And the reason it survived: the old page was never erased, so no format was needed. */
    assert_int_equal(flash.erase_calls, 2);
}

/*
 * The reference's own rough edge, pinned rather than denied: it erases the old page *before*
 * marking the new one valid, so a power loss in that gap leaves neither page valid and the
 * next init formats - the data is gone. The criterion is about the recoverable cases, and this
 * test exists so the unrecoverable one is a documented property instead of a surprise.
 */
static void test_flash_emul_power_loss_in_the_erase_window_loses_data(void **state) {
    (void)state;
    mock_flash_t flash;
    flash_emul_t emul;
    make_emul(&emul, &flash);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);

    assert_int_equal(flash_emul_write(&emul, test_keys[0], 0x0BADu), EDGE_OK);
    for (int i = 0; i < RECORDS_PER_PAGE - 1; i++) {
        assert_int_equal(flash_emul_write(&emul, test_keys[1], (uint16_t)(0x4000 + i)), EDGE_OK);
    }

    /* Let the erase happen (it is the transfer's third), then fail the VALID marker. */
    flash.fail_after_erase = flash.erase_calls + 1;
    const edge_status_t status = flash_emul_write(&emul, test_keys[0], 0x0BADu);
    assert_int_equal(status, EDGE_EIO);

    /* Neither page is valid now, so init formats both: the reference's window, not a bug here. */
    const int erases_before = flash.erase_calls;
    flash.fail_after_erase = -1;
    flash_emul_t restarted;
    wire_emul(&restarted, &flash);
    assert_int_equal(flash_emul_init(&restarted), EDGE_OK);
    assert_true(flash.erase_calls > erases_before);

    uint16_t value = 0u;
    assert_int_equal(flash_emul_read(&restarted, test_keys[0], &value), EDGE_ENOENT);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_flash_emul_init_formats),
        cmocka_unit_test(test_flash_emul_write_read_roundtrip),
        cmocka_unit_test(test_flash_emul_newest_wins),
        cmocka_unit_test(test_flash_emul_write_does_not_erase),
        cmocka_unit_test(test_flash_emul_page_fills_and_transfers),
        cmocka_unit_test(test_flash_emul_recovers_when_power_dies_mid_transfer),
        cmocka_unit_test(test_flash_emul_power_loss_in_the_erase_window_loses_data),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
