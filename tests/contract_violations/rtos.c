/*
 * A contract that cannot fail is documentation with a green tick.
 *
 * This port accepts a NULL entry point, which is the shape of a mistake that turns
 * into a jump to address zero the first time the scheduler runs a task. CTest is
 * told to expect this binary to fail (WILL_FAIL), so the suite cannot quietly stop
 * rejecting it.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/rtos_contract.h"
#include "edge/errors.h"

static edge_status_t broken_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                        uint32_t stack_words, uint32_t priority) {
    (void)name;
    (void)arg;
    (void)stack_words;
    (void)priority;
    if (fn == NULL)
        return EDGE_OK; /* the violation: garbage in, success out */
    return EDGE_OK;
}

static void broken_start(void) {
}

static void broken_yield(void *self) {
    (void)self;
}

static edge_os_port_t broken_os_port(void) {
    const edge_os_port_t port = {.yield = broken_yield, .sleep_ms = NULL, .self = NULL};
    return port;
}

static uint32_t broken_high_water(void) {
    return 0u;
}

static void broken_wake_target(void) {
}

static void broken_wake_isr(void) {
}

static bool broken_wait(uint32_t timeout_ticks) {
    (void)timeout_ticks;
    return true;
}

static void test_broken_port_is_rejected(void **state) {
    (void)state;
    const edge_rtos_contract_t contract = {
        .name = "broken",
        .task_create = broken_task_create,
        .start = broken_start,
        .os_port = broken_os_port,
        .task_stack_high_water = broken_high_water,
        .wake_target_set_self = broken_wake_target,
        .wake_from_isr = broken_wake_isr,
        .wait_for_work = broken_wait,
    };
    edge_contract_rtos_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {cmocka_unit_test(test_broken_port_is_rejected)};
    return cmocka_run_group_tests(tests, NULL, NULL);
}
