/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "digital_filter/digital_filter.h"

static void test_lowpass_filter(void **state) {
    (void)state;
    lowpass_filter_t lp;
    lowpass_init(&lp, 100.0f, 1000.0f);

    float out = 0.0f;
    for (int i = 0; i < 50; i++) {
        out = lowpass_process(&lp, 10.0f);
    }
    assert_true(out > 9.0f && out <= 10.0f);

    lowpass_reset(&lp);
}

static void test_moving_average_filter(void **state) {
    (void)state;
    moving_average_filter_t ma;
    moving_avg_init(&ma, 4);

    assert_float_equal(moving_avg_process(&ma, 2.0f), 2.0f, 0.001f);
    assert_float_equal(moving_avg_process(&ma, 4.0f), 3.0f, 0.001f);
    assert_float_equal(moving_avg_process(&ma, 6.0f), 4.0f, 0.001f);
    assert_float_equal(moving_avg_process(&ma, 8.0f), 5.0f, 0.001f);

    /* Buffer full: replace 2.0 with 10.0 -> avg = (4+6+8+10)/4 = 7.0 */
    assert_float_equal(moving_avg_process(&ma, 10.0f), 7.0f, 0.001f);
}

static void test_biquad_filter(void **state) {
    (void)state;
    biquad_filter_t bq;
    biquad_init_lowpass(&bq, 10000.0f, 1000.0f, 0.7071f);

    float out = 0.0f;
    for (int i = 0; i < 100; i++) {
        out = biquad_process(&bq, 5.0f);
    }
    assert_true(out > 4.5f && out < 5.5f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_lowpass_filter),
        cmocka_unit_test(test_moving_average_filter),
        cmocka_unit_test(test_biquad_filter),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
