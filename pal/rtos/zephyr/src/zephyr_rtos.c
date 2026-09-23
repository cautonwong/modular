#include "pal_rtos/rtos.h"

#if !defined(__ZEPHYR__)
#error "pal/rtos/zephyr is a Zephyr port: build it inside a Zephyr build, not on the host"
#endif

#include <zephyr/kernel.h>

#include <stddef.h>
#include <stdint.h>

#define EDGE_ZEPHYR_MAX_TASKS 4u

/* One knob: the product's Kconfig value, so the port cannot disagree with prj.conf. */
#ifndef CONFIG_MODULAR_RUNNER_STACK_SIZE
#define EDGE_ZEPHYR_STACK_BYTES 2048u
#else
#define EDGE_ZEPHYR_STACK_BYTES ((uint32_t)CONFIG_MODULAR_RUNNER_STACK_SIZE)
#endif

/* The port owns the task storage (the contract requires it): static stacks, no heap. */
K_THREAD_STACK_ARRAY_DEFINE(g_task_stacks, EDGE_ZEPHYR_MAX_TASKS, EDGE_ZEPHYR_STACK_BYTES);
static struct k_thread g_threads[EDGE_ZEPHYR_MAX_TASKS];
static bool g_used[EDGE_ZEPHYR_MAX_TASKS];

/* Zephyr's static initialiser: no lazy init, no "initialised yet" flag. */
K_SEM_DEFINE(g_wake_sem, 0, 1);

static void os_yield(void *self) {
    (void)self;
    k_yield();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    k_msleep(ms);
}

edge_os_port_t edge_rtos_os_port(void) {
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = NULL,
    };
    return port;
}

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority) {
    if (fn == NULL)
        return EDGE_EINVAL;

    /* The direction matches Zephyr's; an unrepresentable value is rejected, not clamped. */
    if (priority >= (uint32_t)CONFIG_NUM_PREEMPT_PRIORITIES)
        return EDGE_EINVAL;

    /* Refused, not silently given the pool size. */
    if ((size_t)stack_words * sizeof(uint32_t) > (size_t)EDGE_ZEPHYR_STACK_BYTES)
        return EDGE_ENOSPC;

    for (uint32_t i = 0u; i < EDGE_ZEPHYR_MAX_TASKS; ++i) {
        if (g_used[i])
            continue;
        g_used[i] = true;
        const k_tid_t tid = k_thread_create(
            &g_threads[i], g_task_stacks[i], EDGE_ZEPHYR_STACK_BYTES, (k_thread_entry_t)fn, arg,
            NULL, NULL, K_PRIO_PREEMPT((int)priority), 0, K_NO_WAIT);
        if (name != NULL)
            k_thread_name_set(tid, name);
        return EDGE_OK;
    }
    return EDGE_ENOSPC;
}

/* Zephyr's kernel is already running when main() starts; the contract only requires
 * that creation before this call works, which it does. */
void edge_rtos_start(void) {
}

uint32_t edge_rtos_task_stack_high_water(void) {
    size_t unused = 0u;
    if (k_thread_stack_space_get(k_current_get(), &unused) == 0)
        return (uint32_t)unused;
    return 0u; /* unavailable, which the contract defines as 0 rather than "plenty" */
}

void edge_rtos_wake_target_set_self(void) {
    /* The semaphore is the rendezvous, so there is no handle to publish. */
}

void edge_rtos_wake_from_isr(void) {
    k_sem_give(&g_wake_sem); /* ISR-safe in Zephyr */
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
    const k_timeout_t timeout = (timeout_ticks == 0u) ? K_NO_WAIT : K_TICKS(timeout_ticks);
    return k_sem_take(&g_wake_sem, timeout) == 0;
}
