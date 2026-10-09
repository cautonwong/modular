/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "cst816s/cst816s.h"

static void test_cst816s_parse_valid_tap(void **state) {
    (void)state;
    // Gesture = SingleTap (0x05), touchPoints = 1, X = 120 (0x0078), Y = 200 (0x00C8)
    const uint8_t raw[6] = {0x05, 0x01, 0x00, 0x78, 0x00, 0xC8};
    cst816s_touch_info_t info;

    assert_int_equal(cst816s_parse_touch_data(raw, 240, 240, &info), EDGE_OK);
    assert_true(info.is_valid);
    assert_true(info.touching);
    assert_int_equal(info.x, 120);
    assert_int_equal(info.y, 200);
    assert_int_equal(info.gesture, CST816S_GESTURE_SINGLE_TAP);
}

static void test_cst816s_parse_swipe_gestures(void **state) {
    (void)state;
    cst816s_touch_info_t info;

    // Slide Down (0x01)
    const uint8_t raw_down[6] = {0x01, 0x01, 0x00, 0x30, 0x00, 0x40};
    assert_int_equal(cst816s_parse_touch_data(raw_down, 240, 240, &info), EDGE_OK);
    assert_int_equal(info.gesture, CST816S_GESTURE_SLIDE_DOWN);

    // Slide Left (0x03)
    const uint8_t raw_left[6] = {0x03, 0x01, 0x00, 0x50, 0x00, 0x60};
    assert_int_equal(cst816s_parse_touch_data(raw_left, 240, 240, &info), EDGE_OK);
    assert_int_equal(info.gesture, CST816S_GESTURE_SLIDE_LEFT);

    // Double Tap (0x0B)
    const uint8_t raw_dtap[6] = {0x0B, 0x01, 0x00, 0x10, 0x00, 0x20};
    assert_int_equal(cst816s_parse_touch_data(raw_dtap, 240, 240, &info), EDGE_OK);
    assert_int_equal(info.gesture, CST816S_GESTURE_DOUBLE_TAP);

    // Long Press (0x0C)
    const uint8_t raw_long[6] = {0x0C, 0x01, 0x00, 0x10, 0x00, 0x20};
    assert_int_equal(cst816s_parse_touch_data(raw_long, 240, 240, &info), EDGE_OK);
    assert_int_equal(info.gesture, CST816S_GESTURE_LONG_PRESS);
}

static void test_cst816s_invalid_inputs(void **state) {
    (void)state;
    cst816s_touch_info_t info;

    // Out of bounds X coordinate (X = 250 > 240)
    const uint8_t raw_bad_x[6] = {0x05, 0x01, 0x00, 0xFA, 0x00, 0x20};
    assert_int_equal(cst816s_parse_touch_data(raw_bad_x, 240, 240, &info), EDGE_EINVAL);
    assert_false(info.is_valid);

    // Null pointer checks
    assert_int_equal(cst816s_parse_touch_data(NULL, 240, 240, &info), EDGE_EINVAL);
    assert_int_equal(cst816s_parse_touch_data(raw_bad_x, 240, 240, NULL), EDGE_EINVAL);
}

static void test_cst816s_ids_and_sleep(void **state) {
    (void)state;
    assert_true(cst816s_validate_device_ids(0xB4, 0));
    assert_true(cst816s_validate_device_ids(0xB5, 0));
    assert_true(cst816s_validate_device_ids(0xB6, 0));
    assert_false(cst816s_validate_device_ids(0x00, 0));
    assert_false(cst816s_validate_device_ids(0xFF, 0));

    uint8_t sleep_cmd[2] = {0};
    assert_int_equal(cst816s_format_sleep_cmd(sleep_cmd), EDGE_OK);
    assert_int_equal(sleep_cmd[0], CST816S_REG_SLEEP_MODE);
    assert_int_equal(sleep_cmd[1], 0x03);

    assert_int_equal(cst816s_format_sleep_cmd(NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_cst816s_parse_valid_tap),
        cmocka_unit_test(test_cst816s_parse_swipe_gestures),
        cmocka_unit_test(test_cst816s_invalid_inputs),
        cmocka_unit_test(test_cst816s_ids_and_sleep),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
