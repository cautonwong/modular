#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "metronome/metronome.h"

static uint32_t g_mock_tick_ms = 0;
static uint32_t g_motor_pulse_count = 0;
static uint32_t g_last_pulse_duration = 0;

static uint32_t mock_get_tick_ms(void *self) {
    (void)self;
    return g_mock_tick_ms;
}

static edge_status_t mock_run_duration(void *self, uint32_t duration_ms) {
    (void)self;
    g_motor_pulse_count++;
    g_last_pulse_duration = duration_ms;
    return EDGE_OK;
}

static const metronome_clock_if_t g_clock = {
    .self = NULL,
    .get_tick_ms = mock_get_tick_ms,
};

static const metronome_motor_if_t g_motor = {
    .self = NULL,
    .run_duration_ms = mock_run_duration,
};

static void test_metronome_init_and_config(void **state) {
    (void)state;
    metronome_app_t app;
    assert_int_equal(metronome_init(&app, &g_motor, &g_clock), EDGE_OK);

    assert_int_equal(app.module.module_id, EDGE_MOD_METRONOME);
    assert_int_equal(metronome_get_bpm(&app), METRONOME_DEFAULT_BPM);
    assert_int_equal(metronome_get_bpb(&app), METRONOME_DEFAULT_BPB);

    assert_int_equal(metronome_set_bpm(&app, 140), EDGE_OK);
    assert_int_equal(metronome_get_bpm(&app), 140);
    assert_int_equal(metronome_set_bpm(&app, 30), EDGE_EINVAL);
    assert_int_equal(metronome_set_bpm(&app, 250), EDGE_EINVAL);

    assert_int_equal(metronome_set_bpb(&app, 3), EDGE_OK);
    assert_int_equal(metronome_get_bpb(&app), 3);
    assert_int_equal(metronome_set_bpb(&app, 0), EDGE_EINVAL);
    assert_int_equal(metronome_set_bpb(&app, 10), EDGE_EINVAL);
}

static void test_metronome_beats_and_accent(void **state) {
    (void)state;
    g_mock_tick_ms = 0;
    g_motor_pulse_count = 0;
    g_last_pulse_duration = 0;

    metronome_app_t app;
    metronome_construct(&app, EDGE_MOD_METRONOME, 50u, &g_motor, &g_clock);
    (void)metronome_set_bpm(&app, 120); /* 500ms per beat */
    (void)metronome_set_bpb(&app, 4);

    metronome_start(&app);

    /* Advance to 500ms -> beat 0: accent 90ms */
    g_mock_tick_ms = 500u;
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(g_motor_pulse_count, 1);
    assert_int_equal(g_last_pulse_duration, METRONOME_ACCENT_PULSE_MS);

    /* Advance to 1000ms -> beat 1: regular 30ms */
    g_mock_tick_ms = 1000u;
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(g_motor_pulse_count, 2);
    assert_int_equal(g_last_pulse_duration, METRONOME_BEAT_PULSE_MS);

    /* Advance to 1500ms -> beat 2 */
    g_mock_tick_ms = 1500u;
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(g_motor_pulse_count, 3);
    assert_int_equal(g_last_pulse_duration, METRONOME_BEAT_PULSE_MS);

    /* Advance to 2000ms -> beat 3 */
    g_mock_tick_ms = 2000u;
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(g_motor_pulse_count, 4);

    /* Advance to 2500ms -> beat 0 of next bar: accent 90ms */
    g_mock_tick_ms = 2500u;
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(g_motor_pulse_count, 5);
    assert_int_equal(g_last_pulse_duration, METRONOME_ACCENT_PULSE_MS);

    metronome_stop(&app);
}

static void test_metronome_tap_tempo(void **state) {
    (void)state;
    metronome_app_t app;
    metronome_construct(&app, EDGE_MOD_METRONOME, 50u, &g_motor, &g_clock);

    /* Tap 1 at 1000ms */
    g_mock_tick_ms = 1000u;
    metronome_tap_tempo(&app);

    /* Tap 2 at 1500ms -> delta 500ms -> 120 BPM */
    g_mock_tick_ms = 1500u;
    metronome_tap_tempo(&app);
    assert_int_equal(metronome_get_bpm(&app), 120);

    /* Tap 3 at 2100ms -> delta 600ms -> 100 BPM */
    g_mock_tick_ms = 2100u;
    metronome_tap_tempo(&app);
    assert_int_equal(metronome_get_bpm(&app), 100);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_metronome_init_and_config),
        cmocka_unit_test(test_metronome_beats_and_accent),
        cmocka_unit_test(test_metronome_tap_tempo),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
