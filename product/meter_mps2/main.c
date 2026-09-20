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
    const uint32_t required[] = {EDGE_MOD_DLT645, EDGE_MOD_RELAY};

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
    /*
     * The SysTick register read is the one part the host tests cannot reach (it
     * lives inside `#if __arm__`), so assert its core property here, on target.
     *
     * The property is monotonic *non-decreasing* plus eventual advance, not
     * "strictly increasing between two adjacent reads": two reads can legitimately
     * report the same count (a coarse virtual clock under QEMU, or a read faster
     * than the counter's resolution), and demanding strictness there would test
     * the harness rather than the port. What must never happen is the backwards
     * jump the wrap race produced - a full period (~0.67 s at 25 MHz) - which
     * stalls every periodic app and reports a fake budget overrun.
     */
#ifdef EDGE_QEMU_SEMIHOSTING
    /*
     * Smoke-only self-check (the real register read lives inside `#if __arm__`, so
     * the host tests cannot reach it): sample for a long window and require that
     * the reported clock never goes backwards and does advance. The window is long
     * because the defect - a wrap crossed between two samples but not accounted for
     * - makes the clock fall back by nearly a whole period (~0.67 s at 25 MHz), and
     * a wrap has to actually happen inside the window for it to be caught.
     *
     * Real work between samples, not just more SysTick reads: under QEMU the
     * virtual clock advances with executed instructions, so a tight MMIO loop can
     * sample the same counter value every time, which would test the harness
     * rather than the port. Deliberately not a proof - the deterministic
     * wrap-crossing refutation is tests/test_pal_cortex_m.c.
     */
    const uint64_t first = pal.monotonic_ticks(pal.self);
    uint64_t previous = first;
    for (uint32_t i = 0u; i < 400000u; ++i) {
        for (volatile uint32_t spin = 0u; spin < 64u; ++spin) {
        }
        const uint64_t next = pal.monotonic_ticks(pal.self);
        if (next < previous)
            return 14; /* monotonic clock went backwards */
        previous = next;
    }
    if (previous == first)
        return 15; /* the clock never advanced: the time source is dead or frozen */
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
