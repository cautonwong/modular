#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "meter/sys.h"
#include "riscv_virt/board.h"

#include <stdint.h>

void product_riscv_meter_make_storage(dlt645_storage_if_t *out, void *flash_state);

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

int main(void) {
    uint64_t clock_tick = 0u;
    uint8_t flash_state[64] = {0};
    dlt645_storage_if_t storage;
    dlt645_t dlt645;
    edge_module_t *apps[1];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {
        .monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = &clock_tick};
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;
    const uint32_t required[] = {EDGE_MOD_DLT645};

    product_riscv_meter_make_storage(&storage, flash_state);
    dlt645_construct(&dlt645, EDGE_MOD_DLT645, 100u, &storage);
    apps[0] = &dlt645.module;

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_riscv_virt_init(&event_sink);

    if (sys_meter_init(&sys, apps, 1u, required, 1u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 5;
    if (edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &dlt645.module) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_BOARD_TIMER0, &dlt645.module) < 0)
        return 6;
    if (edge_sys_start(&sys) < 0)
        return 4;

    board_riscv_virt_timer_init();

    for (unsigned i = 0u; i < 1000000u; ++i) {
        if (edge_sys_run_once(&sys) < 0)
            break;
        if (dlt645.last_event == EDGE_EVT_BOARD_TIMER0) {
            (void)edge_sys_power_off(&sys);
            (void)edge_sys_deinit(&sys);
            board_riscv_virt_exit(0);
        }
    }

    (void)edge_sys_power_off(&sys);
    (void)edge_sys_deinit(&sys);
    board_riscv_virt_exit(1);
}
