#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "example/sys.h"

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
    apps[0] = &app.module;

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_example_init(&event_sink);

    if (sys_example_init(&sys, apps, 1u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &app.module) < 0)
        return 4;
    if (edge_sys_start(&sys) < 0)
        return 5;

    for (unsigned step = 0u; step < 50u; ++step) {
        if (edge_sys_run_once(&sys) < 0)
            return 6;
        if (step == 5u)
            board_example_irq_uart0_rx(3u);
    }

    (void)edge_sys_power_off(&sys);
    (void)edge_sys_deinit(&sys);
    return 0;
}
