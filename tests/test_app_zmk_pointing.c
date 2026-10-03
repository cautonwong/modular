/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_pointing/pointing.h"

typedef struct mock_pointing_sink {
    int motion_calls;
    int button_calls;
    int16_t last_dx;
    int16_t last_dy;
    int8_t last_v;
    int8_t last_h;
    uint8_t last_buttons;
} mock_pointing_sink_t;

static edge_status_t mock_report_motion(void *self, int16_t dx, int16_t dy, int8_t v_scroll,
                                        int8_t h_scroll) {
    mock_pointing_sink_t *sink = (mock_pointing_sink_t *)self;
    sink->motion_calls++;
    sink->last_dx = dx;
    sink->last_dy = dy;
    sink->last_v = v_scroll;
    sink->last_h = h_scroll;
    return EDGE_OK;
}

static edge_status_t mock_report_buttons(void *self, uint8_t buttons_mask) {
    mock_pointing_sink_t *sink = (mock_pointing_sink_t *)self;
    sink->button_calls++;
    sink->last_buttons = buttons_mask;
    return EDGE_OK;
}

static void test_pointing_motion_scaling_and_buttons(void **state) {
    (void)state;
    mock_pointing_sink_t sink_ctx = {0};
    zmk_pointing_hid_if_t hid_if = {
        .self = &sink_ctx,
        .report_motion = mock_report_motion,
        .report_buttons = mock_report_buttons,
    };

    zmk_pointing_app_t app;
    zmk_pointing_construct(&app, EDGE_MOD_ZMK_POINTING, 50, &hid_if);
    assert_int_equal(zmk_pointing_init(&app), EDGE_OK);

    // 1:1 motion
    assert_int_equal(zmk_pointing_motion(&app, 10, -5, 1, 0), EDGE_OK);
    assert_int_equal(sink_ctx.motion_calls, 1);
    assert_int_equal(sink_ctx.last_dx, 10);
    assert_int_equal(sink_ctx.last_dy, -5);
    assert_int_equal(sink_ctx.last_v, 1);

    // 2x scaling
    zmk_pointing_set_scaler(&app, 2, 1);
    assert_int_equal(zmk_pointing_motion(&app, 10, -5, 1, 0), EDGE_OK);
    assert_int_equal(sink_ctx.motion_calls, 2);
    assert_int_equal(sink_ctx.last_dx, 20);
    assert_int_equal(sink_ctx.last_dy, -10);
    assert_int_equal(sink_ctx.last_v, 2);

    // Button presses
    assert_int_equal(zmk_pointing_button_press(&app, ZMK_MOUSE_BTN_LEFT), EDGE_OK);
    assert_int_equal(sink_ctx.button_calls, 1);
    assert_int_equal(sink_ctx.last_buttons, ZMK_MOUSE_BTN_LEFT);

    assert_int_equal(zmk_pointing_button_press(&app, ZMK_MOUSE_BTN_RIGHT), EDGE_OK);
    assert_int_equal(sink_ctx.button_calls, 2);
    assert_int_equal(sink_ctx.last_buttons, ZMK_MOUSE_BTN_LEFT | ZMK_MOUSE_BTN_RIGHT);

    assert_int_equal(zmk_pointing_button_release(&app, ZMK_MOUSE_BTN_LEFT), EDGE_OK);
    assert_int_equal(sink_ctx.button_calls, 3);
    assert_int_equal(sink_ctx.last_buttons, ZMK_MOUSE_BTN_RIGHT);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pointing_motion_scaling_and_buttons),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
