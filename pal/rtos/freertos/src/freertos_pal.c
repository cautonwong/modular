#include "pal_rtos_freertos/pal_rtos_freertos.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>

/* --- architecture primitives (D46) -------------------------------------- */

/*
 * Task context only. taskENTER_CRITICAL/taskEXIT_CRITICAL raise BASEPRI to
 * configMAX_SYSCALL_INTERRUPT_PRIORITY and nest, so a critical section never
 * masks an interrupt above that priority. Calling these from an ISR would
 * corrupt the kernel's nesting count; ISR code uses edge_rtos_irq_guard().
 */
static void critical_enter(void *self) {
    (void)self;
    taskENTER_CRITICAL();
}

static void critical_exit(void *self) {
    (void)self;
    taskEXIT_CRITICAL();
}

static void memory_barrier(void *self) {
    (void)self;
    /*
     * The ARM_CM3 port ships no public hardware-barrier macro and no CMSIS
     * header (port.c uses bare `dsb`/`isb` internally), so the arch layer emits
     * it here with a compiler clobber - strictly stronger than the port's own
     * portMEMORY_BARRIER().
     */
    __asm volatile("dsb 0xF" ::: "memory");
}

/*
 * The kernel tick, extended to 64 bit.
 *
 * Masked across the sample: a task and an ISR can read the clock during the same
 * wrap, and the accumulator must not lose the epoch. portSET_INTERRUPT_MASK_
 * FROM_ISR() is BASEPRI-based and therefore valid in both contexts, unlike
 * taskENTER_CRITICAL(). The read itself uses the ISR variant in interrupt
 * context, which also enforces the priority ceiling (configASSERT).
 */
static uint64_t monotonic_ticks(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    const UBaseType_t mask = portSET_INTERRUPT_MASK_FROM_ISR();
    const TickType_t now =
        (xPortIsInsideInterrupt() != 0) ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
    const uint64_t extended =
        edge_tick64_extend(state != NULL ? &state->tick : NULL, (uint32_t)now);
    portCLEAR_INTERRUPT_MASK_FROM_ISR(mask);
    return extended;
}

static bool in_isr(void *self) {
    (void)self;
    return xPortIsInsideInterrupt() != 0;
}

/* Context markers: IPSR already answers in_isr() on Cortex-M, so there is no
 * state to keep. They stay because the contract has them. */
static void isr_enter(void *self) {
    (void)self;
}

static void isr_exit(void *self) {
    (void)self;
}

static void idle(void *self) {
    (void)self;
    /*
     * Plain wait. Tickless sleep (configUSE_TICKLESS_IDLE +
     * vPortSuppressTicksAndSleep) is the battery-product lever and belongs to
     * the scheduler's idle task; see docs/low-power.md.
     */
    __asm volatile("wfi" ::: "memory");
}

/* --- ISR-side guard ------------------------------------------------------ */

/*
 * One level of state. On Cortex-M an ISR cannot preempt another ISR at the same
 * or lower priority, and the kernel's own ulPortRaiseBASEPRI() is
 * reentrancy-hostile for the same reason. Documented ceiling: the guard is not
 * re-entrant; a nested user would need a mask-carrying handle instead.
 */
static UBaseType_t g_isr_mask;
static uint32_t g_isr_depth;

static void irq_guard_enter(void *self) {
    (void)self;
    if (g_isr_depth == 0u)
        g_isr_mask = portSET_INTERRUPT_MASK_FROM_ISR();
    ++g_isr_depth;
}

static void irq_guard_exit(void *self) {
    (void)self;
    if (g_isr_depth > 0u)
        --g_isr_depth;
    if (g_isr_depth != 0u)
        return;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(g_isr_mask);
    /*
     * No portYIELD_FROM_ISR here. The capsule polls the queue, so the push wakes
     * no task and an unconditional yield would only add a PendSV to every IRQ -
     * a real power cost on a water meter, and a livelock risk under a fast timer.
     * The yield bridge belongs with the blocking consumer that needs it (#90),
     * not with a guard that has no receiver.
     */
}

edge_irq_guard_t edge_rtos_irq_guard(void) {
    const edge_irq_guard_t guard = {
        .enter = irq_guard_enter,
        .exit = irq_guard_exit,
        .self = NULL,
    };
    return guard;
}

void edge_rtos_pal_init(edge_rtos_pal_state_t *state) {
    if (state == NULL)
        return;
    edge_tick64_reset(&state->tick);
}

edge_pal_port_t edge_rtos_pal_port(edge_rtos_pal_state_t *state) {
    const edge_pal_port_t port = {
        .critical_enter = critical_enter,
        .critical_exit = critical_exit,
        .memory_barrier = memory_barrier,
        .monotonic_ticks = monotonic_ticks,
        .in_isr = in_isr,
        .isr_enter = isr_enter,
        .isr_exit = isr_exit,
        .idle = idle,
        .self = state,
    };
    return port;
}
