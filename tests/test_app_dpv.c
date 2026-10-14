/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include <cmocka.h>
/* clang-format on */

#include "dpv/dpv.h"

/*
 * applications/app_dpv.c:104-113's own reading: the angle's window is four hundred to three
 * thousand eight hundred counts and the two speeds it maps onto are a tenth and one, a reading that
 * moved further than a thousand counts is a pot that sprang back and keeps the value it had, and
 * every reading is clamped into the window first.
 */
static void test_dpv_speed_from_angle_matches_the_reference(void **state) {
    (void)state;

    float last = 400.0f;
    assert_float_equal(dpv_speed_from_angle(400.0f, &last), DPV_SPEED_MIN, 1e-5f);
    /* The top of the window is a hand that turned it that far, so the reading is followed. */
    last = 3700.0f;
    assert_float_equal(dpv_speed_from_angle(3800.0f, &last), DPV_SPEED_MAX, 1e-5f);
    assert_float_equal(last, 3800.0f, 1e-5f);

    /* Halfway up the window is halfway up the speeds. */
    last = 2000.0f;
    assert_float_equal(dpv_speed_from_angle(2100.0f, &last), (DPV_SPEED_MIN + DPV_SPEED_MAX) / 2.0f,
                       1e-5f);

    /*
     * Outside the window it is the window's own end - but the clamp happens after the sprung-pot
     * rejection, so the reading has to be within the jump of what is remembered for the window to
     * be what answers.
     */
    last = 1300.0f;
    assert_float_equal(dpv_speed_from_angle(300.0f, &last), DPV_SPEED_MIN, 1e-5f);
    last = 3000.0f;
    assert_float_equal(dpv_speed_from_angle(3900.0f, &last), DPV_SPEED_MAX, 1e-5f);
    /* And a reading far outside that is the pot springing back, which remembers what it had. */
    last = 3700.0f;
    assert_float_equal(dpv_speed_from_angle(9999.0f, &last),
                       (3700.0f - DPV_ANGLE_MIN) * (DPV_SPEED_MAX - DPV_SPEED_MIN) /
                               (DPV_ANGLE_MAX - DPV_ANGLE_MIN) +
                           DPV_SPEED_MIN,
                       1e-5f);
    assert_float_equal(last, 3700.0f, 1e-5f);

    /* A jump further than a thousand counts keeps the value the last reading left, and the memory
     * of it as well: the law does not follow a sprung pot. */
    last = 2000.0f;
    assert_float_equal(dpv_speed_from_angle(3200.0f, &last), dpv_speed_from_angle(2000.0f, &last),
                       1e-5f);
    assert_float_equal(last, 2000.0f, 1e-5f);
    /* Exactly a thousand is still a turn, and is followed. */
    last = 2000.0f;
    (void)dpv_speed_from_angle(3000.0f, &last);
    assert_float_equal(last, 3000.0f, 1e-5f);

    /* With nowhere to remember the last reading, every one of them is taken. */
    assert_float_equal(dpv_speed_from_angle(3800.0f, NULL), DPV_SPEED_MAX, 1e-5f);
}

/*
 * :131-143: the ramp's own time - five seconds towards more speed, half of one towards less, and
 * then three whenever the target is anything but nought, which is the reference's override
 * reproduced as it stands - and one step of the walk, which lands on the target rather than past
 * it.
 */
static void test_dpv_ramp_matches_the_reference(void **state) {
    (void)state;

    /* While the target is nought the law's own pair of times is what shows. */
    assert_float_equal(dpv_ramp_time(DPV_SPEED_OFF, 0.5f), 0.5f, 1e-6f);
    assert_float_equal(dpv_ramp_time(DPV_SPEED_OFF, -0.5f), 0.5f, 1e-6f);
    /* And with any target above a hundredth it is three, whichever way it is going. */
    assert_float_equal(dpv_ramp_time(0.5f, 0.2f), 3.0f, 1e-6f);
    assert_float_equal(dpv_ramp_time(-0.5f, -0.2f), 3.0f, 1e-6f);

    /* A target of exactly a hundredth does not take the override. */
    assert_float_equal(dpv_ramp_time(0.01f, 0.005f), 5.0f, 1e-6f);

    /* One step: a ramp of three seconds moves a three-thousandth of the way each millisecond. */
    float ramped = 0.0f;
    assert_float_equal(dpv_ramp_step(&ramped, 1.0f, 1.0f, 3.0f), 1.0f / 3000.0f, 1e-7f);
    assert_float_equal(dpv_ramp_step(&ramped, 1.0f, 0.0f, 3.0f), 1.0f / 3000.0f, 1e-7f);

    /* A step further than what is left lands on the target rather than beyond it, both ways. */
    ramped = 0.999f;
    assert_float_equal(dpv_ramp_step(&ramped, 1.0f, 1000.0f, 3.0f), 1.0f, 1e-6f);
    ramped = 0.001f;
    assert_float_equal(dpv_ramp_step(&ramped, 0.0f, 1000.0f, 3.0f), 0.0f, 1e-6f);

    /* A ramp that is not worth walking holds what it has, and so does having nowhere to hold it. */
    ramped = 0.4f;
    assert_float_equal(dpv_ramp_step(&ramped, 1.0f, 100.0f, 0.01f), 0.4f, 1e-6f);
    assert_float_equal(dpv_ramp_step(&ramped, 1.0f, 100.0f, 0.0f), 0.4f, 1e-6f);
    assert_float_equal(dpv_ramp_step(NULL, 1.0f, 100.0f, 3.0f), 0.0f, 1e-6f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dpv_speed_from_angle_matches_the_reference),
        cmocka_unit_test(test_dpv_ramp_matches_the_reference),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
