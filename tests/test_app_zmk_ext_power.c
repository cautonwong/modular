/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_ext_power/ext_power.h"

typedef struct mock_hw {
    int calls;
    bool last_enable;
    edge_status_t rc;
} mock_hw_t;

static edge_status_t mock_set_power(void *self, bool enable) {
    mock_hw_t *h = (mock_hw_t *)self;
    h->calls++;
    h->last_enable = enable;
    return h->rc;
}

typedef struct mock_sink {
    int calls;
    bool last_state;
    edge_status_t rc;
} mock_sink_t;

static edge_status_t mock_post_power_changed(void *self, bool is_on) {
    mock_sink_t *s = (mock_sink_t *)self;
    s->calls++;
    s->last_state = is_on;
    return s->rc;
}

static void test_ext_power_control_and_toggle(void **state) {
    (void)state;
    mock_hw_t hw_ctx = {0};
    zmk_ext_power_hw_if_t hw = {.self = &hw_ctx, .set_power = mock_set_power};
    mock_sink_t sink_ctx = {0};
    zmk_ext_power_event_sink_if_t sink = {.self = &sink_ctx,
                                          .post_power_changed = mock_post_power_changed};

    zmk_ext_power_app_t app;
    zmk_ext_power_construct(&app, EDGE_MOD_ZMK_EXT_POWER, 30, &hw, &sink);

    assert_int_equal(zmk_ext_power_init(&app), EDGE_OK);
    assert_true(hw_ctx.last_enable);
    assert_true(sink_ctx.last_state);
    assert_true(zmk_ext_power_is_enabled(&app));

    // Disable
    assert_int_equal(zmk_ext_power_disable(&app), EDGE_OK);
    assert_false(hw_ctx.last_enable);
    assert_false(sink_ctx.last_state);
    assert_false(zmk_ext_power_is_enabled(&app));

    // Toggle back on
    assert_int_equal(zmk_ext_power_toggle(&app), EDGE_OK);
    assert_true(hw_ctx.last_enable);
    assert_true(sink_ctx.last_state);
    assert_true(zmk_ext_power_is_enabled(&app));
}

static void test_ext_power_sleep_cutoff(void **state) {
    (void)state;
    mock_hw_t hw_ctx = {0};
    zmk_ext_power_hw_if_t hw = {.self = &hw_ctx, .set_power = mock_set_power};
    mock_sink_t sink_ctx = {0};
    zmk_ext_power_event_sink_if_t sink = {.self = &sink_ctx,
                                          .post_power_changed = mock_post_power_changed};

    zmk_ext_power_app_t app;
    zmk_ext_power_construct(&app, EDGE_MOD_ZMK_EXT_POWER, 30, &hw, &sink);
    zmk_ext_power_init(&app);

    // Sleep event cuts power
    assert_int_equal(zmk_ext_power_on_activity_change(&app, 2 /* SLEEP */), EDGE_OK);
    assert_false(hw_ctx.last_enable);

    // Wake event restores power
    assert_int_equal(zmk_ext_power_on_activity_change(&app, 0 /* ACTIVE */), EDGE_OK);
    assert_true(hw_ctx.last_enable);

    // Module power_off turns off power
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_false(hw_ctx.last_enable);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ext_power_control_and_toggle),
        cmocka_unit_test(test_ext_power_sleep_cutoff),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
