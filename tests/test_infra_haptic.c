#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "haptic/haptic.h"

static void test_haptic_short_pattern(void **state) {
    (void)state;
    haptic_t h;
    haptic_init(&h);
    assert_int_equal(haptic_play(&h, HAPTIC_PATTERN_SHORT), EDGE_OK);

    bool pin = false;
    assert_int_equal(haptic_update(&h, 20u, &pin), EDGE_OK);
    assert_true(pin);

    assert_int_equal(haptic_update(&h, 40u, &pin), EDGE_OK); // 60ms elapsed -> stopped
    assert_false(pin);
    assert_false(h.is_active);
}

static void test_haptic_double_pattern(void **state) {
    (void)state;
    haptic_t h;
    haptic_init(&h);
    assert_int_equal(haptic_play(&h, HAPTIC_PATTERN_DOUBLE), EDGE_OK);

    bool pin = false;
    haptic_update(&h, 30u, &pin); // 30ms -> ON
    assert_true(pin);

    haptic_update(&h, 40u, &pin); // 70ms -> OFF
    assert_false(pin);

    haptic_update(&h, 50u, &pin); // 120ms -> ON
    assert_true(pin);

    haptic_update(&h, 40u, &pin); // 160ms -> OFF
    assert_false(pin);
    assert_false(h.is_active);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_haptic_short_pattern),
        cmocka_unit_test(test_haptic_double_pattern),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
