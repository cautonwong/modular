#include "gateway.h"

#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "meter/sys.h"
#include "meter_core/meter_core.h"
#include "modbus_slave/modbus_slave.h"

#include <stdint.h>

void product_meter_gateway_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_gateway_host_make_modbus(modbus_store_if_t *store,
                                            modbus_transport_if_t *transport,
                                            gateway_state_t *state);

/*
 * Dual-protocol product. Three apps share one runner:
 *  - `meter_core` (provider) owns the register/coil data;
 *  - `modbus_slave` (consumer) reads/writes it through its own `modbus_store_if`,
 *    adapted in glue.c;
 *  - `dlt645` (metering protocol) runs independently.
 * The frame below is a precomputed RTU request (unit 1, 0x03, start 0, qty 1).
 */
static const uint8_t g_modbus_request[8] = {0x01u, 0x03u, 0x00u, 0x00u, 0x00u, 0x01u, 0x84u, 0x0Au};

static gateway_state_t g_state;
static uint64_t g_clock_tick;
static dlt645_t g_dlt645;
static modbus_slave_t g_modbus;

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

int main(void) {
    dlt645_storage_if_t storage;
    modbus_store_if_t store;
    modbus_transport_if_t transport;
    edge_module_t *apps[3];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {
        .monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = &g_clock_tick};
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;
    const uint32_t required[] = {EDGE_MOD_DLT645, EDGE_MOD_METER, EDGE_MOD_MODBUS};

    product_meter_gateway_host_make_storage(&storage, g_state.flash);
    product_meter_gateway_host_make_modbus(&store, &transport, &g_state);
    dlt645_construct(&g_dlt645, EDGE_MOD_DLT645, 100u, &storage);
    meter_core_construct(&g_state.meter, EDGE_MOD_METER, 90u);
    modbus_slave_construct(&g_modbus, EDGE_MOD_MODBUS, 110u, 1u, &store, &transport);
    apps[0] = &g_dlt645.module;
    apps[1] = &g_state.meter.module;
    apps[2] = &g_modbus.module;

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_example_init(&event_sink);

    if (sys_meter_init(&sys, apps, 3u, required, 3u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_DLT645_RX, &g_dlt645.module) < 0)
        return 4;
    if (edge_sys_subscribe(&sys, EDGE_EVT_MODBUS_RX, &g_modbus.module) < 0)
        return 5;
    if (edge_sys_start(&sys) < 0)
        return 6;

    for (unsigned step = 0u; step < 20u; ++step) {
        if (edge_sys_run_once(&sys) < 0)
            return 7;
    }

    /* App-to-app proof: publish a value in the provider, then let the consumer
     * read it through the composition-root adapter and answer on its transport. */
    if (meter_core_write_register(&g_state.meter, 0u, 0x1234u) != EDGE_OK)
        return 8;
    if (modbus_slave_feed(&g_modbus, g_modbus_request, sizeof(g_modbus_request)) != EDGE_OK)
        return 9;
    if (g_state.uart_writes == 0u || g_modbus.responses == 0u)
        return 10;
    if (g_state.uart_tx[3] != 0x12u || g_state.uart_tx[4] != 0x34u)
        return 11;

    (void)edge_sys_power_off(&sys);
    (void)edge_sys_deinit(&sys);
    return 0;
}
