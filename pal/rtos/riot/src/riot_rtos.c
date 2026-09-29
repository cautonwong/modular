#if defined(RIOT_VERSION) || defined(__RIOT__)
#include "irq.h"
#include "sched.h"
#include "thread.h"
#include "thread_flags.h"
#include "ztimer.h"
#elif defined(__linux__) || defined(__APPLE__) || defined(_POSIX_C_SOURCE) ||                      \
    defined(_XOPEN_SOURCE) || defined(__unix__)
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#define EDGE_RIOT_HOST_SIMULATION 1
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

#include "pal_rtos/rtos.h"
#include "pal_rtos_riot/pal_rtos_riot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EDGE_RIOT_MAX_TASKS 4u
#define EDGE_RIOT_MAX_PRIORITIES 16u
#define EDGE_RIOT_STACK_BYTES 4096u
#define EDGE_RIOT_FLAG_WORK (1u << 0)

typedef struct riot_task_entry {
#if defined(RIOT_VERSION) || defined(__RIOT__)
    kernel_pid_t pid;
    char stack[EDGE_RIOT_STACK_BYTES];
#elif defined(EDGE_RIOT_HOST_SIMULATION)
    pthread_t thread;
    char stack[EDGE_RIOT_STACK_BYTES];
#endif
    edge_rtos_task_fn fn;
    void *arg;
    uint32_t priority;
    bool used;
} riot_task_entry_t;

static riot_task_entry_t g_tasks[EDGE_RIOT_MAX_TASKS];

#if defined(RIOT_VERSION) || defined(__RIOT__)

static void os_yield(void *self) {
    (void)self;
    thread_yield();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    ztimer_sleep(ZTIMER_MSEC, ms);
}

static kernel_pid_t g_wake_pid = KERNEL_PID_UNDEF;

static void *task_trampoline(void *arg) {
    const uint32_t idx = (uint32_t)(uintptr_t)arg;
    if (idx < EDGE_RIOT_MAX_TASKS && g_tasks[idx].fn != NULL) {
        g_tasks[idx].fn(g_tasks[idx].arg);
    }
    return NULL;
}

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority) {
    if (fn == NULL || priority >= EDGE_RIOT_MAX_PRIORITIES) {
        return EDGE_EINVAL;
    }
    if ((uint64_t)stack_words * sizeof(uint32_t) > EDGE_RIOT_STACK_BYTES) {
        return EDGE_ENOSPC;
    }

    for (uint32_t i = 0u; i < EDGE_RIOT_MAX_TASKS; ++i) {
        if (!g_tasks[i].used) {
            g_tasks[i].fn = fn;
            g_tasks[i].arg = arg;
            g_tasks[i].priority = priority;
            g_tasks[i].used = true;

            const kernel_pid_t pid =
                thread_create(g_tasks[i].stack, sizeof(g_tasks[i].stack), (char)priority,
                              THREAD_CREATE_SLEEPING | THREAD_CREATE_STACKTEST, task_trampoline,
                              (void *)(uintptr_t)i, name);

            if (pid <= KERNEL_PID_UNDEF) {
                g_tasks[i].used = false;
                return EDGE_ENOSPC;
            }
            g_tasks[i].pid = pid;
            return EDGE_OK;
        }
    }
    return EDGE_ENOSPC;
}

void edge_rtos_start(void) {
    for (uint32_t i = 0u; i < EDGE_RIOT_MAX_TASKS; ++i) {
        if (g_tasks[i].used && g_tasks[i].pid > KERNEL_PID_UNDEF) {
            thread_wakeup(g_tasks[i].pid);
        }
    }
    while (1) {
        thread_yield();
    }
}

uint32_t edge_rtos_task_stack_high_water(void) {
    const thread_t *t = thread_get_active();
    if (t == NULL) {
        return 0u;
    }
    return (uint32_t)thread_measure_stack_free(t->stack_start);
}

void edge_rtos_wake_target_set_self(void) {
    g_wake_pid = thread_getpid();
}

void edge_rtos_wake_from_isr(void) {
    if (g_wake_pid != KERNEL_PID_UNDEF) {
        thread_flags_set(thread_get(g_wake_pid), EDGE_RIOT_FLAG_WORK);
    }
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
    if (timeout_ticks == 0u) {
        thread_flags_t flags = thread_flags_clear(EDGE_RIOT_FLAG_WORK);
        return (flags & EDGE_RIOT_FLAG_WORK) != 0;
    }
    /* Wait for flag */
    thread_flags_t flags = thread_flags_wait_any(EDGE_RIOT_FLAG_WORK);
    return (flags & EDGE_RIOT_FLAG_WORK) != 0;
}

#elif defined(EDGE_RIOT_HOST_SIMULATION)

static pthread_mutex_t g_sim_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_sim_cond = PTHREAD_COND_INITIALIZER;
static bool g_sim_woken = false;
static bool g_sim_has_target = false;

static void os_yield(void *self) {
    (void)self;
    sched_yield();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    struct timespec ts = {
        .tv_sec = ms / 1000u,
        .tv_nsec = (long)(ms % 1000u) * 1000000L,
    };
    nanosleep(&ts, NULL);
}

static void *sim_trampoline(void *arg) {
    const uint32_t idx = (uint32_t)(uintptr_t)arg;
    if (idx < EDGE_RIOT_MAX_TASKS && g_tasks[idx].fn != NULL) {
        g_tasks[idx].fn(g_tasks[idx].arg);
    }
    return NULL;
}

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority) {
    (void)name;
    if (fn == NULL || priority >= EDGE_RIOT_MAX_PRIORITIES) {
        return EDGE_EINVAL;
    }
    if ((uint64_t)stack_words * sizeof(uint32_t) > EDGE_RIOT_STACK_BYTES) {
        return EDGE_ENOSPC;
    }

    for (uint32_t i = 0u; i < EDGE_RIOT_MAX_TASKS; ++i) {
        if (!g_tasks[i].used) {
            g_tasks[i].fn = fn;
            g_tasks[i].arg = arg;
            g_tasks[i].priority = priority;
            g_tasks[i].used = true;
            return EDGE_OK;
        }
    }
    return EDGE_ENOSPC;
}

void edge_rtos_start(void) {
    for (uint32_t i = 0u; i < EDGE_RIOT_MAX_TASKS; ++i) {
        if (g_tasks[i].used && g_tasks[i].fn != NULL) {
            pthread_create(&g_tasks[i].thread, NULL, sim_trampoline, (void *)(uintptr_t)i);
        }
    }
    while (1) {
        os_sleep_ms(NULL, 1000u);
    }
}

uint32_t edge_rtos_task_stack_high_water(void) {
    return 0u;
}

void edge_rtos_wake_target_set_self(void) {
    pthread_mutex_lock(&g_sim_lock);
    g_sim_has_target = true;
    pthread_mutex_unlock(&g_sim_lock);
}

void edge_rtos_wake_from_isr(void) {
    pthread_mutex_lock(&g_sim_lock);
    if (g_sim_has_target) {
        g_sim_woken = true;
        pthread_cond_broadcast(&g_sim_cond);
    }
    pthread_mutex_unlock(&g_sim_lock);
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
    pthread_mutex_lock(&g_sim_lock);
    if (g_sim_woken) {
        g_sim_woken = false;
        pthread_mutex_unlock(&g_sim_lock);
        return true;
    }
    if (timeout_ticks == 0u) {
        pthread_mutex_unlock(&g_sim_lock);
        return false;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += (long)(timeout_ticks % 1000u) * 1000000L;
    ts.tv_sec += timeout_ticks / 1000u + (ts.tv_nsec / 1000000000L);
    ts.tv_nsec %= 1000000000L;

    pthread_cond_timedwait(&g_sim_cond, &g_sim_lock, &ts);
    const bool woken = g_sim_woken;
    g_sim_woken = false;
    pthread_mutex_unlock(&g_sim_lock);
    return woken;
}

#endif /* EDGE_RIOT_HOST_SIMULATION */

edge_os_port_t edge_rtos_os_port(void) {
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = NULL,
    };
    return port;
}
