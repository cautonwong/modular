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

/*
 * mcpwm_read_hall_phase (mcpwm.c:2307): the three pins as one reading, bit zero first. The whole of
 * it is eight readings, and this is that run's output.
 */
static void test_bldc_hall_phase_matches_the_reference(void **state) {
    (void)state;

    for (int bits = 0; bits < 8; bits++) {
        assert_int_equal(bldc_hall_phase((bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0), bits);
    }
}

/*
 * The hall branch of the reference's ADC ISR (mcpwm.c:1939-1952) as a pure decision. Four cases
 * carry what matters, and all four are that run's output: a changed reading while running applies
 * the step the reading named; the same reading with nothing committed yet applies it once anyway;
 * the same reading after a commutation applies nothing; and a motor that is not running takes the
 * step without applying it. A reading of nought or seven is a step here exactly as it is there.
 */
static void test_bldc_hall_commutation_matches_the_reference(void **state) {
    (void)state;

    bldc_hall_commutation_t out;

    /* 1,0,running=1,has_commutated=0 -> step 0, changed, applied. */
    bldc_hall_commutation(1, 0, true, false, &out);
    assert_int_equal(out.comm_step, 0);
    assert_true(out.step_changed);
    assert_true(out.apply);

    /* 1,1,1,0 -> unchanged, and applied because nothing has commutated yet. */
    bldc_hall_commutation(1, 1, true, false, &out);
    assert_int_equal(out.comm_step, 1);
    assert_false(out.step_changed);
    assert_true(out.apply);

    /* 1,1,1,1 -> unchanged and applied to nothing. */
    bldc_hall_commutation(1, 1, true, true, &out);
    assert_false(out.step_changed);
    assert_false(out.apply);

    /* 1,3,0,0 -> the step is taken, nothing is applied. */
    bldc_hall_commutation(1, 3, false, false, &out);
    assert_int_equal(out.comm_step, 3);
    assert_true(out.step_changed);
    assert_false(out.apply);

    /* 6,7,1,1 -> seven is a step like any other reading. */
    bldc_hall_commutation(6, 7, true, true, &out);
    assert_int_equal(out.comm_step, 7);
    assert_true(out.step_changed);
    assert_true(out.apply);

    /* Nothing to write the decision into. */
    bldc_hall_commutation(1, 2, true, false, NULL);
}

typedef struct mock_bldc_ports {
    uint8_t pins;
    int applies;
    int last_step;
    edge_status_t apply_status;
} mock_bldc_ports_t;

static uint8_t mock_read_hall(void *self) {
    return ((mock_bldc_ports_t *)self)->pins;
}

static edge_status_t mock_apply_step(void *self, int comm_step) {
    mock_bldc_ports_t *ports = (mock_bldc_ports_t *)self;
    ports->applies++;
    ports->last_step = comm_step;
    return ports->apply_status;
}

/*
 * The same decision, driven through the module: the reading comes from the hall port, the step goes
 * to the phase port, and the drive's own state is what carries it between calls - including the
 * catch-up apply for a motor that started on the step it is already standing on.
 */
static void test_bldc_drive_hall_commutation(void **state) {
    (void)state;

    mock_bldc_ports_t ports = {.pins = 0x01u, .apply_status = EDGE_OK};
    bldc_hall_port_t hall = {.read_hall = mock_read_hall, .self = &ports};
    bldc_phase_port_t phase = {.apply_step = mock_apply_step, .self = &ports};

    bldc_drive_config_t config = {.sensor_mode = BLDC_SENSOR_MODE_SENSORED,
                                  .hall_sl_erpm = 4000.0f,
                                  .hall_table = {0, 5, 3, 1, 6, 4, 2, 0}};
    bldc_drive_t drive;
    bldc_drive_construct(&drive, EDGE_MOD_BLDC_DRIVE, 25u, &config);
    assert_int_equal(bldc_drive_init(&drive), EDGE_OK);
    assert_false(bldc_drive_has_commutated(&drive));

    /* No hall port yet: a drive that cannot read the halls says so. */
    assert_int_equal(bldc_drive_commutate_hall(&drive, true), EDGE_ENOTSUP);
    assert_int_equal(bldc_drive_commutate_hall(NULL, true), EDGE_EINVAL);

    bldc_drive_set_hall_port(&drive, &hall);
    bldc_drive_set_phase_port(&drive, &phase);

    /* Reading one, and the configuration's table forwards names step three for it - so the step
     * changes, and it is applied because the motor is running. */
    assert_int_equal(bldc_drive_commutate_hall(&drive, true), EDGE_OK);
    assert_int_equal(ports.applies, 1);
    assert_int_equal(ports.last_step, 3);
    assert_true(bldc_drive_has_commutated(&drive));

    /* Not running: the step is taken and nothing is applied. Reading three is step one forwards. */
    ports.pins = 0x03u;
    assert_int_equal(bldc_drive_commutate_hall(&drive, false), EDGE_OK);
    assert_int_equal(bldc_drive_get_comm_step(&drive), 1);
    assert_int_equal(ports.applies, 1);

    /* Running again with nothing changed, and something already commutated: applied to nothing,
     * which is the reference's other branch. */
    assert_int_equal(bldc_drive_commutate_hall(&drive, true), EDGE_OK);
    assert_int_equal(ports.applies, 1);

    /* A reading that does change is applied, and the tachometer saw the commutation. Reading two is
     * step five forwards. */
    ports.pins = 0x02u;
    assert_int_equal(bldc_drive_commutate_hall(&drive, true), EDGE_OK);
    assert_int_equal(ports.applies, 2);
    assert_int_equal(ports.last_step, 5);

    /* A phase port that refuses is reported, and nothing is recorded as applied. */
    bldc_drive_t bare;
    bldc_drive_construct(&bare, EDGE_MOD_BLDC_DRIVE, 25u, &config);
    assert_int_equal(bldc_drive_init(&bare), EDGE_OK);
    bldc_drive_set_hall_port(&bare, &hall);
    ports.pins = 0x06u;
    assert_int_equal(bldc_drive_commutate_hall(&bare, true), EDGE_ENOTSUP);

    ports.apply_status = EDGE_EIO;
    bldc_drive_set_phase_port(&bare, &phase);
    assert_int_equal(bldc_drive_commutate_hall(&bare, true), EDGE_EIO);
    assert_false(bldc_drive_has_commutated(&bare));

    bldc_drive_set_hall_port(NULL, &hall);
    bldc_drive_set_phase_port(NULL, &phase);
}

/*
 * mcpwm_read_hall_phase (mcpwm.c:2302-2304): the reading looked up in the half of the table the
 * commanded direction selects. The numbers are the harness run's own, and they are what shows the
 * two halves are not the same lookup: forwards the table is the mapped one, backwards it is the raw
 * one.
 */
static void test_bldc_hall_phase_from_table_matches_the_reference(void **state) {
    (void)state;

    /* The configuration's table and its forward mapping, as bldc_build_hall_tables makes them. */
    const int8_t reverse[8] = {0, 5, 3, 1, 6, 4, 2, 0};
    const int8_t forward[8] = {0, 3, 5, 1, 2, 4, 6, 0};
    const int forward_expected[8] = {0, 3, 5, 1, 2, 4, 6, 0};
    const int reverse_expected[8] = {0, 5, 3, 1, 6, 4, 2, 0};

    for (uint8_t reading = 0u; reading < 8u; reading++) {
        assert_int_equal(bldc_hall_phase_from_table(forward, reverse, reading, 0),
                         forward_expected[reading]);
        assert_int_equal(bldc_hall_phase_from_table(forward, reverse, reading, 1),
                         reverse_expected[reading]);
    }

    /* A reading is three bits, and nothing to look up in. */
    assert_int_equal(bldc_hall_phase_from_table(forward, reverse, 0xFFu, 0), forward_expected[7]);
    assert_int_equal(bldc_hall_phase_from_table(NULL, reverse, 1u, 0), 0);
    assert_int_equal(bldc_hall_phase_from_table(forward, NULL, 1u, 0), 0);
}

/*
 * The detection table's own numbers, from the harness run of mcpwm_reset_hall_detect_table and
 * mcpwm_get_hall_detect_result: twenty samples for each reading's own step, and a sample above the
 * gate which is not counted; then the same table with one reading short, and finally a sensor port
 * that is not a hall one.
 */
static void test_bldc_hall_detect_matches_the_reference(void **state) {
    (void)state;

    bldc_hall_detect_counts_t counts;
    int8_t out[8];

    bldc_hall_detect_reset(counts);
    for (int reading = 1; reading < 7; reading++) {
        for (int n = 0; n < 20; n++) {
            bldc_hall_detect_sample(counts, (uint8_t)reading, reading, true);
        }
        /* Above the gate, so the reference's own condition leaves it out. */
        bldc_hall_detect_sample(counts, (uint8_t)reading, (reading % 6) + 1, false);
    }

    assert_int_equal(bldc_hall_detect_result(counts, true, out), 0);
    const int8_t expected[8] = {-1, 1, 2, 3, 4, 5, 6, -1};
    assert_memory_equal(out, expected, sizeof(expected));

    /* One reading fewer than the reference needs named. */
    bldc_hall_detect_reset(counts);
    for (int reading = 1; reading < 6; reading++) {
        for (int n = 0; n < 20; n++) {
            bldc_hall_detect_sample(counts, (uint8_t)reading, reading, true);
        }
    }
    assert_int_equal(bldc_hall_detect_result(counts, true, out), -1);

    /* Not a hall sensor port, and nothing to write into. */
    assert_int_equal(bldc_hall_detect_result(counts, false, out), -3);
    assert_int_equal(bldc_hall_detect_result(counts, true, NULL), -1);
    assert_int_equal(bldc_hall_detect_result(counts, false, NULL), -3);

    /* A step outside one to six is not a column the reference's table has. */
    bldc_hall_detect_sample(counts, 1u, 0, true);
    bldc_hall_detect_sample(counts, 1u, 7, true);
}

/*
 * The same detection table through the drive, and the configuration's table looked up by direction
 * - which is the correction this slice carries: the commutation is driven by the looked-up phase,
 * not by the raw reading, so the two directions name different steps for the same halls.
 */
static void test_bldc_drive_direction_and_hall_detect(void **state) {
    (void)state;

    mock_bldc_ports_t ports = {.pins = 0x01u, .apply_status = EDGE_OK};
    bldc_hall_port_t hall = {.read_hall = mock_read_hall, .self = &ports};
    bldc_phase_port_t phase = {.apply_step = mock_apply_step, .self = &ports};

    bldc_drive_config_t config = {.sensor_mode = BLDC_SENSOR_MODE_SENSORED,
                                  .hall_sl_erpm = 4000.0f,
                                  .hall_table = {0, 5, 3, 1, 6, 4, 2, 0}};
    bldc_drive_t drive;
    bldc_drive_construct(&drive, EDGE_MOD_BLDC_DRIVE, 25u, &config);
    assert_int_equal(bldc_drive_init(&drive), EDGE_OK);
    bldc_drive_set_hall_port(&drive, &hall);
    bldc_drive_set_phase_port(&drive, &phase);

    /* Reading one, forwards: the forward half names step three. */
    bldc_drive_set_direction(&drive, 0);
    assert_int_equal(bldc_drive_get_direction(&drive), 0);
    assert_int_equal(bldc_drive_commutate_hall(&drive, true), EDGE_OK);
    assert_int_equal(ports.last_step, 3);

    /* The same halls backwards: the raw half names step five, and the commutation follows it. */
    bldc_drive_set_direction(&drive, 1);
    assert_int_equal(bldc_drive_commutate_hall(&drive, true), EDGE_OK);
    assert_int_equal(ports.last_step, 5);

    /* The detection table: the drive's own, filled from its hall port and its commutation step. */
    bldc_drive_hall_detect_reset(&drive);
    const bldc_hall_detect_counts_t *counts = bldc_drive_hall_detect_counts(&drive);
    assert_non_null(counts);
    bldc_drive_hall_detect_sample(&drive, true);
    assert_int_equal((*counts)[1][5], 1); /* pin reading one, step five */
    bldc_drive_hall_detect_sample(&drive, false);
    assert_int_equal((*counts)[1][5], 1); /* the gate kept it out */

    int8_t table[8];
    assert_int_equal(bldc_drive_hall_detect_result(&drive, true, table), -1); /* one sample only */

    /* Guards. */
    bldc_drive_set_direction(NULL, 1);
    assert_int_equal(bldc_drive_get_direction(NULL), 0);
    bldc_drive_hall_detect_reset(NULL);
    bldc_drive_hall_detect_sample(NULL, true);
    assert_int_equal(bldc_drive_hall_detect_result(NULL, true, table), -1);
    assert_null(bldc_drive_hall_detect_counts(NULL));

    bldc_drive_t no_hall;
    bldc_drive_construct(&no_hall, EDGE_MOD_BLDC_DRIVE, 25u, &config);
    assert_int_equal(bldc_drive_init(&no_hall), EDGE_OK);
    bldc_drive_hall_detect_sample(&no_hall, true); /* no hall port: nothing to count */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bldc_hall_tables_match_the_reference),
        cmocka_unit_test(test_bldc_comm_step_advance_matches_the_reference),
        cmocka_unit_test(test_bldc_tacho_delta_matches_the_reference),
        cmocka_unit_test(test_bldc_sensorless_decision_matches_the_reference),
        cmocka_unit_test(test_bldc_drive_module_behaviour),
        cmocka_unit_test(test_bldc_hall_phase_matches_the_reference),
        cmocka_unit_test(test_bldc_hall_commutation_matches_the_reference),
        cmocka_unit_test(test_bldc_drive_hall_commutation),
        cmocka_unit_test(test_bldc_hall_phase_from_table_matches_the_reference),
        cmocka_unit_test(test_bldc_hall_detect_matches_the_reference),
        cmocka_unit_test(test_bldc_drive_direction_and_hall_detect),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
