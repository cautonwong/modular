#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "example/sys.h"
#include "parity_capsule.h"

#include <stdint.h>

void minimal_product_make_storage(dlt645_storage_if_t *out, void *flash_state);

static uint64_t g_clock_tick;

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

/*
 * Neutrality fixture: the bare-metal runner. The same app (`dlt645`) is built
 * unchanged for every board/runner combination in `edge_add_minimal_variant`.
 */
int main(void) {
    uint8_t flash_state[64] = {0};
    dlt645_storage_if_t storage;
    dlt645_t app;
    edge_module_t *apps[1];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {
        .monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = &g_clock_tick};
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;

    minimal_product_make_storage(&storage, flash_state);
    dlt645_construct(&app, EDGE_MOD_DLT645, 100u, &storage);
    const int init_status = dlt645_init(&app);
    if (init_status < 0)
        return 9; /* D51: the composition root initialises the module */
    apps[0] = dlt645_module(&app);

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_example_init(&event_sink);

    if (sys_example_init(&sys, apps, 1u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, dlt645_module(&app)) < 0)
        return 4;
    if (edge_sys_start(&sys) < 0)
        return 5;

    /* Frozen parity capsule (#242): the runner injects exactly the script the other
     * runner injects, through the same sink a board ISR would use. */
    for (unsigned step = 0u; step < PARITY_EVENTS; ++step) {
        const edge_event_t parity_event = {.id = PARITY_EVENT_ID, .arg0 = PARITY_EVENT_ARG(step)};
        if (edge_event_sink_push_isr(&event_sink, &parity_event) != EDGE_OK)
            return 6;
        if (edge_sys_run_once(&sys) < 0)
            return 7;
    }

    parity_emit(&sys, &event_queue, EDGE_MOD_DLT645, init_status, app.last_event);

    (void)edge_sys_power_off(&sys);
    (void)dlt645_deinit(&app);
    (void)edge_sys_deinit(&sys);
    return 0;
}
