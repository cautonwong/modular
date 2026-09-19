#ifndef EDGE_TEST_BOARD_CONTRACT_H
#define EDGE_TEST_BOARD_CONTRACT_H

#include "edge/event.h"

#include <stdint.h>

/*
 * Board contract (D75/D84): a board owns the IRQ vectors, and its interrupt
 * entry points are the one place in the system that cannot be debugged after the
 * fact. What they may do is therefore fixed: clear the source, publish exactly
 * one fact, and never block or silently lose it when the queue is full.
 *
 * The contract drives one IRQ entry point on the host board. A real board is
 * covered by Renode/HIL (D62/D63); this keeps the shape honest in the meantime.
 */
typedef struct edge_board_contract {
    const char *name;
    void (*init)(edge_event_sink_t *sink);
    void (*irq)(uint32_t arg); /* one IRQ entry point that publishes a fact */
    uint32_t expected_event_id;
} edge_board_contract_t;

void edge_contract_board_run(const edge_board_contract_t *contract);

#endif
