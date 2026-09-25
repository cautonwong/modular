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

    ppm_receiver_port_t ppm;
    vesc_host_make_ppm_port(&ppm, &glue_state);
    assert_non_null(ppm.read_pulse_us);

    adc_input_port_t adc;
    vesc_host_make_adc_port(&adc, &glue_state);
    assert_non_null(adc.read_throttle_v);

    vesc_can_port_t can;
    vesc_host_make_can_port(&can, &glue_state);
    assert_non_null(can.send_frame);

    motor_id_measure_port_t id_m;
    vesc_host_make_motor_id_measure_port(&id_m, &glue_state);
    assert_non_null(id_m.get_currents);

    motor_id_control_port_t id_c;
    vesc_host_make_motor_id_control_port(&id_c, &glue_state);
    assert_non_null(id_c.set_voltage_alpha_beta);

    nunchuk_port_t nunchuk;
    vesc_host_make_nunchuk_port(&nunchuk, &glue_state);
    assert_non_null(nunchuk.read_data);

    pas_port_t pas;
    vesc_host_make_pas_port(&pas, &glue_state);
    assert_non_null(pas.read_cadence_rpm);

    balance_port_t balance;
    vesc_host_make_balance_port(&balance, &glue_state);
    assert_non_null(balance.read_attitude);

    terminal_stream_port_t term_stream;
    vesc_host_make_terminal_stream_port(&term_stream, &glue_state);
    assert_non_null(term_stream.write_string);

    bms_can_port_t bms_can;
    vesc_host_make_bms_can_port(&bms_can, &glue_state);
    assert_non_null(bms_can.send_can_msg);
}

/*
 * The vesc_host adapters were only checked for being non-null, and that is how
 * motor_set_rpm / motor_set_pos came to return EDGE_OK while doing nothing and how
 * temp_motor came to report the FET temperature. These two tests drive the
 * adapters instead of looking at them: everything here traces to a defect that
 * shipped or to a semantic the wire format depends on.
 */
static edge_status_t gp_set_duty(void *self, float a, float b, float c) {
    (void)self;
    (void)a;
    (void)b;
    (void)c;
    return EDGE_OK;
}

static edge_status_t gp_set_phase(void *self, bool enable) {
    (void)self;
    (void)enable;
    return EDGE_OK;
}

static edge_status_t gp_read_currents(void *self, float *ia, float *ib, float *ic) {
    (void)self;
    *ia = 0.0f;
    *ib = 0.0f;
    *ic = 0.0f;
    return EDGE_OK;
}

static edge_status_t gp_read_vbus(void *self, float *v_bus) {
    (void)self;
    *v_bus = 24.0f;
    return EDGE_OK;
}

static edge_status_t gp_read_angle(void *self, float *angle_rad, float *rpm) {
    (void)self;
    *angle_rad = 0.5f;
    *rpm = 1234.0f;
    return EDGE_OK;
}

static void test_vesc_host_motor_provider_semantics(void **state) {
    (void)state;

    foc_inverter_port_t inverter = {
        .set_duty = gp_set_duty, .set_phase_state = gp_set_phase, .self = NULL};
    foc_current_port_t current = {
        .read_currents = gp_read_currents, .read_vbus = gp_read_vbus, .self = NULL};
    foc_rotor_port_t rotor = {.read_angle = gp_read_angle, .self = NULL};

    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 5e-5f,
                        .lambda_wb = 0.005f,
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 8.0f,
                        .temp_fet_max_c = 100.0f,
                        .sensorless_mode = false};

    foc_core_t foc;
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    vesc_motor_provider_port_t port;
    vesc_host_make_motor_provider_port(&port, &foc);
    assert_non_null(port.get_values);
    assert_non_null(port.get_stats);
    assert_non_null(port.reset_stats);

    /* Every setter must actually move the aggregate's state, not just return OK.
     * Two of them used to be empty. */
    assert_int_equal(port.set_rpm(port.self, 3000.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_RPM);

    assert_int_equal(port.set_pos(port.self, 90.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_POS);

    assert_int_equal(port.set_duty(port.self, 0.3f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_DUTY);

    assert_int_equal(port.set_current(port.self, 7.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_CURRENT);

    assert_int_equal(port.set_current_brake(port.self, 4.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_CURRENT);

    /* Masked telemetry: only the requested fields are filled, and each comes from
     * its own source. Bit 1 reported the FET temperature once. */
    foc_core_set_temperature(&foc, 42.0f);
    vesc_values_t val;

    assert_int_equal(port.get_values(port.self, (1u << 0), &val), EDGE_OK);
    assert_float_equal(val.temp_mos, 42.0f, 1e-3f);
    assert_float_equal(val.temp_motor, 0.0f, 1e-6f);

    assert_int_equal(port.get_values(port.self, (1u << 1), &val), EDGE_OK);
    assert_float_equal(val.temp_motor, 0.0f, 1e-6f); /* no motor NTC, not a copy */

    assert_int_equal(port.get_values(port.self, (1u << 17), &val), EDGE_OK);
    assert_int_equal(val.controller_id, 1u);

    assert_int_equal(port.get_values(port.self, (1u << 18), &val), EDGE_OK);
    assert_float_equal(val.temp_mos_1, 0.0f, 1e-6f); /* no three-NTC source */

    /* Statistics: the averages are sum/samples, so before any sample they are the
     * reference's 0/0, and a reset puts them back there. */
    vesc_stats_t st;
    assert_int_equal(port.get_stats(port.self, &st), EDGE_OK);
    assert_true(isnan(st.power_avg));

    assert_int_equal(foc.module.poll(&foc.module), EDGE_OK);
    assert_int_equal(port.get_stats(port.self, &st), EDGE_OK);
    assert_false(isnan(st.power_avg));

    assert_int_equal(port.reset_stats(port.self), EDGE_OK);
    assert_int_equal(port.get_stats(port.self, &st), EDGE_OK);
    assert_true(isnan(st.power_avg));
    assert_float_equal(st.temp_mos_max, -300.0f, 1e-6f); /* reference's seed */
}

/* Mock inputs for the decoded-input adapters. */
static float gp_ppm_pulse = 1600.0f;
static float gp_throttle_v = 1.5f;
static float gp_brake_v = 0.5f;

static edge_status_t gp_read_pulse(void *self, float *pulse_us) {
    (void)self;
    *pulse_us = gp_ppm_pulse;
    return EDGE_OK;
}

static bool gp_signal_present(void *self) {
    (void)self;
    return true;
}

static edge_status_t gp_read_throttle_v(void *self, float *v) {
    (void)self;
    *v = gp_throttle_v;
    return EDGE_OK;
}

static edge_status_t gp_read_brake_v(void *self, float *v) {
    (void)self;
    *v = gp_brake_v;
    return EDGE_OK;
}

static void test_vesc_host_app_status_adapters(void **state) {
    (void)state;

    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;

    ppm_receiver_port_t ppm_in = {
        .read_pulse_us = gp_read_pulse, .is_signal_present = gp_signal_present, .self = NULL};
    ppm_config_t ppm_cfg = {.mode = PPM_MODE_CURRENT,
                            .pulse_min_us = 1000.0f,
                            .pulse_max_us = 2000.0f,
                            .pulse_center_us = 1500.0f,
                            .pulse_deadband_us = 50.0f,
                            .timeout_s = 0.2f,
                            .safe_start = false};
    ppm_app_t ppm;
    ppm_construct(&ppm, EDGE_MOD_PPM, 25u, &ppm_cfg, &ppm_in);
    assert_int_equal(ppm_init(&ppm), EDGE_OK);
    assert_int_equal(ppm_update(&ppm, 0.001f), EDGE_OK);

    adc_input_port_t adc_in = {
        .read_throttle_v = gp_read_throttle_v, .read_brake_v = gp_read_brake_v, .self = NULL};
    adc_input_config_t adc_cfg = {.mode = ADC_MODE_CURRENT,
                                  .voltage_min = 0.2f,
                                  .voltage_max = 3.2f,
                                  .voltage_start = 0.8f,
                                  .voltage_end = 2.8f,
                                  .use_brake_input = true,
                                  .brake_start = 0.3f,
                                  .brake_end = 2.5f,
                                  .safe_start = false};
    adc_input_app_t adc;
    adc_input_construct(&adc, EDGE_MOD_ADC_INPUT, 25u, &adc_cfg, &adc_in);
    assert_int_equal(adc_input_init(&adc), EDGE_OK);
    assert_int_equal(adc_input_update(&adc), EDGE_OK);

    /* The adapters read through the apps, so the composition root has to hand them
     * over; that wiring is part of what this checks. */
    glue_state.ppm = &ppm;
    glue_state.adc = &adc;

    vesc_app_status_port_t status;
    vesc_host_make_app_status_port(&status, &glue_state);
    assert_non_null(status.get_decoded_ppm);
    assert_non_null(status.get_decoded_adc);

    float level = 0.0f;
    float pulse = 0.0f;
    assert_int_equal(status.get_decoded_ppm(status.self, &level, &pulse), EDGE_OK);
    assert_float_equal(pulse, 1600.0f, 1e-3f);
    assert_float_equal(level, ppm_get_output(&ppm), 1e-6f);

    float v1 = 0.0f;
    float v2 = 0.0f;
    float l2 = 0.0f;
    assert_int_equal(status.get_decoded_adc(status.self, &level, &v1, &l2, &v2), EDGE_OK);
    assert_float_equal(v1, 1.5f, 1e-3f);
    assert_float_equal(v2, 0.5f, 1e-3f);
    assert_float_equal(level, adc_input_get_throttle(&adc), 1e-6f);
    assert_float_equal(l2, adc_input_get_brake(&adc), 1e-6f);

    /* Without the apps wired the adapter refuses instead of inventing values. */
    glue_state.ppm = NULL;
    glue_state.adc = NULL;
    assert_int_equal(status.get_decoded_ppm(status.self, &level, &pulse), EDGE_EINVAL);
    assert_int_equal(status.get_decoded_adc(status.self, &level, &v1, &l2, &v2), EDGE_EINVAL);
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
        cmocka_unit_test(test_vesc_host_motor_provider_semantics),
        cmocka_unit_test(test_vesc_host_app_status_adapters),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
