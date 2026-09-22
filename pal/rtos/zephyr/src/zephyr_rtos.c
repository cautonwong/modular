#include "pal_rtos/rtos.h"

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#endif

#include <stddef.h>
#include <stdint.h>

#define EDGE_ZEPHYR_MAX_TASKS 4u
#define EDGE_ZEPHYR_DEFAULT_STACK_BYTES 2048u

#if defined(__ZEPHYR__)

static void os_yield(void *self) {
    (void)self;
    k_yield();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    k_msleep(ms);
}

typedef struct zephyr_task_entry {
    struct k_thread thread;
    edge_rtos_task_fn fn;
    void *arg;
    bool used;
} zephyr_task_entry_t;

K_THREAD_STACK_ARRAY_DEFINE(g_task_stacks, EDGE_ZEPHYR_MAX_TASKS, EDGE_ZEPHYR_DEFAULT_STACK_BYTES);
static zephyr_task_entry_t g_tasks[EDGE_ZEPHYR_MAX_TASKS];
static struct k_sem g_wake_sem;
static bool g_wake_sem_inited = false;
static k_tid_t g_wake_target = NULL;

#endif /* __ZEPHYR__ */

edge_os_port_t edge_rtos_os_port(void) {
#if defined(__ZEPHYR__)
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = NULL,
    };
    return port;
#else
    const edge_os_port_t port = {
        .yield = NULL,
        .sleep_ms = NULL,
        .self = NULL,
    };
    return port;
#endif
}

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority) {
    if (fn == NULL) {
        return EDGE_EINVAL;
    }

#if defined(__ZEPHYR__)
    /*
     * Priority translation: 0 is the highest priority in the neutral contract.
     * In Zephyr preemptible threads, 0 is also the highest priority (K_PRIO_PREEMPT(0)).
     */
    if (priority >= (uint32_t)CONFIG_NUM_PREEMPT_PRIORITIES) {
        return EDGE_EINVAL;
    }

    /* Convert stack words to bytes */
    const size_t req_bytes = (size_t)stack_words * sizeof(uint32_t);
    if (req_bytes > EDGE_ZEPHYR_DEFAULT_STACK_BYTES) {
        return EDGE_ENOSPC;
    }

    for (uint32_t i = 0u; i < EDGE_ZEPHYR_MAX_TASKS; ++i) {
        if (!g_tasks[i].used) {
            g_tasks[i].used = true;
            g_tasks[i].fn = fn;
            g_tasks[i].arg = arg;

            const int zephyr_prio = K_PRIO_PREEMPT(priority);
            k_tid_t tid = k_thread_create(&g_tasks[i].thread, g_task_stacks[i],
                                          EDGE_ZEPHYR_DEFAULT_STACK_BYTES, (k_thread_entry_t)fn,
                                          arg, NULL, NULL, zephyr_prio, 0, K_NO_WAIT);
            if (name != NULL) {
                k_thread_name_set(tid, name);
            }
            return EDGE_OK;
        }
    }
    return EDGE_ENOSPC;
#else
    (void)name;
    (void)arg;
    (void)stack_words;
    (void)priority;
    return EDGE_OK;
#endif
}

void edge_rtos_start(void) {
#if defined(__ZEPHYR__)
    /* In Zephyr, the kernel is already started when main() runs. */
#endif
}

uint32_t edge_rtos_task_stack_high_water(void) {
#if defined(__ZEPHYR__)
    size_t unused = 0;
    if (k_thread_stack_space_get(k_current_get(), &unused) == 0) {
        return (uint32_t)unused;
    }
    return 0u;
#else
    return 256u;
#endif
}

/* ---- wake/block (D47/D71) ---------------------------------------------- */

void edge_rtos_wake_target_set_self(void) {
#if defined(__ZEPHYR__)
    if (!g_wake_sem_inited) {
        k_sem_init(&g_wake_sem, 0, 1);
        g_wake_sem_inited = true;
    }
    g_wake_target = k_current_get();
#endif
}

void edge_rtos_wake_from_isr(void) {
#if defined(__ZEPHYR__)
    if (g_wake_sem_inited) {
        k_sem_give(&g_wake_sem);
    }
#endif
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
#if defined(__ZEPHYR__)
    if (!g_wake_sem_inited) {
        k_sem_init(&g_wake_sem, 0, 1);
        g_wake_sem_inited = true;
    }
    const k_timeout_t timeout = (timeout_ticks == 0u) ? K_NO_WAIT : K_TICKS(timeout_ticks);
    return k_sem_take(&g_wake_sem, timeout) == 0;
#else
    (void)timeout_ticks;
    return true;
#endif
}
