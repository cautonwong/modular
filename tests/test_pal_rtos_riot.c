#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/rtos_contract.h"
#include "pal_rtos/rtos.h"
#include "pal_rtos_riot/pal_rtos_riot.h"

/*
 * RIOT-OS RTOS contract verification suite.
 */

static void test_riot_rtos_contract(void **state) {
    (void)state;

    const edge_rtos_contract_t contract = {
        .name = "riot",
        .max_priority = 16u,
        .task_create = edge_rtos_task_create,
        .start = edge_rtos_start,
        .os_port = edge_rtos_os_port,
        .task_stack_high_water = edge_rtos_task_stack_high_water,
        .wake_target_set_self = edge_rtos_wake_target_set_self,
        .wake_from_isr = edge_rtos_wake_from_isr,
        .wait_for_work = edge_rtos_wait_for_work,
        .step = NULL,
        .step_ctx = NULL,
    };
    edge_contract_rtos_run(&contract);
}

static void test_riot_pal_primitives(void **state) {
    (void)state;
    edge_rtos_pal_state_t pal_state;
    edge_rtos_pal_init(&pal_state);

    edge_pal_port_t port = edge_rtos_pal_port(&pal_state);
    assert_non_null(port.critical_enter);
    assert_non_null(port.critical_exit);
    assert_non_null(port.memory_barrier);
    assert_non_null(port.monotonic_ticks);
    assert_non_null(port.in_isr);
    assert_non_null(port.idle);

    /* Critical section nesting */
    port.critical_enter(port.self);
    assert_int_equal(pal_state.lock_depth, 1u);
    port.critical_enter(port.self);
    assert_int_equal(pal_state.lock_depth, 2u);
    port.critical_exit(port.self);
    assert_int_equal(pal_state.lock_depth, 1u);
    port.critical_exit(port.self);
    assert_int_equal(pal_state.lock_depth, 0u);

    /* NULL self safety */
    port.critical_enter(NULL);
    port.critical_exit(NULL);

    /* Memory barrier */
    port.memory_barrier(port.self);

    /* Monotonic ticks advance */
    const uint64_t t1 = port.monotonic_ticks(port.self);
    const uint64_t t2 = port.monotonic_ticks(port.self);
    assert_true(t2 >= t1);

    /* In-ISR */
    assert_false(port.in_isr(port.self));

    /* Idle hook */
    port.idle(port.self);

    /* IRQ guard */
    edge_irq_guard_t guard = edge_rtos_irq_guard();
    assert_non_null(guard.enter);
    assert_non_null(guard.exit);
    guard.enter(guard.self);
    guard.exit(guard.self);
}

static void dummy_task(void *arg) {
    (void)arg;
}

static void test_riot_task_bounds(void **state) {
    (void)state;

    /* Invalid function pointer */
    assert_int_equal(edge_rtos_task_create("null", NULL, NULL, 128u, 0u), EDGE_EINVAL);

    /* Priority out of range (>= 16) */
    assert_int_equal(edge_rtos_task_create("prio16", dummy_task, NULL, 128u, 16u), EDGE_EINVAL);
    assert_int_equal(edge_rtos_task_create("prio20", dummy_task, NULL, 128u, 20u), EDGE_EINVAL);

    /* Stack request exceeds limit (4096 bytes = 1024 words) */
    assert_int_equal(edge_rtos_task_create("huge_stack", dummy_task, NULL, 2048u, 1u), EDGE_ENOSPC);

    /* OS port yield and sleep */
    edge_os_port_t os = edge_rtos_os_port();
    assert_non_null(os.yield);
    assert_non_null(os.sleep_ms);
    os.yield(os.self);
    os.sleep_ms(os.self, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_riot_rtos_contract),
        cmocka_unit_test(test_riot_pal_primitives),
        cmocka_unit_test(test_riot_task_bounds),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
