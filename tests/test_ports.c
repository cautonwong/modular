#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>
#include <string.h>

#include "contract/board_contract.h"
#include "contract/port_contract.h"
#include "edge/ports.h"
#include "gpio/gpio.h"
#include "uart/uart.h"

static void test_uart_port_shape(void **state) {
    (void)state;
    uint8_t link[8] = {0};
    const uint8_t tx[4] = {0xAAu, 0xBBu, 0xCCu, 0xDDu};
    uint8_t rx[4] = {0};
    edge_uart_port_t uart = {.write = uart_write, .self = link};

    assert_int_equal(uart.write(uart.self, tx, sizeof(tx)), EDGE_OK);
    assert_memory_equal(link, tx, sizeof(tx));
    assert_int_equal(uart_read(link, rx, sizeof(rx)), EDGE_OK);
    assert_memory_equal(rx, tx, sizeof(rx));

    assert_int_equal(uart_write(NULL, tx, sizeof(tx)), EDGE_EINVAL);
    assert_int_equal(uart_write(link, NULL, sizeof(tx)), EDGE_EINVAL);
    assert_int_equal(uart_read(link, NULL, sizeof(rx)), EDGE_EINVAL);
}

static void test_uart_contract_compliance(void **state) {
    (void)state;
    uint8_t link[8] = {0};
    const edge_byte_writer_contract_t writer_contract = {
        .name = "uart/byte_writer",
        .write = uart_write,
        .self = link,
    };
    edge_contract_byte_writer_run(&writer_contract);

    const edge_byte_reader_contract_t reader_contract = {
        .name = "uart/byte_reader",
        .read = uart_read,
        .self = link,
    };
    edge_contract_byte_reader_run(&reader_contract);
}

static void test_gpio_port_shape(void **state) {
    (void)state;
    uint8_t level_state[4] = {0};
    bool level = false;
    edge_gpio_port_t gpio = {.write = gpio_write, .read = gpio_read, .self = level_state};

    assert_int_equal(gpio.write(gpio.self, 2u, true), EDGE_OK);
    assert_int_equal(gpio.read(gpio.self, 2u, &level), EDGE_OK);
    assert_true(level);
    assert_int_equal(gpio.write(gpio.self, 2u, false), EDGE_OK);
    assert_int_equal(gpio.read(gpio.self, 2u, &level), EDGE_OK);
    assert_false(level);

    assert_int_equal(gpio_write(NULL, 0u, true), EDGE_EINVAL);
    assert_int_equal(gpio_read(level_state, 0u, NULL), EDGE_EINVAL);
}

static void test_gpio_contract_compliance(void **state) {
    (void)state;
    uint8_t level_state[4] = {0};
    const edge_gpio_contract_t gpio_contract = {
        .name = "gpio/port_contract",
        .write = gpio_write,
        .read = gpio_read,
        .self = level_state,
    };
    edge_contract_gpio_run(&gpio_contract);
}

typedef struct fake_spi {
    uint8_t last_tx[16];
    size_t last_len;
} fake_spi_t;

static edge_status_t fake_spi_transfer(void *self, const void *tx, void *rx, size_t len) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (len == 0u) {
        return EDGE_OK;
    }
    if (tx == NULL && rx == NULL) {
        return EDGE_EINVAL;
    }
    fake_spi_t *s = (fake_spi_t *)self;
    if (tx != NULL) {
        size_t c = len < sizeof(s->last_tx) ? len : sizeof(s->last_tx);
        memcpy(s->last_tx, tx, c);
    }
    if (rx != NULL) {
        memset(rx, 0x5A, len);
    }
    s->last_len = len;
    return EDGE_OK;
}

typedef struct fake_i2c {
    uint16_t last_addr;
    uint8_t mem[32];
} fake_i2c_t;

static edge_status_t fake_i2c_write(void *self, uint16_t addr, const void *buf, size_t len) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (len == 0u) {
        return EDGE_OK;
    }
    if (buf == NULL) {
        return EDGE_EINVAL;
    }
    fake_i2c_t *i2c = (fake_i2c_t *)self;
    i2c->last_addr = addr;
    size_t copy_len = len < sizeof(i2c->mem) ? len : sizeof(i2c->mem);
    memcpy(i2c->mem, buf, copy_len);
    return EDGE_OK;
}

static edge_status_t fake_i2c_read(void *self, uint16_t addr, void *buf, size_t len) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (len == 0u) {
        return EDGE_OK;
    }
    if (buf == NULL) {
        return EDGE_EINVAL;
    }
    fake_i2c_t *i2c = (fake_i2c_t *)self;
    i2c->last_addr = addr;
    size_t copy_len = len < sizeof(i2c->mem) ? len : sizeof(i2c->mem);
    memcpy(buf, i2c->mem, copy_len);
    return EDGE_OK;
}

typedef struct fake_flash {
    uint8_t storage[4096];
} fake_flash_t;

static edge_status_t fake_flash_read(void *self, uint32_t offset, void *buf, size_t len) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (len == 0u) {
        return EDGE_OK;
    }
    if (buf == NULL) {
        return EDGE_EINVAL;
    }
    fake_flash_t *fl = (fake_flash_t *)self;
    uint32_t eff_off = offset % sizeof(fl->storage);
    size_t copy_len =
        (eff_off + len <= sizeof(fl->storage)) ? len : (sizeof(fl->storage) - eff_off);
    memcpy(buf, &fl->storage[eff_off], copy_len);
    return EDGE_OK;
}

static edge_status_t fake_flash_write(void *self, uint32_t offset, const void *buf, size_t len) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (len == 0u) {
        return EDGE_OK;
    }
    if (buf == NULL) {
        return EDGE_EINVAL;
    }
    fake_flash_t *fl = (fake_flash_t *)self;
    uint32_t eff_off = offset % sizeof(fl->storage);
    size_t copy_len =
        (eff_off + len <= sizeof(fl->storage)) ? len : (sizeof(fl->storage) - eff_off);
    memcpy(&fl->storage[eff_off], buf, copy_len);
    return EDGE_OK;
}

static edge_status_t fake_flash_erase(void *self, uint32_t offset, size_t len) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (len == 0u) {
        return EDGE_OK;
    }
    fake_flash_t *fl = (fake_flash_t *)self;
    uint32_t eff_off = offset % sizeof(fl->storage);
    size_t erase_len =
        (eff_off + len <= sizeof(fl->storage)) ? len : (sizeof(fl->storage) - eff_off);
    memset(&fl->storage[eff_off], 0xFF, erase_len);
    return EDGE_OK;
}

static void test_spi_contract_compliance(void **state) {
    (void)state;
    fake_spi_t spi_dev = {0};
    const edge_spi_contract_t contract = {
        .name = "spi/fake",
        .transfer = fake_spi_transfer,
        .self = &spi_dev,
    };
    edge_contract_spi_run(&contract);
}

static void test_i2c_contract_compliance(void **state) {
    (void)state;
    fake_i2c_t i2c_dev = {0};
    const edge_i2c_contract_t contract = {
        .name = "i2c/fake",
        .write = fake_i2c_write,
        .read = fake_i2c_read,
        .self = &i2c_dev,
    };
    edge_contract_i2c_run(&contract);
}

static void test_flash_block_contract_compliance(void **state) {
    (void)state;
    fake_flash_t fl_dev = {0};
    const edge_flash_block_contract_t contract = {
        .name = "flash_block/fake",
        .read = fake_flash_read,
        .write = fake_flash_write,
        .erase = fake_flash_erase,
        .self = &fl_dev,
    };
    edge_contract_flash_block_run(&contract);
}

typedef struct fake_board_irq_slot {
    uint32_t irq;
    void (*cb)(void *ctx);
    void *ctx;
} fake_board_irq_slot_t;

#define FAKE_BOARD_MAX_HANDLERS 6u
static fake_board_irq_slot_t s_fake_board_slots[FAKE_BOARD_MAX_HANDLERS];
static uint32_t s_fake_board_count = 0u;

static edge_status_t fake_board_attach(uint32_t irq, void (*cb)(void *), void *ctx) {
    if (cb == NULL) {
        return EDGE_EINVAL;
    }
    if (s_fake_board_count >= FAKE_BOARD_MAX_HANDLERS) {
        return EDGE_ENOSPC;
    }
    s_fake_board_slots[s_fake_board_count++] = (fake_board_irq_slot_t){
        .irq = irq,
        .cb = cb,
        .ctx = ctx,
    };
    return EDGE_OK;
}

static void fake_board_dispatch(uint32_t irq) {
    for (uint32_t i = 0u; i < s_fake_board_count; ++i) {
        if (s_fake_board_slots[i].irq == irq && s_fake_board_slots[i].cb != NULL) {
            s_fake_board_slots[i].cb(s_fake_board_slots[i].ctx);
        }
    }
}

static uint32_t fake_board_handler_count(void) {
    return s_fake_board_count;
}

static void test_board_irq_contract_compliance(void **state) {
    (void)state;
    s_fake_board_count = 0u;
    memset(s_fake_board_slots, 0, sizeof(s_fake_board_slots));
    const edge_board_irq_contract_t contract = {
        .name = "board/fake_irq",
        .attach = fake_board_attach,
        .dispatch = fake_board_dispatch,
        .max_handlers = FAKE_BOARD_MAX_HANDLERS,
        .handler_count = fake_board_handler_count,
    };
    edge_contract_board_irq_run(&contract);
}

static void test_canonical_shapes_default_null(void **state) {
    (void)state;
    edge_storage_kv_t kv = {0};
    edge_byte_reader_t reader = {0};
    edge_byte_writer_t writer = {0};

    assert_null(kv.read);
    assert_null(kv.write);
    assert_null(reader.read);
    assert_null(writer.write);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_uart_port_shape),
        cmocka_unit_test(test_uart_contract_compliance),
        cmocka_unit_test(test_gpio_port_shape),
        cmocka_unit_test(test_gpio_contract_compliance),
        cmocka_unit_test(test_spi_contract_compliance),
        cmocka_unit_test(test_i2c_contract_compliance),
        cmocka_unit_test(test_flash_block_contract_compliance),
        cmocka_unit_test(test_board_irq_contract_compliance),
        cmocka_unit_test(test_canonical_shapes_default_null),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
