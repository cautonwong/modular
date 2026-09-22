#include "contract/rtos_contract.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "pal_rtos/assert.h"

/*
 * The parts of the port contract that are observable on the host - see the header
 * for how the scheduling half is closed. Small on purpose: every assertion here is
 * one a port can actually fail.
 */

static void must_not_run(void *arg) {
    (void)arg;
}

static uint32_t g_order[4];
static uint32_t g_order_len;

static void record_run(void *arg) {
    if (g_order_len < (uint32_t)(sizeof(g_order) / sizeof(g_order[0])))
        g_order[g_order_len++] = (uint32_t)(uintptr_t)arg;
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

    /* A priority the port cannot represent is rejected, **never clamped**: clamping
     * turns a caller's mistake into a scheduling surprise nobody can trace back. */
    if (contract->max_priority > 0u) {
        assert_int_equal(
            contract->task_create("too-high", must_not_run, NULL, 128u, contract->max_priority),
            EDGE_EINVAL);
    }

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

    /*
     * The pinned priority direction, when the harness can advance the scheduler:
     * priority 0 must run before priority 1 in the same round. This is the assertion
     * #163 asked for - "0 = highest" was documented but never executed.
     */
    if (contract->step != NULL) {
        g_order_len = 0u;
        assert_int_equal(contract->task_create("prio0", record_run, (void *)1, 128u, 0u), EDGE_OK);
        assert_int_equal(contract->task_create("prio1", record_run, (void *)2, 128u, 1u), EDGE_OK);
        contract->step(contract->step_ctx);
        assert_true(g_order_len >= 2u);
        assert_int_equal(g_order[0], 1u); /* 0 ran first */
        assert_int_equal(g_order[1], 2u);
    }

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
