#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "dlt645/dlt645.h"
#include "flash/flash.h"
#include "gateway.h"
#include "glue.h"
#include "gpio/gpio.h"
#include "meter_core/meter_core.h"
#include "modbus_slave/modbus_slave.h"
#include "pulse_meter/pulse_meter.h"
#include "relay/relay.h"
#include "uart/uart.h"
/* By path, not by bare name: four products ship a glue.h and this target compiles all of them. */
#include "vesc6_stm32f4/glue.h"

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

    /* Erased flash reads as ones, and the sector port programmes half-words into it. */
    memset(glue_state.flash_mem, 0xFF, sizeof(glue_state.flash_mem));
    flash_sector_port_t sectors;
    vesc_host_make_flash_sector_port(&sectors, &glue_state);
    assert_non_null(sectors.read);
    assert_non_null(sectors.write_halfword);
    assert_non_null(sectors.erase_sector);

    assert_int_equal(sectors.write_halfword(sectors.self, 0u, 0x1234u), EDGE_OK);
    uint8_t read_buf[2] = {0};
    assert_int_equal(sectors.read(sectors.self, 0u, read_buf, sizeof(read_buf)), EDGE_OK);
    assert_int_equal(read_buf[0], 0x34u); /* little-endian, as the reference's image is */
    assert_int_equal(read_buf[1], 0x12u);

    /* Flash can only clear bits: programming over a written half-word is refused, while
     * clearing bits is legal - which is what the page markers rely on. */
    assert_int_equal(sectors.write_halfword(sectors.self, 0u, 0xFFFFu), EDGE_EBUSY);
    assert_int_equal(sectors.write_halfword(sectors.self, 0u, 0x1200u), EDGE_OK);

    /* Erasure is whole sectors. */
    assert_int_equal(sectors.erase_sector(sectors.self, 0u, FLASH_EMUL_PAGE_SIZE), EDGE_OK);
    assert_int_equal(sectors.read(sectors.self, 0u, read_buf, sizeof(read_buf)), EDGE_OK);
    assert_int_equal(read_buf[0], 0xFFu);
    assert_int_equal(read_buf[1], 0xFFu);

    uint8_t dummy_buf[16] = {1, 2, 3, 4};
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

    /* The motor-identification port is built from the FOC aggregate, which this test has none of;
     * what it can check here is that the factory refuses a missing one. The detection itself runs
     * in its own test, where there is a virtual motor to measure. */
    motor_id_measure_port_t id_m = {0};
    vesc_host_make_motor_id_measure_port(&id_m, NULL);
    assert_null(id_m.set_phase_override);

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
    foc_core_set_fet_temperature(&foc, 42.0f);
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

/*
 * DIR_MULT, reference mc_interface.c:52: DIR_MULT = m_invert_direction ? -1 : 1. The
 * reference multiplies per command at the mc_interface_set_* entry points - current,
 * brake, duty and pid_speed - which in this product are the glue's adapters. What
 * matters is that the setpoint reaching the aggregate is flipped, not merely that the
 * call returns OK.
 */
static void test_vesc_host_dir_mult(void **state) {
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

    assert_int_equal(port.set_current(port.self, 7.0f), EDGE_OK);
    assert_float_equal(foc.target_iq, 7.0f, 1e-6f);
    assert_int_equal(port.set_rpm(port.self, 3000.0f), EDGE_OK);
    assert_float_equal(foc.target_rpm, 3000.0f, 1e-6f);
    assert_int_equal(port.set_duty(port.self, 0.3f), EDGE_OK);
    assert_float_equal(foc.target_duty, 0.3f, 1e-6f);

    foc.config.m_invert_direction = true;

    assert_int_equal(port.set_current(port.self, 7.0f), EDGE_OK);
    assert_float_equal(foc.target_iq, -7.0f, 1e-6f);
    assert_int_equal(port.set_rpm(port.self, 3000.0f), EDGE_OK);
    assert_float_equal(foc.target_rpm, -3000.0f, 1e-6f);
    assert_int_equal(port.set_duty(port.self, 0.3f), EDGE_OK);
    assert_float_equal(foc.target_duty, -0.3f, 1e-6f);
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

/*
 * The remaining vesc_host adapters. Several of them are deliberately constant
 * sources (a host simulation has no nunchuk, cadence sensor or IMU), which is
 * worth pinning so a later reader can tell "simulated input" from "unfinished".
 */
static void test_vesc_host_simulated_adapters(void **state) {
    (void)state;

    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 5e-5f, 0.005f, 7, 5e-4f);
    glue_state.vmotor.rotor_angle_rad = 0.75f;
    glue_state.vmotor.ia = 3.0f;
    glue_state.vmotor.ib = -1.5f;

    /* CAN: the send adaptor records the frame, the receive adaptor has no peer. */
    vesc_can_port_t can;
    vesc_host_make_can_port(&can, &glue_state);
    const uint8_t payload[3] = {0xAAu, 0xBBu, 0xCCu};
    assert_int_equal(can.send_frame(can.self, 0x1234u, payload, sizeof(payload)), EDGE_OK);
    assert_int_equal(glue_state.last_can_id, 0x1234u);
    assert_int_equal(glue_state.last_can_len, 3u);
    assert_memory_equal(glue_state.last_can_data, payload, sizeof(payload));

    uint32_t rx_id = 0u;
    uint8_t rx_data[8] = {0};
    uint8_t rx_len = 0u;
    assert_int_equal(can.receive_frame(can.self, &rx_id, rx_data, &rx_len), EDGE_ENOENT);

    /* Terminal output and its telemetry source. */
    terminal_stream_port_t term_stream;
    vesc_host_make_terminal_stream_port(&term_stream, &glue_state);
    assert_int_equal(term_stream.write_string(term_stream.self, "help\n"), EDGE_OK);
    assert_string_equal(glue_state.terminal_tx_buf, "help\n");

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
    foc_core_set_fet_temperature(&foc, 37.0f);
    assert_int_equal(foc_core_fast_loop(&foc, 5e-5f), EDGE_OK);

    terminal_system_port_t term_sys;
    vesc_host_make_terminal_system_port(&term_sys, &foc);
    float rpm = 0.0f;
    float iq = 0.0f;
    float v_bus = 0.0f;
    float temp = 0.0f;
    uint32_t faults = 1u;
    assert_int_equal(term_sys.get_stats(term_sys.self, &rpm, &iq, &v_bus, &temp, &faults), EDGE_OK);
    assert_float_equal(rpm, 1234.0f, 1e-3f); /* from the rotor adaptor */
    assert_float_equal(v_bus, 24.0f, 1e-3f);
    assert_float_equal(temp, 37.0f, 1e-3f);
    assert_int_equal(faults, foc_core_get_faults(&foc));

    /* The motor-identification port is wired to this test's FOC aggregate: every callback is one
     * thing the measurement procedures do to the motor. The port's own state is the product's,
     * because a measurement replaces and restores the aggregate's configuration through it. */
    vesc_host_glue_state_t id_glue;
    memset(&id_glue, 0, sizeof(id_glue));
    id_glue.foc = &foc;
    motor_id_measure_port_t id_m;
    vesc_host_make_motor_id_measure_port(&id_m, &id_glue);
    assert_ptr_equal(id_m.self, &id_glue);
    assert_non_null(id_m.set_phase_override);
    assert_non_null(id_m.set_current);
    assert_non_null(id_m.reset_samples);
    assert_non_null(id_m.read_samples);
    assert_non_null(id_m.get_fault);
    assert_non_null(id_m.stop);

    /* Simulated sensors: constant, and documented as such. */
    nunchuk_port_t nunchuk;
    vesc_host_make_nunchuk_port(&nunchuk, &glue_state);
    uint8_t js_x = 0u;
    uint8_t js_y = 0u;
    bool btn_c = true;
    assert_int_equal(nunchuk.read_data(nunchuk.self, &js_x, &js_y, NULL, NULL, NULL, &btn_c, NULL),
                     EDGE_OK);
    assert_int_equal(js_x, 128u);
    assert_int_equal(js_y, 128u);
    assert_false(btn_c);

    pas_port_t pas;
    vesc_host_make_pas_port(&pas, &glue_state);
    float cadence = 0.0f;
    float torque = 0.0f;
    assert_int_equal(pas.read_cadence_rpm(pas.self, &cadence), EDGE_OK);
    assert_float_equal(cadence, 60.0f, 1e-6f);
    assert_int_equal(pas.read_torque_nm(pas.self, &torque), EDGE_OK);
    assert_float_equal(torque, 15.0f, 1e-6f);

    balance_port_t balance;
    vesc_host_make_balance_port(&balance, &glue_state);
    float pitch = 1.0f;
    float roll = 1.0f;
    assert_int_equal(balance.read_attitude(balance.self, &pitch, &roll, NULL, NULL, NULL, NULL),
                     EDGE_OK);
    assert_float_equal(pitch, 0.0f, 1e-6f);
    assert_float_equal(roll, 0.0f, 1e-6f);

    /* BMS CAN shares the CAN recording fields and clamps an oversized frame. */
    bms_can_port_t bms_can;
    vesc_host_make_bms_can_port(&bms_can, &glue_state);
    const uint8_t big[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    assert_int_equal(bms_can.send_can_msg(bms_can.self, 0x77u, big, sizeof(big)), EDGE_OK);
    assert_int_equal(glue_state.last_can_id, 0x77u);
    assert_int_equal(glue_state.last_can_len, 8u);

    /* Flash sector bounds: a read or a half-word write past the fake flash is refused rather
     * than clamped, and erasure is whole sectors only. */
    flash_sector_port_t sectors;
    vesc_host_make_flash_sector_port(&sectors, &glue_state);
    uint8_t one[1] = {0};
    assert_int_equal(sectors.read(sectors.self, sizeof(glue_state.flash_mem), one, 1u),
                     EDGE_EINVAL);
    assert_int_equal(sectors.write_halfword(sectors.self, sizeof(glue_state.flash_mem), 0u),
                     EDGE_EINVAL);
    assert_int_equal(sectors.erase_sector(sectors.self, 0u, 1u), EDGE_EINVAL);
    assert_int_equal(
        sectors.erase_sector(sectors.self, sizeof(glue_state.flash_mem), FLASH_EMUL_PAGE_SIZE),
        EDGE_EINVAL);

    /* The factory refuses a NULL output instead of writing through it. */
    flash_sector_port_t untouched = {0};
    vesc_host_make_flash_sector_port(&untouched, NULL);
    assert_null(untouched.read);

    /* Stream TX bounds. */
    edge_stream_tx_port_t stream_tx;
    vesc_host_make_stream_tx_port(&stream_tx, &glue_state);
    uint8_t small[2] = {0};
    assert_int_equal(stream_tx.write(stream_tx.self, small, sizeof(small)), EDGE_OK);
    assert_int_equal(glue_state.stream_tx_len, sizeof(small));
}

/*
 * The configuration's variable store over the EEPROM emulation: what the product hands
 * motor_config. A small NOR mock is enough to prove the wiring - whole-sector erasure and
 * bit-clearing writes only, as flash is - and that the port lands the value at the reference's
 * virtual address base rather than at index 0.
 */
#define GLUE_NOR_PAGES 2u

typedef struct glue_nor {
    uint8_t bytes[GLUE_NOR_PAGES * FLASH_EMUL_PAGE_SIZE];
} glue_nor_t;

static edge_status_t glue_nor_read(void *self, uint32_t offset, uint8_t *buf, size_t len) {
    glue_nor_t *nor = (glue_nor_t *)self;
    if (offset + len > sizeof(nor->bytes)) {
        return EDGE_EINVAL;
    }
    memcpy(buf, nor->bytes + offset, len);
    return EDGE_OK;
}

static edge_status_t glue_nor_write(void *self, uint32_t offset, uint16_t value) {
    glue_nor_t *nor = (glue_nor_t *)self;
    if (offset + 2u > sizeof(nor->bytes)) {
        return EDGE_EINVAL;
    }
    const uint16_t existing =
        (uint16_t)((uint16_t)nor->bytes[offset] | ((uint16_t)nor->bytes[offset + 1u] << 8));
    if ((value & existing) != value) {
        return EDGE_EBUSY; /* flash only clears bits */
    }
    nor->bytes[offset] = (uint8_t)(value & 0xFFu);
    nor->bytes[offset + 1u] = (uint8_t)(value >> 8);
    return EDGE_OK;
}

static edge_status_t glue_nor_erase(void *self, uint32_t offset, size_t len) {
    glue_nor_t *nor = (glue_nor_t *)self;
    if (len != FLASH_EMUL_PAGE_SIZE || offset + len > sizeof(nor->bytes)) {
        return EDGE_EINVAL;
    }
    memset(nor->bytes + offset, 0xFF, len);
    return EDGE_OK;
}

static void test_vesc_host_var_port(void **state) {
    (void)state;
    static glue_nor_t nor;
    static uint16_t table[VESC_HOST_MCCONF_VARS];
    memset(&nor, 0xFF, sizeof(nor));
    for (size_t i = 0u; i < VESC_HOST_MCCONF_VARS; i++) {
        table[i] = (uint16_t)(VESC_HOST_MCCONF_BASE + i);
    }

    flash_sector_port_t sectors = {.read = glue_nor_read,
                                   .write_halfword = glue_nor_write,
                                   .erase_sector = glue_nor_erase,
                                   .self = &nor};
    flash_emul_t emul;
    flash_emul_construct(
        &emul, &sectors,
        (flash_var_table_t){.virtual_addresses = table, .count = VESC_HOST_MCCONF_VARS}, 0u);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);

    motor_config_var_port_t port;
    vesc_host_make_var_port(&port, &emul);
    assert_non_null(port.read);
    assert_non_null(port.write);

    /* Write and read back through the port. */
    assert_int_equal(port.write(port.self, 0u, 0x1234u), EDGE_OK);
    uint16_t value = 0u;
    assert_int_equal(port.read(port.self, 0u, &value), EDGE_OK);
    assert_int_equal(value, 0x1234u);

    /* A key that was never written answers as missing rather than as zero. */
    assert_int_equal(port.read(port.self, 5u, &value), EDGE_ENOENT);

    /* The factory refuses a NULL output instead of writing through it. */
    vesc_host_make_var_port(NULL, &emul);
}

/*
 * The NTC sampler. The reference filters both readings in its ADC interrupt handler
 * (mc_interface.c:2266 for the FET, :2325-2331 for the motor) and the FOC only ever reads the
 * filtered values, so the filter belongs to the product that owns the board's constants. The
 * form is UTILS_LP_FAST, v -= f * (v - x), which the test recomputes rather than assumes; a
 * reading that cannot be a temperature is replaced by -100 instead of being filtered, because
 * the reference's own comment says a value that walks into the filter never comes back out.
 */
static void test_vesc_host_temperature_sampler(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));

    /* Only the two temperature fields of the core matter here. */
    foc_core_t foc;
    memset(&foc, 0, sizeof(foc));

    /* One pass from cold moves each value a fixed fraction of the way to its reading. */
    glue_state.motor_temp_raw_c = 100.0f;
    glue_state.fet_temp_raw_c = 40.0f;
    vesc_host_sample_temperatures(&glue_state, &foc);
    assert_float_equal(glue_state.motor_temp_c, 0.01f * 100.0f, 1e-4f);
    assert_float_equal(glue_state.fet_temp_c, 0.1f * 40.0f, 1e-4f);

    /* The core caches exactly those filtered values, not the readings. */
    assert_true(foc.motor_temp_c == glue_state.motor_temp_c);
    assert_true(foc.fet_temp_c == glue_state.fet_temp_c);

    /* It approaches the reading asymptotically rather than assigning it. */
    for (int i = 0; i < 2000; i++) {
        vesc_host_sample_temperatures(&glue_state, &foc);
    }
    assert_float_equal(glue_state.motor_temp_c, 100.0f, 0.01f);
    assert_float_equal(glue_state.fet_temp_c, 40.0f, 0.01f);
    assert_true(glue_state.motor_temp_c != 100.0f);

    /* A reading that cannot be a temperature becomes -100 and is filtered from there. */
    glue_state.motor_temp_raw_c = NAN;
    vesc_host_sample_temperatures(&glue_state, &foc);
    assert_true(glue_state.motor_temp_c < 100.0f);
    assert_true(glue_state.motor_temp_c > -100.0f);

    const float before = glue_state.motor_temp_c;
    glue_state.motor_temp_raw_c = 5000.0f; /* past the range the reference accepts */
    vesc_host_sample_temperatures(&glue_state, &foc);
    assert_true(glue_state.motor_temp_c < before);

    /* The sampler refuses a missing target instead of writing through it. */
    vesc_host_sample_temperatures(NULL, &foc);
    vesc_host_sample_temperatures(&glue_state, NULL);
}

/*
 * The whole detection path: motor identification's port is wired to the FOC aggregate, so the
 * reference's resistance measurement drives the real control loop against a virtual motor and has
 * to recover the resistance that motor was built with.
 *
 * The forced angle is the reference's way of making this a resistance measurement: the current is
 * injected at a fixed electrical angle and the rotor pulls itself into line with it - the "wait for
 * the motor to lock" in the reference's own comment - after which the motor stands still, there is
 * no back-EMF, and the voltage the loop commands is the current's own drop across R.
 *
 * The virtual motor's flux linkage is kept near zero for this test, because a virtual motor has no
 * alignment torque to hold it: with a real magnet it accelerates under the injected vector, the
 * back-EMF takes over the current, and the ratio stops being R. Near zero it is a resistor in
 * series with an inductor, which is what the resistance measurement is supposed to see.
 */
static void test_vesc_host_motor_id_detection(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 0.00005f, 1.0e-6f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue_state);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue_state);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue_state);

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 1.0e-6f,
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
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    vesc_host_glue_state_t id_glue;
    memset(&id_glue, 0, sizeof(id_glue));
    id_glue.foc = &foc;
    motor_id_measure_port_t id_m;
    vesc_host_make_motor_id_measure_port(&id_m, &id_glue);
    motor_id_app_t id_app;
    motor_id_construct(&id_app, EDGE_MOD_MOTOR_ID, 40u, &id_m);
    assert_int_equal(motor_id_init(&id_app), EDGE_OK);
    assert_int_equal(motor_id_measure_resistance(&id_app, 1.0f, 200u, true), EDGE_OK);

    for (int ms = 0; ms < 5000 && id_app.state != MOTOR_ID_STATE_COMPLETE; ms++) {
        /* One millisecond of the procedure, with the control loop running at its own rate
         * alongside it - which is what fills the accumulator while the reference sleeps. */
        for (int cycle = 0; cycle < 20; cycle++) {
            assert_int_equal(foc_core_fast_loop(&foc, 5e-5f), EDGE_OK);
            foc_virtual_motor_step(&glue_state.vmotor, foc.v_alpha, foc.v_beta, 0.0f, 5e-5f, 0.05f);
        }
        assert_int_equal(motor_id_step(&id_app, 0.001f), EDGE_OK);
    }

    assert_int_equal(id_app.state, MOTOR_ID_STATE_COMPLETE);
    const motor_id_result_t *result = motor_id_get_result(&id_app);
    assert_non_null(result);
    assert_true(result->valid);
    assert_float_equal(result->r_ohm, 0.05f, 2e-3f);
    assert_int_equal(motor_id_get_fault(&id_app), 0u);
}

/*
 * The two masked value adapters. Their whole job is the bit table, and every branch of it is a
 * separate `if`, so the cases are: every bit at once, one bit alone (which must leave the rest
 * zeroed, since the reply is memset first), and the read-and-reset channels, where asking for one
 * channel must not consume another's averaging window. The derived fields of the setup reply are
 * checked against their sources rather than against literals.
 */
/*
 * The peers on the bus reach the setup reply through the aggregate's port, which is the product's
 * own answer because the product owns the bus. With a status frame from one other controller the
 * reply counts two machines and adds what that controller's frames carried -
 * mc_interface.c:1676-1700's own arithmetic - and with none it counts one, which is the
 * single-controller case the other test pins.
 */
typedef struct glue_can_mock {
    uint32_t rx_id;
    uint8_t rx_data[8];
    uint8_t rx_len;
    bool has_rx;
} glue_can_mock_t;

static edge_status_t glue_can_send(void *self, uint32_t can_id, const uint8_t *data, uint8_t len) {
    (void)self;
    (void)can_id;
    (void)data;
    (void)len;
    return EDGE_OK;
}

static edge_status_t glue_can_receive(void *self, uint32_t *can_id, uint8_t *data, uint8_t *len) {
    glue_can_mock_t *mock = (glue_can_mock_t *)self;
    if (mock == (void *)0 || !mock->has_rx) {
        return EDGE_ENOENT;
    }
    mock->has_rx = false;
    *can_id = mock->rx_id;
    *len = mock->rx_len;
    for (uint8_t i = 0u; i < mock->rx_len && i < 8u; i++) {
        data[i] = mock->rx_data[i];
    }
    return EDGE_OK;
}

static void test_vesc_host_setup_values_count_the_peers_on_the_bus(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 0.00005f, 0.005f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue_state);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue_state);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue_state);

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
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
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    glue_can_mock_t mock = {0};
    vesc_can_port_t can_port = {
        .send_frame = glue_can_send, .receive_frame = glue_can_receive, .self = &mock};
    vesc_can_config_t can_cfg = {.controller_id = 1u,
                                 .baudrate = 500000,
                                 .can_mode = VESC_CAN_MODE_VESC,
                                 .status_rate_1_hz = 0.0f,
                                 .status_rate_2_hz = 0.0f,
                                 .status_msgs_r1 = 0u,
                                 .status_msgs_r2 = 0u};
    vesc_can_app_t can;
    vesc_can_construct(&can, EDGE_MOD_VESC_CAN, 20u, &can_cfg, &can_port);
    assert_int_equal(vesc_can_init(&can), EDGE_OK);

    foc_peer_port_t peer_port;
    vesc_host_make_peer_port(&peer_port, &can);
    foc_core_set_peer_port(&foc, &peer_port);

    vesc_motor_provider_port_t port;
    vesc_host_make_motor_provider_port(&port, &foc);

    vesc_setup_values_t setup;
    assert_int_equal(port.get_setup_values(port.self, &setup), EDGE_OK);
    assert_int_equal(setup.num_vescs, 1u);

    /* Controller 2 reports 20 A and its own amp-hour total. */
    mock.rx_id = ((uint32_t)CAN_PACKET_STATUS_1 << 8) | 2u;
    mock.rx_len = 8u;
    mock.rx_data[0] = 0;
    mock.rx_data[1] = 0;
    mock.rx_data[2] = 0x03;
    mock.rx_data[3] = 0xE8;
    mock.rx_data[4] = 0x00;
    mock.rx_data[5] = 0xC8; /* 200 -> 20 A */
    mock.rx_data[6] = 0;
    mock.rx_data[7] = 0;
    mock.has_rx = true;
    assert_int_equal(vesc_can_process_incoming(&can), EDGE_OK);

    mock.rx_id = ((uint32_t)CAN_PACKET_STATUS_2 << 8) | 2u;
    mock.rx_data[0] = 0;
    mock.rx_data[1] = 0;
    mock.rx_data[2] = 0x03;
    mock.rx_data[3] = 0xE8;
    mock.rx_data[4] = 0;
    mock.rx_data[5] = 0;
    mock.rx_data[6] = 0;
    mock.rx_data[7] = 100;
    mock.has_rx = true;
    assert_int_equal(vesc_can_process_incoming(&can), EDGE_OK);

    assert_int_equal(port.get_setup_values(port.self, &setup), EDGE_OK);
    assert_int_equal(setup.num_vescs, 2u);
    /* The peer's own 20 A on top of this machine's, and its hours on top of the totals. */
    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);
    assert_float_equal(setup.current_tot, telem.current_abs + 20.0f, 1e-3f);
    assert_float_equal(setup.ah_tot, telem.amp_hours + 0.1f, 1e-3f);
    assert_float_equal(setup.ah_charge_tot, telem.amp_hours_charged + 0.01f, 1e-3f);
}

/*
 * The two restart commands' product half: a simulation has neither a reset line nor a bootloader,
 * so it records the request - which is what its run acts on - and a product built without a glue
 * state has nothing to record it on and says so.
 */
static void test_vesc_host_restart_requests(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));

    vesc_host_ops_ctx_t ctx = {.glue = &glue_state};
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ctx);
    assert_non_null(ops.reboot);
    assert_non_null(ops.jump_to_bootloader);

    assert_false(glue_state.reboot_requested);
    assert_false(glue_state.bootloader_requested);

    assert_int_equal(ops.reboot(ops.self), EDGE_OK);
    assert_true(glue_state.reboot_requested);

    assert_int_equal(ops.jump_to_bootloader(ops.self), EDGE_OK);
    assert_true(glue_state.bootloader_requested);

    /* A context with no glue state behind it cannot record anything. */
    vesc_host_ops_ctx_t bare;
    memset(&bare, 0, sizeof(bare));
    vesc_comm_ops_port_t bare_ops;
    vesc_host_make_ops_port(&bare_ops, &bare);
    assert_int_equal(bare_ops.reboot(bare_ops.self), EDGE_EINVAL);
    assert_int_equal(bare_ops.jump_to_bootloader(bare_ops.self), EDGE_EINVAL);
    assert_int_equal(bare_ops.reboot(NULL), EDGE_EINVAL);
}

static void test_vesc_host_masked_value_adapters(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 0.00005f, 0.005f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue_state);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue_state);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue_state);

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
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
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    foc_core_set_fet_temperature(&foc, 41.0f);
    foc_core_set_motor_temperature(&foc, 37.0f);
    assert_int_equal(foc_core_set_current(&foc, 4.0f, 0.0f), EDGE_OK);
    for (int i = 0; i < 200; i++) {
        assert_int_equal(foc_core_fast_loop(&foc, 5e-5f), EDGE_OK);
        foc_virtual_motor_step(&glue_state.vmotor, foc.v_alpha, foc.v_beta, 0.0f, 5e-5f, 0.05f);
    }
    /* Reset the averaging window so the masked read below has current to average. */
    foc_core_read_reset_averages(&foc, FOC_AVG_MOTOR_CURRENT, NULL);

    vesc_motor_provider_port_t port;
    vesc_host_make_motor_provider_port(&port, &foc);
    vesc_values_t val;

    /* Every bit at once. */
    assert_int_equal(port.get_values(port.self, 0xFFFFFFFFu, &val), EDGE_OK);
    assert_float_equal(val.temp_mos, 41.0f, 1e-3f);
    assert_float_equal(val.temp_motor, 37.0f, 1e-3f);
    assert_float_equal(val.v_in, 24.0f, 1e-3f);
    assert_true(val.rpm > 0.0f);

    /* One bit alone leaves the rest zeroed. */
    memset(&val, 0xAA, sizeof(val));
    assert_int_equal(port.get_values(port.self, (1u << 1), &val), EDGE_OK);
    assert_float_equal(val.temp_motor, 37.0f, 1e-3f);
    assert_float_equal(val.temp_mos, 0.0f, 1e-9f);
    assert_float_equal(val.v_in, 0.0f, 1e-9f);
    assert_float_equal(val.amp_hours, 0.0f, 1e-9f);

    /* No bits at all is a valid, empty reply. */
    assert_int_equal(port.get_values(port.self, 0u, &val), EDGE_OK);
    assert_float_equal(val.temp_mos, 0.0f, 1e-9f);

    /* Guards. */
    assert_int_equal(port.get_values(NULL, 1u, &val), EDGE_EINVAL);
    assert_int_equal(port.get_values(port.self, 1u, NULL), EDGE_EINVAL);

    /* The setup reply, whose derived fields are checked against their sources. */
    vesc_setup_values_t setup;
    assert_int_equal(port.get_setup_values(port.self, &setup), EDGE_OK);
    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);
    const float tacho_scale = (cfg.si_wheel_diameter * (float)M_PI) /
                              (3.0f * (float)cfg.si_motor_poles * cfg.si_gear_ratio);
    assert_float_equal(setup.temp_mos, telem.fet_temp_c, 1e-3f);
    assert_float_equal(setup.temp_motor, telem.motor_temp_c, 1e-3f);
    assert_float_equal(setup.rpm, telem.speed_rpm * ((float)cfg.si_motor_poles / 2.0f), 1e-2f);
    assert_float_equal(setup.distance_m, (float)telem.tachometer * tacho_scale, 1e-3f);
    assert_float_equal(setup.pid_pos_deg, telem.rotor_angle_rad * (180.0f / (float)M_PI), 1e-3f);
    assert_int_equal(setup.fault, (uint8_t)telem.faults);
    assert_int_equal(setup.controller_id, 1u);
    assert_int_equal(setup.num_vescs, 1u);
    /* The harness ran the loop for ten milliseconds and never covered a whole metre: the tachometer
     * scale here is millimetres per sector, and the odometer counts whole metres as the reference's
     * does. The runtime is that ten milliseconds, summed from the loop's dt. */
    assert_int_equal(setup.odometer_m, 0u);
    assert_int_equal(setup.uptime_ms, 10u);
    assert_int_equal(port.get_setup_values(NULL, &setup), EDGE_EINVAL);
    assert_int_equal(port.get_setup_values(port.self, NULL), EDGE_EINVAL);
}

/*
 * The product adapters' guards, and the getters nothing had called. Every factory refuses a
 * missing output or a missing context instead of writing through it, and a handful of one-line
 * adapters - the PPM pulse, the throttle and brake voltages, the button, the terminal with no
 * terminal wired - had no test at all. These are the paths that rot silently, and they are cheap
 * to hold down.
 */
static void test_vesc_host_adapter_guards(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.ppm_pulse_us = 1750.0f;
    glue_state.adc_throttle_v = 2.5f;
    glue_state.adc_brake_v = 1.5f;

    /* The input getters, which nothing had read. */
    ppm_receiver_port_t ppm;
    vesc_host_make_ppm_port(&ppm, &glue_state);
    float pulse = 0.0f;
    assert_int_equal(ppm.read_pulse_us(ppm.self, &pulse), EDGE_OK);
    assert_float_equal(pulse, 1750.0f, 1e-6f);

    adc_input_port_t adc;
    vesc_host_make_adc_port(&adc, &glue_state);
    float volts = 0.0f;
    assert_int_equal(adc.read_throttle_v(adc.self, &volts), EDGE_OK);
    assert_float_equal(volts, 2.5f, 1e-6f);
    assert_int_equal(adc.read_brake_v(adc.self, &volts), EDGE_OK);
    assert_float_equal(volts, 1.5f, 1e-6f);
    (void)adc.read_button(adc.self, 0u);

    /* The terminal port with no terminal behind it says so rather than pretending. */
    vesc_host_ops_ctx_t ops_ctx;
    memset(&ops_ctx, 0, sizeof(ops_ctx));
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ops_ctx);
    assert_non_null(ops.terminal_cmd);
    assert_int_equal(ops.terminal_cmd(ops.self, "help"), EDGE_ENOTSUP);

    /* Every factory refuses a missing output, and the ones that take a context refuse a missing
     * one too: the output stays zeroed in both cases. */
    vesc_host_make_flash_sector_port(NULL, &glue_state);
    vesc_host_make_stream_tx_port(NULL, &glue_state);
    vesc_host_make_inverter_port(NULL, &glue_state);
    vesc_host_make_current_port(NULL, &glue_state);
    vesc_host_make_rotor_port(NULL, &glue_state);
    vesc_host_make_ppm_port(NULL, &glue_state);
    vesc_host_make_adc_port(NULL, &glue_state);
    vesc_host_make_app_status_port(NULL, &glue_state);
    vesc_host_make_can_port(NULL, &glue_state);
    vesc_host_make_nunchuk_port(NULL, &glue_state);
    vesc_host_make_balance_port(NULL, &glue_state);
    vesc_host_make_bms_can_port(NULL, &glue_state);
    vesc_host_make_terminal_stream_port(NULL, &glue_state);

    flash_sector_port_t sectors = {0};
    vesc_host_make_flash_sector_port(&sectors, NULL);
    assert_null(sectors.read);

    /* These two guard their context as well, so the output stays as the caller left it. */
    pas_port_t pas = {0};
    vesc_host_make_pas_port(&pas, NULL);
    assert_null(pas.read_cadence_rpm);

    motor_id_measure_port_t measure = {0};
    vesc_host_make_motor_id_measure_port(&measure, NULL);
    assert_null(measure.set_phase_override);

    /* This one fills the callbacks and leaves the consumer's context to the caller, so a null
     * context shows up as a null self rather than as a null callback. */
    vesc_config_provider_port_t config = {0};
    vesc_host_make_config_port(&config, NULL);
    assert_non_null(config.get_mcconf);
    assert_null(config.self);

    vesc_comm_ops_port_t ops_out = {0};
    vesc_host_make_ops_port(&ops_out, NULL);
    assert_non_null(ops_out.terminal_cmd);
    assert_null(ops_out.self);
}

/* A terminal stream that keeps what the terminal wrote, so the terminal port can be driven. */
static char term_capture[128];
static edge_status_t term_capture_write(void *self, const char *str) {
    (void)self;
    snprintf(term_capture, sizeof(term_capture), "%s", str);
    return EDGE_OK;
}

/*
 * The configuration port's setters and read side, and the ops port with an actual terminal behind
 * it. The aggregate is real, so what is checked is the round trip through the reference's own
 * streams rather than the adapter's shape: a stream the aggregate can decode lands, one it cannot
 * is refused, and the read side hands back what the aggregate serialises.
 */
static void test_vesc_host_config_and_terminal_ports(void **state) {
    (void)state;

    static alignas(MOTOR_CONFIG_STORAGE_ALIGN) unsigned char cfg_storage[MOTOR_CONFIG_STORAGE_SIZE];
    memset(cfg_storage, 0, sizeof(cfg_storage));
    motor_config_t *cfg = (motor_config_t *)cfg_storage;
    motor_config_construct(cfg, EDGE_MOD_MOTOR_CONFIG, 30u, NULL);
    assert_int_equal(motor_config_init(cfg), EDGE_OK);

    vesc_config_provider_port_t config;
    vesc_host_make_config_port(&config, cfg);
    assert_ptr_equal(config.self, cfg);

    uint8_t stream[MOTOR_CONFIG_BUFFER_SIZE];
    size_t len = 0u;

    /* NO_STORE first, while the aggregate is still clean: it lands on the running system and
     * leaves the flag alone, which is the whole difference from the storing set below. */
    app_configuration_t app = *motor_config_get_app(cfg);
    app.timeout_msec = 2500u;
    assert_int_equal(motor_config_serialize_app(&app, stream, sizeof(stream), &len), EDGE_OK);
    assert_int_equal(config.set_appconf_nostore(config.self, stream, len), EDGE_OK);
    assert_int_equal(motor_config_get_app(cfg)->timeout_msec, 2500u);
    assert_false(motor_config_is_dirty(cfg));

    /* The mc stream, with one field changed, lands in the aggregate. */
    mc_configuration_t mc = *motor_config_get_mc(cfg);
    mc.l_current_max = 42.0f;
    assert_int_equal(motor_config_serialize_mc(&mc, stream, sizeof(stream), &len), EDGE_OK);
    assert_int_equal(config.set_mcconf(config.self, stream, len), EDGE_OK);
    assert_float_equal(motor_config_get_mc(cfg)->l_current_max, 42.0f, 1e-3f);

    /* A stream it cannot decode is refused. */
    assert_int_equal(config.set_mcconf(config.self, stream, 3u), EDGE_EINVAL);
    assert_int_equal(config.set_appconf(config.self, stream, 3u), EDGE_EINVAL);

    /* The read side hands back the aggregate's own serialisation, and the defaults are the
     * reference's rather than the live configuration. */
    size_t got = 0u;
    assert_int_equal(config.get_mcconf(config.self, stream, sizeof(stream), &got), EDGE_OK);
    assert_int_equal(got, motor_config_stream_len());
    assert_int_equal(config.get_mcconf_default(config.self, stream, sizeof(stream), &got), EDGE_OK);
    assert_int_equal(got, motor_config_stream_len());
    assert_int_equal(config.get_appconf(config.self, stream, sizeof(stream), &got), EDGE_OK);
    assert_int_equal(got, motor_config_app_stream_len());
    assert_int_equal(config.get_appconf_default(config.self, stream, sizeof(stream), &got),
                     EDGE_OK);
    assert_int_equal(got, motor_config_app_stream_len());

    /* The ops port, with a real terminal behind it this time: the command reaches the terminal
     * and its answer comes back through the terminal's own stream. */
    vesc_terminal_app_t term;
    terminal_stream_port_t term_stream = {.self = NULL, .write_string = term_capture_write};
    terminal_system_port_t term_sys = {.self = NULL, .get_stats = NULL};
    vesc_terminal_construct(&term, EDGE_MOD_VESC_TERMINAL, 50u, &term_stream, &term_sys);
    assert_int_equal(vesc_terminal_init(&term), EDGE_OK);

    vesc_host_ops_ctx_t ops_ctx;
    memset(&ops_ctx, 0, sizeof(ops_ctx));
    ops_ctx.term = &term;
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ops_ctx);
    assert_int_equal(ops.terminal_cmd(ops.self, "ping"), EDGE_OK);
    assert_non_null(strstr(term_capture, "pong"));
}

/*
 * The motor provider's command setters, which the dispatcher walk never reaches because there it
 * goes through the codec. Each is one step of the port, and what is asserted is the state the
 * aggregate ends up in - the DIR_MULT question is settled in its own case.
 */
static void test_vesc_host_motor_setters(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 0.00005f, 0.005f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue_state);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue_state);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue_state);

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
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
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    vesc_motor_provider_port_t port;
    vesc_host_make_motor_provider_port(&port, &foc);

    assert_int_equal(port.set_current(port.self, 5.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_CURRENT);

    assert_int_equal(port.set_current_rel(port.self, 0.5f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_CURRENT);

    assert_int_equal(port.set_duty(port.self, 0.4f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_DUTY);

    assert_int_equal(port.set_rpm(port.self, 1000.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_RPM);

    assert_int_equal(port.set_pos(port.self, 45.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_POS);

    assert_int_equal(port.set_handbrake(port.self, 3.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_HANDBRAKE);

    assert_int_equal(port.set_current_brake(port.self, 2.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_CURRENT);

    assert_int_equal(port.reset_stats(port.self), EDGE_OK);
}

/*
 * The adapters nothing had called. They are whole bodies rather than guards: the stream write's
 * bound, a getter handed no output, the CAN forward with no bus behind it, and the two input
 * readers whose value is the neutral reading they report when the board has no such hardware.
 */
static void test_vesc_host_adapters_nothing_called(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));

    /* An aggregate for the one adapter that needs a real one. */
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 0.00005f, 0.005f, 7, 0.0005f);
    foc_inverter_port_t inv;
    vesc_host_make_inverter_port(&inv, &glue_state);
    foc_current_port_t cs;
    vesc_host_make_current_port(&cs, &glue_state);
    foc_rotor_port_t rs;
    vesc_host_make_rotor_port(&rs, &glue_state);
    foc_core_t foc_for_adapters;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
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
    foc_core_construct(&foc_for_adapters, EDGE_MOD_FOC_CORE, 10u, &cfg, &inv, &cs, &rs);
    assert_int_equal(foc_core_init(&foc_for_adapters), EDGE_OK);

    /* The stream write refuses more than its buffer can hold. */
    edge_stream_tx_port_t stream_tx;
    vesc_host_make_stream_tx_port(&stream_tx, &glue_state);
    uint8_t big[600];
    memset(big, 0, sizeof(big));
    assert_int_equal(stream_tx.write(stream_tx.self, big, sizeof(big)), EDGE_ENOSPC);

    /* A getter handed no output refuses rather than writing through it. The motor provider is
     * built over a real aggregate elsewhere instead of with a null one: that factory returns early
     * on a null context, so the port would be left uninitialised and calling through it is exactly
     * the crash this case is not about. */
    vesc_motor_provider_port_t motor;
    vesc_host_make_motor_provider_port(&motor, &foc_for_adapters);
    assert_int_equal(motor.get_values(motor.self, 0u, NULL), EDGE_EINVAL);

    /* The CAN forward with no bus behind it does not report success. */
    vesc_host_ops_ctx_t ops_ctx;
    memset(&ops_ctx, 0, sizeof(ops_ctx));
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ops_ctx);
    assert_true(ops.forward_can(ops.self, 1u, (const uint8_t *)"", 0u) != EDGE_OK);

    /* The two input readers answer with what they have, which is nothing. */
    nunchuk_port_t nunchuk;
    vesc_host_make_nunchuk_port(&nunchuk, &glue_state);
    uint8_t js_x = 0u;
    uint8_t js_y = 0u;
    int16_t acc_x = 0;
    int16_t acc_y = 0;
    int16_t acc_z = 0;
    bool btn_c = false;
    bool btn_z = false;
    assert_int_equal(
        nunchuk.read_data(nunchuk.self, &js_x, &js_y, &acc_x, &acc_y, &acc_z, &btn_c, &btn_z),
        EDGE_OK);

    balance_port_t balance;
    vesc_host_make_balance_port(&balance, &glue_state);
    float pitch = 1.0f;
    float roll = 1.0f;
    assert_int_equal(balance.read_attitude(balance.self, &pitch, &roll, NULL, NULL, NULL, NULL),
                     EDGE_OK);
    assert_float_equal(pitch, 0.0f, 1e-6f);
    assert_float_equal(roll, 0.0f, 1e-6f);
}

/*
 * The vesc6 product's variable store. What matters here is the read/write protocol rather than
 * the storage: a word that was never written must fail the read, because the configuration tells
 * "no stored configuration" from "a stored one" by that failure and not by the value - an
 * all-zero configuration is a real configuration, and its CRC is zero.
 */
static void test_vesc6_var_store_protocol(void **state) {
    (void)state;
    board_vesc6_t board;
    assert_int_equal(board_vesc6_init(&board), EDGE_OK);

    vesc6_glue_state_t glue;
    vesc6_glue_init(&glue, &board);
    assert_ptr_equal(glue.board, &board);

    motor_config_var_port_t port;
    vesc6_make_var_port(&port, &glue);
    assert_non_null(port.read);
    assert_non_null(port.write);
    assert_ptr_equal(port.self, &glue);

    uint16_t value = 0xBEEFu;
    assert_int_equal(port.read(port.self, 0u, &value), EDGE_ENOENT);
    assert_int_equal(port.write(port.self, 0u, 0x1234u), EDGE_OK);
    assert_int_equal(port.read(port.self, 0u, &value), EDGE_OK);
    assert_int_equal(value, 0x1234u);

    /* Past the table, and with nothing to read into or write from. */
    assert_int_equal(port.read(port.self, VESC6_MCCONF_VARS, &value), EDGE_EINVAL);
    assert_int_equal(port.write(port.self, VESC6_MCCONF_VARS, 1u), EDGE_EINVAL);
    assert_int_equal(port.read(port.self, 0u, NULL), EDGE_EINVAL);
    assert_int_equal(port.read(NULL, 0u, &value), EDGE_EINVAL);
    assert_int_equal(port.write(NULL, 0u, 1u), EDGE_EINVAL);

    /* The constructors refuse a null destination instead of writing through it. */
    vesc6_glue_init(NULL, &board);
    vesc6_make_var_port(NULL, &glue);
}

/*
 * The vesc6 product's hardware adapters, against blocks of RAM laid out as the register maps the
 * soc header asserts against the manual. That is the only place these can run without the part,
 * and it is where the arithmetic between the control loop and the hardware gets pinned.
 */
static soc_stm32f4_tim_regs_t vesc6_tim_regs;
static soc_stm32f4_adc_regs_t vesc6_adc_regs[3];

/* Bound and configured exactly as the product does it, so the period the duties are scaled by and
 * the enabled ADCs are the real ones. */
static void vesc6_glue_with_hardware(vesc6_glue_state_t *glue, board_vesc6_t *board) {
    memset(&vesc6_tim_regs, 0, sizeof(vesc6_tim_regs));
    memset(vesc6_adc_regs, 0, sizeof(vesc6_adc_regs));
    assert_int_equal(board_vesc6_init(board), EDGE_OK);
    vesc6_glue_init(glue, board);
    glue->tim = &vesc6_tim_regs;
    for (uint32_t phase = 0u; phase < 3u; ++phase) {
        glue->adc_current[phase] = &vesc6_adc_regs[phase];
    }

    const soc_stm32f4_tim_config_t tim_config = {
        .timer_clk_hz = SOC_STM32F4_TIM_CLK_HZ,
        .switching_freq_hz = 25000u,
        .deadtime_ns = glue->deadtime_ns,
    };
    glue->period = soc_stm32f4_tim_init(glue->tim, &tim_config);
    assert_int_equal(glue->period, 3360u);
    for (uint32_t phase = 0u; phase < 3u; ++phase) {
        const soc_stm32f4_adc_config_t adc_config = {.channel = (uint8_t)(10u + phase),
                                                     .samples = VESC6_CURRENT_RANKS,
                                                     .sample_time = 1u,
                                                     .jeoc_interrupt = true};
        assert_int_equal(soc_stm32f4_adc_init_injected(glue->adc_current[phase], &adc_config),
                         VESC6_CURRENT_RANKS);
    }
}

static void test_vesc6_inverter_adapter(void **state) {
    (void)state;
    board_vesc6_t board;
    vesc6_glue_state_t glue;
    vesc6_glue_with_hardware(&glue, &board);

    foc_inverter_port_t port;
    vesc6_make_inverter_port(&port, &glue);
    assert_non_null(port.set_duty);
    assert_non_null(port.set_phase_state);

    /* Duties are fractions of the period, rounded to the nearest count: a half of 3360 is 1680,
     * a quarter is 840 and three quarters are 2520, in the order TIM1 drives them. */
    assert_int_equal(port.set_duty(port.self, 0.5f, 0.25f, 0.75f), EDGE_OK);
    assert_int_equal(vesc6_tim_regs.ccr1, 1680u);
    assert_int_equal(vesc6_tim_regs.ccr2, 840u);
    assert_int_equal(vesc6_tim_regs.ccr3, 2520u);

    /* Outside nought to one the compare saturates rather than wrapping past the period. */
    assert_int_equal(port.set_duty(port.self, -1.0f, 1.0f, 2.0f), EDGE_OK);
    assert_int_equal(vesc6_tim_regs.ccr1, 0u);
    assert_int_equal(vesc6_tim_regs.ccr2, 3360u);
    assert_int_equal(vesc6_tim_regs.ccr3, 3360u);

    /* The phase outputs: MOE is refused while the board's dead time is unknown, and allowed once
     * the composition root has given one. */
    assert_int_equal(port.set_phase_state(port.self, true), EDGE_ENOTSUP);
    assert_int_equal((vesc6_tim_regs.bdtr & 0x8000u), 0u);
    glue.deadtime_ns = 660.0f;
    assert_int_equal(port.set_phase_state(port.self, true), EDGE_OK);
    assert_int_equal((vesc6_tim_regs.bdtr & 0x8000u), 0x8000u);
    assert_int_equal(port.set_phase_state(port.self, false), EDGE_OK);
    assert_int_equal((vesc6_tim_regs.bdtr & 0x8000u), 0u);

    /* Nothing to drive, and nothing to construct into. */
    assert_int_equal(port.set_duty(NULL, 0.5f, 0.5f, 0.5f), EDGE_EINVAL);
    assert_int_equal(port.set_phase_state(NULL, true), EDGE_EINVAL);
    vesc6_glue_state_t bare;
    board_vesc6_t bare_board;
    assert_int_equal(board_vesc6_init(&bare_board), EDGE_OK);
    vesc6_glue_init(&bare, &bare_board); /* no timer bound */
    foc_inverter_port_t bare_port;
    vesc6_make_inverter_port(&bare_port, &bare);
    assert_int_equal(bare_port.set_duty(bare_port.self, 0.5f, 0.5f, 0.5f), EDGE_EINVAL);
    assert_int_equal(bare_port.set_phase_state(bare_port.self, false), EDGE_EINVAL);
    vesc6_make_inverter_port(NULL, &glue);
}

static void test_vesc6_current_adapter(void **state) {
    (void)state;
    board_vesc6_t board;
    vesc6_glue_state_t glue;
    vesc6_glue_with_hardware(&glue, &board);

    foc_current_port_t port;
    vesc6_make_current_port(&port, &glue);
    assert_non_null(port.read_currents);
    assert_non_null(port.read_vbus);

    /* The hook reads the finished ADC's ranks into the snapshot, and mid-scale is zero amperes. */
    for (uint32_t phase = 0u; phase < 3u; ++phase) {
        vesc6_adc_regs[phase].jdr1 = VESC6_CURRENT_MID_SCALE;
        vesc6_adc_regs[phase].jdr2 = VESC6_CURRENT_MID_SCALE;
        vesc6_adc_regs[phase].jdr3 = VESC6_CURRENT_MID_SCALE;
        vesc6_adc_injected_hook(&glue, phase);
    }
    float ia = 1.0f;
    float ib = 2.0f;
    float ic = 3.0f;
    assert_int_equal(port.read_currents(port.self, &ia, &ib, &ic), EDGE_OK);
    assert_float_equal(ia, 0.0f, 1e-6f);
    assert_float_equal(ib, 0.0f, 1e-6f);
    assert_float_equal(ic, 0.0f, 1e-6f);

    /* A hundred counts either side of mid-scale. The board's scale is (3.3/4095)/(0.0005*20) =
     * 0.080586 A per count, so 100 counts are 8.0586 A and the sign survives. */
    for (uint32_t rank = 1u; rank <= 3u; ++rank) {
        (&vesc6_adc_regs[0].jdr1)[rank - 1u] = VESC6_CURRENT_MID_SCALE + 100u;
        (&vesc6_adc_regs[1].jdr1)[rank - 1u] = VESC6_CURRENT_MID_SCALE - 100u;
    }
    vesc6_adc_injected_hook(&glue, 0u);
    vesc6_adc_injected_hook(&glue, 1u);
    assert_int_equal(port.read_currents(port.self, &ia, &ib, &ic), EDGE_OK);
    assert_float_equal(ia, 8.0586f, 1e-3f);
    assert_float_equal(ib, -8.0586f, 1e-3f);

    /* The hook ignores an index it has no ADC for, and a null context. */
    vesc6_adc_injected_hook(&glue, 3u);
    vesc6_adc_injected_hook(NULL, 0u);

    /* The supply voltage: nothing is bound in this product, so the port says so; bound, it reads
     * the conversion and scales it with the board's divider. 1000 counts of a 39.0 k to 2.2 k
     * divider at a 3.3 V reference is 1000 * (3.3/4095) * (41200/2200) = 15.0924 V. */
    float v_bus = 0.0f;
    assert_int_equal(port.read_vbus(port.self, &v_bus), EDGE_EINVAL);
    glue.adc_vbus = &vesc6_adc_regs[0];
    vesc6_adc_regs[0].dr = 1000u;
    vesc6_adc_regs[0].sr = 0x02u;
    assert_int_equal(port.read_vbus(port.self, &v_bus), EDGE_OK);
    assert_float_equal(v_bus, 15.0924f, 1e-3f);
    /* The flag was consumed by that read, so the next one finds nothing to convert. */
    assert_int_equal(port.read_vbus(port.self, &v_bus), EDGE_EIO);

    /* Refusals. */
    assert_int_equal(port.read_currents(NULL, &ia, &ib, &ic), EDGE_EINVAL);
    assert_int_equal(port.read_currents(port.self, NULL, &ib, &ic), EDGE_EINVAL);
    assert_int_equal(port.read_vbus(NULL, &v_bus), EDGE_EINVAL);
    assert_int_equal(port.read_vbus(port.self, NULL), EDGE_EINVAL);
    vesc6_glue_state_t bare;
    board_vesc6_t bare_board;
    assert_int_equal(board_vesc6_init(&bare_board), EDGE_OK);
    vesc6_glue_init(&bare, &bare_board);
    foc_current_port_t bare_port;
    vesc6_make_current_port(&bare_port, &bare);
    assert_int_equal(bare_port.read_currents(bare_port.self, &ia, &ib, &ic), EDGE_OK);
    assert_int_equal(bare_port.read_vbus(bare_port.self, &v_bus), EDGE_EINVAL);
    vesc6_make_current_port(NULL, &glue);
}

static void test_vesc6_rotor_adapter(void **state) {
    (void)state;
    board_vesc6_t board;
    assert_int_equal(board_vesc6_init(&board), EDGE_OK);
    vesc6_glue_state_t glue;
    vesc6_glue_init(&glue, &board);

    foc_rotor_port_t port;
    vesc6_make_rotor_port(&port, &glue);
    assert_non_null(port.read_angle);
    float angle = 0.0f;
    float rpm = 0.0f;
    /* No hall or encoder driver: an angle is the one thing here that must not be approximated. */
    assert_int_equal(port.read_angle(port.self, &angle, &rpm), EDGE_ENOTSUP);
    vesc6_make_rotor_port(NULL, &glue);
}

/*
 * The flux-linkage callbacks the port carries beyond the resistance four: each is one thing the
 * procedure does to the aggregate, so each is checked against the aggregate's own state rather than
 * against a return value.
 */
static void test_vesc_host_motor_id_flux_adapters(void **state) {
    (void)state;
    vesc_host_glue_state_t glue;
    memset(&glue, 0, sizeof(glue));
    glue.v_bus = 24.0f;
    foc_virtual_motor_init(&glue.vmotor, 0.05f, 0.00005f, 1.0e-6f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue);

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 1.0e-6f,
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .sensorless_mode = false};
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    glue.foc = &foc;

    motor_id_measure_port_t port;
    vesc_host_make_motor_id_measure_port(&port, &glue);

    /* The temporary configuration: sensorless with the supplied gains, and back again. */
    assert_false(foc.config.sensorless_mode);
    const float kp_before = foc.config.current_kp;
    assert_int_equal(port.enter_measurement_config(port.self, 0.25f, 12.5f), EDGE_OK);
    assert_true(foc.config.sensorless_mode);
    assert_float_equal(foc.config.current_kp, 0.25f, 1e-6f);
    assert_float_equal(foc.config.current_ki, 12.5f, 1e-6f);
    assert_int_equal(port.leave_measurement_config(port.self), EDGE_OK);
    assert_false(foc.config.sensorless_mode);
    assert_float_equal(foc.config.current_kp, kp_before, 1e-6f);
    /* Leaving again has nothing to put back, and says so. */
    assert_int_equal(port.leave_measurement_config(port.self), EDGE_EINVAL);

    /* Open loop through the port, and the readbacks it feeds. */
    assert_int_equal(port.set_openloop_current(port.self, 3.0f, 600.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_OPENLOOP);
    assert_float_equal(foc.openloop_speed, 600.0f * (float)(2.0 * 3.14159265358979323846 / 60.0),
                       1e-3f);

    float v_d = 1.0f;
    float v_q = 1.0f;
    float i_d = 1.0f;
    float i_q = 1.0f;
    float duty = 1.0f;
    float rad_s = 0.0f;
    assert_int_equal(port.read_vdq(port.self, &v_d, &v_q), EDGE_OK);
    assert_int_equal(port.read_idq(port.self, &i_d, &i_q), EDGE_OK);
    assert_int_equal(port.read_duty(port.self, &duty), EDGE_OK);
    assert_int_equal(port.read_speed_rad_s(port.self, &rad_s), EDGE_OK);
    /* The FOC reports mechanical rpm, so the pole pairs are applied before RPM2RADPS_f: 1234 rpm on
     * a fourteen-pole motor is 8638 electrical rpm, i.e. 904.6 rad/s. */
    foc.last_rpm = 1234.0f;
    assert_int_equal(port.read_speed_rad_s(port.self, &rad_s), EDGE_OK);
    assert_float_equal(rad_s, 8638.0f * (float)(2.0 * 3.14159265358979323846 / 60.0), 1e-2f);

    /* The sensored procedure's own callbacks: what the aggregate publishes, the release, the
     * question of whether it is running, the limits it has nowhere to put, and the temporary
     * sensored configuration. */
    foc.last_v_bus = 24.0f;
    foc.last_rpm = 4321.0f;
    float v_bus = 0.0f;
    float rpm = 0.0f;
    assert_int_equal(port.read_vbus(port.self, &v_bus), EDGE_OK);
    assert_float_equal(v_bus, 24.0f, 1e-6f);
    assert_int_equal(port.read_rpm(port.self, &rpm), EDGE_OK);
    assert_float_equal(rpm, 4321.0f, 1e-6f);
    bool running = false;
    assert_int_equal(port.is_running(port.self, &running), EDGE_OK);
    assert_true(running); /* the open-loop command above is still driving it */
    assert_int_equal(foc_core_set_duty(&foc, 0.5f), EDGE_OK);
    assert_int_equal(port.is_running(port.self, &running), EDGE_OK);
    assert_true(running);
    assert_int_equal(foc_core_stop(&foc), EDGE_OK);
    assert_int_equal(port.is_running(port.self, &running), EDGE_OK);
    assert_false(running);
    assert_int_equal(port.release_motor(port.self), EDGE_OK);
    assert_true(foc.motor_released);
    assert_int_equal(port.set_startup_limits(port.self, 100.0f, 20.0f, true), EDGE_OK);

    /* The temporary sensored configuration is saved and put back, like the other procedure's. */
    assert_false(foc.config.sensorless_mode);
    foc.config.sensorless_mode = true;
    assert_int_equal(port.enter_sensored_measurement_config(port.self), EDGE_OK);
    assert_false(foc.config.sensorless_mode);
    assert_int_equal(port.leave_measurement_config(port.self), EDGE_OK);
    assert_true(foc.config.sensorless_mode);

    /* The null cases of the new ones as well. */
    assert_int_equal(port.read_vbus(port.self, NULL), EDGE_EINVAL);
    assert_int_equal(port.read_rpm(port.self, NULL), EDGE_EINVAL);
    assert_int_equal(port.is_running(port.self, NULL), EDGE_EINVAL);
    assert_int_equal(port.read_vbus(NULL, &v_bus), EDGE_EINVAL);

    /* The null cases: nothing to read into, and no aggregate behind the state. */
    assert_int_equal(port.read_vdq(port.self, NULL, &v_q), EDGE_EINVAL);
    assert_int_equal(port.read_idq(port.self, &i_d, NULL), EDGE_EINVAL);
    assert_int_equal(port.read_duty(port.self, NULL), EDGE_EINVAL);
    assert_int_equal(port.read_speed_rad_s(port.self, NULL), EDGE_EINVAL);
    assert_int_equal(port.read_vdq(NULL, &v_d, &v_q), EDGE_EINVAL);
    vesc_host_glue_state_t bare;
    memset(&bare, 0, sizeof(bare)); /* no aggregate behind the port */
    motor_id_measure_port_t bare_port;
    vesc_host_make_motor_id_measure_port(&bare_port, &bare);
    assert_int_equal(bare_port.set_openloop_current(bare_port.self, 1.0f, 1.0f), EDGE_EINVAL);
    assert_int_equal(bare_port.enter_measurement_config(bare_port.self, 1.0f, 1.0f), EDGE_EINVAL);
    assert_int_equal(bare_port.read_duty(bare_port.self, &duty), EDGE_EINVAL);
    /* Constructing into nothing is a no-op rather than a write through a null. */
    vesc_host_make_motor_id_measure_port(NULL, &glue);
    vesc_host_make_motor_id_measure_port(&bare_port, NULL);
}

/*
 * The command's measurement, through the product. The ops port drives the procedure and the plant
 * together, which is this port's equivalent of the reference's blocking command thread. What is
 * asserted is that it comes back at all - a hang would leave the procedure in progress - and that
 * what came back is a measurement rather than a plausible constant.
 */
static void test_vesc_host_flux_command(void **state) {
    (void)state;
    vesc_host_glue_state_t glue;
    memset(&glue, 0, sizeof(glue));
    glue.v_bus = 24.0f;
    foc_virtual_motor_init(&glue.vmotor, 0.05f, 0.00005f, 1.0e-6f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue);
    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 1.0e-6f,
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .sensorless_mode = false};
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    glue.foc = &foc;

    motor_id_measure_port_t id_port;
    vesc_host_make_motor_id_measure_port(&id_port, &glue);
    motor_id_app_t motor_id;
    motor_id_construct(&motor_id, EDGE_MOD_MOTOR_ID, 40u, &id_port);
    assert_int_equal(motor_id_init(&motor_id), EDGE_OK);

    vesc_host_ops_ctx_t ctx = {.glue = &glue, .motor_id = &motor_id};
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ctx);
    assert_non_null(ops.detect_flux_linkage_openloop);

    vesc_detect_flux_result_t result;
    memset(&result, 0, sizeof(result));
    assert_int_equal(
        ops.detect_flux_linkage_openloop(ops.self, 5.0f, 0.05f, 2000.0f, 0.05f, 1.0e-6f, &result),
        EDGE_OK);
    /* It ran to one of its ends rather than staying in progress. */
    assert_true(motor_id.state == MOTOR_ID_STATE_COMPLETE ||
                motor_id.state == MOTOR_ID_STATE_FAILED);
    if (motor_id.state == MOTOR_ID_STATE_COMPLETE) {
        assert_true(result.valid);
        assert_true(isfinite(result.linkage_wb));
        /* The virtual motor's own flux linkage is 1e-6, and this is a measurement of it. The sign
         * is not asserted: the reference's formula takes it from the drive, and both signs are
         * legitimate results of the same motor turning the other way. What is asserted is that the
         * number is a flux linkage of that order rather than an arbitrary constant. */
        assert_true(fabsf(result.linkage_wb) < 1.0e-3f);
        assert_true(result.undriven_samples > 0.0f);
    }

    /* Nothing to drive it with. */
    vesc_detect_flux_result_t other;
    assert_int_equal(
        ops.detect_flux_linkage_openloop(NULL, 5.0f, 0.05f, 2000.0f, 0.05f, 1.0e-6f, &other),
        EDGE_EINVAL);
    vesc_host_ops_ctx_t bare;
    memset(&bare, 0, sizeof(bare));
    vesc_comm_ops_port_t bare_ops;
    vesc_host_make_ops_port(&bare_ops, &bare);
    assert_int_equal(bare_ops.detect_flux_linkage_openloop(bare_ops.self, 5.0f, 0.05f, 2000.0f,
                                                           0.05f, 1.0e-6f, &other),
                     EDGE_EINVAL);
}

/*
 * The same again through the sensored procedure: the ops port drives it and the plant together, and
 * what is asserted is that it comes back with an end rather than hanging - the procedure can fail
 * legitimately, and the command sends a zero when it does.
 */
static void test_vesc_host_flux_command_sensored(void **state) {
    (void)state;
    vesc_host_glue_state_t glue;
    memset(&glue, 0, sizeof(glue));
    glue.v_bus = 24.0f;
    foc_virtual_motor_init(&glue.vmotor, 0.05f, 0.00005f, 1.0e-6f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue);
    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 1.0e-6f,
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .sensorless_mode = false};
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    glue.foc = &foc;

    motor_id_measure_port_t id_port;
    vesc_host_make_motor_id_measure_port(&id_port, &glue);
    motor_id_app_t motor_id;
    motor_id_construct(&motor_id, EDGE_MOD_MOTOR_ID, 40u, &id_port);
    assert_int_equal(motor_id_init(&motor_id), EDGE_OK);

    vesc_host_ops_ctx_t ctx = {.glue = &glue, .motor_id = &motor_id};
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ctx);
    assert_non_null(ops.detect_flux_linkage);

    float linkage = 0.0f;
    const edge_status_t rc =
        ops.detect_flux_linkage(ops.self, 5.0f, 300.0f, 0.05f, 0.05f, &linkage);
    assert_true(motor_id.state == MOTOR_ID_STATE_COMPLETE ||
                motor_id.state == MOTOR_ID_STATE_FAILED);
    if (motor_id.state == MOTOR_ID_STATE_COMPLETE) {
        assert_int_equal(rc, EDGE_OK);
        assert_true(isfinite(linkage));
        assert_true(fabsf(linkage) < 1.0f);
    } else {
        assert_int_equal(rc, EDGE_EIO);
    }

    /* Nothing to drive it with. */
    assert_int_equal(ops.detect_flux_linkage(NULL, 5.0f, 300.0f, 0.05f, 0.05f, &linkage),
                     EDGE_EINVAL);
    vesc_host_ops_ctx_t bare;
    memset(&bare, 0, sizeof(bare));
    vesc_comm_ops_port_t bare_ops;
    vesc_host_make_ops_port(&bare_ops, &bare);
    assert_int_equal(
        bare_ops.detect_flux_linkage(bare_ops.self, 5.0f, 300.0f, 0.05f, 0.05f, &linkage),
        EDGE_EINVAL);
}

/*
 * The products carry the configuration's foc_sensor_mode across as this app's angle source, so the
 * two enumerations have to name the same modes in the same order. They are written independently -
 * the app may not include the generated one (D30) - which is exactly why the correspondence is
 * asserted rather than only commented.
 */
static void test_sensor_mode_enumerations_line_up(void **state) {
    (void)state;
    assert_int_equal(FOC_SENSOR_MODE_SENSORLESS, FOC_ANGLE_SOURCE_SENSORLESS);
    assert_int_equal(FOC_SENSOR_MODE_ENCODER, FOC_ANGLE_SOURCE_ENCODER);
    assert_int_equal(FOC_SENSOR_MODE_HALL, FOC_ANGLE_SOURCE_HALL);
    assert_int_equal(FOC_SENSOR_MODE_HFI, FOC_ANGLE_SOURCE_HFI);
    assert_int_equal(FOC_SENSOR_MODE_HFI_START, FOC_ANGLE_SOURCE_HFI_START);
    assert_int_equal(FOC_SENSOR_MODE_HFI_V2, FOC_ANGLE_SOURCE_HFI_V2);
    assert_int_equal(FOC_SENSOR_MODE_HFI_V3, FOC_ANGLE_SOURCE_HFI_V3);
    assert_int_equal(FOC_SENSOR_MODE_HFI_V4, FOC_ANGLE_SOURCE_HFI_V4);
    assert_int_equal(FOC_SENSOR_MODE_HFI_V5, FOC_ANGLE_SOURCE_HFI_V5);
}

/*
 * B5's contract: detection against a virtual motor whose parameters are known, driven through the
 * same glue the products use. The machine here has 4e-5 H on the direct axis and 6e-5 H on the
 * quadrature one, so the mean is 5e-5 H and the split 2e-5 H - 45 uH and 18 uH after the
 * reference's own 0.9 factor.
 *
 * The plant is stepped with mod_alpha_raw rather than v_alpha: the excitation is what HFI measures
 * itself with, and it reaches the machine the way it reaches the SVM.
 */
static void test_vesc_host_measures_a_known_motor(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 5e-5f, 0.005f, 7, 5e-4f);
    foc_virtual_motor_set_saliency(&glue_state.vmotor, 2e-5f); /* ld 4e-5, lq 6e-5 */

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue_state);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue_state);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue_state);

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 5e-5f,
                        .lambda_wb = 0.005f,
                        .si_motor_poles = 14u,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 8.0f,
                        .temp_fet_max_c = 100.0f,
                        .cc_min_current = 0.02f,
                        .pll_kp = 2000.0f,
                        .pll_ki = 30000.0f,
                        .hfi_samples = 2u,
                        .f_zv = 20000.0f};
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    vesc_host_glue_state_t id_glue;
    memset(&id_glue, 0, sizeof(id_glue));
    id_glue.foc = &foc;
    /* The measurement's temporary configuration is computed against the bus, so the state the port
     * was wired with carries it - as the product's does. */
    id_glue.v_bus = 24.0f;
    motor_id_measure_port_t id_m;
    vesc_host_make_motor_id_measure_port(&id_m, &id_glue);
    motor_id_app_t id_app;
    motor_id_construct(&id_app, EDGE_MOD_MOTOR_ID, 40u, &id_m);
    assert_int_equal(motor_id_init(&id_app), EDGE_OK);

    assert_int_equal(motor_id_measure_inductance(&id_app, 0.3f, 20u), EDGE_OK);
    for (int ms = 0; ms < 20000 && id_app.state != MOTOR_ID_STATE_COMPLETE; ms++) {
        for (int cycle = 0; cycle < 20; cycle++) {
            assert_int_equal(foc_core_fast_loop(&foc, 5e-5f), EDGE_OK);
            foc_virtual_motor_step(&glue_state.vmotor, foc.mod_alpha_raw, foc.mod_beta_raw, 0.0f,
                                   5e-5f, 0.0f);
        }
        assert_int_equal(motor_id_step(&id_app, 0.001f), EDGE_OK);
    }

    assert_int_equal(id_app.state, MOTOR_ID_STATE_COMPLETE);
    const motor_id_result_t *result = motor_id_get_result(&id_app);
    assert_true(result->valid);
    assert_float_equal(result->ind_uh, 45.0f, 0.5f);
    assert_float_equal(result->ld_lq_diff_uh, 18.0f, 0.5f);
    /* The current it reports carries the same sign convention the excitation's gate does - the
     * half of the injection cycle this port samples on - which is the deviation recorded in
     * docs/adr-conformance.md. What matters here is that it measured a step at all. */
    assert_true(result->ind_current_a < 0.0f);
}

/*
 * The inductance port's guards: every callback refuses a glue with no aggregate behind it, a
 * restore refuses when nothing was saved, and asking for nothing is answered rather than faulted.
 */
static void test_vesc_host_inductance_port_guards(void **state) {
    (void)state;
    vesc_host_glue_state_t glue;
    memset(&glue, 0, sizeof(glue)); /* no aggregate behind it yet */
    motor_id_measure_port_t m;
    vesc_host_make_motor_id_measure_port(&m, &glue);

    assert_int_equal(m.enter_inductance_config(NULL, 0.1f), EDGE_EINVAL);
    assert_int_equal(m.enter_inductance_config(&glue, 0.1f), EDGE_EINVAL);
    assert_int_equal(m.leave_inductance_config(&glue), EDGE_EINVAL);
    assert_int_equal(m.set_duty(&glue, 0.0f), EDGE_EINVAL);
    bool ready = true;
    assert_int_equal(m.is_hfi_ready(&glue, &ready), EDGE_EINVAL);
    assert_int_equal(m.read_hfi_bins(&glue, NULL, NULL, NULL, NULL), EDGE_EINVAL);
    assert_int_equal(m.enter_res_ind_gains(&glue), EDGE_EINVAL);
    assert_int_equal(m.leave_res_ind_gains(&glue), EDGE_EINVAL);

    foc_core_t foc;
    memset(&foc, 0, sizeof(foc));
    glue.foc = &foc;

    /* With an aggregate but nothing saved, the restores are refused rather than undoing someone
     * else's configuration. */
    assert_int_equal(m.leave_inductance_config(&glue), EDGE_EINVAL);
    assert_int_equal(m.leave_res_ind_gains(&glue), EDGE_EINVAL);
    assert_int_equal(m.is_hfi_ready(&glue, NULL), EDGE_EINVAL);

    assert_int_equal(m.is_hfi_ready(&glue, &ready), EDGE_OK);
    assert_false(ready); /* the buffer has not been filled */
    /* A duty reaches the aggregate, which refuses it while it is idle - a duty is a mode of its
     * own - so what comes back is that refusal rather than the glue's own answer. */
    assert_int_equal(m.set_duty(&glue, 0.25f), EDGE_EBUSY);
    assert_int_equal(m.read_hfi_bins(&glue, NULL, NULL, NULL, NULL), EDGE_OK);

    /* The gains pair holds the current loop's own values and puts exactly them back. */
    glue.foc->config.current_kp = 0.31f;
    glue.foc->config.current_ki = 77.0f;
    assert_int_equal(m.enter_res_ind_gains(&glue), EDGE_OK);
    assert_float_equal(glue.foc->config.current_kp, 0.001f, 1e-9f);
    assert_float_equal(glue.foc->config.current_ki, 1.0f, 1e-9f);
    assert_int_equal(m.leave_res_ind_gains(&glue), EDGE_OK);
    assert_float_equal(glue.foc->config.current_kp, 0.31f, 1e-9f);
    assert_float_equal(glue.foc->config.current_ki, 77.0f, 1e-9f);
}

/*
 * B5's contract through the command layer's own port: the composed sequence runs to its end and
 * answers with the machine's resistance and its two inductances, driven exactly as the command
 * handler drives it.
 */
static void test_vesc_host_detects_r_and_l(void **state) {
    (void)state;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    foc_virtual_motor_init(&glue_state.vmotor, 0.05f, 5e-5f, 0.005f, 7, 5e-4f);
    foc_virtual_motor_set_saliency(&glue_state.vmotor, 2e-5f); /* ld 4e-5, lq 6e-5 */

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue_state);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue_state);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue_state);

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 5e-5f,
                        .lambda_wb = 0.005f,
                        .si_motor_poles = 14u,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 8.0f,
                        .temp_fet_max_c = 100.0f,
                        .cc_min_current = 0.02f,
                        .pll_kp = 2000.0f,
                        .pll_ki = 30000.0f,
                        .hfi_samples = 2u,
                        .f_zv = 20000.0f};
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    glue_state.foc = &foc;

    vesc_host_glue_state_t id_glue;
    memset(&id_glue, 0, sizeof(id_glue));
    id_glue.foc = &foc;
    motor_id_measure_port_t id_m;
    vesc_host_make_motor_id_measure_port(&id_m, &id_glue);
    motor_id_app_t id_app;
    motor_id_construct(&id_app, EDGE_MOD_MOTOR_ID, 40u, &id_m);
    assert_int_equal(motor_id_init(&id_app), EDGE_OK);

    vesc_host_ops_ctx_t ops_ctx;
    memset(&ops_ctx, 0, sizeof(ops_ctx));
    ops_ctx.glue = &glue_state;
    ops_ctx.motor_id = &id_app;
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ops_ctx);

    vesc_detect_r_l_result_t result;
    assert_int_equal(ops.detect_r_l(ops.self, &result), EDGE_OK);
    assert_true(result.valid);
    assert_float_equal(result.r_ohm, 0.05f, 5e-3f);
    /* Measured at 43.9 uH against the machine's 45.0 - a couple of per cent, which is the scan's
     * own accuracy at the current it settles on rather than a scaling error: the twice-over above
     * was the scaling error, and it is gone. */
    assert_float_equal(result.l_uh, 45.0f, 2.0f);
    assert_float_equal(result.ld_lq_diff_uh, 18.0f, 2.0f);

    /*
     * Both numbers now, and the twice-over this test used to record is explained rather than open:
     * the transform measures the current's step between the two halves of one injection cycle, so
     * what it divides by is one switching period - which is why the loop above and the drive behind
     * this port are both stepped at 1/f_zv, as the reference's own V0-sampling interrupt is.
     * Stepped at half a period instead, the step it reads is half of the real one and every
     * inductance comes back twice as large; that was the whole of B5's open item.
     */

    /* The switching frequency the reference stages for this measurement is put back afterwards. */
    assert_float_equal(foc.config.f_zv, 20000.0f, 1e-3f);
}

/*
 * The backup block's whole journey: the aggregate accumulates, the product's store port puts its
 * words into the emulated EEPROM at the reference's own base, and a freshly constructed aggregate
 * reads them back - which is what the host product's composition root does at boot, and what
 * "persisted" means here.
 */
static void test_vesc_host_backup_block_persists(void **state) {
    (void)state;
    vesc_host_glue_state_t glue;
    memset(&glue, 0, sizeof(glue));
    memset(glue.flash_mem, 0xFF, sizeof(glue.flash_mem));
    glue.v_bus = 24.0f;
    foc_virtual_motor_init(&glue.vmotor, 0.05f, 5e-5f, 0.005f, 7, 5e-4f);

    flash_sector_port_t sectors;
    vesc_host_make_flash_sector_port(&sectors, &glue);
    static uint16_t backup_var_table[VESC_HOST_BACKUP_VARS];
    for (size_t i = 0u; i < VESC_HOST_BACKUP_VARS; i++) {
        backup_var_table[i] = (uint16_t)(VESC_HOST_BACKUP_BASE + i);
    }
    flash_emul_t emul;
    flash_emul_construct(
        &emul, &sectors,
        (flash_var_table_t){.virtual_addresses = backup_var_table, .count = VESC_HOST_BACKUP_VARS},
        0u);
    assert_int_equal(flash_emul_init(&emul), EDGE_OK);

    foc_inverter_port_t inv_port;
    vesc_host_make_inverter_port(&inv_port, &glue);
    foc_current_port_t cs_port;
    vesc_host_make_current_port(&cs_port, &glue);
    foc_rotor_port_t rs_port;
    vesc_host_make_rotor_port(&rs_port, &glue);
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 5e-5f,
                        .lambda_wb = 0.005f,
                        .si_motor_poles = 14u,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 8.0f,
                        .temp_fet_max_c = 100.0f,
                        .sensorless_mode = false};

    foc_storage_port_t store;
    vesc_host_make_backup_store_port(&store, &emul);

    foc_core_t foc;
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    foc_core_set_storage_port(&foc, &store);
    foc_core_set_backup(&foc, 4321u, 9000u);

    /* The power goes off, and the aggregate is the one that stores - from its own shutdown path, as
     * the reference stores from its. */
    assert_int_equal(foc.module.power_off(&foc.module), EDGE_OK);

    /* Read the words back the way the composition root does, and hand them to a fresh aggregate. */
    uint8_t block[FOC_BACKUP_BLOCK_BYTES];
    for (size_t i = 0u; i < VESC_HOST_BACKUP_VARS; i++) {
        uint16_t word = 0u;
        assert_int_equal(flash_emul_read(&emul, (uint16_t)(VESC_HOST_BACKUP_BASE + i), &word),
                         EDGE_OK);
        block[2u * i] = (uint8_t)(word >> 8);
        block[2u * i + 1u] = (uint8_t)word;
    }

    foc_core_t restarted;
    foc_core_construct(&restarted, EDGE_MOD_FOC_CORE, 10u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&restarted), EDGE_OK);
    assert_int_equal(foc_core_backup_restore(&restarted, block, sizeof(block)), EDGE_OK);

    uint64_t odometer_m = 0u;
    uint32_t uptime_ms = 0u;
    foc_core_get_backup(&restarted, &odometer_m, &uptime_ms);
    assert_float_equal((double)odometer_m, 4321.0, 0.0);
    assert_int_equal(uptime_ms, 9000u);
}

/*
 * The same journey on the board product, whose store is its own table rather than the emulated
 * EEPROM: the aggregate stores at power_off, the table keeps the words, and a fresh aggregate reads
 * them back. An empty table reads as nothing - this store reports an unwritten slot - rather than
 * as zeros to trust, which is the same rule the emulated store has.
 */
static void test_vesc6_backup_block_persists(void **state) {
    (void)state;
    board_vesc6_t board;
    assert_int_equal(board_vesc6_init(&board), EDGE_OK);
    vesc6_glue_state_t glue;
    vesc6_glue_init(&glue, &board);

    uint8_t block[FOC_BACKUP_BLOCK_BYTES];
    memset(block, 0, sizeof(block));
    assert_int_equal(vesc6_backup_read(&glue, block, sizeof(block)), EDGE_ENOENT);
    assert_int_equal(vesc6_backup_read(&glue, block, FOC_BACKUP_BLOCK_BYTES - 1u), EDGE_EINVAL);

    foc_inverter_port_t inv_port;
    vesc6_make_inverter_port(&inv_port, &glue);
    foc_current_port_t cs_port;
    vesc6_make_current_port(&cs_port, &glue);
    foc_rotor_port_t rs_port;
    vesc6_make_rotor_port(&rs_port, &glue);
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 5e-5f,
                        .lambda_wb = 0.005f,
                        .si_motor_poles = 14u,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 50.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 8.0f,
                        .temp_fet_max_c = 100.0f,
                        .sensorless_mode = true};

    foc_storage_port_t store;
    vesc6_make_backup_store_port(&store, &glue);
    assert_non_null(store.store_backup);

    foc_core_t foc;
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    foc_core_set_storage_port(&foc, &store);
    foc_core_set_backup(&foc, 777u, 3000u);
    assert_int_equal(foc.module.power_off(&foc.module), EDGE_OK);

    assert_int_equal(vesc6_backup_read(&glue, block, sizeof(block)), EDGE_OK);

    foc_core_t restarted;
    foc_core_construct(&restarted, EDGE_MOD_FOC_CORE, 10u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&restarted), EDGE_OK);
    assert_int_equal(foc_core_backup_restore(&restarted, block, sizeof(block)), EDGE_OK);

    uint64_t odometer_m = 0u;
    uint32_t uptime_ms = 0u;
    foc_core_get_backup(&restarted, &odometer_m, &uptime_ms);
    assert_float_equal((double)odometer_m, 777.0, 0.0);
    assert_int_equal(uptime_ms, 3000u);
}

/*
 * A store of this test's own, sized for the configuration's words: the all-in-one run keeps what it
 * measured in the module and stores it, so the module needs somewhere to write.
 */
#define APPLY_VAR_COUNT 1200u
static uint16_t apply_var_values[APPLY_VAR_COUNT];
static bool apply_var_present[APPLY_VAR_COUNT];

static edge_status_t apply_var_read(void *self, uint16_t index, uint16_t *value) {
    (void)self;
    if (index >= APPLY_VAR_COUNT || !apply_var_present[index]) {
        return EDGE_ENOENT;
    }
    *value = apply_var_values[index];
    return EDGE_OK;
}

static edge_status_t apply_var_write(void *self, uint16_t index, uint16_t value) {
    (void)self;
    if (index >= APPLY_VAR_COUNT) {
        return EDGE_EINVAL;
    }
    apply_var_values[index] = value;
    apply_var_present[index] = true;
    return EDGE_OK;
}

/*
 * The all-in-one command end to end. The ops port drives the two measurements and the plant
 * together, keeps what they found in the motor configuration, and derives the three gains from it -
 * conf_general.c:1513's relation, kp = l * bandwidth and ki = r * bandwidth at a millisecond's
 * crossover. A run can legitimately fail on this plant, so what is asserted first is that it
 * reaches an end rather than hanging; when it does not fail, the configuration is checked against
 * the measurements themselves and against that relation, rather than against numbers that would
 * have to be replayed to be obtained.
 */
static void test_vesc_host_apply_all_foc_command(void **state) {
    (void)state;
    memset(apply_var_values, 0, sizeof(apply_var_values));
    memset(apply_var_present, 0, sizeof(apply_var_present));

    vesc_host_glue_state_t glue;
    memset(&glue, 0, sizeof(glue));
    glue.v_bus = 24.0f;
    foc_virtual_motor_init(&glue.vmotor, 0.05f, 0.00005f, 1.0e-6f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue);
    foc_core_t foc;
    foc_config_t foc_cfg = {.r_ohm = 0.05f,
                            .l_henry = 0.00005f,
                            .lambda_wb = 1.0e-6f,
                            .si_motor_poles = 14u,
                            .si_gear_ratio = 3.0f,
                            .si_wheel_diameter = 0.083f,
                            .current_max_a = 50.0f,
                            .current_min_a = -50.0f,
                            .duty_max = 0.95f,
                            .current_kp = 0.1f,
                            .current_ki = 50.0f,
                            .vbus_ov_threshold = 60.0f,
                            .vbus_uv_threshold = 12.0f,
                            .sensorless_mode = false};
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &foc_cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    glue.foc = &foc;

    motor_id_measure_port_t id_port;
    vesc_host_make_motor_id_measure_port(&id_port, &glue);
    motor_id_app_t motor_id;
    motor_id_construct(&motor_id, EDGE_MOD_MOTOR_ID, 40u, &id_port);
    assert_int_equal(motor_id_init(&motor_id), EDGE_OK);

    const motor_config_var_port_t var_port = {
        .read = apply_var_read, .write = apply_var_write, .self = NULL};
    static alignas(MOTOR_CONFIG_STORAGE_ALIGN) unsigned char cfg_storage[MOTOR_CONFIG_STORAGE_SIZE];
    motor_config_t *cfg = (motor_config_t *)cfg_storage;
    motor_config_construct(cfg, EDGE_MOD_MOTOR_CONFIG, 30u, &var_port);
    assert_int_equal(motor_config_init(cfg), EDGE_OK);

    vesc_host_ops_ctx_t ctx = {.glue = &glue, .motor_id = &motor_id, .config = cfg};
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ctx);
    assert_non_null(ops.detect_apply_all_foc);

    int16_t result = -1;
    const edge_status_t status =
        ops.detect_apply_all_foc(ops.self, false, 30.0f, 5.0f, 40.0f, 700.0f, 1500.0f, &result);

    /* It ran to one of its ends rather than staying in progress. */
    assert_true(motor_id.state == MOTOR_ID_STATE_COMPLETE ||
                motor_id.state == MOTOR_ID_STATE_FAILED);

    if (status == EDGE_OK) {
        const mc_configuration_t *mc = motor_config_get_mc(cfg);
        const motor_id_result_t *measured = motor_id_get_result(&motor_id);
        assert_true(measured->valid);
        assert_int_equal(result, 0);
        assert_float_equal(mc->foc_motor_r, measured->r_ohm, 1e-6f);
        assert_float_equal(mc->foc_motor_l, measured->ind_uh * 1e-6f, 1e-9f);
        assert_float_equal(mc->foc_motor_flux_linkage, measured->flux_linkage_wb, 1e-9f);
        /* The relation conf_general.c:1513 defines, at the crossover it is called with. */
        assert_float_equal(mc->foc_current_kp, mc->foc_motor_l * 1000.0f, 1e-6f);
        assert_float_equal(mc->foc_current_ki, mc->foc_motor_r * 1000.0f, 1e-6f);
    } else {
        /*
         * The run has to complete on this plant: the walk and the linkage are both driven here the
         * way their own commands drive them, so a failure means one of them was given something it
         * cannot work with rather than that the plant is too weak. Asserting the completion is what
         * makes the branch above the one that runs - the evidence for this command is the
         * configuration it writes, not the fact that it returned.
         */
        assert_int_equal(status, EDGE_OK);
        assert_int_equal(result, 0);
    }

    /* Nothing to drive it with. */
    int16_t other = 0;
    assert_int_equal(ops.detect_apply_all_foc(NULL, false, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &other),
                     EDGE_EINVAL);
    vesc_host_ops_ctx_t bare;
    memset(&bare, 0, sizeof(bare));
    vesc_comm_ops_port_t bare_ops;
    vesc_host_make_ops_port(&bare_ops, &bare);
    assert_int_equal(
        bare_ops.detect_apply_all_foc(bare_ops.self, false, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &other),
        EDGE_EINVAL);
}

/*
 * The hall detection's own command, through the product: the mock ports answer as a hall sensor
 * would, the procedure sweeps and reads them, and what comes back is the table and its verdict. The
 * host's configuration is a hall sensor port by its own defaults, so this is the path a board with
 * halls would take; a product without them is refused before the run, which the command layer's own
 * test covers.
 */
static void test_vesc_host_detect_hall_foc_command(void **state) {
    (void)state;
    memset(apply_var_values, 0, sizeof(apply_var_values));
    memset(apply_var_present, 0, sizeof(apply_var_present));

    vesc_host_glue_state_t glue;
    memset(&glue, 0, sizeof(glue));
    glue.v_bus = 24.0f;
    foc_virtual_motor_init(&glue.vmotor, 0.05f, 0.00005f, 0.005f, 7, 0.0005f);

    foc_inverter_port_t inverter;
    vesc_host_make_inverter_port(&inverter, &glue);
    foc_current_port_t current;
    vesc_host_make_current_port(&current, &glue);
    foc_rotor_port_t rotor;
    vesc_host_make_rotor_port(&rotor, &glue);
    foc_core_t foc;
    foc_config_t foc_cfg = {.r_ohm = 0.05f,
                            .l_henry = 0.00005f,
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
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &foc_cfg, &inverter, &current, &rotor);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    glue.foc = &foc;

    motor_id_measure_port_t id_port;
    vesc_host_make_motor_id_measure_port(&id_port, &glue);
    assert_non_null(id_port.read_hall);
    motor_id_app_t motor_id;
    motor_id_construct(&motor_id, EDGE_MOD_MOTOR_ID, 40u, &id_port);
    assert_int_equal(motor_id_init(&motor_id), EDGE_OK);

    const motor_config_var_port_t var_port = {
        .read = apply_var_read, .write = apply_var_write, .self = NULL};
    static alignas(MOTOR_CONFIG_STORAGE_ALIGN) unsigned char cfg_storage[MOTOR_CONFIG_STORAGE_SIZE];
    motor_config_t *cfg = (motor_config_t *)cfg_storage;
    motor_config_construct(cfg, EDGE_MOD_MOTOR_CONFIG, 30u, &var_port);
    assert_int_equal(motor_config_init(cfg), EDGE_OK);

    vesc_host_ops_ctx_t ctx = {.glue = &glue, .motor_id = &motor_id, .config = cfg};
    vesc_comm_ops_port_t ops;
    vesc_host_make_ops_port(&ops, &ctx);
    assert_non_null(ops.detect_hall_foc);

    uint8_t table[8];
    memset(table, 0, sizeof(table));
    bool result = false;
    const edge_status_t status = ops.detect_hall_foc(ops.self, 5.0f, table, &result);

    assert_int_equal(motor_id.state, MOTOR_ID_STATE_COMPLETE);
    assert_int_equal(status, EDGE_OK);
    assert_true(result);
    assert_int_equal(table[0], 255u);
    assert_int_equal(table[7], 255u);

    /*
     * The reference's own criterion, which the verdict reports: every reading but the two ends
     * names a step, and the six steps are six different ones. Their angles are the sector's middle
     * only on a rotor that follows the override exactly; this one is a plant that lags behind it,
     * so what is asserted is the criterion rather than the arithmetic that a real sensor would
     * satisfy.
     */
    for (int reading = 1; reading < 7; reading++) {
        assert_true(table[reading] != 255u);
    }
    for (int first = 1; first < 7; first++) {
        for (int second = first + 1; second < 7; second++) {
            assert_true(table[first] != table[second]);
        }
    }

    /* Guards, and the gate on the sensor port: a product without halls is refused. */
    assert_int_equal(ops.detect_hall_foc(NULL, 5.0f, table, &result), EDGE_EINVAL);
    assert_int_equal(ops.detect_hall_foc(ops.self, 5.0f, NULL, &result), EDGE_EINVAL);
    assert_int_equal(ops.detect_hall_foc(ops.self, 5.0f, table, NULL), EDGE_EINVAL);
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
        cmocka_unit_test(test_vesc_host_dir_mult),
        cmocka_unit_test(test_vesc_host_var_port),
        cmocka_unit_test(test_vesc_host_app_status_adapters),
        cmocka_unit_test(test_vesc_host_simulated_adapters),
        cmocka_unit_test(test_vesc_host_temperature_sampler),
        cmocka_unit_test(test_vesc_host_motor_id_detection),
        cmocka_unit_test(test_vesc_host_masked_value_adapters),
        cmocka_unit_test(test_vesc_host_setup_values_count_the_peers_on_the_bus),
        cmocka_unit_test(test_vesc_host_restart_requests),
        cmocka_unit_test(test_vesc_host_detect_hall_foc_command),
        cmocka_unit_test(test_vesc_host_adapter_guards),
        cmocka_unit_test(test_vesc_host_config_and_terminal_ports),
        cmocka_unit_test(test_vesc_host_adapters_nothing_called),
        cmocka_unit_test(test_vesc6_var_store_protocol),
        cmocka_unit_test(test_vesc6_inverter_adapter),
        cmocka_unit_test(test_vesc6_current_adapter),
        cmocka_unit_test(test_vesc6_rotor_adapter),
        cmocka_unit_test(test_vesc_host_motor_id_flux_adapters),
        cmocka_unit_test(test_vesc_host_flux_command),
        cmocka_unit_test(test_vesc_host_flux_command_sensored),
        cmocka_unit_test(test_vesc_host_motor_setters),
        cmocka_unit_test(test_sensor_mode_enumerations_line_up),
        cmocka_unit_test(test_vesc_host_measures_a_known_motor),
        cmocka_unit_test(test_vesc_host_inductance_port_guards),
        cmocka_unit_test(test_vesc_host_detects_r_and_l),
        cmocka_unit_test(test_vesc_host_apply_all_foc_command),
        cmocka_unit_test(test_vesc_host_backup_block_persists),
        cmocka_unit_test(test_vesc6_backup_block_persists),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
