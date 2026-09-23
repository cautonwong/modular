#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "edge/pal.h"
#include "example/board.h"
#include "meter/sys.h"
#include "pal_os/power.h"
#include "pulse_meter/pulse_meter.h"

#include <stdint.h>
#include <stdio.h>

void product_water_meter_host_make_storage(pulse_meter_storage_t *out, void *state_buf);

static uint64_t g_clock_tick = 0u;
static uint64_t monotonic_ticks(void *self) {
    (void)self;
    return ++g_clock_tick;
}

typedef struct water_idle_ctx {
    edge_sys_t *sys;
    edge_pm_state_t *pm;
    edge_pal_port_t pal;
    uint32_t stop_mode_entries;
    uint32_t idle_mode_entries;
} water_idle_ctx_t;

static bool water_pending(void *ctx) {
    return edge_sys_pending(((water_idle_ctx_t *)ctx)->sys);
}

static edge_status_t board_pm_handler(edge_pm_mode_t mode, uint64_t idle_ticks, void *ctx) {
    water_idle_ctx_t *ictx = (water_idle_ctx_t *)ctx;
    if (mode == EDGE_PM_STOP || mode == EDGE_PM_STANDBY) {
        ++ictx->stop_mode_entries;
        board_example_enter_low_power();
    } else if (mode == EDGE_PM_IDLE) {
        ++ictx->idle_mode_entries;
    }
    (void)idle_ticks;
    return EDGE_OK;
}

static void water_meter_idle_hook(void *ctx) {
    water_idle_ctx_t *ictx = (water_idle_ctx_t *)ctx;
    uint64_t next_due = 0u;
    const uint64_t now = g_clock_tick;

    if (edge_sys_next_due(ictx->sys, now, &next_due) != EDGE_OK)
        return;

    uint64_t idle_ticks = 0u;
    if (next_due == UINT64_MAX) {
        idle_ticks = 100000u; /* Purely interrupt-driven sleep */
    } else if (next_due > now) {
        idle_ticks = next_due - now;
    }

    (void)edge_pm_execute(ictx->pm, idle_ticks, &ictx->pal, board_pm_handler, ictx, water_pending,
                          ictx);
}

int main(void) {
    uint8_t storage_buf[128] = {0};
    pulse_meter_storage_t storage;
    pulse_meter_t pulse_app;
    edge_module_t *apps[1];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {.monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = NULL};
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;
    edge_pm_state_t pm;
    water_idle_ctx_t idle_ctx;
    static const uint32_t required[] = {EDGE_MOD_PULSE_METER};

    product_water_meter_host_make_storage(&storage, storage_buf);

    pulse_meter_config_t config = {
        .pulses_per_unit = 100u,
        .debounce_ticks = 2u,
        .low_battery_mv = 2700u,
    };

    pulse_meter_construct(&pulse_app, EDGE_MOD_PULSE_METER, 1u, &config, &storage, NULL);
    if (pulse_meter_init(&pulse_app) < 0)
        return 10;
    apps[0] = pulse_meter_module(&pulse_app);

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_example_init(&event_sink);

    if (sys_meter_init(&sys, apps, 1u, required, 1u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 5;
    if (edge_sys_subscribe(&sys, EDGE_EVT_PULSE_COUNT, &pulse_app.module) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_TAMPER_DETECTED, &pulse_app.module) < 0)
        return 6;

    if (edge_pm_init(&pm, 100u, 10000u) < 0)
        return 7;

    idle_ctx = (water_idle_ctx_t){
        .sys = &sys,
        .pm = &pm,
        .pal = (edge_pal_port_t){0},
        .stop_mode_entries = 0u,
        .idle_mode_entries = 0u,
    };
    if (edge_sys_set_idle(&sys, water_meter_idle_hook, &idle_ctx) < 0)
        return 8;

    if (edge_sys_start(&sys) < 0)
        return 4;

    /* Simulation loop */
    for (unsigned i = 0u; i < 50u; ++i) {
        if (edge_sys_run_once(&sys) < 0)
            break;
        /* Inject a water pulse at step 10, 20, 30 */
        if (i == 10u || i == 20u || i == 30u) {
            const edge_event_t pulse_evt = {
                .id = EDGE_EVT_PULSE_COUNT,
                .source = EDGE_MOD_PULSE_METER,
                .arg0 = 0u,
                .arg1 = 0u,
                .timestamp = g_clock_tick,
            };
            (void)edge_event_sink_push_isr(&event_sink, &pulse_evt);
        }
    }

    if (pulse_meter_total_pulses(&pulse_app) != 3u) {
        printf("FAIL: Expected 3 pulses, got %llu\n",
               (unsigned long long)pulse_meter_total_pulses(&pulse_app));
        return 11;
    }

    if (idle_ctx.stop_mode_entries == 0u) {
        printf("FAIL: Expected deep stop mode entries during idle periods\n");
        return 12;
    }

    (void)edge_sys_power_off(&sys);
    (void)edge_sys_deinit(&sys);
    printf("PASS: Water meter host tickless power management ran successfully (pulses=%llu, "
           "stop_entries=%u)\n",
           (unsigned long long)pulse_meter_total_pulses(&pulse_app), idle_ctx.stop_mode_entries);
    return 0;
}
