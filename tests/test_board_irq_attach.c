#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"
#include "mps2/board.h"
#include "soc_mps2/soc_mps2.h"

/*
 * Shared-IRQ dispatch (D84).
 *
 * These run in order and share the board's handler table, which is process-global
 * by design: a board wires its interrupts once, at startup, and there is no
 * detach. The first test therefore brings the board up the way a product does,
 * and the others work with the capacity that is left - which they query instead
 * of assuming.
 */

static uint32_t g_a_calls;
static uint32_t g_b_calls;
static uint32_t g_order[8];
static uint32_t g_order_len;

static void handler_a(void *ctx) {
    (void)ctx;
    ++g_a_calls;
    g_order[g_order_len++] = 1u;
}

static void handler_b(void *ctx) {
    (void)ctx;
    ++g_b_calls;
    g_order[g_order_len++] = 2u;
}

/*
 * The board's own timer consumer has to be reachable *through the dispatcher*:
 * that is the path every real interrupt takes, so it is the one worth asserting.
 * Reaching around it would leave the mechanism untested on target.
 */
static void test_builtin_timer_path_uses_the_dispatcher(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_sink_t sink;
    edge_event_t event;

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    sink = (edge_event_sink_t){.queue = &queue, .clock = NULL, .guard = NULL};
    board_mps2_init(&sink);
    assert_true(board_mps2_irq_handler_count() >= 1u);

    board_mps2_irq_dispatch(SOC_MPS2_TIMER0_IRQ);
    assert_int_equal(edge_event_pop(&queue, &event), EDGE_OK);
    assert_int_equal(event.id, EDGE_EVT_BOARD_TIMER0);
    assert_int_equal(event.source, SOC_MPS2_TIMER0_IRQ);

    /* A line with no handler is a no-op, not a crash. */
    board_mps2_irq_dispatch(99u);
}

static void test_registration_order_and_line_filtering(void **state) {
    (void)state;
    g_a_calls = 0u;
    g_b_calls = 0u;
    g_order_len = 0u;

    assert_int_equal(board_mps2_irq_attach(7u, handler_a, NULL), EDGE_OK);
    assert_int_equal(board_mps2_irq_attach(7u, handler_b, NULL), EDGE_OK);
    board_mps2_irq_dispatch(7u);

    assert_int_equal(g_a_calls, 1u);
    assert_int_equal(g_b_calls, 1u);
    assert_int_equal(g_order_len, 2u);
    /* Registration order is dispatch order - the property a shared bus line
     * depends on. */
    assert_int_equal(g_order[0], 1u);
    assert_int_equal(g_order[1], 2u);

    /* Another line does not drag them along. Deliberately not 8: that is
     * SOC_MPS2_TIMER0_IRQ, whose built-in handler needs a live sink. */
    board_mps2_irq_dispatch(9u);
    assert_int_equal(g_a_calls, 1u);
    assert_int_equal(g_b_calls, 1u);
}

static void test_null_callback_and_capacity(void **state) {
    (void)state;
    assert_int_equal(board_mps2_irq_attach(7u, NULL, NULL), EDGE_EINVAL);

    uint32_t room = BOARD_MPS2_IRQ_MAX_HANDLERS - board_mps2_irq_handler_count();
    while (room > 0u) {
        assert_int_equal(board_mps2_irq_attach(7u, handler_a, NULL), EDGE_OK);
        --room;
    }
    assert_int_equal(board_mps2_irq_handler_count(), BOARD_MPS2_IRQ_MAX_HANDLERS);
    assert_int_equal(board_mps2_irq_attach(7u, handler_a, NULL), EDGE_ENOSPC);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_builtin_timer_path_uses_the_dispatcher),
        cmocka_unit_test(test_registration_order_and_line_filtering),
        cmocka_unit_test(test_null_callback_and_capacity),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
