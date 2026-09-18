#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/events.h"
#include "edge/modules.h"
#include "meter_core/meter_core.h"
#include "modbus_slave/modbus_slave.h"

/*
 * App-to-app interaction (D78): neither app includes the other.
 *
 * The consumer (`modbus_slave`) defines `modbus_store_if`; the provider
 * (`meter_core`) exposes a concrete API; this test acts as the composition root
 * and adapts one to the other. Events are not involved: this is a direct,
 * explicitly wired service.
 */
typedef struct fake_transport {
    uint8_t buf[320];
    size_t len;
} fake_transport_t;

static edge_status_t store_read_holding(void *self, uint16_t addr, uint16_t *value) {
    return meter_core_read_register((const meter_core_t *)self, addr, value);
}

static edge_status_t store_write_holding(void *self, uint16_t addr, uint16_t value) {
    return meter_core_write_register((meter_core_t *)self, addr, value);
}

static edge_status_t store_read_coil(void *self, uint16_t addr, bool *value) {
    return meter_core_read_coil((const meter_core_t *)self, addr, value);
}

static edge_status_t store_write_coil(void *self, uint16_t addr, bool value) {
    return meter_core_write_coil((meter_core_t *)self, addr, value);
}

static edge_status_t transport_write(void *self, const void *buf, size_t len) {
    fake_transport_t *tx = (fake_transport_t *)self;
    const uint8_t *in = (const uint8_t *)buf;
    for (size_t i = 0u; i < len && i < sizeof(tx->buf); ++i)
        tx->buf[i] = in[i];
    tx->len = len;
    return EDGE_OK;
}

static uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
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

static void test_consumer_reads_provider(void **state) {
    (void)state;
    meter_core_t meter;
    modbus_slave_t slave;
    modbus_store_if_t store;
    modbus_transport_if_t transport;
    fake_transport_t tx = {0};
    uint8_t frame[16];

    meter_core_construct(&meter, EDGE_MOD_METER, 100u);
    assert_int_equal(meter_core_write_register(&meter, 3u, 0xBEEFu), EDGE_OK);

    store = (modbus_store_if_t){.read_coil = store_read_coil,
                                .write_coil = store_write_coil,
                                .read_holding = store_read_holding,
                                .write_holding = store_write_holding,
                                .self = &meter};
    transport = (modbus_transport_if_t){.write = transport_write, .self = &tx};
    modbus_slave_construct(&slave, EDGE_MOD_MODBUS, 110u, 1u, &store, &transport);

    /* Consumer reads the provider's register 3 through its own interface. */
    const uint8_t read_pdu[5] = {0x03u, 0x00u, 0x03u, 0x00u, 0x01u};
    size_t len = build(frame, 1u, read_pdu, sizeof(read_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(tx.buf[1], 0x03u);
    assert_int_equal(tx.buf[2], 0x02u);
    assert_int_equal(tx.buf[3], 0xBEu);
    assert_int_equal(tx.buf[4], 0xEFu);
}

static void test_consumer_writes_provider(void **state) {
    (void)state;
    meter_core_t meter;
    modbus_slave_t slave;
    modbus_store_if_t store;
    modbus_transport_if_t transport;
    fake_transport_t tx = {0};
    uint16_t value = 0u;
    uint8_t frame[16];

    meter_core_construct(&meter, EDGE_MOD_METER, 100u);
    store = (modbus_store_if_t){.read_coil = store_read_coil,
                                .write_coil = store_write_coil,
                                .read_holding = store_read_holding,
                                .write_holding = store_write_holding,
                                .self = &meter};
    transport = (modbus_transport_if_t){.write = transport_write, .self = &tx};
    modbus_slave_construct(&slave, EDGE_MOD_MODBUS, 110u, 1u, &store, &transport);

    /* Consumer writes through its interface; the provider sees the change. */
    const uint8_t write_pdu[5] = {0x06u, 0x00u, 0x02u, 0x01u, 0x02u};
    const size_t len = build(frame, 1u, write_pdu, sizeof(write_pdu));
    assert_int_equal(modbus_slave_feed(&slave, frame, len), EDGE_OK);
    assert_int_equal(meter_core_read_register(&meter, 2u, &value), EDGE_OK);
    assert_int_equal(value, 0x0102u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_consumer_reads_provider),
        cmocka_unit_test(test_consumer_writes_provider),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
