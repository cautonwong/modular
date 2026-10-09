#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "stopwatch/stopwatch.h"

static uint32_t g_mock_now_ms = 0;

static uint32_t mock_get_tick_ms(void *self) {
    (void)self;
    return g_mock_now_ms;
}

static void test_stopwatch_basic_start_pause_resume(void **state) {
    (void)state;

    g_mock_now_ms = 1000;
    const stopwatch_clock_if_t clock_if = {
        .get_tick_ms = mock_get_tick_ms,
        .self = NULL,
    };

    stopwatch_app_t sw;
    stopwatch_construct(&sw, EDGE_MOD_STOPWATCH, 20u, &clock_if);
    assert_int_equal(stopwatch_init(&sw), EDGE_OK);
    assert_true(stopwatch_is_cleared(&sw));
    assert_int_equal(stopwatch_get_elapsed_ms(&sw), 0);

    /* Start at t = 1000ms */
    stopwatch_start(&sw);
    assert_true(stopwatch_is_running(&sw));

    /* Advance to t = 3500ms -> elapsed = 2500ms */
    g_mock_now_ms = 3500;
    assert_int_equal(stopwatch_get_elapsed_ms(&sw), 2500);

    /* Pause at t = 3500ms */
    stopwatch_pause(&sw);
    assert_true(stopwatch_is_paused(&sw));
    assert_int_equal(stopwatch_get_elapsed_ms(&sw), 2500);

    /* Advance time while paused to t = 5000ms -> elapsed remains 2500ms */
    g_mock_now_ms = 5000;
    assert_int_equal(stopwatch_get_elapsed_ms(&sw), 2500);

    /* Resume at t = 5000ms */
    stopwatch_start(&sw);
    assert_true(stopwatch_is_running(&sw));

    /* Advance to t = 6200ms -> total elapsed = 2500 + 1200 = 3700ms */
    g_mock_now_ms = 6200;
    assert_int_equal(stopwatch_get_elapsed_ms(&sw), 3700);

    /* Clear */
    stopwatch_clear(&sw);
    assert_true(stopwatch_is_cleared(&sw));
    assert_int_equal(stopwatch_get_elapsed_ms(&sw), 0);
}

static void test_stopwatch_lap_recording(void **state) {
    (void)state;

    g_mock_now_ms = 0;
    const stopwatch_clock_if_t clock_if = {
        .get_tick_ms = mock_get_tick_ms,
        .self = NULL,
    };

    stopwatch_app_t sw;
    stopwatch_construct(&sw, EDGE_MOD_STOPWATCH, 20u, &clock_if);
    stopwatch_start(&sw);

    /* Lap 1 at 1000ms */
    g_mock_now_ms = 1000;
    assert_int_equal(stopwatch_add_lap(&sw), EDGE_OK);
    assert_int_equal(stopwatch_get_max_lap_number(&sw), 1);

    /* Lap 2 at 2500ms */
    g_mock_now_ms = 2500;
    assert_int_equal(stopwatch_add_lap(&sw), EDGE_OK);
    assert_int_equal(stopwatch_get_max_lap_number(&sw), 2);

    /* Lap 3 at 4000ms */
    g_mock_now_ms = 4000;
    assert_int_equal(stopwatch_add_lap(&sw), EDGE_OK);

    /* Lap 4 at 5500ms */
    g_mock_now_ms = 5500;
    assert_int_equal(stopwatch_add_lap(&sw), EDGE_OK);

    /* Lap 5 at 7000ms (pushes out lap 1) */
    g_mock_now_ms = 7000;
    assert_int_equal(stopwatch_add_lap(&sw), EDGE_OK);
    assert_int_equal(stopwatch_get_max_lap_number(&sw), 5);

    /* Verify latest lap (index 0) is Lap 5 @ 7000ms */
    stopwatch_lap_t lap;
    assert_int_equal(stopwatch_get_lap(&sw, 0, &lap), EDGE_OK);
    assert_int_equal(lap.number, 5);
    assert_int_equal(lap.time_since_start_ms, 7000);

    /* Verify index 1 is Lap 4 @ 5500ms */
    assert_int_equal(stopwatch_get_lap(&sw, 1, &lap), EDGE_OK);
    assert_int_equal(lap.number, 4);
    assert_int_equal(lap.time_since_start_ms, 5500);

    /* Verify index 3 is Lap 2 @ 2500ms */
    assert_int_equal(stopwatch_get_lap(&sw, 3, &lap), EDGE_OK);
    assert_int_equal(lap.number, 2);
    assert_int_equal(lap.time_since_start_ms, 2500);

    /* Verify index 4 (past buffer) is ENOENT */
    assert_int_equal(stopwatch_get_lap(&sw, 4, &lap), EDGE_ENOENT);

    /* Verify lap durations: Lap 5 took 7000 - 5500 = 1500ms; Lap 4 took 5500 - 4000 = 1500ms */
    assert_int_equal(stopwatch_get_lap_duration_ms(&sw, 0), 1500);
    assert_int_equal(stopwatch_get_lap_duration_ms(&sw, 1), 1500);
}

static void test_stopwatch_lap_boundary_and_rollover(void **state) {
    (void)state;
    g_mock_now_ms = 0;
    const stopwatch_clock_if_t clock_if = {
        .get_tick_ms = mock_get_tick_ms,
        .self = NULL,
    };

    stopwatch_app_t sw;
    stopwatch_construct(&sw, EDGE_MOD_STOPWATCH, 20u, &clock_if);
    stopwatch_start(&sw);

    /* Lap 1 at 500ms -> duration is 500ms */
    g_mock_now_ms = 500;
    assert_int_equal(stopwatch_add_lap(&sw), EDGE_OK);
    assert_int_equal(stopwatch_get_lap_duration_ms(&sw, 0), 500);

    /* Lap 2 at 1200ms -> duration is 1200 - 500 = 700ms */
    g_mock_now_ms = 1200;
    assert_int_equal(stopwatch_add_lap(&sw), EDGE_OK);
    assert_int_equal(stopwatch_get_lap_duration_ms(&sw, 0), 700);

    /* Out of bounds index -> returns 0 */
    assert_int_equal(stopwatch_get_lap_duration_ms(&sw, 5), 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_stopwatch_basic_start_pause_resume),
        cmocka_unit_test(test_stopwatch_lap_recording),
        cmocka_unit_test(test_stopwatch_lap_boundary_and_rollover),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
