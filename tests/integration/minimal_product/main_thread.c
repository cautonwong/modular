#include <pthread.h>

#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "example/sys.h"

#include <stdint.h>

void minimal_product_make_storage(dlt645_storage_if_t *out, void *flash_state);

#define MINIMAL_THREAD_EVENTS 200u

static edge_event_queue_t g_queue;
static edge_event_sink_t g_sink;
static edge_sys_t g_sys;
static int g_handled;

static uint64_t g_clock_tick;

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

static edge_status_t app_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    ++g_handled;
    return EDGE_OK;
}

static void *producer(void *arg) {
    (void)arg;
    for (uint32_t i = 0u; i < MINIMAL_THREAD_EVENTS; ++i) {
        const edge_event_t event = {.id = EDGE_EVT_UART0_RX, .arg0 = i};
        while (edge_event_sink_push_isr(&g_sink, &event) == EDGE_EOVERFLOW) {
        }
    }
    return NULL;
}

/*
 * Neutrality fixture: the RTOS-like runner on the host. The runner step runs in
 * one thread while another thread injects events through the same sink, exactly
 * as the FreeRTOS task does on target. The app is unchanged.
 */
int main(void) {
    uint8_t flash_state[64] = {0};
    dlt645_storage_if_t storage;
    dlt645_t app;
    edge_module_t *apps[1];
    edge_event_t event_storage[256];
    edge_clock_port_t clock = {
        .monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = &g_clock_tick};
    edge_sys_subscription_t subscriptions[2];
    pthread_t thread;

    minimal_product_make_storage(&storage, flash_state);
    dlt645_construct(&app, EDGE_MOD_DLT645, 100u, &storage);
    app.module.period = 0u;
    app.module.on_event = app_on_event;
    apps[0] = &app.module;

    if (edge_event_queue_init(&g_queue, event_storage, 256u) < 0)
        return 1;
    g_sink = (edge_event_sink_t){.queue = &g_queue, .clock = &clock, .guard = NULL};
    board_example_init(&g_sink);

    if (sys_example_init(&g_sys, apps, 1u, &g_queue, subscriptions, 2u) < 0)
        return 2;
    if (edge_sys_set_clock(&g_sys, &clock) < 0)
        return 3;
    if (edge_sys_subscribe(&g_sys, EDGE_EVT_UART0_RX, &app.module) < 0)
        return 4;
    if (edge_sys_start(&g_sys) < 0)
        return 5;

    if (pthread_create(&thread, NULL, producer, NULL) != 0)
        return 6;
    while (g_handled < (int)MINIMAL_THREAD_EVENTS) {
        if (edge_sys_run_once(&g_sys) < 0)
            return 7;
    }
    (void)pthread_join(thread, NULL);

    if (edge_event_dropped(&g_queue) != 0u)
        return 8;

    (void)edge_sys_power_off(&g_sys);
    (void)edge_sys_deinit(&g_sys);
    return 0;
}
