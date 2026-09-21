#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/rtos_contract.h"
#include "pal_eos/eos.h"

/*
 * The RTOS port contract, run against the one port that exists on the host.
 *
 * EOS implements `pal_rtos/rtos.h` with the same function names a kernel port uses,
 * so this binary is what makes the contract executable rather than declared: without
 * it, every assertion in `tests/contract/rtos_contract.c` would need a target and a
 * kernel to mean anything.
 */

extern edge_status_t edge_eos_bind_rtos(edge_eos_t *eos);

static uint64_t g_now;

static uint64_t fake_now(void *self) {
    (void)self;
    return g_now;
}

static const edge_clock_port_t g_clock = {
    .monotonic_ticks = fake_now, .wall_time = NULL, .self = NULL};

static void run_suite(void **state) {
    (void)state;
    edge_eos_t eos;
    assert_int_equal(edge_eos_init(&eos, &g_clock), EDGE_OK);
    assert_int_equal(edge_eos_bind_rtos(&eos), EDGE_OK);
    assert_int_equal(edge_eos_bind_rtos(NULL), EDGE_EINVAL);

    const edge_rtos_contract_t contract = {
        .name = "eos",
        .task_create = edge_rtos_task_create,
        .start = edge_rtos_start,
        .os_port = edge_rtos_os_port,
        .task_stack_high_water = edge_rtos_task_stack_high_water,
        .wake_target_set_self = edge_rtos_wake_target_set_self,
        .wake_from_isr = edge_rtos_wake_from_isr,
        .wait_for_work = edge_rtos_wait_for_work,
    };
    edge_contract_rtos_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {cmocka_unit_test(run_suite)};
    return cmocka_run_group_tests(tests, NULL, NULL);
}
