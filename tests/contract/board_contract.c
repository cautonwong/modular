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

static uint32_t s_irq_a_calls;
static uint32_t s_irq_b_calls;
static uint32_t s_irq_order[8];
static uint32_t s_irq_order_len;
static void *s_irq_a_ctx;
static void *s_irq_b_ctx;

static void irq_cb_a(void *ctx) {
    s_irq_a_calls++;
    s_irq_a_ctx = ctx;
    if (s_irq_order_len < 8u) {
        s_irq_order[s_irq_order_len++] = 1u;
    }
}

static void irq_cb_b(void *ctx) {
    s_irq_b_calls++;
    s_irq_b_ctx = ctx;
    if (s_irq_order_len < 8u) {
        s_irq_order[s_irq_order_len++] = 2u;
    }
}

void edge_contract_board_irq_run(const edge_board_irq_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->attach != NULL, name, "attach must be provided");
    edge_contract_require(contract->dispatch != NULL, name, "dispatch must be provided");

    s_irq_a_calls = 0u;
    s_irq_b_calls = 0u;
    s_irq_order_len = 0u;
    s_irq_a_ctx = NULL;
    s_irq_b_ctx = NULL;

    /* 1. NULL callback rejection */
    edge_contract_require(contract->attach(100u, NULL, NULL) == EDGE_EINVAL, name,
                          "attach must reject NULL callback with EDGE_EINVAL");

    /* 2. Multiple handlers attached on the same IRQ line */
    int marker_a = 0x1111;
    int marker_b = 0x2222;
    edge_contract_require(contract->attach(100u, irq_cb_a, &marker_a) == EDGE_OK, name,
                          "attach handler A must succeed");
    edge_contract_require(contract->attach(100u, irq_cb_b, &marker_b) == EDGE_OK, name,
                          "attach handler B must succeed");

    /* 3. Dispatch line 100u and verify FIFO registration order and context passing */
    contract->dispatch(100u);
    edge_contract_require(s_irq_a_calls == 1u, name, "handler A must be called once");
    edge_contract_require(s_irq_b_calls == 1u, name, "handler B must be called once");
    edge_contract_require(s_irq_a_ctx == &marker_a, name,
                          "handler A must receive marker A context");
    edge_contract_require(s_irq_b_ctx == &marker_b, name,
                          "handler B must receive marker B context");
    edge_contract_require(s_irq_order_len == 2u && s_irq_order[0] == 1u && s_irq_order[1] == 2u,
                          name, "handlers must execute in FIFO registration order");

    /* 4. Line filtering: dispatch on an unhandled line must not invoke line 100 handlers */
    contract->dispatch(101u);
    edge_contract_require(s_irq_a_calls == 1u && s_irq_b_calls == 1u, name,
                          "dispatch on unhandled line must not invoke other handlers");

    /* 5. Capacity saturation: if max_handlers is specified and handler_count is provided */
    if (contract->max_handlers > 0u && contract->handler_count != NULL) {
        uint32_t current = contract->handler_count();
        while (current < contract->max_handlers) {
            edge_contract_require(contract->attach(102u, irq_cb_a, NULL) == EDGE_OK, name,
                                  "fill remaining handler slots");
            current++;
        }
        edge_contract_require(contract->attach(102u, irq_cb_a, NULL) == EDGE_ENOSPC, name,
                              "attach beyond max capacity must return EDGE_ENOSPC");
    }
}
