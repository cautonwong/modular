#include <pthread.h>

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

#define MINIMAL_THREAD_EVENTS PARITY_EVENTS

static edge_event_queue_t g_queue;
static edge_event_sink_t g_sink;
static edge_sys_t g_sys;

static uint64_t g_clock_tick;

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

static void *producer(void *arg) {
    (void)arg;
    for (uint32_t i = 0u; i < MINIMAL_THREAD_EVENTS; ++i) {
        const edge_event_t event = {.id = PARITY_EVENT_ID, .arg0 = PARITY_EVENT_ARG(i)};
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
    const int init_status = dlt645_init(&app);
    if (init_status < 0)
        return 9; /* D51: the composition root initialises the module */
    apps[0] = dlt645_module(&app);

    if (edge_event_queue_init(&g_queue, event_storage, 256u) < 0)
        return 1;
    g_sink = (edge_event_sink_t){.queue = &g_queue, .clock = &clock, .guard = NULL};
    board_example_init(&g_sink);

    if (sys_example_init(&g_sys, apps, 1u, &g_queue, subscriptions, 2u) < 0)
        return 2;
    if (edge_sys_set_clock(&g_sys, &clock) < 0)
        return 3;
    if (edge_sys_subscribe(&g_sys, EDGE_EVT_UART0_RX, dlt645_module(&app)) < 0)
        return 4;
    if (edge_sys_start(&g_sys) < 0)
        return 5;

    if (pthread_create(&thread, NULL, producer, NULL) != 0)
        return 6;
    /* Progress is read from the capsule's own counters, not from a hook that would
     * replace the app's handler: the parity harness compares this runner with the
     * bare-metal one, so the app has to behave identically in both (#242). */
    for (;;) {
        edge_sys_stats_t stats = {0};
        (void)edge_sys_stats_get(&g_sys, &stats);
        if (stats.events >= MINIMAL_THREAD_EVENTS)
            break;
        if (edge_sys_run_once(&g_sys) < 0)
            return 7;
    }
    (void)pthread_join(thread, NULL);

    if (edge_event_dropped(&g_queue) != 0u)
        return 8;

    parity_emit(&g_sys, &g_queue, EDGE_MOD_DLT645, init_status, app.last_event);

    (void)edge_sys_power_off(&g_sys);
    (void)dlt645_deinit(&app);
    (void)edge_sys_deinit(&g_sys);
    return 0;
}
