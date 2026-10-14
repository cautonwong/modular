/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "digital_filter/digital_filter.h"

/*
 * The impulse response below is the reference firmware's util/digital_filter.c
 * biquad_config(BQ_LOWPASS, 1000/25000) output, dumped by running it. Anything
 * that only agrees with itself (an RBJ biquad with a cutoff in Hz, or a different
 * state form) lands elsewhere.
 */
static void test_biquad_filter(void **state) {
    (void)state;
    static const float golden[12] = {
        0.0133588957f, 0.0487255380f, 0.0842677653f, 0.1046749428f, 0.1133841127f, 0.1134292632f,
        0.1073997542f, 0.0974349529f, 0.0852445439f, 0.0721457005f, 0.0591101199f, 0.0468154401f,
    };

    biquad_filter_t bq;
    biquad_config(&bq, 1000.0f / 25000.0f);

    for (int n = 0; n < 12; n++) {
        float out = biquad_process(&bq, n == 0 ? 1.0f : 0.0f);
        assert_float_equal(out, golden[n], 1e-6f);
    }

    /* DC gain is 1: a constant settles on the constant. */
    biquad_reset(&bq);
    float out = 0.0f;
    for (int i = 0; i < 200; i++) {
        out = biquad_process(&bq, 5.0f);
    }
    assert_float_equal(out, 5.0f, 1e-3f);

    /* NULL and unconfigured inputs are the caller's problem, not a crash. */
    assert_float_equal(biquad_process(NULL, 3.0f), 3.0f, 1e-6f);
    biquad_reset(NULL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_biquad_filter),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
