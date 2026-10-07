/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include <cmocka.h>
/* clang-format on */

#include "skypuff/skypuff.h"

/*
 * applications/app_skypuff.c:62-63's two derived quantities, :132-157's three conversions and
 * :176-208's own direction: a revolution of the motor is the wheel's circumference over the gear
 * ratio, its steps are three a pole, and the states that drive anything take their sign from the
 * count itself.
 */
static void test_skypuff_arithmetic_matches_the_reference(void **state) {
    (void)state;

    /* A tenth of a metre over a ratio of two is its half of a turn's worth of line. */
    const float meters_per_rev = skypuff_meters_per_rev(0.1f, 2.0f);
    assert_float_equal(meters_per_rev, 0.05f * 3.14159265358979323846f, 1e-6f);

    /* Seven pole pairs is forty-two steps a revolution. */
    const float steps_per_rev = skypuff_steps_per_rev(14.0f);
    assert_float_equal(steps_per_rev, 42.0f, 1e-6f);

    /* A whole revolution of them is the line it wound, and less than one is less of it. */
    assert_float_equal(skypuff_steps_to_meters(42, steps_per_rev, meters_per_rev), meters_per_rev,
                       1e-6f);
    assert_float_equal(skypuff_steps_to_meters(21, steps_per_rev, meters_per_rev),
                       meters_per_rev / 2.0f, 1e-6f);
    assert_float_equal(skypuff_steps_to_meters(-42, steps_per_rev, meters_per_rev), -meters_per_rev,
                       1e-6f);
    /* A revolution that is nought steps long has no line to give. */
    assert_float_equal(skypuff_steps_to_meters(10, 0.0f, meters_per_rev), 0.0f, 1e-6f);

    /* The two speeds are each other's way back. */
    const float erpm = skypuff_ms_to_erpm(1.0f, meters_per_rev, 14.0f);
    assert_float_equal(erpm, (1.0f / meters_per_rev) * 60.0f * 7.0f, 1e-1f);
    assert_float_equal(skypuff_erpm_to_ms(erpm, meters_per_rev, 14.0f), 1.0f, 1e-4f);
    assert_float_equal(skypuff_ms_to_erpm(1.0f, 0.0f, 14.0f), 0.0f, 1e-6f);
    assert_float_equal(skypuff_erpm_to_ms(1.0f, meters_per_rev, 0.0f), 0.0f, 1e-6f);

    /* The direction comes from the count's own sign, which is the reference's own choice. */
    assert_float_equal(skypuff_signed_command(-1.0f, 5.0f), 5.0f, 1e-6f);
    assert_float_equal(skypuff_signed_command(1.0f, 5.0f), -5.0f, 1e-6f);
    assert_float_equal(skypuff_signed_command(0.0f, 5.0f), -5.0f, 1e-6f);
    assert_float_equal(skypuff_signed_command(-0.5f, 2.0f), 2.0f, 1e-6f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_skypuff_arithmetic_matches_the_reference),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
