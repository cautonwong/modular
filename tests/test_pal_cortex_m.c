#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "pal_cortex_m/pal_cortex_m.h"

/* PAL contract (D46): the host fallback lets the bare-metal Cortex-M port be
 * exercised off target; the on-target behaviour is validated by the QEMU smoke. */
static void test_pal_contract(void **state) {
    (void)state;
    edge_pal_cortex_m_state_t pal_state;
    edge_pal_cortex_m_bare_init(&pal_state);
    const edge_pal_port_t pal = edge_pal_cortex_m_bare_port(&pal_state);

    assert_non_null(pal.critical_enter);
    assert_non_null(pal.critical_exit);
    assert_non_null(pal.memory_barrier);
    assert_non_null(pal.monotonic_ticks);
    assert_non_null(pal.in_isr);
    assert_non_null(pal.isr_enter);
    assert_non_null(pal.isr_exit);

    pal.critical_enter(pal.self);
    pal.critical_enter(pal.self);
    assert_int_equal(pal_state.depth, 2u);
    pal.critical_exit(pal.self);
    pal.critical_exit(pal.self);
    pal.critical_exit(pal.self); /* unbalanced exit is clamped */
    assert_int_equal(pal_state.depth, 0u);

    const uint64_t first = pal.monotonic_ticks(pal.self);
    const uint64_t second = pal.monotonic_ticks(pal.self);
    assert_true(second > first);

    assert_false(pal.in_isr(pal.self));
    pal.isr_enter(pal.self);
    pal.isr_exit(pal.self);
    pal.memory_barrier(pal.self);
    pal.idle(pal.self);
}

/*
 * The 64-bit extension arithmetic is a pure function so these boundaries are
 * reachable from the host at all: the register read sits inside `#if __arm__`,
 * so before the extraction the arithmetic had no test on any platform.
 */
static void test_extend_boundaries(void **state) {
    (void)state;
    edge_pal_cortex_m_state_t pal_state;
    edge_pal_cortex_m_bare_init(&pal_state);
    const uint32_t load = pal_state.load;

    /* VAL == LOAD: the instant after a reload, zero elapsed in this period. */
    assert_int_equal(edge_pal_cortex_m_extend(&pal_state, load), 0u);
    /* One tick before the reload. */
    assert_int_equal(edge_pal_cortex_m_extend(&pal_state, load - 1u), 1u);
    /* The reload itself: the sample is *above* the previous one, so a wrap was
     * crossed and time must move forward by the rest of the period - never back
     * to zero. */
    const uint64_t wrapped = edge_pal_cortex_m_extend(&pal_state, load);
    assert_int_equal(pal_state.wrap, 1u);
    assert_int_equal(wrapped, (uint64_t)load + 1u);
    assert_true(wrapped > 1u);
    /* One tick after the wrap. */
    assert_int_equal(edge_pal_cortex_m_extend(&pal_state, load - 1u), (uint64_t)load + 2u);
    assert_int_equal(pal_state.anomalies, 0u);
}

/*
 * The defect this guards: the wrap happens between two samples and is not
 * counted, so the clock falls back by nearly a whole period (~0.67 s at 25 MHz).
 * Detecting it from the counter itself is what makes the sequence monotonic.
 */
static void test_wrap_is_detected_from_the_counter(void **state) {
    (void)state;
    edge_pal_cortex_m_state_t pal_state;
    edge_pal_cortex_m_bare_init(&pal_state);
    const uint32_t load = pal_state.load;

    const uint64_t before = edge_pal_cortex_m_extend(&pal_state, load - 255u);
    assert_int_equal(before, 255u);

    /* The counter ran down and reloaded: the next sample is high again. */
    const uint64_t after = edge_pal_cortex_m_extend(&pal_state, load);
    assert_int_equal(pal_state.wrap, 1u);
    assert_true(after > before);
}

static void test_three_wraps_stay_monotonic(void **state) {
    (void)state;
    edge_pal_cortex_m_state_t pal_state;
    edge_pal_cortex_m_bare_init(&pal_state);
    const uint32_t load = pal_state.load;

    uint64_t previous = edge_pal_cortex_m_extend(&pal_state, load);
    for (uint32_t epoch = 1u; epoch <= 3u; ++epoch) {
        (void)edge_pal_cortex_m_extend(&pal_state, 5u); /* deep into the period */
        const uint64_t reloaded = edge_pal_cortex_m_extend(&pal_state, load);
        assert_true(reloaded > previous);
        assert_int_equal(pal_state.wrap, epoch);
        previous = reloaded;
    }
    assert_int_equal(pal_state.anomalies, 0u);
}

/*
 * The documented ceiling of counter-based detection: a gap longer than one period
 * looks like an ordinary smaller step, so whole periods are lost. This is the
 * price of not trusting COUNTFLAG, and the reason the header requires sampling at
 * least once per period - the test states the limitation instead of hiding it.
 */
static void test_sampling_gap_undercounts(void **state) {
    (void)state;
    edge_pal_cortex_m_state_t pal_state;
    edge_pal_cortex_m_bare_init(&pal_state);
    const uint32_t load = pal_state.load;

    assert_int_equal(edge_pal_cortex_m_extend(&pal_state, load), 0u);
    const uint64_t missed = edge_pal_cortex_m_extend(&pal_state, load - 1000u);
    assert_int_equal(pal_state.wrap, 0u);
    assert_true(missed < ((uint64_t)load + 1u));
}

/* A LOAD change after init is a contract violation (see the header): it must be
 * observable, and it must neither be absorbed nor move the clock backwards. */
static void test_load_change_is_observable_not_silent(void **state) {
    (void)state;
    edge_pal_cortex_m_state_t pal_state;
    edge_pal_cortex_m_bare_init(&pal_state);
    const uint32_t load = pal_state.load;

    assert_int_equal(edge_pal_cortex_m_extend(&pal_state, load), 0u);
    assert_int_equal(pal_state.anomalies, 0u);

    /* Someone rewrote SYSTICK_LOAD: the sample is above the cached period. */
    const uint64_t frozen = edge_pal_cortex_m_extend(&pal_state, load + 100u);
    assert_int_equal(pal_state.anomalies, 1u);
    assert_int_equal(frozen, 0u); /* frozen at the last accepted value */

    /* The timeline still advances from there. */
    const uint64_t next = edge_pal_cortex_m_extend(&pal_state, load - 1u);
    assert_int_equal(next, 1u);
    assert_true(next > frozen);

    assert_int_equal(edge_pal_cortex_m_extend(NULL, 0u), 0u);
}

static void test_null_state_is_safe(void **state) {
    (void)state;
    edge_pal_cortex_m_bare_init(NULL);
    const edge_pal_port_t pal = edge_pal_cortex_m_bare_port(NULL);
    assert_null(pal.self);
    pal.critical_enter(NULL);
    pal.critical_exit(NULL);
    assert_int_equal(pal.monotonic_ticks(NULL), 0u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pal_contract),
        cmocka_unit_test(test_extend_boundaries),
        cmocka_unit_test(test_wrap_is_detected_from_the_counter),
        cmocka_unit_test(test_three_wraps_stay_monotonic),
        cmocka_unit_test(test_sampling_gap_undercounts),
        cmocka_unit_test(test_load_change_is_observable_not_silent),
        cmocka_unit_test(test_null_state_is_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
