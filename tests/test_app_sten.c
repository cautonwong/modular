/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include <cmocka.h>
/* clang-format on */

#include "sten/sten.h"

/*
 * applications/app_sten.c:76-90's own reading: the middle of the three bytes last seen, over the
 * packet's centre and less one, with the two before them remembered as they were.
 */
static void test_sten_packet_reading_matches_the_reference(void **state) {
    (void)state;

    assert_int_equal(sten_middle_of_three(1u, 2u, 3u), 2u);
    assert_int_equal(sten_middle_of_three(3u, 1u, 2u), 2u);
    assert_int_equal(sten_middle_of_three(2u, 3u, 1u), 2u);
    assert_int_equal(sten_middle_of_three(5u, 5u, 5u), 5u);

    /* A packet at the centre is no stick at all, and nothing is remembered by it. */
    uint16_t c1 = 128u;
    uint16_t c2 = 128u;
    assert_float_equal(sten_output_from_packet(128u, &c1, &c2), 0.0f, 1e-6f);
    assert_int_equal(c1, 128u);
    assert_int_equal(c2, 128u);

    /* The top of a byte is one and the bottom is minus one. */
    c1 = 255u;
    c2 = 255u;
    assert_float_equal(sten_output_from_packet(255u, &c1, &c2), (255.0f / 128.0f) - 1.0f, 1e-6f);
    c1 = 0u;
    c2 = 0u;
    assert_float_equal(sten_output_from_packet(0u, &c1, &c2), -1.0f, 1e-6f);

    /* Nowhere to remember them: a byte is taken against a resting one. */
    assert_float_equal(sten_output_from_packet(128u, NULL, NULL), 0.0f, 1e-6f);
}

/*
 * utils_math.c:57-66's deadband and :46-68's ceiling and smoothing: below the threshold nothing,
 * past it the value resuming at nought rather than jumping, the current wound down over the two
 * speeds, and three taps of average behind it.
 */
static void test_sten_current_law_matches_the_reference(void **state) {
    (void)state;

    /* Below the deadband is nought, in both directions and at its own edge. */
    float v = 0.09f;
    sten_deadband(&v, STEN_HYST, 1.0f);
    assert_float_equal(v, 0.0f, 1e-6f);
    v = -0.09f;
    sten_deadband(&v, STEN_HYST, 1.0f);
    assert_float_equal(v, 0.0f, 1e-6f);
    v = STEN_HYST;
    sten_deadband(&v, STEN_HYST, 1.0f);
    assert_float_equal(v, 0.0f, 1e-6f);

    /* A full reading stays full, and half of one resumes halfway past the threshold. */
    const float k = 1.0f / (1.0f - STEN_HYST);
    v = 1.0f;
    sten_deadband(&v, STEN_HYST, 1.0f);
    assert_float_equal(v, 1.0f, 1e-6f);
    v = 0.5f;
    sten_deadband(&v, STEN_HYST, 1.0f);
    assert_float_equal(v, k * 0.5f + (1.0f - k), 1e-6f);
    v = -0.5f;
    sten_deadband(&v, STEN_HYST, 1.0f);
    assert_float_equal(v, -(k * 0.5f + (1.0f - k)), 1e-6f);

    sten_deadband(NULL, STEN_HYST, 1.0f);

    /* The ceiling: unchanged below the first speed, the floor's own negative past the second, and a
     * straight line between them - with the first speed itself not yet wound down. */
    const float midway = (STEN_RPM_MAX_1 + STEN_RPM_MAX_2) / 2.0f;
    assert_float_equal(sten_soft_rpm_limit(30.0f, 1000.0f, STEN_RPM_MAX_1, STEN_RPM_MAX_2, 1.0f),
                       30.0f, 1e-6f);
    assert_float_equal(
        sten_soft_rpm_limit(30.0f, STEN_RPM_MAX_1, STEN_RPM_MAX_1, STEN_RPM_MAX_2, 1.0f), 30.0f,
        1e-6f);
    assert_float_equal(sten_soft_rpm_limit(30.0f, midway, STEN_RPM_MAX_1, STEN_RPM_MAX_2, 1.0f),
                       14.5f, 1e-3f);
    assert_float_equal(
        sten_soft_rpm_limit(30.0f, STEN_RPM_MAX_2 + 1.0f, STEN_RPM_MAX_1, STEN_RPM_MAX_2, 1.0f),
        -1.0f, 1e-6f);

    /* Three taps, and a caller with nowhere to keep them. */
    float p1 = 0.0f;
    float p2 = 0.0f;
    assert_float_equal(sten_smooth3(&p1, &p2, 3.0f), 1.0f, 1e-6f);
    /* The next value goes into a window that still holds the first one and the silence before it.
     */
    assert_float_equal(sten_smooth3(&p1, &p2, 0.0f), 1.0f / 3.0f, 1e-6f);
    assert_float_equal(sten_smooth3(NULL, NULL, 7.0f), 7.0f, 1e-6f);

    /* The two commands: a positive reading under the speed's own brake limit is a current, and a
     * negative one - or a machine past that limit - is a brake at the reading over the minimum. */
    sten_output_t drive = sten_output_command(0.5f, 1000.0f, 60.0f, -30.0f, 100000.0f, 1.0f);
    assert_false(drive.brake);
    assert_float_equal(drive.current_a, 30.0f, 1e-4f);

    sten_output_t backwards = sten_output_command(-0.5f, 1000.0f, 60.0f, -30.0f, 100000.0f, 1.0f);
    assert_true(backwards.brake);
    assert_float_equal(backwards.current_a, 15.0f, 1e-4f);

    /* A machine running backwards past the brake's own limit is the brake's, even at a reading that
     * would otherwise be a current: the reference's own gate compares the speed against the
     * negative of that limit. */
    sten_output_t over = sten_output_command(0.5f, -200000.0f, 60.0f, -30.0f, 100000.0f, 1.0f);
    assert_true(over.brake);
    assert_float_equal(over.current_a, -15.0f, 1e-4f);

    /* And a reading past the ceiling is wound down even where it is a current. */
    sten_output_t wound =
        sten_output_command(0.5f, STEN_RPM_MAX_2 + 1.0f, 60.0f, -30.0f, 100000.0f, 1.0f);
    assert_false(wound.brake);
    assert_float_equal(wound.current_a, -1.0f, 1e-4f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sten_packet_reading_matches_the_reference),
        cmocka_unit_test(test_sten_current_law_matches_the_reference),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
