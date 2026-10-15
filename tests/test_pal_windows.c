#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/pal.h"
#include "pal_windows/pal_windows.h"

static void test_pal_windows_basic_contract(void **state) {
    (void)state;
    pal_windows_state_t pal_state;
    pal_windows_init(&pal_state);

    const edge_pal_port_t pal = pal_windows_port(&pal_state);
    const edge_os_port_t os = pal_windows_os_port(&pal_state);

    assert_non_null(pal.critical_enter);
    assert_non_null(pal.critical_exit);
    assert_non_null(pal.memory_barrier);
    assert_non_null(pal.monotonic_ticks);
    assert_non_null(pal.in_isr);
    assert_non_null(pal.isr_enter);
    assert_non_null(pal.isr_exit);
    assert_non_null(pal.idle);

    assert_non_null(os.yield);
    assert_non_null(os.sleep_ms);

    /* Critical section recursion & depth tracking */
    assert_int_equal(pal_windows_critical_depth(&pal_state), 0u);
    pal.critical_enter(pal.self);
    pal.critical_enter(pal.self);
    assert_int_equal(pal_windows_critical_depth(&pal_state), 2u);
    pal.critical_exit(pal.self);
    assert_int_equal(pal_windows_critical_depth(&pal_state), 1u);
    pal.critical_exit(pal.self);
    assert_int_equal(pal_windows_critical_depth(&pal_state), 0u);

    /* Clamped underflow */
    pal.critical_exit(pal.self);
    assert_int_equal(pal_windows_critical_depth(&pal_state), 0u);

    /* Memory barrier */
    pal.memory_barrier(pal.self);

    /* Monotonic ticks test */
    const uint64_t t1 = pal.monotonic_ticks(pal.self);
    const uint64_t t2 = pal.monotonic_ticks(pal.self);
    assert_true(t2 >= t1);

    /* ISR flags */
    assert_false(pal.in_isr(pal.self));
    pal.isr_enter(pal.self);
    assert_true(pal.in_isr(pal.self));
    pal.isr_exit(pal.self);
    assert_false(pal.in_isr(pal.self));

    pal_windows_set_in_isr(&pal_state, true);
    assert_true(pal.in_isr(pal.self));
    pal_windows_set_in_isr(&pal_state, false);
    assert_false(pal.in_isr(pal.self));

    /* Yield & idle counters */
    const uint32_t yields_before = pal_windows_yields(&pal_state);
    os.yield(os.self);
    assert_int_equal(pal_windows_yields(&pal_state), yields_before + 1u);

    const uint32_t idles_before = pal_windows_idle_waits(&pal_state);
    pal.idle(pal.self);
    assert_int_equal(pal_windows_idle_waits(&pal_state), idles_before + 1u);

    pal_windows_deinit(&pal_state);
}

static void test_pal_windows_default_instance(void **state) {
    (void)state;
    const edge_pal_port_t pal = pal_windows_port(NULL);
    const edge_os_port_t os = pal_windows_os_port(NULL);

    assert_non_null(pal.critical_enter);
    assert_non_null(pal.monotonic_ticks);

    pal.critical_enter(pal.self);
    assert_int_equal(pal_windows_critical_depth(NULL), 1u);
    pal.critical_exit(pal.self);
    assert_int_equal(pal_windows_critical_depth(NULL), 0u);

    os.yield(os.self);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pal_windows_basic_contract),
        cmocka_unit_test(test_pal_windows_default_instance),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
