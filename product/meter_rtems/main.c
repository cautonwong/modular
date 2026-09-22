#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "meter/sys.h"
#include "pal_os/idle.h"
#include "pal_rtos/rtos.h"
#include "pal_rtos_rtems/pal_rtos_rtems.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__rtems__)
#include <rtems.h>

#define CONFIGURE_INIT
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_MAXIMUM_TASKS 6
#define CONFIGURE_MAXIMUM_SEMAPHORES 4
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_STACK_SIZE (8 * 1024)
#define CONFIGURE_INIT_TASK_PRIORITY 10
#define CONFIGURE_UNIFIED_WORK_AREAS

#include <rtems/confdefs.h>
#endif

#include <stdint.h>

void product_meter_rtems_make_storage(dlt645_storage_if_t *out, void *flash_state);

static edge_rtos_pal_state_t g_pal_state;
static edge_pal_port_t g_pal;
static edge_irq_guard_t g_irq_guard;
static edge_irq_guard_t g_sink_guard;

static void sink_guard_enter(void *self) {
    (void)self;
    if (g_irq_guard.enter != NULL) {
        g_irq_guard.enter(g_irq_guard.self);
    }
}

static void sink_guard_exit(void *self) {
    (void)self;
    if (g_irq_guard.exit != NULL) {
        g_irq_guard.exit(g_irq_guard.self);
    }
    edge_rtos_wake_from_isr();
}

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

static void os_sleep_task(uint32_t ms) {
    if (g_os.sleep_ms != NULL) {
        g_os.sleep_ms(g_os.self, ms);
    }
}

#define CAPSULE_PERIOD_TICKS 100u

static volatile uint32_t g_witness_ticks;

static void witness_task(void *arg) {
    (void)arg;
    for (;;) {
        ++g_witness_ticks;
        os_sleep_task(1u);
    }
}

static void capsule_task(void *arg) {
    (void)arg;
    edge_rtos_wake_target_set_self();

    /* Simulate periodic loop and event handling */
    for (uint32_t i = 0u; i < 5u; ++i) {
        const edge_event_t tick_ev = {.id = EDGE_EVT_BOARD_TIMER0};
        (void)edge_event_sink_push_isr(&g_sink, &tick_ev);
        (void)edge_sys_run_once(&g_sys);
        (void)edge_rtos_wait_for_work(CAPSULE_PERIOD_TICKS);
    }

    /* Starvation probe (D47 yield policy) */
    const uint32_t witness_before = g_witness_ticks;
    for (uint32_t i = 0u; i < 20u; ++i) {
        os_sleep_task(1u);
    }

    if (g_witness_ticks == witness_before) {
        printf("FAIL: Witness task was starved!\n");
        exit(16);
    }
    printf("PASS: RTEMS modular meter ran successfully with witness ticks=%u\n",
           (unsigned int)g_witness_ticks);

    (void)edge_sys_power_off(&g_sys);
    (void)dlt645_deinit(&g_dlt645);
    (void)edge_sys_deinit(&g_sys);

    exit(0);
}

static int app_main(void) {
    static const uint32_t required[] = {EDGE_MOD_DLT645};

    edge_rtos_pal_init(&g_pal_state);
    g_pal = edge_rtos_pal_port(&g_pal_state);
    g_clock = (edge_clock_port_t){
        .monotonic_ticks = g_pal.monotonic_ticks, .wall_time = NULL, .self = g_pal.self};

    g_irq_guard = edge_rtos_irq_guard();
    g_sink_guard =
        (edge_irq_guard_t){.enter = sink_guard_enter, .exit = sink_guard_exit, .self = NULL};
    g_os = edge_rtos_os_port();

    product_meter_rtems_make_storage(&g_storage, g_flash_state);
    dlt645_construct(&g_dlt645, EDGE_MOD_DLT645, 100u, &g_storage);
    if (dlt645_init(&g_dlt645) < 0) {
        return 13;
    }
    g_apps[0] = dlt645_module(&g_dlt645);

    if (edge_event_queue_init(&g_queue, g_ev_storage, 16u) < 0) {
        return 1;
    }
    g_sink = (edge_event_sink_t){.queue = &g_queue, .clock = &g_clock, .guard = &g_sink_guard};

    if (sys_meter_init(&g_sys, g_apps, 1u, required, 1u, &g_queue, g_subs, 4u) < 0) {
        return 2;
    }
    if (edge_sys_set_clock(&g_sys, &g_clock) < 0) {
        return 3;
    }
    g_idle = (edge_os_idle_t){.os = &g_os, .sleep_ms = 1u};
    if (edge_sys_set_idle(&g_sys, edge_os_idle_hook, &g_idle) < 0) {
        return 4;
    }
    if (edge_sys_subscribe(&g_sys, EDGE_EVT_BOARD_TIMER0, dlt645_module(&g_dlt645)) < 0) {
        return 5;
    }
    if (edge_sys_start(&g_sys) < 0) {
        return 6;
    }

    /* Create witness (priority 2) and capsule runner (priority 1) */
    if (edge_rtos_task_create("witn", witness_task, NULL, 512u, 2u) < 0) {
        return 8;
    }
    if (edge_rtos_task_create("caps", capsule_task, NULL, 1024u, 1u) < 0) {
        return 7;
    }

    edge_rtos_start();

    return 0;
}

#if defined(__rtems__)
rtems_task Init(rtems_task_argument argument) {
    (void)argument;
    (void)app_main();
    rtems_task_delete(RTEMS_SELF);
}
#else
int main(void) {
    return app_main();
}
#endif
