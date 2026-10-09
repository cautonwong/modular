/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_matrix/zmk_matrix.h"

typedef struct mock_hw {
    int scan_calls;
    uint32_t last_scan_time;
    edge_status_t rc;
} mock_hw_t;

static edge_status_t mock_scan_raw(void *self, uint32_t timestamp_ms) {
    mock_hw_t *hw = (mock_hw_t *)self;
    hw->scan_calls++;
    hw->last_scan_time = timestamp_ms;
    return hw->rc;
}

typedef struct mock_sink {
    int post_calls;
    uint32_t last_pos;
    bool last_pressed;
    uint32_t last_time;
    edge_status_t rc;
} mock_sink_t;

static edge_status_t mock_post_position(void *self, uint32_t position, bool pressed,
                                        uint32_t timestamp_ms) {
    mock_sink_t *sink = (mock_sink_t *)self;
    sink->post_calls++;
    sink->last_pos = position;
    sink->last_pressed = pressed;
    sink->last_time = timestamp_ms;
    return sink->rc;
}

static void test_matrix_construct_and_init(void **state) {
    (void)state;
    mock_hw_t hw_ctx = {0};
    zmk_matrix_hw_if_t hw = {.self = &hw_ctx, .scan_raw = mock_scan_raw};
    mock_sink_t sink_ctx = {0};
    zmk_matrix_event_sink_if_t sink = {.self = &sink_ctx,
                                       .post_position_event = mock_post_position};

    zmk_matrix_app_t app;
    zmk_matrix_construct(&app, EDGE_MOD_ZMK_MATRIX, 50, &hw, &sink, NULL);

    assert_int_equal(app.module.module_id, EDGE_MOD_ZMK_MATRIX);
    assert_int_equal(app.module.priority, 50);
    assert_int_equal(zmk_matrix_init(&app), EDGE_OK);

    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(hw_ctx.scan_calls, 1);
}

static void test_matrix_transform_and_state_change(void **state) {
    (void)state;
    mock_hw_t hw_ctx = {0};
    zmk_matrix_hw_if_t hw = {.self = &hw_ctx, .scan_raw = mock_scan_raw};
    mock_sink_t sink_ctx = {0};
    zmk_matrix_event_sink_if_t sink = {.self = &sink_ctx,
                                       .post_position_event = mock_post_position};

    zmk_matrix_transform_t custom_transform = {0};
    custom_transform.rows = 4;
    custom_transform.cols = 12;
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            custom_transform.map[r][c] = 0xFF;
        }
    }
    // Row 0 Col 1 -> Pos 5
    custom_transform.map[0][1] = 5;
    // Row 1 Col 2 -> Pos 15
    custom_transform.map[1][2] = 15;

    zmk_matrix_app_t app;
    zmk_matrix_construct(&app, EDGE_MOD_ZMK_MATRIX, 50, &hw, &sink, &custom_transform);
    assert_int_equal(zmk_matrix_init(&app), EDGE_OK);

    // Unmapped key: Row 0 Col 0
    assert_int_equal(zmk_matrix_on_key_state_change(&app, 0, 0, true, 100), EDGE_OK);
    assert_int_equal(sink_ctx.post_calls, 0);

    // Mapped key: Row 0 Col 1 -> Pos 5
    assert_int_equal(zmk_matrix_on_key_state_change(&app, 0, 1, true, 105), EDGE_OK);
    assert_int_equal(sink_ctx.post_calls, 1);
    assert_int_equal(sink_ctx.last_pos, 5);
    assert_true(sink_ctx.last_pressed);
    assert_int_equal(sink_ctx.last_time, 105);
    assert_true(zmk_matrix_is_position_pressed(&app, 5));
    assert_false(zmk_matrix_is_position_pressed(&app, 15));

    // Release key: Pos 5
    assert_int_equal(zmk_matrix_on_key_state_change(&app, 0, 1, false, 150), EDGE_OK);
    assert_int_equal(sink_ctx.post_calls, 2);
    assert_int_equal(sink_ctx.last_pos, 5);
    assert_false(sink_ctx.last_pressed);
    assert_false(zmk_matrix_is_position_pressed(&app, 5));

    // Power off clears state
    assert_int_equal(zmk_matrix_on_key_state_change(&app, 1, 2, true, 200), EDGE_OK);
    assert_true(zmk_matrix_is_position_pressed(&app, 15));
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_false(zmk_matrix_is_position_pressed(&app, 15));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_matrix_construct_and_init),
        cmocka_unit_test(test_matrix_transform_and_state_change),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
