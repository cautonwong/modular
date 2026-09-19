#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/board_contract.h"

#include "edge/errors.h"

#define EDGE_CONTRACT_QUEUE_CAPACITY 4u

static void edge_contract_require(bool condition, const char *name, const char *item) {
    if (!condition) {
        fail_msg("[%s] board contract violated: %s", name, item);
    }
}

void edge_contract_board_run(const edge_board_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->init != NULL, name, "init must be provided");
    edge_contract_require(contract->irq != NULL, name, "an IRQ entry point must be provided");

    edge_event_t storage[EDGE_CONTRACT_QUEUE_CAPACITY];
    edge_event_queue_t queue;
    edge_event_sink_t sink = {.queue = &queue, .clock = NULL, .guard = NULL};
    assert_int_equal(edge_event_queue_init(&queue, storage, EDGE_CONTRACT_QUEUE_CAPACITY), EDGE_OK);

    contract->init(&sink);

    /* One interrupt publishes exactly one fact, carrying the argument that
     * identifies the source. */
    contract->irq(7u);
    edge_contract_require(edge_event_count(&queue) == 1u, name,
                          "one IRQ must publish exactly one event");
    edge_event_t event = {0};
    assert_int_equal(edge_event_pop(&queue, &event), EDGE_OK);
    edge_contract_require(event.id == contract->expected_event_id, name,
                          "the published event must carry the expected event id");
    edge_contract_require(event.arg0 == 7u, name,
                          "the published event must carry the IRQ argument in arg0");

    /* A burst of interrupts is bounded by the queue, and what does not fit is
     * counted: an ISR must never block, and a lost fact must never be silent.
     * How many events fit is the queue's business, so the contract fills it
     * rather than assuming a capacity. */
    uint32_t accepted = edge_event_count(&queue);
    for (uint32_t i = 0; i <= EDGE_CONTRACT_QUEUE_CAPACITY; ++i) {
        contract->irq(i);
        const uint32_t now = edge_event_count(&queue);
        if (now == accepted) {
            break; /* full */
        }
        accepted = now;
    }
    edge_contract_require(accepted > 1u, name, "the queue must accept more than one event");

    const uint32_t dropped_before = edge_event_dropped(&queue);
    contract->irq(99u);
    edge_contract_require(edge_event_dropped(&queue) == dropped_before + 1u, name,
                          "an event that does not fit must be counted as dropped, not lost");
    edge_contract_require(edge_event_count(&queue) == accepted, name,
                          "a dropped event must not corrupt the queue");
}
