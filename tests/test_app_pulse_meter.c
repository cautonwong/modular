#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/app_contract.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "pulse_meter/pulse_meter.h"

typedef struct fake_storage {
    uint32_t pulses;
    uint32_t tamper;
    uint32_t write_count;
    uint32_t read_count;
} fake_storage_t;

static edge_status_t storage_read(void *self, uint32_t *pulses, uint32_t *tamper_count) {
    fake_storage_t *st = (fake_storage_t *)self;
    ++st->read_count;
    *pulses = st->pulses;
    *tamper_count = st->tamper;
    return EDGE_OK;
}

static edge_status_t storage_write(void *self, uint32_t pulses, uint32_t tamper_count) {
    fake_storage_t *st = (fake_storage_t *)self;
    ++st->write_count;
    st->pulses = pulses;
    st->tamper = tamper_count;
    return EDGE_OK;
}

static uint32_t g_fake_voltage = 3300u;
static edge_status_t battery_read(void *self, uint32_t *mv) {
    (void)self;
    *mv = g_fake_voltage;
    return EDGE_OK;
}

static void test_pulse_meter_init_and_storage(void **state) {
    (void)state;
    pulse_meter_t meter;
    pulse_meter_config_t config = {
        .pulses_per_unit = 100u, .debounce_ticks = 10u, .low_battery_mv = 2700u};
    fake_storage_t storage = {.pulses = 1234u, .tamper = 2u};
    pulse_meter_storage_t st_port = {
        .read = storage_read, .write = storage_write, .self = &storage};
    pulse_meter_battery_t bat_port = {.read_voltage_mv = battery_read, .self = NULL};

    /* NULL check on init */
    assert_int_equal(pulse_meter_init(NULL), EDGE_EINVAL);

    /* Construct + Init */
    pulse_meter_construct(&meter, EDGE_MOD_PULSE_METER, 1u, &config, &st_port, &bat_port);
    assert_int_equal(pulse_meter_init(&meter), EDGE_OK);
    assert_int_equal(pulse_meter_total_pulses(&meter), 1234u);
    assert_int_equal(meter.tamper_events, 2u);
    assert_int_equal(storage.read_count, 1u);
}

static void test_pulse_counting_and_debouncing(void **state) {
    (void)state;
    pulse_meter_t meter;
    pulse_meter_config_t config = {
        .pulses_per_unit = 100u, .debounce_ticks = 10u, .low_battery_mv = 2700u};
    pulse_meter_construct(&meter, EDGE_MOD_PULSE_METER, 1u, &config, NULL, NULL);
    assert_int_equal(pulse_meter_init(&meter), EDGE_OK);

    /* First pulse at t=100 */
    assert_int_equal(pulse_meter_on_pulse(&meter, 100u, false), EDGE_OK);
    assert_int_equal(pulse_meter_total_pulses(&meter), 1u);

    /* Pulse at t=105 (within 10-tick debounce window) -> ignored */
    assert_int_equal(pulse_meter_on_pulse(&meter, 105u, false), EDGE_OK);
    assert_int_equal(pulse_meter_total_pulses(&meter), 1u);

    /* Pulse at t=111 (after debounce window) -> valid */
    assert_int_equal(pulse_meter_on_pulse(&meter, 111u, false), EDGE_OK);
    assert_int_equal(pulse_meter_total_pulses(&meter), 2u);

    /* Reverse pulse at t=125 -> total decreases, tamper increments */
    assert_int_equal(pulse_meter_on_pulse(&meter, 125u, true), EDGE_OK);
    assert_int_equal(pulse_meter_total_pulses(&meter), 1u);
    assert_int_equal(meter.tamper_events, 1u);
}

static void test_tamper_and_freeze(void **state) {
    (void)state;
    pulse_meter_t meter;
    pulse_meter_config_t config = {
        .pulses_per_unit = 100u, .debounce_ticks = 10u, .low_battery_mv = 2700u};
    fake_storage_t storage = {0};
    pulse_meter_storage_t st_port = {
        .read = storage_read, .write = storage_write, .self = &storage};

    pulse_meter_construct(&meter, EDGE_MOD_PULSE_METER, 1u, &config, &st_port, NULL);
    assert_int_equal(pulse_meter_init(&meter), EDGE_OK);
    assert_int_equal(pulse_meter_on_pulse(&meter, 100u, false), EDGE_OK);
    assert_int_equal(pulse_meter_on_pulse(&meter, 120u, false), EDGE_OK);

    /* Tamper detection */
    assert_int_equal(pulse_meter_on_tamper(&meter, 130u), EDGE_OK);
    assert_true(meter.tamper_detected);
    assert_int_equal(meter.tamper_events, 1u);
    assert_int_equal(storage.write_count, 1u);
    assert_int_equal(storage.pulses, 2u);

    /* Freeze snapshot */
    uint64_t frozen_vol = 0u;
    assert_int_equal(pulse_meter_freeze_snapshot(&meter, &frozen_vol), EDGE_OK);
    assert_int_equal(frozen_vol, 2u);
    assert_int_equal(storage.write_count, 2u);
}

static void test_battery_poll_and_events(void **state) {
    (void)state;
    pulse_meter_t meter;
    pulse_meter_config_t config = {
        .pulses_per_unit = 100u, .debounce_ticks = 10u, .low_battery_mv = 2700u};
    pulse_meter_battery_t bat_port = {.read_voltage_mv = battery_read, .self = NULL};

    pulse_meter_construct(&meter, EDGE_MOD_PULSE_METER, 1u, &config, NULL, &bat_port);
    assert_int_equal(pulse_meter_init(&meter), EDGE_OK);

    /* Poll with normal voltage */
    g_fake_voltage = 3300u;
    assert_int_equal(meter.module.poll(&meter.module), EDGE_OK);
    assert_false(meter.low_battery_warned);

    /* Poll with low voltage (2500 < 2700) */
    g_fake_voltage = 2500u;
    assert_int_equal(meter.module.poll(&meter.module), EDGE_OK);
    assert_true(meter.low_battery_warned);

    /* Event handling */
    const edge_event_t pulse_evt = {.id = EDGE_EVT_PULSE_COUNT,
                                    .source = EDGE_MOD_PULSE_METER,
                                    .arg0 = 0u,
                                    .arg1 = 0u,
                                    .timestamp = 500u};
    assert_int_equal(meter.module.on_event(&meter.module, &pulse_evt), EDGE_OK);
    assert_int_equal(pulse_meter_total_pulses(&meter), 1u);

    const edge_event_t tamper_evt = {.id = EDGE_EVT_TAMPER_DETECTED,
                                     .source = EDGE_MOD_PULSE_METER,
                                     .arg0 = 0u,
                                     .arg1 = 0u,
                                     .timestamp = 600u};
    assert_int_equal(meter.module.on_event(&meter.module, &tamper_evt), EDGE_OK);
    assert_true(meter.tamper_detected);
}

/* --- App contract verification --- */
static pulse_meter_t g_contract_app;
static pulse_meter_config_t g_contract_config = {
    .pulses_per_unit = 100u,
    .debounce_ticks = 10u,
    .low_battery_mv = 2700u,
};

static edge_status_t contract_prepare(void) {
    pulse_meter_construct(&g_contract_app, EDGE_MOD_PULSE_METER, 1u, &g_contract_config, NULL,
                          NULL);
    return pulse_meter_init(&g_contract_app);
}

static edge_status_t contract_release(void) {
    return pulse_meter_deinit(&g_contract_app);
}

static const edge_module_t *contract_module(void) {
    return pulse_meter_module(&g_contract_app);
}

static void test_pulse_meter_contract(void **state) {
    (void)state;
    const edge_app_contract_t contract = {
        .name = "pulse_meter",
        .prepare = contract_prepare,
        .release = contract_release,
        .module = contract_module,
        .module_id = EDGE_MOD_PULSE_METER,
        .priority = 1u,
    };
    edge_contract_app_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pulse_meter_init_and_storage),
        cmocka_unit_test(test_pulse_counting_and_debouncing),
        cmocka_unit_test(test_tamper_and_freeze),
        cmocka_unit_test(test_battery_poll_and_events),
        cmocka_unit_test(test_pulse_meter_contract),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
