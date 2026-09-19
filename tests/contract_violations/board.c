/*
 * A contract that cannot fail is documentation with a green tick.
 *
 * This board's IRQ entry point publishes nothing, so the fact is lost silently --
 * exactly the failure a real interrupt handler hides best. CTest is told to
 * expect this binary to fail (WILL_FAIL).
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/board_contract.h"
#include "edge/event.h"
#include "edge/events.h"

static edge_event_sink_t *g_sink;

static void broken_init(edge_event_sink_t *sink) {
    g_sink = sink;
}

static void broken_irq(uint32_t arg) {
    (void)arg;
    (void)g_sink; /* the violation: nothing is ever published */
}

static void test_violation_is_rejected(void **state) {
    (void)state;
    const edge_board_contract_t contract = {
        .name = "violation/silent_irq",
        .init = broken_init,
        .irq = broken_irq,
        .expected_event_id = EDGE_EVT_UART0_RX,
    };
    edge_contract_board_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_violation_is_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
