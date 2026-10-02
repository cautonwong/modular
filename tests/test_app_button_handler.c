#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "button_handler/button_handler.h"
#include "edge/modules.h"

static void test_button_debounce_and_short_press(void **state) {
    (void)state;
    button_handler_t btn;
    button_handler_construct(&btn, 0x1C00u, 100u, NULL);
    assert_int_equal(button_handler_init(&btn), EDGE_OK);

    // Initial state
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE);

    // Glitch/bounce < 50ms (30ms pressed then released)
    assert_int_equal(button_handler_update(&btn, true, 30u), EDGE_OK);
    assert_int_equal(button_handler_update(&btn, false, 10u), EDGE_OK);
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE); // Rejected by debounce

    // Valid short press: 200ms pressed then released
    assert_int_equal(button_handler_update(&btn, true, 100u), EDGE_OK);
    assert_int_equal(button_handler_update(&btn, true, 100u), EDGE_OK);
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE); // Not fired while holding

    assert_int_equal(button_handler_update(&btn, false, 10u), EDGE_OK); // Released
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_SHORT_PRESS);
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE); // Cleared
}

static void test_button_long_press(void **state) {
    (void)state;
    button_handler_t btn;
    button_handler_construct(&btn, 0x1C00u, 100u, NULL);
    assert_int_equal(button_handler_init(&btn), EDGE_OK);

    // Hold for 2500ms
    assert_int_equal(button_handler_update(&btn, true, 1000u), EDGE_OK);
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE);

    assert_int_equal(button_handler_update(&btn, true, 1000u), EDGE_OK);
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE);

    assert_int_equal(button_handler_update(&btn, true, 500u), EDGE_OK); // Hit 2500ms
    assert_int_equal(button_handler_get_event(&btn),
                     WATCH_BUTTON_LONG_PRESS); // Long press emitted!

    // Still holding -> should not re-trigger
    assert_int_equal(button_handler_update(&btn, true, 500u), EDGE_OK);
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE);

    // Release after long press -> should not emit short press
    assert_int_equal(button_handler_update(&btn, false, 10u), EDGE_OK);
    assert_int_equal(button_handler_get_event(&btn), WATCH_BUTTON_NONE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_button_debounce_and_short_press),
        cmocka_unit_test(test_button_long_press),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
