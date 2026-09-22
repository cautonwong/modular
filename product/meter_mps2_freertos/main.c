#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "meter/sys.h"
#include "mps2/board.h"
#include "pal_os/idle.h"
#include "pal_rtos/rtos.h"
#include "pal_rtos_freertos/pal_rtos_freertos.h"
#include "soc_mps2/freertos_config.h"

#include <stdint.h>

void product_meter_mps2_freertos_make_storage(dlt645_storage_if_t *out, void *flash_state);

/*
 * FreeRTOS-hosted product: the whole sys capsule runs as one task
 * (`capsule_task`), and its event is delivered by the real TIMER0 interrupt
 * through the guarded event sink - the same ISR path the bare-metal product
 * uses, with the FreeRTOS ISR bridge (mask save/restore) instead of a sibling
 * injecting task.
 *
 * Every object referenced after vTaskStartScheduler() has static storage
 * duration; the main stack frame is not ours once the scheduler runs.
 */
static edge_rtos_pal_state_t g_pal_state;
static edge_pal_port_t g_pal;
static edge_irq_guard_t g_irq_guard;
static edge_irq_guard_t g_sink_guard;

/*
 * The sink's guard is the composition root's adapter (D14): the PAL's ISR-side
 * mask save/restore, plus the RTOS wake so the runner stops polling. The wake is
 * issued here because this is the only place that knows an event was delivered;
 * the board's ISR stays RTOS-free (D85).
 */
static void sink_guard_enter(void *self) {
    (void)self;
    g_irq_guard.enter(g_irq_guard.self);
}

static void sink_guard_exit(void *self) {
    (void)self;
    g_irq_guard.exit(g_irq_guard.self);
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

static void os_yield_task(void) {
    if (g_os.yield != NULL)
        g_os.yield(g_os.self);
}

static void os_sleep_task(uint32_t ms) {
    if (g_os.sleep_ms != NULL)
        g_os.sleep_ms(g_os.self, ms);
}

/* The capsule's own period, in ticks: the runner must wake at least this often so
 * periodic work still runs. Fixed here because the composition root owns the
 * app's period (the same value is passed to dlt645_construct below). */
#define CAPSULE_PERIOD_TICKS 100u

/*
 * Lowest-priority witness (D47 yield policy). It only ever runs while the capsule
 * parks, so its progress is what proves the runner does not starve a
 * lower-priority task - an always-ready runner of higher priority would.
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
    for (;;) {
        if (edge_sys_run_once(&g_sys) < 0)
            break;
        if (g_dlt645.last_event == EDGE_EVT_BOARD_TIMER0)
            break;
        /*
         * Park until an event arrives or the current period elapses. This is the
         * power path: no spinning, and the CPU is the lower-priority task's while
         * we wait. The timeout is the app period, not a fixed tick, because every
         * wake has a fixed energy cost (docs/low-power.md section 1).
         */
        (void)edge_rtos_wait_for_work(CAPSULE_PERIOD_TICKS);
    }
    /*
     * Yield-policy probe (D47): over five ticks, the runner must yield by
     * *blocking*, never by a bare taskYIELD. Blocking hands the CPU to a
     * lower-priority task and stops burning current; a taskYIELD loop keeps the
     * CPU at the same priority and starves everything below. The witness is what
     * makes that difference observable, and swapping this call for os_yield_task()
     * is exactly the refutation (measured: rc=16).
     */
    const uint32_t witness_before = g_witness_ticks;
    /* Differential: progress *inside* the window, not accumulated earlier, or a
     * witness that ran once at startup would mask a starving loop.
     *
     * The window counts *blocking sleeps*, not kernel ticks, and that is the fix
     * for a real flake: this product has tickless sleep enabled (#90), so the
     * kernel tick advances in jumps once ticks were suppressed, and a
     * tick-difference window can be satisfied by one such jump during which the
     * lower-priority witness was never scheduled. Measured on an otherwise
     * unmodified build: rc=16 in about one run out of seven. The probe was
     * measuring the clock instead of the scheduling policy. */
    for (uint32_t i = 0u; i < 20u; ++i)
        os_sleep_task(1u); /* blocks, so the CPU is the witness's while we wait */
    if (g_witness_ticks == witness_before)
        board_mps2_exit(16); /* a lower-priority task was starved */
    (void)edge_sys_power_off(&g_sys);
    (void)dlt645_deinit(&g_dlt645);
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
    /* Static: `edge_sys_set_required` stores this pointer and
     * `edge_sys_validate_required()` dereferences it on every run, so its lifetime
     * has to be the program's - not a stack frame's. */
    static const uint32_t required[] = {EDGE_MOD_DLT645};

    edge_rtos_set_assert_hook(on_rtos_assert, NULL);

    /*
     * Architecture primitives first: the clock, the ISR guard and the sink all
     * derive from the PAL, and the ISR path is live from board_mps2_timer_init().
     */
    edge_rtos_pal_init(&g_pal_state);
    g_pal = edge_rtos_pal_port(&g_pal_state);
    g_clock = (edge_clock_port_t){
        .monotonic_ticks = g_pal.monotonic_ticks, .wall_time = NULL, .self = g_pal.self};
    g_irq_guard = edge_rtos_irq_guard();
    g_sink_guard =
        (edge_irq_guard_t){.enter = sink_guard_enter, .exit = sink_guard_exit, .self = NULL};
    g_os = edge_rtos_os_port();
    product_meter_mps2_freertos_make_storage(&g_storage, g_flash_state);
    dlt645_construct(&g_dlt645, EDGE_MOD_DLT645, 100u, &g_storage);
    if (dlt645_init(&g_dlt645) < 0)
        return 13; /* D51/D53: assembly-time init belongs to the composition root */
    g_apps[0] = dlt645_module(&g_dlt645);

    if (edge_event_queue_init(&g_queue, g_ev_storage, 16u) < 0)
        return 1;
    g_sink = (edge_event_sink_t){.queue = &g_queue, .clock = &g_clock, .guard = &g_sink_guard};
    board_mps2_init(&g_sink);

    if (sys_meter_init(&g_sys, g_apps, 1u, required, 1u, &g_queue, g_subs, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&g_sys, &g_clock) < 0)
        return 3;
    g_idle = (edge_os_idle_t){.os = &g_os, .sleep_ms = 1u};
    if (edge_sys_set_idle(&g_sys, edge_os_idle_hook, &g_idle) < 0)
        return 4;
    if (edge_sys_subscribe(&g_sys, EDGE_EVT_BOARD_TIMER0, dlt645_module(&g_dlt645)) < 0)
        return 5;
    if (edge_sys_start(&g_sys) < 0)
        return 6;

    /* Priorities follow the contract: 0 is highest, so the capsule's 1 beats the
     * witness's 2. (The port inverts them for FreeRTOS, whose convention is the
     * opposite.) */
    if (edge_rtos_task_create("witness", witness_task, NULL, 256u, 2u) < 0)
        return 8;
    if (edge_rtos_task_create("capsule", capsule_task, NULL, 512u, 1u) < 0)
        return 7;

    /*
     * Real IRQ from here on: the capsule's event comes from the TIMER0 handler
     * through the guarded sink. The IRQ priority must sit at or numerically below
     * the syscall ceiling before the IRQ is enabled - the reset value (0) is
     * above it, which would make the guard's FromISR path a violation.
     */
    board_mps2_timer_set_priority((uint8_t)configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    board_mps2_timer_init();

    edge_rtos_start();
    for (;;) {
    }
}
