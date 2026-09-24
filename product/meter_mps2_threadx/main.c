/*
 * ThreadX-hosted Cortex-M4 meter firmware for the MPS2 AN386 board.
 *
 * The shape differs from the other firmware products in one way, and it is a measured
 * one: ThreadX's clock cannot be read before `tx_kernel_enter()` has run
 * `_tx_initialize_low_level()` (on the host port that read blocks on a mutex nothing has
 * created yet), so everything the framework needs is initialised from the runner thread
 * rather than from `main()`. `main()` creates the tasks and enters the kernel.
 */
#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "meter/sys.h"
#include "mps2/board.h"
#include "pal_os/idle.h"
#include "pal_rtos/rtos.h"
#include "pal_rtos_threadx/pal_rtos_threadx.h"

#include <stdint.h>

void product_meter_mps2_threadx_make_storage(dlt645_storage_if_t *out, void *flash_state);

static edge_rtos_pal_state_t g_pal_state;
static edge_pal_port_t g_pal;
static edge_irq_guard_t g_irq_guard;
static edge_irq_guard_t g_sink_guard;
static edge_os_port_t g_os;
static edge_clock_port_t g_clock;
static edge_event_sink_t g_sink;
static edge_event_queue_t g_queue;
static edge_event_t g_ev_storage[16];
static edge_sys_t g_sys;
static edge_sys_subscription_t g_subs[4];
static edge_module_t *g_apps[1];
static dlt645_t g_dlt645;
static dlt645_storage_if_t g_storage;
static uint8_t g_flash_state[64];
static edge_os_idle_t g_idle;

/* Static: the framework borrows this pointer, so its lifetime is the program's. */
static const uint32_t g_required[] = {EDGE_MOD_DLT645};

static void sink_guard_enter(void *self) {
    (void)self;
    if (g_irq_guard.enter != NULL)
        g_irq_guard.enter(g_irq_guard.self);
}

static void sink_guard_exit(void *self) {
    (void)self;
    if (g_irq_guard.exit != NULL)
        g_irq_guard.exit(g_irq_guard.self);
    /* The wake is part of the sink contract: an ISR that has just made work available
     * must not leave the runner parked. */
    edge_rtos_wake_from_isr();
}

static void os_sleep_task(uint32_t ms) {
    if (g_os.sleep_ms != NULL)
        g_os.sleep_ms(g_os.self, ms);
}

#define CAPSULE_PERIOD_TICKS 100u

/*
 * Lowest-priority witness (D47 yield policy). It only ever runs while the capsule
 * parks, so its progress is what proves the runner does not starve it.
 */
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
    edge_rtos_wake_target_set_self(); /* the ISR wakes this task from now on */

    /* The handler uses a kernel service (the wake), so its priority goes first. */
    board_mps2_timer_set_priority(5u);

    product_meter_mps2_threadx_make_storage(&g_storage, g_flash_state);
    dlt645_construct(&g_dlt645, EDGE_MOD_DLT645, 100u, &g_storage);
    if (dlt645_init(&g_dlt645) < 0)
        board_mps2_exit(13);
    g_apps[0] = dlt645_module(&g_dlt645);

    if (edge_event_queue_init(&g_queue, g_ev_storage, 16u) < 0)
        board_mps2_exit(1);
    g_sink = (edge_event_sink_t){.queue = &g_queue, .clock = &g_clock, .guard = &g_sink_guard};
    board_mps2_init(&g_sink); /* attach the timer consumer */
    board_mps2_timer_init();  /* and only now let the timer fire */

    if (sys_meter_init(&g_sys, g_apps, 1u, g_required, 1u, &g_queue, g_subs, 4u) < 0)
        board_mps2_exit(2);
    if (edge_sys_set_clock(&g_sys, &g_clock) < 0)
        board_mps2_exit(3);
    g_idle = (edge_os_idle_t){.os = &g_os, .sleep_ms = 1u};
    if (edge_sys_set_idle(&g_sys, edge_os_idle_hook, &g_idle) < 0)
        board_mps2_exit(4);
    if (edge_sys_subscribe(&g_sys, EDGE_EVT_BOARD_TIMER0, dlt645_module(&g_dlt645)) < 0)
        board_mps2_exit(5);
    if (edge_sys_start(&g_sys) < 0)
        board_mps2_exit(6);

    for (;;) {
        if (edge_sys_run_once(&g_sys) < 0)
            break;
        if (g_dlt645.last_event == EDGE_EVT_BOARD_TIMER0)
            break;
        (void)edge_rtos_wait_for_work(CAPSULE_PERIOD_TICKS);
    }

    /* Yield-policy probe (D47): the runner must yield by blocking, not by a bare
     * reschedule, and the witness's progress inside the window is what makes the
     * difference observable (measured: rc=16 when it spins). */
    const uint32_t witness_before = g_witness_ticks;
    for (uint32_t i = 0u; i < 20u; ++i)
        os_sleep_task(1u);
    if (g_witness_ticks == witness_before)
        board_mps2_exit(16);

    (void)edge_sys_power_off(&g_sys);
    (void)dlt645_deinit(&g_dlt645);
    (void)edge_sys_deinit(&g_sys);

    /* No stack check here: ThreadX exposes no high-water mark through this port, and the
     * contract defines 0 as unavailable rather than as "plenty" - so the honest thing is
     * to skip the check, not to pass it vacuously. */
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
    edge_rtos_set_assert_hook(on_rtos_assert, NULL);

    /* Architecture primitives first: the clock, the ISR guard and the sink all derive
     * from the PAL. Nothing here touches the kernel - it is not running yet. */
    edge_rtos_pal_init(&g_pal_state);
    g_pal = edge_rtos_pal_port(&g_pal_state);
    g_clock = (edge_clock_port_t){
        .monotonic_ticks = g_pal.monotonic_ticks, .wall_time = NULL, .self = g_pal.self};
    g_irq_guard = edge_rtos_irq_guard();
    g_sink_guard =
        (edge_irq_guard_t){.enter = sink_guard_enter, .exit = sink_guard_exit, .self = NULL};
    g_os = edge_rtos_os_port();

    /* Sizes are in words, as the contract says. Witness at priority 2, capsule at 1:
     * 0 is highest in both the contract and ThreadX, so the capsule outranks it. */
    if (edge_rtos_task_create("witn", witness_task, NULL, 256u, 2u) < 0)
        return 8;
    if (edge_rtos_task_create("caps", capsule_task, NULL, 512u, 1u) < 0)
        return 7;

    edge_rtos_start(); /* enters the ThreadX scheduler and never returns */

    return 0;
}
