/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_pointing_processors/pointing_processors.h"

typedef struct mock_sink {
    int layer_calls;
    uint8_t last_layer;
    bool last_layer_active;
    int motion_calls;
    int16_t last_dx;
    int16_t last_dy;
    int16_t last_dwheel;
} mock_sink_t;

static edge_status_t mock_set_temp_layer(void *self, uint8_t layer, bool active) {
    mock_sink_t *s = (mock_sink_t *)self;
    s->layer_calls++;
    s->last_layer = layer;
    s->last_layer_active = active;
    return EDGE_OK;
}

static edge_status_t mock_forward_motion(void *self, int16_t dx, int16_t dy, int16_t dwheel) {
    mock_sink_t *s = (mock_sink_t *)self;
    s->motion_calls++;
    s->last_dx = dx;
    s->last_dy = dy;
    s->last_dwheel = dwheel;
    return EDGE_OK;
}

static void test_pointing_scaler_with_remainder(void **state) {
    (void)state;
    mock_sink_t sink_ctx = {0};
    zmk_pointing_proc_sink_if_t sink = {
        .self = &sink_ctx,
        .set_temp_layer = mock_set_temp_layer,
        .forward_motion = mock_forward_motion,
    };

    zmk_pointing_processors_app_t app;
    zmk_pointing_processors_construct(&app, EDGE_MOD_ZMK_POINTING_PROC, 40, &sink);
    assert_int_equal(zmk_pointing_processors_init(&app), EDGE_OK);

    // Scale 1 / 2
    zmk_pointing_processors_set_scaler(&app, 1, 2);

    // dx = 3 -> 3/2 = 1 with remainder 1
    assert_int_equal(zmk_pointing_processors_process_motion(&app, 3, 0, 0, 100), EDGE_OK);
    assert_int_equal(sink_ctx.last_dx, 1);

    // dx = 3 again -> (3 + 1)/2 = 2 with remainder 0
    assert_int_equal(zmk_pointing_processors_process_motion(&app, 3, 0, 0, 110), EDGE_OK);
    assert_int_equal(sink_ctx.last_dx, 2);
}

static void test_pointing_temp_layer_activation_and_timeout(void **state) {
    (void)state;
    mock_sink_t sink_ctx = {0};
    zmk_pointing_proc_sink_if_t sink = {
        .self = &sink_ctx,
        .set_temp_layer = mock_set_temp_layer,
        .forward_motion = mock_forward_motion,
    };

    zmk_pointing_processors_app_t app;
    zmk_pointing_processors_construct(&app, EDGE_MOD_ZMK_POINTING_PROC, 40, &sink);
    zmk_pointing_processors_init(&app);

    // Auto-activate Layer 3 on motion with 300ms timeout
    zmk_pointing_processors_set_temp_layer(&app, 3, 300);

    // Motion at t=1000ms activates layer 3
    assert_int_equal(zmk_pointing_processors_process_motion(&app, 5, 5, 0, 1000), EDGE_OK);
    assert_int_equal(sink_ctx.layer_calls, 1);
    assert_int_equal(sink_ctx.last_layer, 3);
    assert_true(sink_ctx.last_layer_active);

    // Tick at t=1200ms -> layer still active
    assert_int_equal(zmk_pointing_processors_process_tick(&app, 1200), EDGE_OK);
    assert_int_equal(sink_ctx.layer_calls, 1);

    // Tick at t=1300ms -> timeout reached, layer deactivated
    assert_int_equal(zmk_pointing_processors_process_tick(&app, 1300), EDGE_OK);
    assert_int_equal(sink_ctx.layer_calls, 2);
    assert_int_equal(sink_ctx.last_layer, 3);
    assert_false(sink_ctx.last_layer_active);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pointing_scaler_with_remainder),
        cmocka_unit_test(test_pointing_temp_layer_activation_and_timeout),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
