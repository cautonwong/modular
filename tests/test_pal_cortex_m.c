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
        cmocka_unit_test(test_null_state_is_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
