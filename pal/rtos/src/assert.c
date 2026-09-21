#include "pal_rtos/assert.h"

#include <stddef.h>

/*
 * The assert contract (D87), shared by every `pal/rtos/<os>` port.
 *
 * It lives here rather than inside a port because it is kernel-independent: the
 * product maps its kernel's assert onto `edge_rtos_assert_failed()`, and what
 * happens next is the same everywhere - count it, tell the composition root, and
 * halt rather than continue in a state whose invariants just failed.
 *
 * `tests/contract/rtos_contract.c` exercises this once for all ports.
 */
static edge_rtos_assert_fn g_assert_fn;
static void *g_assert_ctx;
static uint32_t g_assert_count;

void edge_rtos_set_assert_hook(edge_rtos_assert_fn fn, void *ctx) {
    g_assert_fn = fn;
    g_assert_ctx = ctx;
}

uint32_t edge_rtos_assert_count(void) {
    return g_assert_count;
}

void edge_rtos_assert_failed(const char *file, int line) {
    ++g_assert_count;
    if (g_assert_fn != NULL)
        g_assert_fn(g_assert_ctx, file, line);
    /* No hook (or a hook that returned): halt rather than continue silently. */
    for (;;) {
    }
}
