#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "meter/sys.h"
#include "relay/relay.h"

#include <stdint.h>

void product_meter_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_host_make_relay_out(relay_out_if_t *out, void *gpio_state);

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

int main(void) {
    uint64_t clock_tick = 0u;
    uint8_t flash_state[64] = {0};
    uint8_t gpio_state[8] = {0};
    dlt645_storage_if_t storage;
    relay_out_if_t relay_out;
    dlt645_t dlt645;
    relay_t relay;
    edge_module_t *apps[2];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {
        .monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = &clock_tick};
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;
    const uint32_t required[] = {EDGE_MOD_DLT645, EDGE_MOD_RELAY};

    product_meter_host_make_storage(&storage, flash_state);
    product_meter_host_make_relay_out(&relay_out, gpio_state);
    dlt645_construct(&dlt645, EDGE_MOD_DLT645, 100u, &storage);
    relay_construct(&relay, EDGE_MOD_RELAY, 110u, &relay_out);
    if (dlt645_init(&dlt645) < 0 || relay_init(&relay) < 0)
        return 10; /* D51/D53: assembly-time init belongs to the composition root */
    apps[0] = dlt645_module(&dlt645);
    apps[1] = relay_module(&relay);

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_example_init(&event_sink);

    if (sys_meter_init(&sys, apps, 2u, required, 2u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 5;
    if (edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, dlt645_module(&dlt645)) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_RELAY_CHANGED, relay_module(&relay)) < 0)
        return 6;
    if (edge_sys_start(&sys) < 0)
        return 4;

    for (unsigned i = 0u; i < 100u; ++i) {
        if (edge_sys_run_once(&sys) < 0)
            break;
        if (i == 10u)
            board_example_irq_uart0_rx(3u);
    }

    (void)edge_sys_power_off(&sys);
    (void)relay_deinit(&relay);
    (void)dlt645_deinit(&dlt645);
    (void)edge_sys_deinit(&sys);
    return 0;
}
