#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "meter/sys.h"
#include "mps2/board.h"
#include "pal_os/idle.h"
#include "pal_rtos/rtos.h"

#include <stdint.h>

void product_meter_mps2_freertos_make_storage(dlt645_storage_if_t *out, void *flash_state);

/*
 * FreeRTOS-hosted product: the whole sys capsule runs as one task
 * (`capsule_task`) and a sibling task injects the event through the same sink.
 *
 * Every object referenced after vTaskStartScheduler() has static storage
 * duration; the main stack frame is not ours once the scheduler runs.
 */
static uint64_t g_clock_tick;
static uint8_t g_flash_state[64];
static edge_event_t g_ev_storage[16];
static edge_event_queue_t g_queue;
static edge_sys_subscription_t g_subs[4];
static dlt645_storage_if_t g_storage;
static dlt645_t g_dlt645;
static edge_sys_t g_sys;
static edge_event_sink_t g_sink;
static edge_os_idle_t g_idle;
static edge_clock_port_t g_clock;
static edge_module_t *g_apps[1];
static edge_os_port_t g_os;

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

static void os_yield_task(void) {
    if (g_os.yield != NULL)
        g_os.yield(g_os.self);
}

static void os_sleep_task(uint32_t ms) {
    if (g_os.sleep_ms != NULL)
        g_os.sleep_ms(g_os.self, ms);
}

static void injector_task(void *arg) {
    (void)arg;
    os_sleep_task(20u);
    const edge_event_t event = {.id = EDGE_EVT_BOARD_TIMER0, .source = 9u};
    (void)edge_event_sink_push_isr(&g_sink, &event);
    for (;;) {
        os_sleep_task(1000u);
    }
}

static void capsule_task(void *arg) {
    (void)arg;
    for (;;) {
        if (edge_sys_run_once(&g_sys) < 0)
            break;
        if (g_dlt645.last_event == EDGE_EVT_BOARD_TIMER0)
            break;
        os_yield_task();
    }
    (void)edge_sys_power_off(&g_sys);
    (void)edge_sys_deinit(&g_sys);
    if (edge_rtos_task_stack_high_water() < 64u)
        board_mps2_exit(12); /* capsule stack nearly exhausted */
    board_mps2_exit(0);
}

/* Observable RTOS assert reaction (D87): a failed assert becomes a distinct
 * run-time-error exit instead of a silent no-op. */
static void on_rtos_assert(void *ctx, const char *file, int line) {
    (void)ctx;
    (void)file;
    (void)line;
    board_mps2_exit(9);
}

int main(void) {
    const uint32_t required[] = {EDGE_MOD_DLT645};

    edge_rtos_set_assert_hook(on_rtos_assert, NULL);

    g_clock = (edge_clock_port_t){
        .monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = &g_clock_tick};
    g_os = edge_rtos_os_port();
    product_meter_mps2_freertos_make_storage(&g_storage, g_flash_state);
    dlt645_construct(&g_dlt645, EDGE_MOD_DLT645, 100u, &g_storage);
    g_apps[0] = &g_dlt645.module;

    if (edge_event_queue_init(&g_queue, g_ev_storage, 16u) < 0)
        return 1;
    g_sink = (edge_event_sink_t){.queue = &g_queue, .clock = &g_clock, .guard = NULL};
    board_mps2_init(&g_sink);

    if (sys_meter_init(&g_sys, g_apps, 1u, required, 1u, &g_queue, g_subs, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&g_sys, &g_clock) < 0)
        return 3;
    g_idle = (edge_os_idle_t){.os = &g_os, .sleep_ms = 1u};
    if (edge_sys_set_idle(&g_sys, edge_os_idle_hook, &g_idle) < 0)
        return 4;
    if (edge_sys_subscribe(&g_sys, EDGE_EVT_BOARD_TIMER0, &g_dlt645.module) < 0)
        return 5;
    if (edge_sys_start(&g_sys) < 0)
        return 6;

    if (edge_rtos_task_create("capsule", capsule_task, NULL, 512u, 2u) < 0)
        return 7;
    if (edge_rtos_task_create("inject", injector_task, NULL, 256u, 3u) < 0)
        return 8;

    edge_rtos_start();
    for (;;) {
    }
}
