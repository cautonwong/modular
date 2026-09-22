#if defined(__rtems__)
#include <rtems.h>
#elif defined(__linux__) || defined(__APPLE__) || defined(_POSIX_C_SOURCE) ||                      \
    defined(_XOPEN_SOURCE) || defined(__unix__)
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#define EDGE_RTEMS_HOST_SIMULATION 1
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

#include "pal_rtos/rtos.h"
#include "pal_rtos_rtems/pal_rtos_rtems.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EDGE_RTEMS_MAX_TASKS 4u
#define EDGE_RTEMS_DEFAULT_STACK_BYTES 4096u

typedef struct rtems_task_entry {
#if defined(__rtems__)
    rtems_id id;
#elif defined(EDGE_RTEMS_HOST_SIMULATION)
    pthread_t thread;
#endif
    edge_rtos_task_fn fn;
    void *arg;
    bool used;
} rtems_task_entry_t;

static rtems_task_entry_t g_tasks[EDGE_RTEMS_MAX_TASKS];

#if defined(__rtems__)

static void os_yield(void *self) {
    (void)self;
    (void)rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    const uint32_t us_per_tick = rtems_configuration_get_microseconds_per_tick();
    const rtems_interval ticks = (us_per_tick > 0u) ? (ms * 1000u) / us_per_tick : ms;
    (void)rtems_task_wake_after(ticks > 0u ? ticks : 1u);
}

static rtems_id g_wake_sem = RTEMS_ID_NONE;
static bool g_wake_sem_inited = false;

static rtems_task task_trampoline(rtems_task_argument arg) {
    const uint32_t idx = (uint32_t)arg;
    if (idx < EDGE_RTEMS_MAX_TASKS && g_tasks[idx].fn != NULL) {
        g_tasks[idx].fn(g_tasks[idx].arg);
    }
    (void)rtems_task_delete(RTEMS_SELF);
}

#elif defined(EDGE_RTEMS_HOST_SIMULATION)

static void os_yield(void *self) {
    (void)self;
    usleep(100);
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    usleep(ms * 1000u);
}

static pthread_mutex_t g_host_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_host_cond = PTHREAD_COND_INITIALIZER;
static bool g_host_notified = false;

static void *host_task_runner(void *arg) {
    const uint32_t idx = (uint32_t)(uintptr_t)arg;
    if (idx < EDGE_RTEMS_MAX_TASKS && g_tasks[idx].fn != NULL) {
        g_tasks[idx].fn(g_tasks[idx].arg);
    }
    return NULL;
}

#else /* Embedded target fallback when RTEMS is not active */

static void os_yield(void *self) {
    (void)self;
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    (void)ms;
}

#endif

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
    if (fn == NULL) {
        return EDGE_EINVAL;
    }
    if (priority >= 254u) {
        return EDGE_EINVAL;
    }

#if defined(__rtems__)
    const rtems_task_priority rtems_prio = (rtems_task_priority)(priority + 1u);
    const size_t req_bytes = (size_t)stack_words * sizeof(uint32_t);
    const size_t stack_size =
        (req_bytes > EDGE_RTEMS_DEFAULT_STACK_BYTES) ? req_bytes : EDGE_RTEMS_DEFAULT_STACK_BYTES;

    for (uint32_t i = 0u; i < EDGE_RTEMS_MAX_TASKS; ++i) {
        if (!g_tasks[i].used) {
            g_tasks[i].used = true;
            g_tasks[i].fn = fn;
            g_tasks[i].arg = arg;

            rtems_name r_name = rtems_build_name('T', 'S', 'K', (char)('0' + i));
            if (name != NULL && name[0] != '\0') {
                r_name = rtems_build_name(name[0], name[1] ? name[1] : ' ',
                                          name[1] && name[2] ? name[2] : ' ',
                                          name[1] && name[2] && name[3] ? name[3] : ' ');
            }

            rtems_status_code sc =
                rtems_task_create(r_name, rtems_prio, stack_size, RTEMS_DEFAULT_MODES,
                                  RTEMS_DEFAULT_ATTRIBUTES, &g_tasks[i].id);
            if (sc != RTEMS_SUCCESSFUL) {
                g_tasks[i].used = false;
                return EDGE_ENOSPC;
            }

            sc = rtems_task_start(g_tasks[i].id, task_trampoline, (rtems_task_argument)i);
            if (sc != RTEMS_SUCCESSFUL) {
                (void)rtems_task_delete(g_tasks[i].id);
                g_tasks[i].used = false;
                return EDGE_ENOSPC;
            }

            return EDGE_OK;
        }
    }
    return EDGE_ENOSPC;
#else
    (void)name;
    (void)stack_words;
    for (uint32_t i = 0u; i < EDGE_RTEMS_MAX_TASKS; ++i) {
        if (!g_tasks[i].used) {
            g_tasks[i].used = true;
            g_tasks[i].fn = fn;
            g_tasks[i].arg = arg;
            return EDGE_OK;
        }
    }
    return EDGE_ENOSPC;
#endif
}

void edge_rtos_start(void) {
#if defined(__rtems__)
    /* In RTEMS, executive is already running in Init task */
#elif defined(EDGE_RTEMS_HOST_SIMULATION)
    for (uint32_t i = 0u; i < EDGE_RTEMS_MAX_TASKS; ++i) {
        if (g_tasks[i].used && g_tasks[i].fn != NULL) {
            (void)pthread_create(&g_tasks[i].thread, NULL, host_task_runner, (void *)(uintptr_t)i);
        }
    }
    for (uint32_t i = 0u; i < EDGE_RTEMS_MAX_TASKS; ++i) {
        if (g_tasks[i].used) {
            (void)pthread_join(g_tasks[i].thread, NULL);
        }
    }
#endif
}

uint32_t edge_rtos_task_stack_high_water(void) {
    return 256u;
}

/* ---- wake/block (D47/D71) ---------------------------------------------- */

void edge_rtos_wake_target_set_self(void) {
#if defined(__rtems__)
    if (!g_wake_sem_inited) {
        (void)rtems_semaphore_create(rtems_build_name('W', 'A', 'K', 'E'), 0u,
                                     RTEMS_SIMPLE_BINARY_SEMAPHORE | RTEMS_FIFO, 0u, &g_wake_sem);
        g_wake_sem_inited = true;
    }
#endif
}

void edge_rtos_wake_from_isr(void) {
#if defined(__rtems__)
    if (g_wake_sem_inited && g_wake_sem != RTEMS_ID_NONE) {
        (void)rtems_semaphore_release(g_wake_sem);
    }
#elif defined(EDGE_RTEMS_HOST_SIMULATION)
    pthread_mutex_lock(&g_host_mutex);
    g_host_notified = true;
    pthread_cond_signal(&g_host_cond);
    pthread_mutex_unlock(&g_host_mutex);
#endif
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
#if defined(__rtems__)
    if (!g_wake_sem_inited) {
        (void)rtems_semaphore_create(rtems_build_name('W', 'A', 'K', 'E'), 0u,
                                     RTEMS_SIMPLE_BINARY_SEMAPHORE | RTEMS_FIFO, 0u, &g_wake_sem);
        g_wake_sem_inited = true;
    }
    const rtems_option option_set = (timeout_ticks == 0u) ? RTEMS_NO_WAIT : RTEMS_WAIT;
    const rtems_interval timeout = (timeout_ticks == 0u) ? RTEMS_NO_TIMEOUT : timeout_ticks;
    const rtems_status_code sc = rtems_semaphore_obtain(g_wake_sem, option_set, timeout);
    return sc == RTEMS_SUCCESSFUL;
#elif defined(EDGE_RTEMS_HOST_SIMULATION)
    pthread_mutex_lock(&g_host_mutex);
    if (!g_host_notified) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)(timeout_ticks * 1000000L);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += ts.tv_nsec / 1000000000L;
            ts.tv_nsec %= 1000000000L;
        }
        pthread_cond_timedwait(&g_host_cond, &g_host_mutex, &ts);
    }
    const bool was_notified = g_host_notified;
    g_host_notified = false;
    pthread_mutex_unlock(&g_host_mutex);
    return was_notified;
#else
    (void)timeout_ticks;
    return true;
#endif
}
