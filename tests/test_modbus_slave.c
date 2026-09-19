#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"
#include "modbus_slave/modbus_slave.h"

typedef struct fake_store {
    uint16_t holding[16];
    bool coils[16];
    uint16_t holding_len;
    uint16_t coils_len;
    edge_status_t read_rc;
} fake_store_t;

typedef struct fake_transport {
    uint8_t buf[320];
    size_t len;
    int calls;
} fake_transport_t;

static edge_status_t f_read_holding(void *self, uint16_t addr, uint16_t *value) {
    fake_store_t *store = (fake_store_t *)self;
    if (store->read_rc < 0)
        return store->read_rc;
    if (addr >= store->holding_len)
        return EDGE_ENOENT;
    *value = store->holding[addr];
    return EDGE_OK;
}

static edge_status_t f_write_holding(void *self, uint16_t addr, uint16_t value) {
    fake_store_t *store = (fake_store_t *)self;
    if (addr >= store->holding_len)
        return EDGE_ENOENT;
    store->holding[addr] = value;
    return EDGE_OK;
}

static edge_status_t f_read_coil(void *self, uint16_t addr, bool *value) {
    fake_store_t *store = (fake_store_t *)self;
    if (addr >= store->coils_len)
        return EDGE_ENOENT;
    *value = store->coils[addr];
    return EDGE_OK;
}

static edge_status_t f_write_coil(void *self, uint16_t addr, bool value) {
    fake_store_t *store = (fake_store_t *)self;
    if (addr >= store->coils_len)
        return EDGE_ENOENT;
    store->coils[addr] = value;
    return EDGE_OK;
}

static edge_status_t f_write(void *self, const void *buf, size_t len) {
    fake_transport_t *tx = (fake_transport_t *)self;
    if (len > sizeof(tx->buf))
        return EDGE_ENOSPC;
    const uint8_t *in = (const uint8_t *)buf;
    for (size_t i = 0u; i < len; ++i)
        tx->buf[i] = in[i];
    tx->len = len;
    ++tx->calls;
    return EDGE_OK;
}

static uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u)
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else
                crc >>= 1;
        }
    }
    return crc;
}

static size_t build(uint8_t *frame, uint8_t unit, const uint8_t *pdu, size_t pdu_len) {
    frame[0] = unit;
    for (size_t i = 0u; i < pdu_len; ++i)
        frame[1u + i] = pdu[i];
    const uint16_t crc = crc16(frame, pdu_len + 1u);
    frame[pdu_len + 1u] = (uint8_t)(crc & 0xFFu);
    frame[pdu_len + 2u] = (uint8_t)(crc >> 8);
    return pdu_len + 3u;
}

static void make_slave(modbus_slave_t *slave, fake_store_t *store, fake_transport_t *tx,
                       modbus_store_if_t *store_if, modbus_transport_if_t *tx_if) {
    *store = (fake_store_t){.holding_len = 16u, .coils_len = 16u};
    *tx = (fake_transport_t){0};
    *store_if = (modbus_store_if_t){.read_coil = f_read_coil,
                                    .write_coil = f_write_coil,
                                    .read_holding = f_read_holding,
                                    .write_holding = f_write_holding,
                                    .self = store};
    *tx_if = (modbus_transport_if_t){.write = f_write, .self = tx};
    modbus_slave_construct(slave, 0x1400u, 100u, 7u, store_if, tx_if);
}

static void test_construct_and_init(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(slave.module.module_id, 0x1400u);
    assert_int_equal(slave.unit_id, 7u);
    assert_ptr_equal(slave.module.private_data, &slave);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);
    assert_int_equal(slave.module.poll(&slave.module), EDGE_OK);
    assert_int_equal(slave.poll_count, 1u);
    assert_int_equal(modbus_slave_deinit(&slave), EDGE_OK);

    modbus_slave_construct(NULL, 1u, 1u, 1u, NULL, NULL);
}

static void test_init_rejects_missing_ports(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    slave.transport = NULL;
    assert_int_equal(modbus_slave_init(&slave), EDGE_EINVAL);

    modbus_slave_construct(&slave, 1u, 1u, 1u, NULL, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_EINVAL);
}

static void test_read_holding_registers(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    uint8_t pdu[5] = {0x03u, 0x00u, 0x02u, 0x00u, 0x02u};
    uint8_t frame[16];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);
    store.holding[2] = 0x1234u;
    store.holding[3] = 0xABCDu;

    const size_t len = build(frame, 7u, pdu, sizeof(pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.calls, 1);
    assert_int_equal(tx.len, 9u);
    assert_int_equal(tx.buf[0], 7u);
    assert_int_equal(tx.buf[1], 0x03u);
    assert_int_equal(tx.buf[2], 4u);
    assert_int_equal(tx.buf[3], 0x12u);
    assert_int_equal(tx.buf[4], 0x34u);
    assert_int_equal(tx.buf[5], 0xABu);
    assert_int_equal(tx.buf[6], 0xCDu);
    const uint16_t crc = crc16(tx.buf, tx.len - 2u);
    assert_int_equal(tx.buf[tx.len - 2u], (uint8_t)(crc & 0xFFu));
    assert_int_equal(tx.buf[tx.len - 1u], (uint8_t)(crc >> 8));
    assert_int_equal(slave.responses, 1u);
}

static void test_read_holding_exceptions(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    uint8_t frame[16];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);

    /* quantity out of range -> illegal value */
    const uint8_t bad_qty[5] = {0x03u, 0x00u, 0x00u, 0x00u, 0x40u};
    size_t len = build(frame, 7u, bad_qty, sizeof(bad_qty));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x83u);
    assert_int_equal(tx.buf[2], 0x03u);

    /* address out of range -> illegal address */
    const uint8_t bad_addr[5] = {0x03u, 0x00u, 0x40u, 0x00u, 0x01u};
    len = build(frame, 7u, bad_addr, sizeof(bad_addr));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x83u);
    assert_int_equal(tx.buf[2], 0x02u);

    /* wrong length -> illegal value */
    const uint8_t short_pdu[3] = {0x03u, 0x00u, 0x00u};
    len = build(frame, 7u, short_pdu, sizeof(short_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x83u);

    /* device failure */
    store.read_rc = EDGE_EIO;
    const uint8_t ok_pdu[5] = {0x03u, 0x00u, 0x00u, 0x00u, 0x01u};
    len = build(frame, 7u, ok_pdu, sizeof(ok_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[2], 0x04u);
    assert_true(slave.errors >= 4u);
}

static void test_read_coils_bit_packing(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    uint8_t pdu[5] = {0x01u, 0x00u, 0x00u, 0x00u, 0x0Au};
    uint8_t frame[16];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);
    store.coils[0] = true;
    store.coils[3] = true;
    store.coils[9] = true;

    const size_t len = build(frame, 7u, pdu, sizeof(pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[2], 2u);
    assert_int_equal(tx.buf[3], 0x09u); /* bits 0 and 3 */
    assert_int_equal(tx.buf[4], 0x02u); /* bit 9 */
}

static void test_write_single_register(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    const uint8_t pdu[5] = {0x06u, 0x00u, 0x05u, 0xBEEFu >> 8, 0xEFu};
    uint8_t frame[16];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);
    const size_t len = build(frame, 7u, pdu, sizeof(pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(store.holding[5], 0xBEEFu);
    assert_int_equal(tx.buf[1], 0x06u);

    /* bad address -> illegal address exception */
    const uint8_t bad[5] = {0x06u, 0x00u, 0x40u, 0x00u, 0x01u};
    const size_t bad_len = build(frame, 7u, bad, sizeof(bad));
    assert_int_equal(modbus_slave_feed(&slave, frame, bad_len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x86u);
    assert_int_equal(tx.buf[2], 0x02u);
}

static void test_write_single_coil(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    const uint8_t on_pdu[5] = {0x05u, 0x00u, 0x01u, 0xFFu, 0x00u};
    const uint8_t bad_pdu[5] = {0x05u, 0x00u, 0x01u, 0x12u, 0x34u};
    uint8_t frame[16];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);

    size_t len = build(frame, 7u, on_pdu, sizeof(on_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_true(store.coils[1]);

    len = build(frame, 7u, bad_pdu, sizeof(bad_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x85u);
    assert_int_equal(tx.buf[2], 0x03u);
}

static void test_write_multiple_registers(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    /* start=1, qty=2, bc=4, data=0x0001,0x0002 */
    const uint8_t pdu[10] = {0x10u, 0x00u, 0x01u, 0x00u, 0x02u, 0x04u, 0x00u, 0x01u, 0x00u, 0x02u};
    uint8_t frame[20];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);
    const size_t len = build(frame, 7u, pdu, sizeof(pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(store.holding[1], 1u);
    assert_int_equal(store.holding[2], 2u);
    assert_int_equal(tx.buf[1], 0x10u);
    assert_int_equal(tx.buf[2], 0x00u);
    assert_int_equal(tx.buf[3], 0x01u);
    assert_int_equal(tx.buf[4], 0x00u);
    assert_int_equal(tx.buf[5], 0x02u);

    /* inconsistent byte count -> illegal value */
    const uint8_t bad[10] = {0x10u, 0x00u, 0x01u, 0x00u, 0x02u, 0x03u, 0x00u, 0x01u, 0x00u, 0x02u};
    const size_t bad_len = build(frame, 7u, bad, sizeof(bad));
    assert_int_equal(modbus_slave_feed(&slave, frame, bad_len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x90u);
    assert_int_equal(tx.buf[2], 0x03u);
}

static void test_addressing_crc_and_unsupported(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    const uint8_t read_pdu[5] = {0x03u, 0x00u, 0x00u, 0x00u, 0x01u};
    uint8_t frame[16];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);

    /* another unit -> no response */
    size_t len = build(frame, 3u, read_pdu, sizeof(read_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_ENOENT);
    assert_int_equal(tx.calls, 0);

    /* bad CRC */
    len = build(frame, 7u, read_pdu, sizeof(read_pdu));
    frame[len - 1u] ^= 0xFFu;
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_EIO);

    /* unsupported function -> illegal function */
    const uint8_t unsupported[2] = {0x42u, 0x00u};
    len = build(frame, 7u, unsupported, sizeof(unsupported));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0xC2u);
    assert_int_equal(tx.buf[2], 0x01u);

    /* broadcast write is applied without a response */
    const uint8_t bc_pdu[5] = {0x06u, 0x00u, 0x05u, 0x11u, 0x22u};
    len = build(frame, 0u, bc_pdu, sizeof(bc_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(store.holding[5], 0x1122u);

    /* unsupported function on broadcast -> no response */
    len = build(frame, 0u, unsupported, sizeof(unsupported));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
}

static void test_bad_arguments_and_events(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    edge_event_t event = {.id = EDGE_EVT_MODBUS_RX};
    const uint8_t frame[8] = {7u, 0x03u, 0u, 0u, 0u, 1u, 0u, 0u};

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);

    assert_int_equal(modbus_slave_feed(NULL, frame, sizeof(frame)), EDGE_EINVAL);
    assert_int_equal(modbus_slave_feed(&slave, NULL, sizeof(frame)), EDGE_EINVAL);
    assert_int_equal(modbus_slave_feed(&slave, frame, 3u), EDGE_EIO);
    assert_int_equal(modbus_slave_feed(&slave, frame, 300u), EDGE_EIO);

    slave.store = NULL;
    assert_int_equal(modbus_slave_feed(&slave, frame, sizeof(frame)), EDGE_EINVAL);
    slave.store = &store_if;

    assert_int_equal(slave.module.on_event(&slave.module, &event), EDGE_OK);
    assert_int_equal(slave.last_event, EDGE_EVT_MODBUS_RX);
    assert_int_equal(slave.module.on_event(&slave.module, NULL), EDGE_EINVAL);
    assert_int_equal(slave.module.power_off(&slave.module), EDGE_OK);
}

static void test_branch_coverage(void **state) {
    (void)state;
    fake_store_t store;
    fake_transport_t tx;
    modbus_store_if_t store_if;
    modbus_transport_if_t tx_if;
    modbus_slave_t slave;
    uint8_t frame[24];

    make_slave(&slave, &store, &tx, &store_if, &tx_if);
    assert_int_equal(modbus_slave_init(&slave), EDGE_OK);

    /* short PDUs hit the length guards */
    const uint8_t short2[2] = {0x01u, 0x00u};
    const uint8_t short3[3] = {0x03u, 0x00u, 0x00u};
    const uint8_t short4[4] = {0x05u, 0x00u, 0x00u, 0x00u};
    const uint8_t short5[2] = {0x10u, 0x00u};
    size_t len = build(frame, 7u, short2, sizeof(short2));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x81u);
    len = build(frame, 7u, short3, sizeof(short3));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x83u);
    len = build(frame, 7u, short4, sizeof(short4));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x85u);
    len = build(frame, 7u, short3, sizeof(short3));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    len = build(frame, 7u, short5, sizeof(short5));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x90u);

    /* zero quantity -> illegal value */
    const uint8_t zero_qty[5] = {0x01u, 0x00u, 0x00u, 0x00u, 0x00u};
    len = build(frame, 7u, zero_qty, sizeof(zero_qty));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x81u);

    /* read-coils address out of range and write-coil failure */
    const uint8_t bad_coil[5] = {0x01u, 0x00u, 0x40u, 0x00u, 0x01u};
    len = build(frame, 7u, bad_coil, sizeof(bad_coil));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[2], 0x02u);
    const uint8_t bad_coil_w[5] = {0x05u, 0x00u, 0x40u, 0xFFu, 0x00u};
    len = build(frame, 7u, bad_coil_w, sizeof(bad_coil_w));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[2], 0x02u);

    /* broadcast reads/writes return without a response */
    const uint8_t read_req[5] = {0x03u, 0x00u, 0x00u, 0x00u, 0x01u};
    const int calls_before = tx.calls;
    len = build(frame, 0u, read_req, sizeof(read_req));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    const uint8_t read_coil_req[5] = {0x01u, 0x00u, 0x00u, 0x00u, 0x02u};
    len = build(frame, 0u, read_coil_req, sizeof(read_coil_req));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    const uint8_t w_coil[5] = {0x05u, 0x00u, 0x02u, 0xFFu, 0x00u};
    len = build(frame, 0u, w_coil, sizeof(w_coil));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_true(store.coils[2]);
    const uint8_t w_multi[8] = {0x10u, 0x00u, 0x01u, 0x00u, 0x01u, 0x02u, 0x00u, 0x09u};
    len = build(frame, 0u, w_multi, sizeof(w_multi));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.calls, calls_before);

    /* write-multiple address out of range */
    const uint8_t bad_multi[8] = {0x10u, 0x00u, 0x40u, 0x00u, 0x01u, 0x02u, 0x00u, 0x01u};
    len = build(frame, 7u, bad_multi, sizeof(bad_multi));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[2], 0x02u);

    /* null private data paths */
    edge_module_t *module = &slave.module;
    module->private_data = NULL;
    assert_int_equal(module->poll(module), EDGE_EINVAL);
    assert_int_equal(module->on_event(module, &(edge_event_t){0}), EDGE_EINVAL);
    /* D51: init/deinit take the app pointer directly. */
    assert_int_equal(modbus_slave_init(NULL), EDGE_EINVAL);
    assert_int_equal(modbus_slave_deinit(NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_construct_and_init),
        cmocka_unit_test(test_init_rejects_missing_ports),
        cmocka_unit_test(test_read_holding_registers),
        cmocka_unit_test(test_read_holding_exceptions),
        cmocka_unit_test(test_read_coils_bit_packing),
        cmocka_unit_test(test_write_single_register),
        cmocka_unit_test(test_write_single_coil),
        cmocka_unit_test(test_write_multiple_registers),
        cmocka_unit_test(test_addressing_crc_and_unsupported),
        cmocka_unit_test(test_bad_arguments_and_events),
        cmocka_unit_test(test_branch_coverage),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
