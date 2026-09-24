#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "dlt645/dlt645.h"
#include "gateway.h"
#include "glue.h"
#include "gpio/gpio.h"
#include "meter_core/meter_core.h"
#include "modbus_slave/modbus_slave.h"
#include "pulse_meter/pulse_meter.h"
#include "relay/relay.h"
#include "uart/uart.h"

/*
 * The product glues are pure adapters, and the coverage gate now includes every
 * product's glue. Without this file those trampolines would only ever be
 * compiled into a product executable that the unit tests never run -- they would
 * read as 0% covered and, worse, nothing would catch a broken adapter.
 */
void product_meter_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_host_make_relay_out(relay_out_if_t *out, void *gpio_state);
void product_meter_mps2_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_mps2_make_relay_out(relay_out_if_t *out, void *gpio_state);
void product_meter_gateway_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_gateway_host_make_modbus(modbus_store_if_t *store,
                                            modbus_transport_if_t *transport,
                                            gateway_state_t *state);
void product_water_meter_host_make_storage(pulse_meter_storage_t *out, void *state_buf);

static void exercise_storage(const dlt645_storage_if_t *storage) {
    const uint8_t payload[3] = {0x11u, 0x22u, 0x33u};
    uint8_t readback[3] = {0};

    assert_non_null(storage->read);
    assert_non_null(storage->write);
    assert_int_equal(storage->write(storage->self, 0u, payload, sizeof payload), EDGE_OK);
    assert_int_equal(storage->read(storage->self, 0u, readback, sizeof readback), EDGE_OK);
    assert_memory_equal(readback, payload, sizeof payload);
}

static void exercise_relay(const relay_out_if_t *relay, uint8_t *gpio) {
    assert_non_null(relay->set);
    assert_int_equal(relay->set(relay->self, 2u, true), EDGE_OK);
    assert_int_equal(gpio[2], 1u);
    assert_int_equal(relay->set(relay->self, 2u, false), EDGE_OK);
    assert_int_equal(gpio[2], 0u);
}

static void test_meter_host_glue(void **state) {
    (void)state;
    uint8_t flash[64] = {0};
    uint8_t gpio[8] = {0};
    dlt645_storage_if_t storage;
    relay_out_if_t relay;

    product_meter_host_make_storage(&storage, flash);
    product_meter_host_make_relay_out(&relay, gpio);
    assert_ptr_equal(storage.self, flash);
    assert_ptr_equal(relay.self, gpio);
    exercise_storage(&storage);
    exercise_relay(&relay, gpio);
}

static void test_meter_mps2_glue(void **state) {
    (void)state;
    uint8_t flash[64] = {0};
    uint8_t gpio[8] = {0};
    dlt645_storage_if_t storage;
    relay_out_if_t relay;

    product_meter_mps2_make_storage(&storage, flash);
    product_meter_mps2_make_relay_out(&relay, gpio);
    exercise_storage(&storage);
    exercise_relay(&relay, gpio);
}

static void test_gateway_glue(void **state) {
    (void)state;
    gateway_state_t gateway = {0};
    dlt645_storage_if_t storage;
    modbus_store_if_t store;
    modbus_transport_if_t transport;
    const uint8_t frame[2] = {0xAAu, 0xBBu};
    uint16_t value = 0u;
    bool coil = false;

    product_meter_gateway_host_make_storage(&storage, gateway.flash);
    exercise_storage(&storage);

    meter_core_construct(&gateway.meter, 1u, 1u);
    assert_int_equal(meter_core_init(&gateway.meter), EDGE_OK);

    /* App-to-app adapter (D78): modbus_slave's store port over meter_core. */
    product_meter_gateway_host_make_modbus(&store, &transport, &gateway);
    assert_ptr_equal(store.self, &gateway.meter);
    assert_ptr_equal(transport.self, &gateway);
    assert_int_equal(store.write_holding(store.self, 3u, 0x1234u), EDGE_OK);
    assert_int_equal(store.read_holding(store.self, 3u, &value), EDGE_OK);
    assert_int_equal(value, 0x1234u);
    assert_int_equal(store.write_coil(store.self, 1u, true), EDGE_OK);
    assert_int_equal(store.read_coil(store.self, 1u, &coil), EDGE_OK);
    assert_true(coil);

    /* Transport over the uart fake, with its own write accounting. */
    assert_int_equal(transport.write(transport.self, frame, sizeof frame), EDGE_OK);
    assert_int_equal(gateway.uart_writes, 1u);
    assert_int_equal(gateway.uart_tx[0], 0xAAu);
    assert_int_equal(gateway.uart_tx[1], 0xBBu);

    /* #170: the largest frame the protocol can build (32 registers: 2 + 64 PDU + 3)
     * fits the sink, and one byte more is refused instead of copied past its end -
     * which is what the previous 64-byte buffer did, silently. */
    uint8_t worst_case[69] = {0};
    assert_int_equal(transport.write(transport.self, worst_case, sizeof worst_case), EDGE_OK);
    assert_int_equal(gateway.uart_writes, 2u);
    uint8_t too_big[GATEWAY_UART_TX_CAPACITY + 1u] = {0};
    assert_int_equal(transport.write(transport.self, too_big, sizeof too_big), EDGE_ENOSPC);
    assert_int_equal(gateway.uart_writes, 2u);
}

static void test_gateway_transport_rejects_missing_state(void **state) {
    (void)state;
    gateway_state_t gateway = {0};
    modbus_store_if_t store;
    modbus_transport_if_t transport;

    product_meter_gateway_host_make_modbus(&store, &transport, &gateway);
    /* A factory is null-safe by contract; so is the adapter with no state. */
    assert_int_equal(transport.write(NULL, "x", 1u), EDGE_EINVAL);
}

static void test_glue_factories_are_null_safe(void **state) {
    (void)state;
    uint8_t buffer[64] = {0};
    gateway_state_t gateway = {0};

    product_meter_host_make_storage(NULL, buffer);
    product_meter_host_make_relay_out(NULL, buffer);
    product_meter_mps2_make_storage(NULL, buffer);
    product_meter_mps2_make_relay_out(NULL, buffer);
    product_meter_gateway_host_make_storage(NULL, buffer);
    product_meter_gateway_host_make_modbus(NULL, NULL, &gateway);
}

static void test_water_meter_host_glue(void **state) {
    (void)state;
    uint8_t buffer[128] = {0};
    pulse_meter_storage_t storage;
    product_water_meter_host_make_storage(&storage, buffer);
    assert_non_null(storage.read);
    assert_non_null(storage.write);
    assert_int_equal(storage.write(storage.self, 50u, 1u), EDGE_OK);
    uint32_t pulses = 0u;
    uint32_t tamper = 0u;
    assert_int_equal(storage.read(storage.self, &pulses, &tamper), EDGE_OK);
    assert_int_equal(pulses, 50u);
    assert_int_equal(tamper, 1u);
}

static void test_vesc_host_glue(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));

    motor_config_storage_port_t storage;
    vesc_host_make_storage_port(&storage, &glue_state);
    assert_non_null(storage.read);
    assert_non_null(storage.write);
    assert_non_null(storage.erase);

    uint8_t dummy_buf[16] = {1, 2, 3, 4};
    assert_int_equal(storage.write(storage.self, 0, dummy_buf, sizeof(dummy_buf)), EDGE_OK);
    uint8_t read_buf[16] = {0};
    assert_int_equal(storage.read(storage.self, 0, read_buf, sizeof(read_buf)), EDGE_OK);
    assert_memory_equal(dummy_buf, read_buf, sizeof(dummy_buf));

    edge_stream_tx_port_t stream_tx;
    vesc_host_make_stream_tx_port(&stream_tx, &glue_state);
    assert_non_null(stream_tx.write);
    assert_int_equal(stream_tx.write(stream_tx.self, dummy_buf, sizeof(dummy_buf)), EDGE_OK);
    assert_int_equal(glue_state.stream_tx_len, sizeof(dummy_buf));

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue_state);
    assert_non_null(inverter.set_duty);

    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue_state);
    assert_non_null(current.read_currents);

    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue_state);
    assert_non_null(rotor.read_angle);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_meter_host_glue),
        cmocka_unit_test(test_meter_mps2_glue),
        cmocka_unit_test(test_gateway_glue),
        cmocka_unit_test(test_gateway_transport_rejects_missing_state),
        cmocka_unit_test(test_glue_factories_are_null_safe),
        cmocka_unit_test(test_water_meter_host_glue),
        cmocka_unit_test(test_vesc_host_glue),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
