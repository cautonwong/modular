#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "timer/timer.h"

static uint32_t g_mock_timer_now_ms = 0;
static bool g_timer_alert_started = false;
static bool g_timer_alert_stopped = false;

static uint32_t mock_timer_get_tick_ms(void *self) {
    (void)self;
    return g_mock_timer_now_ms;
}

static edge_status_t mock_timer_alert_start(void *self) {
    (void)self;
    g_timer_alert_started = true;
    g_timer_alert_stopped = false;
    return EDGE_OK;
}

static edge_status_t mock_timer_alert_stop(void *self) {
    (void)self;
    g_timer_alert_stopped = true;
    g_timer_alert_started = false;
    return EDGE_OK;
}

static void test_watch_timer_countdown_and_expiry(void **state) {
    (void)state;

    g_mock_timer_now_ms = 1000;
    g_timer_alert_started = false;
    g_timer_alert_stopped = false;

    const timer_clock_if_t clock_if = {
        .get_tick_ms = mock_timer_get_tick_ms,
        .self = NULL,
    };
    const timer_alert_if_t alert_if = {
        .start_alert = mock_timer_alert_start,
        .stop_alert = mock_timer_alert_stop,
        .self = NULL,
    };

    watch_timer_app_t tmr;
    watch_timer_construct(&tmr, EDGE_MOD_WATCH_TIMER, 20u, &clock_if, &alert_if);
    assert_int_equal(watch_timer_init(&tmr), EDGE_OK);
    assert_false(watch_timer_is_running(&tmr));

    /* Start 5-second timer at t = 1000ms (expiry = 6000ms) */
    watch_timer_start(&tmr, 5000u);
    assert_true(watch_timer_is_running(&tmr));

    /* At t = 3000ms -> remaining 3000ms, not expired */
    g_mock_timer_now_ms = 3000;
    watch_timer_status_t st;
    assert_int_equal(watch_timer_get_status(&tmr, &st), EDGE_OK);
    assert_int_equal(st.distance_to_expiry_ms, 3000);
    assert_false(st.expired);
    assert_int_equal(tmr.module.poll(&tmr.module), EDGE_OK);
    assert_false(g_timer_alert_started);

    /* At t = 6000ms -> expired! */
    g_mock_timer_now_ms = 6000;
    assert_int_equal(tmr.module.poll(&tmr.module), EDGE_OK);
    assert_false(watch_timer_is_running(&tmr));
    assert_true(g_timer_alert_started);

    assert_int_equal(watch_timer_get_status(&tmr, &st), EDGE_OK);
    assert_true(st.expired);
    assert_int_equal(st.distance_to_expiry_ms, 0);

    /* At t = 7500ms -> elapsed since expiry = 1500ms */
    g_mock_timer_now_ms = 7500;
    assert_int_equal(watch_timer_get_status(&tmr, &st), EDGE_OK);
    assert_true(st.expired);
    assert_int_equal(st.distance_to_expiry_ms, 1500);

    /* Reset / Stop */
    watch_timer_stop(&tmr);
    assert_true(g_timer_alert_stopped);
    assert_int_equal(watch_timer_get_status(&tmr, &st), EDGE_ENOENT);
}

static void test_watch_timer_auto_stop_and_auto_reset(void **state) {
    (void)state;
    g_mock_timer_now_ms = 0;
    g_timer_alert_started = false;
    g_timer_alert_stopped = false;

    const timer_clock_if_t clock_if = {
        .get_tick_ms = mock_timer_get_tick_ms,
        .self = NULL,
    };
    const timer_alert_if_t alert_if = {
        .start_alert = mock_timer_alert_start,
        .stop_alert = mock_timer_alert_stop,
        .self = NULL,
    };

    watch_timer_app_t tmr;
    watch_timer_construct(&tmr, EDGE_MOD_WATCH_TIMER, 20u, &clock_if, &alert_if);
    watch_timer_init(&tmr);

    /* Start 2-second timer at t = 0ms (expiry = 2000ms) */
    watch_timer_start(&tmr, 2000u);

    /* Trigger expiry at t = 2000ms */
    g_mock_timer_now_ms = 2000;
    assert_int_equal(tmr.module.poll(&tmr.module), EDGE_OK);
    assert_true(g_timer_alert_started);
    assert_false(g_timer_alert_stopped);

    /* Advance 10 seconds post-expiry (t = 12001ms) -> motor alert stops */
    g_mock_timer_now_ms = 12001;
    assert_int_equal(tmr.module.poll(&tmr.module), EDGE_OK);
    assert_true(g_timer_alert_stopped);

    /* Advance 60 seconds post-expiry (t = 62001ms) -> auto-resets expired timer */
    g_mock_timer_now_ms = 62001;
    assert_int_equal(tmr.module.poll(&tmr.module), EDGE_OK);

    watch_timer_status_t st;
    assert_int_equal(watch_timer_get_status(&tmr, &st), EDGE_ENOENT);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_watch_timer_countdown_and_expiry),
        cmocka_unit_test(test_watch_timer_auto_stop_and_auto_reset),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
