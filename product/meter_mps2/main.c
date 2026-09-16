#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "meter/sys.h"
#include "mps2/board.h"
#include "relay/relay.h"

#include <stdint.h>

void product_meter_mps2_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_mps2_make_relay_out(relay_out_if_t *out, void *gpio_state);

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
    const uint32_t required[] = {0x1001u, 0x1002u};

    product_meter_mps2_make_storage(&storage, flash_state);
    product_meter_mps2_make_relay_out(&relay_out, gpio_state);
    dlt645_construct(&dlt645, 0x1001u, 100u, &storage);
    relay_construct(&relay, 0x1002u, 110u, &relay_out);
    apps[0] = &dlt645.module;
    apps[1] = &relay.module;

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_mps2_init(&event_sink);

    if (sys_meter_init(&sys, apps, 2u, required, 2u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 5;
    if (edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &dlt645.module) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_RELAY_CHANGED, &relay.module) < 0)
        return 6;
#ifdef EDGE_QEMU_SEMIHOSTING
    if (edge_sys_subscribe(&sys, EDGE_EVT_BOARD_TIMER0, &dlt645.module) < 0)
        return 7;
#endif
    if (edge_sys_start(&sys) < 0)
        return 4;

#ifdef EDGE_QEMU_SEMIHOSTING
    board_mps2_timer_init();
#endif

    for (unsigned i = 0u; i < 100000u; ++i) {
        if (edge_sys_run_once(&sys) < 0)
            break;
#ifdef EDGE_QEMU_SEMIHOSTING
        if (dlt645.last_event == EDGE_EVT_BOARD_TIMER0) {
            (void)edge_sys_power_off(&sys);
            (void)edge_sys_deinit(&sys);
            return 0;
        }
#endif
    }

    (void)edge_sys_power_off(&sys);
    (void)edge_sys_deinit(&sys);
#ifdef EDGE_QEMU_SEMIHOSTING
    return 8;
#else
    return 0;
#endif
}
