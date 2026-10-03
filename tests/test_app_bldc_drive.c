/*
 * The six-step arithmetic against the reference's own output.
 *
 * motor/mcpwm.c's mcpwm_init_hall_table was compiled verbatim - only the sixteen-entry array it
 * writes into and the sensor-mode flag were supplied as globals - and the three snippets from
 * commutate(), update_rpm_tacho() and update_sensor_mode() were copied into the harness character
 * for character, because that is what they are: statements inside larger functions that read the
 * hardware's own state. The numbers below are that run's output.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "bldc_drive/bldc_commutation.h"
#include "bldc_drive/bldc_drive.h"
#include "edge/errors.h"
#include "edge/event.h"

/* The reference's forward-to-reverse map, indexed by the step: {-1,1,6,5,4,3,2}. */
static void test_bldc_hall_tables_match_the_reference(void **state) {
    (void)state;

    /* A table the reference's own default configuration would carry. Its reverse half is the table
     * itself, and its forward half is each step mapped through fwd_to_rev: 5 to 3, 3 to 5, 6 to 2,
     * 4 to 4, 1 to 1, 2 to 6. */
    const int8_t t1[8] = {0, 5, 3, 1, 6, 4, 2, 0};
    int8_t forward[8];
    int8_t reverse[8];
    bldc_build_hall_tables(t1, forward, reverse);

    const int8_t t1_rev[8] = {0, 5, 3, 1, 6, 4, 2, 0};
    const int8_t t1_fwd[8] = {0, 3, 5, 1, 2, 4, 6, 0};
    assert_memory_equal(reverse, t1_rev, sizeof(t1_rev));
    assert_memory_equal(forward, t1_fwd, sizeof(t1_fwd));

    /* The other direction's table, which the reference passes in when the motor is wired the other
     * way round. */
    const int8_t t2[8] = {0, 6, 4, 2, 5, 3, 1, 0};
    bldc_build_hall_tables(t2, forward, reverse);
    const int8_t t2_rev[8] = {0, 6, 4, 2, 5, 3, 1, 0};
    const int8_t t2_fwd[8] = {0, 2, 4, 6, 3, 5, 1, 0};
    assert_memory_equal(reverse, t2_rev, sizeof(t2_rev));
    assert_memory_equal(forward, t2_fwd, sizeof(t2_fwd));

    /* A blank one: below one is passed through in both halves, which is the reference's own first
     * case and also how this port treats a step outside the map rather than reading past it. */
    const int8_t blank[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    bldc_build_hall_tables(blank, forward, reverse);
    for (size_t i = 0u; i < 8u; i++) {
        assert_int_equal(forward[i], -1);
        assert_int_equal(reverse[i], -1);
    }

    /* Nothing to write into, or nothing to read from. */
    bldc_build_hall_tables(NULL, forward, reverse);
    bldc_build_hall_tables(t1, NULL, reverse);
    bldc_build_hall_tables(t1, forward, NULL);
}

static void test_bldc_comm_step_advance_matches_the_reference(void **state) {
    (void)state;

    /* commutate()'s own two whiles, over every step it can be given and a spread of advances
     * including one that wraps more than a whole revolution. */
    const int steps[7] = {-7, -6, -1, 0, 1, 6, 7};

    const int from_one[7] = {6, 1, 6, 1, 2, 1, 2};
    const int from_three[7] = {2, 3, 2, 3, 4, 3, 4};
    const int from_six[7] = {5, 6, 5, 6, 1, 6, 1};

    for (size_t i = 0u; i < 7u; i++) {
        assert_int_equal(bldc_comm_step_advance(1, steps[i]), from_one[i]);
        assert_int_equal(bldc_comm_step_advance(3, steps[i]), from_three[i]);
        assert_int_equal(bldc_comm_step_advance(6, steps[i]), from_six[i]);
    }
}

static void test_bldc_tacho_delta_matches_the_reference(void **state) {
    (void)state;

    /* update_rpm_tacho()'s normalization: the difference modulo six, taken to plus or minus three -
     * a step that looks five forward is one back. Rows are the current step, columns the previous
     * one, and the whole table is the reference's output. */
    const int expected[6][6] = {
        {-1, -2, 3, 2, 1, 0}, {0, -1, -2, 3, 2, 1}, {1, 0, -1, -2, 3, 2},
        {2, 1, 0, -1, -2, 3}, {3, 2, 1, 0, -1, -2}, {-2, 3, 2, 1, 0, -1},
    };

    for (int step = 1; step <= 6; step++) {
        for (int last = 1; last <= 6; last++) {
            assert_int_equal(bldc_tacho_step_delta(step, last), expected[step - 1][last - 1]);
        }
    }
}

static void test_bldc_sensorless_decision_matches_the_reference(void **state) {
    (void)state;

    /* update_sensor_mode(): sensorless when configured that way; hybrid once the speed is past
     * hall_sl_erpm, whichever way the motor turns. */
    assert_true(bldc_sensorless_now(BLDC_SENSOR_MODE_SENSORLESS, -100.0f, 50.0f));
    assert_true(bldc_sensorless_now(BLDC_SENSOR_MODE_SENSORLESS, 0.0f, 50.0f));
    assert_true(bldc_sensorless_now(BLDC_SENSOR_MODE_SENSORLESS, 100.0f, 50.0f));

    assert_false(bldc_sensorless_now(BLDC_SENSOR_MODE_SENSORED, -100.0f, 50.0f));
    assert_false(bldc_sensorless_now(BLDC_SENSOR_MODE_SENSORED, 0.0f, 50.0f));
    assert_false(bldc_sensorless_now(BLDC_SENSOR_MODE_SENSORED, 100.0f, 50.0f));

    assert_true(bldc_sensorless_now(BLDC_SENSOR_MODE_HYBRID, -100.0f, 50.0f));
    assert_false(bldc_sensorless_now(BLDC_SENSOR_MODE_HYBRID, 0.0f, 50.0f));
    assert_true(bldc_sensorless_now(BLDC_SENSOR_MODE_HYBRID, 100.0f, 50.0f));

    /* On the threshold is not past it, which is the reference's own comparison. */
    assert_false(bldc_sensorless_now(BLDC_SENSOR_MODE_HYBRID, 50.0f, 50.0f));
    assert_true(bldc_sensorless_now(BLDC_SENSOR_MODE_HYBRID, 50.001f, 50.0f));
}

/*
 * The module itself: the configuration's hall table turned into both directions at init, the
 * sensor-mode decision recomputed from the speed the product hands in, and the commutation step and
 * its tachometer delta kept where the reference keeps its globals. Its poll is driven through the
 * module table, which is how the scheduler reaches it.
 */
static void test_bldc_drive_module_behaviour(void **state) {
    (void)state;

    bldc_drive_config_t config = {.sensor_mode = BLDC_SENSOR_MODE_HYBRID,
                                  .hall_sl_erpm = 4000.0f,
                                  .hall_table = {0, 5, 3, 1, 6, 4, 2, 0}};

    bldc_drive_t drive;
    bldc_drive_construct(&drive, EDGE_MOD_BLDC_DRIVE, 25u, &config);
    assert_int_equal(bldc_drive_init(&drive), EDGE_OK);

    /* The tables are the reference's own, built at init as mcpwm_init_hall_table builds them. */
    const int8_t *forward = bldc_drive_hall_forward(&drive);
    const int8_t *reverse = bldc_drive_hall_reverse(&drive);
    const int8_t expected_fwd[8] = {0, 3, 5, 1, 2, 4, 6, 0};
    const int8_t expected_rev[8] = {0, 5, 3, 1, 6, 4, 2, 0};
    assert_memory_equal(forward, expected_fwd, sizeof(expected_fwd));
    assert_memory_equal(reverse, expected_rev, sizeof(expected_rev));

    /* The step starts at one, and advancing wraps it within one to six. */
    assert_int_equal(bldc_drive_get_comm_step(&drive), 1);
    bldc_drive_advance_step(&drive, 1);
    assert_int_equal(bldc_drive_get_comm_step(&drive), 2);
    bldc_drive_advance_step(&drive, -3);
    assert_int_equal(bldc_drive_get_comm_step(&drive), 5);

    /* Hybrid is sensorless only past hall_sl_erpm, and the decision is made in the poll - driving
     * it through the module table, as the scheduler does. */
    edge_module_t *module = bldc_drive_module(&drive);
    assert_non_null(module);
    assert_non_null(module->poll);

    bldc_drive_set_rpm(&drive, 1000.0f);
    assert_int_equal(module->poll(module), EDGE_OK);
    assert_false(bldc_drive_is_sensorless(&drive));

    bldc_drive_set_rpm(&drive, 5000.0f);
    assert_int_equal(module->poll(module), EDGE_OK);
    assert_true(bldc_drive_is_sensorless(&drive));

    /* The tachometer delta is update_rpm_tacho's own, and reading it consumes it. */
    bldc_drive_advance_step(&drive, 1);
    assert_int_equal(bldc_drive_get_tacho_delta(&drive), 1);
    assert_int_equal(bldc_drive_get_tacho_delta(&drive), 0);

    if (module->on_event != NULL) {
        const edge_event_t evt = {0};
        assert_int_equal(module->on_event(module, &evt), EDGE_OK);
    }
    assert_int_equal(module->power_off(module), EDGE_OK);

    /* A sensorless configuration is sensorless from the start, whatever the speed. */
    bldc_drive_t sensorless;
    bldc_drive_config_t sensorless_cfg = config;
    sensorless_cfg.sensor_mode = BLDC_SENSOR_MODE_SENSORLESS;
    bldc_drive_construct(&sensorless, EDGE_MOD_BLDC_DRIVE, 25u, &sensorless_cfg);
    assert_int_equal(bldc_drive_init(&sensorless), EDGE_OK);
    assert_true(bldc_drive_is_sensorless(&sensorless));

    /* Guards: nothing to write into, and a configuration that is not one. */
    bldc_drive_construct(NULL, EDGE_MOD_BLDC_DRIVE, 25u, NULL);
    bldc_drive_t bare;
    bldc_drive_construct(&bare, EDGE_MOD_BLDC_DRIVE, 25u, NULL);
    assert_int_equal(bldc_drive_init(NULL), EDGE_EINVAL);
    assert_int_equal(bldc_drive_init(&bare), EDGE_OK);
    bldc_drive_set_rpm(NULL, 1.0f);
    bldc_drive_advance_step(NULL, 1);
    assert_int_equal(bldc_drive_get_comm_step(NULL), 0);
    assert_int_equal(bldc_drive_get_tacho_delta(NULL), 0);
    assert_false(bldc_drive_is_sensorless(NULL));
    assert_null(bldc_drive_hall_forward(NULL));
    assert_null(bldc_drive_hall_reverse(NULL));
    assert_null(bldc_drive_module(NULL));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bldc_hall_tables_match_the_reference),
        cmocka_unit_test(test_bldc_comm_step_advance_matches_the_reference),
        cmocka_unit_test(test_bldc_tacho_delta_matches_the_reference),
        cmocka_unit_test(test_bldc_sensorless_decision_matches_the_reference),
        cmocka_unit_test(test_bldc_drive_module_behaviour),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
