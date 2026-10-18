#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "glue.h"
#include "meter/sys.h"
#include "mps2/board.h"
#include "pal_cortex_m/pal_cortex_m.h"
#include "pal_eos/eos.h"
#include "pal_os/idle.h"
#include "pal_rtos/rtos.h"

#include <stdint.h>

static edge_pal_cortex_m_state_t g_pal_state;
static edge_pal_port_t g_pal;
static edge_clock_port_t g_clock;
static edge_irq_guard_t g_guard;
static edge_event_sink_t g_sink;
static uint8_t g_flash_state[64];
static edge_event_t g_ev_storage[16];
static edge_event_queue_t g_queue;
static edge_sys_subscription_t g_subs[4];
static dlt645_storage_if_t g_storage;
static dlt645_t g_dlt645;
static edge_sys_t g_sys;
static edge_module_t *g_apps[1];
static edge_eos_t g_eos;
static uint32_t g_rounds;

typedef struct low_power_ctx {
    const edge_pal_port_t *pal;
    edge_sys_t *sys;
} low_power_ctx_t;

static low_power_ctx_t g_low_power;

static bool capsule_pending(void *ctx) {
    return edge_sys_pending(((low_power_ctx_t *)ctx)->sys);
}

static void capsule_idle(void *ctx) {
    low_power_ctx_t *low_power = (low_power_ctx_t *)ctx;
    if (edge_sys_healthy(low_power->sys))
        board_mps2_feed_watchdog();
    board_mps2_enter_low_power();
    edge_os_idle_wait(low_power->pal, capsule_pending, low_power->sys);
}

static void capsule_task(void *arg) {
    (void)arg;
    (void)edge_sys_run_once(&g_sys);
#ifdef EDGE_QEMU_SEMIHOSTING
    if (g_dlt645.last_event == EDGE_EVT_BOARD_TIMER0) {
        (void)edge_sys_power_off(&g_sys);
        (void)dlt645_deinit(&g_dlt645);
        (void)edge_sys_deinit(&g_sys);
        edge_eos_stop(&g_eos);
        board_mps2_exit(0);
    }
    if (++g_rounds > 100000u) {
        (void)edge_sys_power_off(&g_sys);
        (void)dlt645_deinit(&g_dlt645);
        (void)edge_sys_deinit(&g_sys);
        edge_eos_stop(&g_eos);
        board_mps2_exit(8);
    }
#else
    if (++g_rounds >= 10u) {
        (void)edge_sys_power_off(&g_sys);
        (void)dlt645_deinit(&g_dlt645);
        (void)edge_sys_deinit(&g_sys);
        edge_eos_stop(&g_eos);
    }
#endif
}

int main(void) {
    static const uint32_t required[] = {EDGE_MOD_DLT645};

    edge_pal_cortex_m_bare_init(&g_pal_state);
    g_pal = edge_pal_cortex_m_bare_port(&g_pal_state);
    g_clock = (edge_clock_port_t){
        .monotonic_ticks = g_pal.monotonic_ticks, .wall_time = NULL, .self = g_pal.self};
    g_guard = (edge_irq_guard_t){
        .enter = g_pal.critical_enter, .exit = g_pal.critical_exit, .self = g_pal.self};
    if (edge_event_queue_init(&g_queue, g_ev_storage, 16u) < 0)
        return 1;
    g_sink = (edge_event_sink_t){.queue = &g_queue, .clock = &g_clock, .guard = &g_guard};
    board_mps2_init(&g_sink);

    product_meter_mps2_eos_make_storage(&g_storage, g_flash_state);
    dlt645_construct(&g_dlt645, EDGE_MOD_DLT645, 100u, &g_storage);
    if (dlt645_init(&g_dlt645) < 0)
        return 13;
    g_apps[0] = dlt645_module(&g_dlt645);

    if (sys_meter_init(&g_sys, g_apps, 1u, required, 1u, &g_queue, g_subs, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&g_sys, &g_clock) < 0)
        return 3;
    g_low_power = (low_power_ctx_t){.pal = &g_pal, .sys = &g_sys};
    if (edge_sys_set_idle(&g_sys, capsule_idle, &g_low_power) < 0)
        return 4;
#ifdef EDGE_QEMU_SEMIHOSTING
    if (edge_sys_subscribe(&g_sys, EDGE_EVT_BOARD_TIMER0, dlt645_module(&g_dlt645)) < 0)
        return 5;
#endif
    if (edge_sys_start(&g_sys) < 0)
        return 6;

    if (edge_eos_init(&g_eos, &g_clock) < 0)
        return 7;
    if (edge_eos_bind_rtos(&g_eos) < 0)
        return 8;

    if (edge_rtos_task_create("capsule", capsule_task, NULL, 256u, 0u) < 0)
        return 9;

#ifdef EDGE_QEMU_SEMIHOSTING
    board_mps2_timer_init();
#endif

    edge_rtos_start();

    return 0;
}
