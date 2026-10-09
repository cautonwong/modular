/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "watch_power/watch_power.h"

static uint8_t g_mock_brightness = 0;
static bool g_mock_display_sleep = false;

static edge_status_t mock_set_brightness(void *self, uint8_t percent) {
    (void)self;
    g_mock_brightness = percent;
    return EDGE_OK;
}

static edge_status_t mock_display_sleep(void *self, bool enable) {
    (void)self;
    g_mock_display_sleep = enable;
    return EDGE_OK;
}

static void test_watch_power_inactivity_state_transitions(void **state) {
    (void)state;
    g_mock_brightness = 0;
    g_mock_display_sleep = false;

    const watch_power_display_if_t display_if = {
        .set_brightness = mock_set_brightness,
        .sleep = mock_display_sleep,
        .self = NULL,
    };

    watch_power_t wp;
    watch_power_construct(&wp, EDGE_MOD_WATCH_POWER, 100u, &display_if, NULL);
    assert_int_equal(watch_power_init(&wp), EDGE_OK);

    // Initial state: AWAKE, brightness = 100%, sleep = false
    assert_int_equal(wp.state, WATCH_POWER_AWAKE);
    assert_int_equal(g_mock_brightness, 100);
    assert_false(g_mock_display_sleep);

    // Inactivity 9900ms -> still AWAKE
    assert_int_equal(watch_power_update(&wp, 9900u), EDGE_OK);
    assert_int_equal(wp.state, WATCH_POWER_AWAKE);

    // +200ms (10100ms total) -> transitions to DIMMED (brightness = 10%)
    assert_int_equal(watch_power_update(&wp, 200u), EDGE_OK);
    assert_int_equal(wp.state, WATCH_POWER_DIMMED);
    assert_int_equal(g_mock_brightness, 10);
    assert_false(g_mock_display_sleep);

    // +4900ms in DIMMED -> still DIMMED
    assert_int_equal(watch_power_update(&wp, 4900u), EDGE_OK);
    assert_int_equal(wp.state, WATCH_POWER_DIMMED);

    // +200ms (5100ms in DIMMED) -> transitions to SLEEPING (brightness = 0%, sleep = true)
    assert_int_equal(watch_power_update(&wp, 200u), EDGE_OK);
    assert_int_equal(wp.state, WATCH_POWER_SLEEPING);
    assert_int_equal(g_mock_brightness, 0);
    assert_true(g_mock_display_sleep);

    // User tap/touch event arrives -> wakes back up to AWAKE
    const edge_event_t ev = {.id = EDGE_EVT_WATCH_TOUCH};
    edge_module_t *mod = watch_power_module(&wp);
    assert_int_equal(mod->on_event(mod, &ev), EDGE_OK);
    assert_int_equal(wp.state, WATCH_POWER_AWAKE);
    assert_int_equal(g_mock_brightness, 100);
    assert_false(g_mock_display_sleep);
}

static void test_watch_power_explicit_sleep_and_wake(void **state) {
    (void)state;
    g_mock_brightness = 0;
    g_mock_display_sleep = false;

    const watch_power_display_if_t display_if = {
        .set_brightness = mock_set_brightness,
        .sleep = mock_display_sleep,
        .self = NULL,
    };

    watch_power_t wp;
    watch_power_construct(&wp, EDGE_MOD_WATCH_POWER, 100u, &display_if, NULL);
    assert_int_equal(watch_power_init(&wp), EDGE_OK);

    // Explicit sleep
    watch_power_go_to_sleep(&wp);
    assert_int_equal(wp.state, WATCH_POWER_SLEEPING);
    assert_true(g_mock_display_sleep);
    assert_int_equal(g_mock_brightness, 0);

    // Explicit wake
    watch_power_wake_up(&wp);
    assert_int_equal(wp.state, WATCH_POWER_AWAKE);
    assert_false(g_mock_display_sleep);
    assert_int_equal(g_mock_brightness, 100);
}

static void test_watch_power_wake_lock(void **state) {
    (void)state;
    g_mock_brightness = 0;
    g_mock_display_sleep = false;

    const watch_power_display_if_t display_if = {
        .set_brightness = mock_set_brightness,
        .sleep = mock_display_sleep,
        .self = NULL,
    };

    watch_power_t wp;
    watch_power_construct(&wp, EDGE_MOD_WATCH_POWER, 100u, &display_if, NULL);
    assert_int_equal(watch_power_init(&wp), EDGE_OK);

    assert_false(watch_power_is_wake_locked(&wp));
    watch_power_acquire_wake_lock(&wp);
    assert_true(watch_power_is_wake_locked(&wp));

    // Even with 30 seconds of inactivity, screen stays AWAKE and not dimmed
    assert_int_equal(watch_power_update(&wp, 30000u), EDGE_OK);
    assert_int_equal(wp.state, WATCH_POWER_AWAKE);
    assert_int_equal(g_mock_brightness, 100);
    assert_false(g_mock_display_sleep);

    // Release wake lock -> now normal inactivity timer resumes
    watch_power_release_wake_lock(&wp);
    assert_false(watch_power_is_wake_locked(&wp));

    assert_int_equal(watch_power_update(&wp, 10100u), EDGE_OK);
    assert_int_equal(wp.state, WATCH_POWER_DIMMED);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_watch_power_inactivity_state_transitions),
        cmocka_unit_test(test_watch_power_explicit_sleep_and_wake),
        cmocka_unit_test(test_watch_power_wake_lock),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
