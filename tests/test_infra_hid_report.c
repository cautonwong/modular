/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "hid_report/hid_report.h"

static void test_hid_report_keyboard_boot_and_nkro(void **state) {
    (void)state;

    hid_report_builder_t builder;
    hid_report_builder_init(&builder);

    // Press 'A' and Left Shift
    assert_int_equal(hid_report_press_keycode(&builder, HID_KEY_A), EDGE_OK);
    assert_int_equal(hid_report_press_keycode(&builder, HID_KEY_LSHIFT), EDGE_OK);

    assert_true(hid_report_is_keycode_pressed(&builder, HID_KEY_A));
    assert_true(hid_report_is_keycode_pressed(&builder, HID_KEY_LSHIFT));
    assert_false(hid_report_is_keycode_pressed(&builder, HID_KEY_B));

    // Serialize boot report
    uint8_t boot_buf[8];
    assert_int_equal(hid_report_serialize_boot(&builder, boot_buf, sizeof(boot_buf)), 8);
    assert_int_equal(boot_buf[0], HID_MOD_LSHIFT);
    assert_int_equal(boot_buf[1], 0);
    assert_int_equal(boot_buf[2], HID_KEY_A);

    // Serialize NKRO report
    uint8_t nkro_buf[1 + HID_REPORT_KEYBOARD_NKRO_BYTES];
    assert_int_equal(hid_report_serialize_nkro(&builder, nkro_buf, sizeof(nkro_buf)),
                     1 + HID_REPORT_KEYBOARD_NKRO_BYTES);
    assert_int_equal(nkro_buf[0], HID_MOD_LSHIFT);
    assert_true((nkro_buf[1 + (HID_KEY_A / 8)] & (1 << (HID_KEY_A % 8))) != 0);

    // Release 'A'
    assert_int_equal(hid_report_release_keycode(&builder, HID_KEY_A), EDGE_OK);
    assert_false(hid_report_is_keycode_pressed(&builder, HID_KEY_A));
    assert_true(hid_report_is_keycode_pressed(&builder, HID_KEY_LSHIFT));
}

static void test_hid_report_consumer_and_mouse(void **state) {
    (void)state;

    hid_report_builder_t builder;
    hid_report_builder_init(&builder);

    // Press Volume Up
    assert_int_equal(hid_report_press_consumer(&builder, HID_CONSUMER_VOLUME_INC), EDGE_OK);
    assert_true(hid_report_is_consumer_pressed(&builder, HID_CONSUMER_VOLUME_INC));

    uint8_t consumer_buf[8];
    assert_int_equal(hid_report_serialize_consumer(&builder, consumer_buf, sizeof(consumer_buf)),
                     8);
    uint16_t key0 = (uint16_t)(consumer_buf[0] | (consumer_buf[1] << 8));
    assert_int_equal(key0, HID_CONSUMER_VOLUME_INC);

    // Mouse move and click
    assert_int_equal(hid_report_press_mouse_button(&builder, HID_MOUSE_BTN_LEFT), EDGE_OK);
    assert_int_equal(hid_report_mouse_move(&builder, 10, -5, 1, 0), EDGE_OK);

    uint8_t mouse_buf[5];
    assert_int_equal(hid_report_serialize_mouse(&builder, mouse_buf, sizeof(mouse_buf)), 5);
    assert_int_equal(mouse_buf[0], HID_MOUSE_BTN_LEFT);
    assert_int_equal((int8_t)mouse_buf[1], 10);
    assert_int_equal((int8_t)mouse_buf[2], -5);
    assert_int_equal((int8_t)mouse_buf[3], 1);
    assert_int_equal((int8_t)mouse_buf[4], 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_hid_report_keyboard_boot_and_nkro),
        cmocka_unit_test(test_hid_report_consumer_and_mouse),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
