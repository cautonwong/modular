#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "touch_gesture/touch_gesture.h"

// cppcheck-suppress constParameterCallback
static edge_status_t mock_read_touch(void *self, touch_raw_info_t *out_info) {
    const touch_raw_info_t *mock_data = (const touch_raw_info_t *)self;
    if (mock_data == NULL || out_info == NULL) {
        return EDGE_EINVAL;
    }
    *out_info = *mock_data;
    return EDGE_OK;
}

static void test_touch_gesture_tap_processing(void **state) {
    (void)state;
    touch_gesture_t app;
    touch_gesture_construct(&app, EDGE_MOD_TOUCH_GESTURE, 100u, NULL);
    assert_int_equal(touch_gesture_init(&app), EDGE_OK);

    // Initial state
    assert_int_equal(touch_gesture_get(&app), WATCH_GESTURE_NONE);

    // Single Tap event (0x05)
    touch_raw_info_t raw = {
        .x = 100u,
        .y = 150u,
        .touching = true,
        .hardware_gesture = 0x05u,
        .is_valid = true,
    };

    assert_int_equal(touch_gesture_process_raw(&app, &raw), EDGE_OK);
    assert_int_equal(app.last_x, 100);
    assert_int_equal(app.last_y, 150);
    assert_true(app.touching);

    // Get gesture (read and clear)
    assert_int_equal(touch_gesture_get(&app), WATCH_GESTURE_TAP);
    assert_int_equal(touch_gesture_get(&app), WATCH_GESTURE_NONE);
}

static void test_touch_gesture_swipe_continuous_lock(void **state) {
    (void)state;
    touch_gesture_t app;
    touch_gesture_construct(&app, EDGE_MOD_TOUCH_GESTURE, 100u, NULL);
    assert_int_equal(touch_gesture_init(&app), EDGE_OK);

    // Slide Left (0x03) while touching
    touch_raw_info_t raw = {
        .x = 200u,
        .y = 100u,
        .touching = true,
        .hardware_gesture = 0x03u,
        .is_valid = true,
    };
    assert_int_equal(touch_gesture_process_raw(&app, &raw), EDGE_OK);
    assert_int_equal(touch_gesture_get(&app), WATCH_GESTURE_SWIPE_LEFT);

    // Subsequent sample with same finger still touching -> should not re-trigger gesture
    raw.x = 180u;
    assert_int_equal(touch_gesture_process_raw(&app, &raw), EDGE_OK);
    assert_int_equal(touch_gesture_get(&app), WATCH_GESTURE_NONE);

    // Finger lifted (touching = false) -> gesture released
    raw.touching = false;
    raw.hardware_gesture = 0x00u;
    assert_int_equal(touch_gesture_process_raw(&app, &raw), EDGE_OK);
    assert_false(app.touching);

    // Next touch slide right (0x04) -> triggers again
    raw.touching = true;
    raw.hardware_gesture = 0x04u;
    assert_int_equal(touch_gesture_process_raw(&app, &raw), EDGE_OK);
    assert_int_equal(touch_gesture_get(&app), WATCH_GESTURE_SWIPE_RIGHT);
}

static void test_touch_gesture_port_step(void **state) {
    (void)state;
    touch_raw_info_t mock_raw = {
        .x = 50u,
        .y = 60u,
        .touching = true,
        .hardware_gesture = 0x0Bu, // Double tap
        .is_valid = true,
    };
    const touch_input_if_t port = {
        .read_touch = mock_read_touch,
        .sleep = NULL,
        .self = &mock_raw,
    };

    touch_gesture_t app;
    touch_gesture_construct(&app, EDGE_MOD_TOUCH_GESTURE, 100u, &port);
    assert_int_equal(touch_gesture_init(&app), EDGE_OK);

    edge_module_t *mod = touch_gesture_module(&app);
    assert_non_null(mod);

    assert_int_equal(mod->poll(mod), EDGE_OK);
    assert_int_equal(app.poll_count, 1);
    assert_int_equal(touch_gesture_get(&app), WATCH_GESTURE_DOUBLE_TAP);

    assert_int_equal(mod->power_off(mod), EDGE_OK);
    assert_int_equal(touch_gesture_deinit(&app), EDGE_OK);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_touch_gesture_tap_processing),
        cmocka_unit_test(test_touch_gesture_swipe_continuous_lock),
        cmocka_unit_test(test_touch_gesture_port_step),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
