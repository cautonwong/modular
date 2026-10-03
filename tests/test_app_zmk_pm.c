/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_pm/pm.h"

typedef struct mock_pm_sink {
    int calls;
    uint8_t last_state;
} mock_pm_sink_t;

static edge_status_t mock_on_activity(void *self, uint8_t state) {
    mock_pm_sink_t *sink = (mock_pm_sink_t *)self;
    sink->calls++;
    sink->last_state = state;
    return EDGE_OK;
}

static void test_pm_fsm_transitions(void **state) {
    (void)state;
    mock_pm_sink_t sink_ctx = {0};
    zmk_pm_sink_if_t sink_if = {.self = &sink_ctx, .on_activity_state_changed = mock_on_activity};

    zmk_pm_config_t cfg = {
        .idle_timeout_ms = 1000,
        .sleep_timeout_ms = 5000,
    };

    zmk_pm_app_t app;
    zmk_pm_construct(&app, EDGE_MOD_ZMK_PM, 50, &cfg, &sink_if);
    assert_int_equal(zmk_pm_init(&app), EDGE_OK);
    assert_int_equal(zmk_pm_get_activity_state(&app), ZMK_ACTIVITY_ACTIVE);

    // Initial activity at t=0
    zmk_pm_notify_activity(&app, 0);

    // Tick at 500ms -> still ACTIVE
    zmk_pm_tick(&app, 500);
    assert_int_equal(zmk_pm_get_activity_state(&app), ZMK_ACTIVITY_ACTIVE);
    assert_int_equal(sink_ctx.calls, 0);

    // Tick at 1200ms (> 1000ms idle timeout) -> transitions to IDLE
    zmk_pm_tick(&app, 1200);
    assert_int_equal(zmk_pm_get_activity_state(&app), ZMK_ACTIVITY_IDLE);
    assert_int_equal(sink_ctx.calls, 1);
    assert_int_equal(sink_ctx.last_state, ZMK_ACTIVITY_IDLE);

    // Tick at 5500ms (> 5000ms sleep timeout) -> transitions to SLEEP
    zmk_pm_tick(&app, 5500);
    assert_int_equal(zmk_pm_get_activity_state(&app), ZMK_ACTIVITY_SLEEP);
    assert_int_equal(sink_ctx.calls, 2);
    assert_int_equal(sink_ctx.last_state, ZMK_ACTIVITY_SLEEP);

    // Key activity at t=6000 -> wakes up to ACTIVE
    zmk_pm_notify_activity(&app, 6000);
    assert_int_equal(zmk_pm_get_activity_state(&app), ZMK_ACTIVITY_ACTIVE);
    assert_int_equal(sink_ctx.calls, 3);
    assert_int_equal(sink_ctx.last_state, ZMK_ACTIVITY_ACTIVE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pm_fsm_transitions),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
