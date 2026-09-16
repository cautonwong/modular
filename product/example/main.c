#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "example/board.h"
#include "example/sys.h"
#include "dlt645/dlt645.h"

#include <stdint.h>

void product_example_make_storage(dlt645_storage_if_t *out, void *flash_state);

static uint64_t monotonic_ticks(void *self)
{
    uint64_t *tick = (uint64_t *)self;
    return ++(*tick);
}

int main(void)
{
    uint64_t clock_tick = 0u;
    uint8_t flash_state[64] = {0};
    dlt645_storage_if_t storage;
    dlt645_t dlt645;
    edge_module_t *apps[1];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {.monotonic_ticks=monotonic_ticks, .wall_time=NULL, .self=&clock_tick};
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;

    product_example_make_storage(&storage, flash_state);
    dlt645_construct(&dlt645, 0x1001u, 100u, &storage);
    apps[0] = &dlt645.module;

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0) return 1;
    event_sink = (edge_event_sink_t){.queue=&event_queue, .clock=&clock};
    board_example_init(&event_sink);

    if (sys_example_init(&sys, apps, 1u, &event_queue, subscriptions, 4u) < 0) return 2;
    if (edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &dlt645.module) < 0) return 3;

    for (unsigned i = 0u; i < 100u; ++i) {
        if (edge_sys_run_once(&sys) < 0) break;
        if (i == 10u) board_example_irq_uart0_rx(3u);
    }

    (void)edge_sys_power_off(&sys);
    (void)edge_sys_deinit(&sys);
    return 0;
}
