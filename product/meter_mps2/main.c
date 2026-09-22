#include "dlt645/dlt645.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "meter/sys.h"
#include "mps2/board.h"
#include "pal_cortex_m/pal_cortex_m.h"
#include "pal_os/idle.h"
#include "relay/relay.h"

#include <stdint.h>

void product_meter_mps2_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_mps2_make_relay_out(relay_out_if_t *out, void *gpio_state);

static edge_pal_cortex_m_state_t g_pal_state;

/*
 * Low-power idle path (D9/D52/D71): feed the watchdog only while healthy, do the
 * board's low-power action, then the atomic critical/re-check/WFI/release wait.
 */
typedef struct low_power_ctx {
    const edge_pal_port_t *pal;
    edge_sys_t *sys;
} low_power_ctx_t;

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

int main(void) {
    uint8_t flash_state[64] = {0};
    uint8_t gpio_state[8] = {0};
    dlt645_storage_if_t storage;
    relay_out_if_t relay_out;
    dlt645_t dlt645;
    relay_t relay;
    edge_module_t *apps[2];
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock;
    edge_irq_guard_t guard;
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[4];
    edge_sys_t sys;
    /* Static: `edge_sys_set_required` stores this pointer and
     * `edge_sys_validate_required()` dereferences it on every run, so its lifetime
     * has to be the program's - not a stack frame's. */
    static const uint32_t required[] = {EDGE_MOD_DLT645, EDGE_MOD_RELAY};

    product_meter_mps2_make_storage(&storage, flash_state);
    product_meter_mps2_make_relay_out(&relay_out, gpio_state);
    dlt645_construct(&dlt645, EDGE_MOD_DLT645, 100u, &storage);
    relay_construct(&relay, EDGE_MOD_RELAY, 110u, &relay_out);
    if (dlt645_init(&dlt645) < 0 || relay_init(&relay) < 0)
        return 10; /* D51/D53: assembly-time init belongs to the composition root */
    apps[0] = dlt645_module(&dlt645);
    apps[1] = relay_module(&relay);

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0)
        return 1;
    edge_pal_cortex_m_bare_init(&g_pal_state);
    const edge_pal_port_t pal = edge_pal_cortex_m_bare_port(&g_pal_state);

#ifdef EDGE_QEMU_SEMIHOSTING
    /*
     * Two cheap checks, and deliberately not a third.
     *
     * (1) A short burst of real reads must never go backwards. Real work happens
     *     between samples because under QEMU the virtual clock advances with
     *     executed instructions, so a tight loop of MMIO reads can sample one value
     *     repeatedly and would test the harness instead of the port.
     *
     * (2) The 64-bit extension arithmetic across a wrap, driven synthetically. This
     *     is deterministic - it does not hope that a wrap falls inside a sampling
     *     window - and it runs the *target* build of the arithmetic instead of the
     *     host one. The state is public, so no hardware is touched and the live
     *     clock is left alone.
     *
     * What is deliberately NOT checked here: that the counter is running. An
     * earlier version tried, with a 400k-sample window: locally it took ~1 s, on a
     * shared CI runner it exceeded the smoke's 10 s budget and the firmware was
     * killed (rc=137), so a correctness check became a timeout; and a shorter
     * window merely reported "never advanced", because QEMU's virtual clock does
     * not reliably advance over a small burst at all. "Is the time source alive" is
     * a hardware question with a hardware answer (HIL, D63), not something a 10 s
     * emulated budget can settle. The deterministic coverage for this arithmetic
     * lives in tests/test_pal_cortex_m.c.
     */
    uint64_t previous = pal.monotonic_ticks(pal.self);
    for (uint32_t i = 0u; i < 256u; ++i) {
        for (volatile uint32_t spin = 0u; spin < 32u; ++spin) {
        }
        const uint64_t next = pal.monotonic_ticks(pal.self);
        if (next < previous)
            return 14; /* the clock went backwards */
        previous = next;
    }

    {
        edge_pal_cortex_m_state_t probe = {0};
        probe.load = g_pal_state.load; /* the period the live PAL cached */
        probe.last = g_pal_state.load;
        /* From `load - 5` the counter runs 5 counts to zero, wraps (+1) and reloads,
         * so the timeline must advance by exactly load - 4 counts and must never
         * fall back. Asserting the exact delta is stronger than asserting progress:
         * it pins that the wrap was counted once, which is the defect this guards. */
        const uint64_t before = edge_pal_cortex_m_extend(&probe, probe.load - 5u);
        const uint64_t after = edge_pal_cortex_m_extend(&probe, probe.load);
        if (before != 5u || after <= before || (after - before) != (uint64_t)probe.load - 4u)
            return 16; /* a crossed wrap did not advance the timeline exactly once */
        if (probe.wrap != 1u)
            return 16;
    }
#endif

    clock = (edge_clock_port_t){
        .monotonic_ticks = pal.monotonic_ticks, .wall_time = NULL, .self = pal.self};
    guard = (edge_irq_guard_t){
        .enter = pal.critical_enter, .exit = pal.critical_exit, .self = pal.self};
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = &guard};
    board_mps2_init(&event_sink);

    if (sys_meter_init(&sys, apps, 2u, required, 2u, &event_queue, subscriptions, 4u) < 0)
        return 2;
    if (edge_sys_set_clock(&sys, &clock) < 0)
        return 5;
    low_power_ctx_t low_power = {.pal = &pal, .sys = &sys};
    if (edge_sys_set_idle(&sys, capsule_idle, &low_power) < 0)
        return 13;
    if (edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, dlt645_module(&dlt645)) < 0)
        return 3;
    if (edge_sys_subscribe(&sys, EDGE_EVT_RELAY_CHANGED, relay_module(&relay)) < 0)
        return 6;
#ifdef EDGE_QEMU_SEMIHOSTING
    if (edge_sys_subscribe(&sys, EDGE_EVT_BOARD_TIMER0, dlt645_module(&dlt645)) < 0)
        return 7;
#endif
    if (edge_sys_start(&sys) < 0)
        return 4;

#ifdef EDGE_QEMU_SEMIHOSTING
    board_mps2_timer_init();
#endif

    for (unsigned i = 0u; i < 100000u; ++i) {
        if (edge_sys_run_once(&sys) < 0)
            break;
#ifdef EDGE_QEMU_SEMIHOSTING
        if (dlt645.last_event == EDGE_EVT_BOARD_TIMER0) {
            (void)edge_sys_power_off(&sys);
            (void)relay_deinit(&relay);
            (void)dlt645_deinit(&dlt645);
            (void)edge_sys_deinit(&sys);
            return 0;
        }
#endif
    }

    (void)edge_sys_power_off(&sys);
    (void)relay_deinit(&relay);
    (void)dlt645_deinit(&dlt645);
    (void)edge_sys_deinit(&sys);
#ifdef EDGE_QEMU_SEMIHOSTING
    return 8;
#else
    return 0;
#endif
}
