#ifndef PARITY_CAPSULE_H
#define PARITY_CAPSULE_H

/*
 * Frozen capsule + observation schema for the cross-runner parity harness (#242).
 *
 * The same product capsule - one app, one subscription, one drive script - is run
 * under two runners; each writes the same bounded observation log and
 * `.github/scripts/check_runner_parity.py` compares them against the checked-in
 * oracle. The script and the log are the contract: changing either one changes
 * what "the same product" means, so a schema bump is a deliberate edit here.
 *
 * Runner-specific numbers (how many `edge_sys_run_once` steps it took, how many
 * times the runner idled) are deliberately absent: they describe the runner, not
 * the capsule, and would make the oracle meaningless.
 */

#include "edge/event.h"
#include "edge/events.h"
#include "example/sys.h"

#include <stdio.h>

#define PARITY_SCHEMA 1u
#define PARITY_EVENTS 64u
#define PARITY_EVENT_ID EDGE_EVT_UART0_RX

/* Frozen drive script: the arg walks 0..PARITY_EVENTS-1, so a lost, duplicated
 * or reordered event shows up as a shift rather than as a missing count. */
#define PARITY_EVENT_ARG(step) ((uint32_t)(step))

static inline void parity_emit(const edge_sys_t *sys, const edge_event_queue_t *queue,
                               uint32_t module_id, int init_status, uint32_t last_event) {
    edge_sys_stats_t stats = {0};
    (void)edge_sys_stats_get(sys, &stats);
    printf("OBS1 schema %u\n", (unsigned)PARITY_SCHEMA);
    printf("OBS1 events %u\n", (unsigned)stats.events);
    printf("OBS1 drops %u\n", (unsigned)stats.drops);
    printf("OBS1 errors %u\n", (unsigned)stats.errors);
    printf("OBS1 budget_hits %u\n", (unsigned)stats.budget_hits);
    printf("OBS1 isolated %u\n", (unsigned)stats.isolated);
    printf("OBS1 queue_dropped %u\n", (unsigned)edge_event_dropped(queue));
    printf("OBS1 module %u init %d last_event %u\n", (unsigned)module_id, init_status,
           (unsigned)last_event);
}

#endif /* PARITY_CAPSULE_H */
