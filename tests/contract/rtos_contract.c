#include "contract/rtos_contract.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "pal_rtos/assert.h"

/*
 * The parts of the port contract that are observable without running a scheduler -
 * see the header for why the scheduling half is evidenced elsewhere. Small on
 * purpose: every assertion here is one a port can actually fail.
 */

static void must_not_run(void *arg) {
    (void)arg;
}

static const char *g_assert_file;
static int g_assert_line;
static uint32_t g_assert_calls;
static jmp_buf g_after_assert;

/*
 * The hook jumps back instead of returning, and that is not a test trick: the shared
 * implementation halts after the hook returns (a failed assert must never continue),
 * so a hook that returns would hang the process. Jumping is what lets this suite
 * assert that the assert was *counted and reported* without pretending the halt away.
 * That the halt itself follows is a product-level fact - the composition root's hook
 * exits with a distinct status, and the QEMU smoke asserts it.
 */
static void on_assert(void *ctx, const char *file, int line) {
    (void)ctx;
    g_assert_file = file;
    g_assert_line = line;
    ++g_assert_calls;
    longjmp(g_after_assert, 1);
}

void edge_contract_rtos_run(const edge_rtos_contract_t *contract) {
    assert_non_null(contract);

    /* A port must name itself: a contract failure that cannot say which port failed
     * is a debugging session. */
    assert_non_null(contract->name);
    assert_non_null(contract->task_create);
    assert_non_null(contract->os_port);
    assert_non_null(contract->wake_from_isr);
    assert_non_null(contract->wait_for_work);

    /* A NULL entry point is not a task. Accepting it means a caller's mistake
     * becomes a jump to address zero the first time the scheduler runs. */
    assert_int_equal(contract->task_create("null-entry", NULL, NULL, 128u, 0u), EDGE_EINVAL);

    /* A valid task is accepted at suite start, so a port with no room left has
     * something wrong with it before the suite even runs. */
    assert_int_equal(contract->task_create("contract", must_not_run, NULL, 128u, 0u), EDGE_OK);

    /* Yielding is the one thing a hosted runner cannot do without; a port that
     * cannot yield cannot host one. */
    const edge_os_port_t os = contract->os_port();
    assert_non_null(os.yield);

    /* The high-water mark may be unavailable - 0 - but it must be callable. */
    (void)contract->task_stack_high_water();

    /* Waking with nothing published is a no-op, not a crash: an ISR must be able to
     * call this without knowing whether a runner is parked. */
    contract->wake_from_isr();
    contract->wake_target_set_self();
    (void)contract->wait_for_work(1u);

    /* The assert contract is shared by every port (`pal/rtos/src/assert.c`), and it
     * is the one rule that may never be violated: a failed assert is counted and
     * reported, never swallowed. */
    edge_rtos_set_assert_hook(on_assert, NULL);
    const uint32_t before = edge_rtos_assert_count();
    g_assert_calls = 0u;
    if (setjmp(g_after_assert) == 0)
        edge_rtos_assert_failed("contract.c", 42);
    assert_int_equal(g_assert_calls, 1u);
    assert_string_equal(g_assert_file, "contract.c");
    assert_int_equal(g_assert_line, 42);
    assert_int_equal(edge_rtos_assert_count(), before + 1u);
}
