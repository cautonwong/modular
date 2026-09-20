#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "pal_os/tick64.h"

static void test_extends_within_a_period(void **state) {
    (void)state;
    edge_tick64_t tick;
    edge_tick64_reset(&tick);
    assert_int_equal(edge_tick64_extend(&tick, 0u), 0u);
    assert_int_equal(edge_tick64_extend(&tick, 10u), 10u);
    assert_int_equal(edge_tick64_extend(&tick, 0x7FFFFFFFu), 0x7FFFFFFFull);
    assert_int_equal(tick.wraps, 0u);
}

/* The one that matters on target: the value must not fall back to 0 when the
 * 32-bit tick passes 2^32. At 100 Hz that happens 497 days after deployment. */
static void test_wrap_extends_the_epoch(void **state) {
    (void)state;
    edge_tick64_t tick;
    edge_tick64_reset(&tick);
    const uint64_t before = edge_tick64_extend(&tick, 0xFFFFFFFFu);
    const uint64_t after = edge_tick64_extend(&tick, 0u);
    assert_int_equal(before, 0xFFFFFFFFull);
    assert_int_equal(after, 0x100000000ull);
    assert_true(after > before);
    assert_int_equal(tick.wraps, 1u);
}

static void test_three_wraps_stay_monotonic(void **state) {
    (void)state;
    edge_tick64_t tick;
    edge_tick64_reset(&tick);
    uint64_t prev = edge_tick64_extend(&tick, 0xFFFFFFF0u);
    for (uint32_t epoch = 1u; epoch <= 3u; ++epoch) {
        const uint64_t high = edge_tick64_extend(&tick, 0xFFFFFFFFu);
        assert_true(high > prev);
        const uint64_t wrapped = edge_tick64_extend(&tick, 5u);
        assert_true(wrapped > high);
        assert_int_equal(wrapped, ((uint64_t)epoch << 32) | 5ull);
        prev = wrapped;
    }
    assert_int_equal(tick.wraps, 3u);
}

/* The documented ceiling: a gap of 2^31 ticks or more is read as a plain forward
 * step, because a wrap and a large jump are indistinguishable. Sampling every
 * sys iteration is what keeps this branch unreachable. */
static void test_sampling_gap_is_the_documented_ceiling(void **state) {
    (void)state;
    edge_tick64_t tick;
    edge_tick64_reset(&tick);
    assert_int_equal(edge_tick64_extend(&tick, 0x80000000u), 0x80000000ull);
    assert_int_equal(tick.wraps, 0u);
}

static void test_null_state_is_a_plain_passthrough(void **state) {
    (void)state;
    edge_tick64_reset(NULL);
    assert_int_equal(edge_tick64_extend(NULL, 7u), 7u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_extends_within_a_period),
        cmocka_unit_test(test_wrap_extends_the_epoch),
        cmocka_unit_test(test_three_wraps_stay_monotonic),
        cmocka_unit_test(test_sampling_gap_is_the_documented_ceiling),
        cmocka_unit_test(test_null_state_is_a_plain_passthrough),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
