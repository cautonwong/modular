#include "pal_os/os.h"
#include "pal_rtos/rtos.h"

#include "tx_api.h"

/* After tx_api.h: this validates the values the product stated in its tx_user.h
 * (pulled in through TX_INCLUDE_USER_DEFINE_FILE) and supplies the defaults. */
#include "edge_threadx_config.h"

#include <stddef.h>
#include <stdint.h>

/*
 * ThreadX as a `pal_rtos` port (D46/D85, issue #134).
 *
 * Two differences from the other ports shape everything here:
 *
 *   - `tx_kernel_enter()` **never returns**, and ThreadX creates its application's
 *     objects from `tx_application_define()`, which the kernel calls from inside it.
 *     Creation is therefore *buffered*: `edge_rtos_task_create()` records the request
 *     and `tx_application_define()` performs it, which is what lets a composition root
 *     keep creating tasks before `edge_rtos_start()`.
 *   - ThreadX does not allocate. The port owns a static pool of thread control blocks
 *     and stacks, and refuses a request it cannot hold rather than allocating.
 *
 * Priority direction needs no translation: 0 is the highest in both.
 */

#ifndef EDGE_THREADX_MAX_TASKS
#define EDGE_THREADX_MAX_TASKS 4u
#endif
/*
 * The host port builds a `ucontext` on the thread's own stack, so a stack that fits the
 * application's frames can still be too small for the port; the floor turns that into a
 * refusal instead of an overflow.
 */
#if defined(__arm__)
#define EDGE_THREADX_MIN_STACK_BYTES 256u
#else
#define EDGE_THREADX_MIN_STACK_BYTES 8192u
#endif

/* A pool nobody stated defaults to the floor per task: small enough to fit the smallest
 * target, and stated by every product that needs more (see the pool comment in the
 * product's tx_user.h). */
#define EDGE_THREADX_STACK_BYTES_DEFAULT EDGE_THREADX_MIN_STACK_BYTES

#ifndef EDGE_THREADX_STACK_BYTES
#define EDGE_THREADX_STACK_BYTES EDGE_THREADX_STACK_BYTES_DEFAULT /* per task, in bytes */
#endif

/* A pool smaller than the floor would refuse every request: say so here rather than at
 * the first create. */
#if EDGE_THREADX_STACK_BYTES < EDGE_THREADX_MIN_STACK_BYTES
#error "EDGE_THREADX_STACK_BYTES is below EDGE_THREADX_MIN_STACK_BYTES"
#endif

typedef struct edge_threadx_task {
    TX_THREAD thread;
    edge_rtos_task_fn fn;
    void *arg;
    char name[8];
    UINT priority;
    ULONG stack_bytes;
    bool buffered;
} edge_threadx_task_t;

static edge_threadx_task_t g_tasks[EDGE_THREADX_MAX_TASKS];
static uint8_t g_stacks[EDGE_THREADX_MAX_TASKS][EDGE_THREADX_STACK_BYTES]
    __attribute__((aligned(8)));
static TX_SEMAPHORE g_wake;
static bool g_started;

/*
 * The entry input is an index, not the task's address: ThreadX's entry parameter is a
 * `ULONG`, and on the Linux port that cannot hold a 64-bit host pointer (measured: the
 * address arrives truncated and the first dereference faults). An index cannot be
 * truncated, and the pool is small enough that the lookup is free.
 */
/* A task function here runs a superloop; returning is a contract violation, and
 * returning into ThreadX's scheduler is worse than parking. */
static void park_forever(void) {
    for (;;)
        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);
}

static void task_entry(ULONG input) {
    /* The valid range is checked here, so the array is indexed only where the range is
     * known good. */
    if (input >= 1u && input <= (ULONG)EDGE_THREADX_MAX_TASKS) {
        edge_threadx_task_t *task = &g_tasks[input - 1u];
        task->fn(task->arg);
    }
    park_forever();
}

/* One place that turns a buffered request into a ThreadX thread. */
static void create_now(edge_threadx_task_t *task, uint32_t index) {
    (void)tx_thread_create(&task->thread, task->name, task_entry, (ULONG)(index + 1u),
                           g_stacks[index], task->stack_bytes, task->priority, task->priority,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
}

static void os_yield(void *self) {
    (void)self;
    tx_thread_relinquish();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    /* ThreadX counts ticks: the unit conversion lives here, and TX_TIMER_TICKS_PER_SECOND
     * is required to be stated for exactly this reason (docs/time-model.md). */
    tx_thread_sleep((ULONG)(((uint64_t)ms * TX_TIMER_TICKS_PER_SECOND) / 1000u));
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
    /* Rejected, not clamped: an out-of-range priority is a caller's mistake. */
    if (priority >= (uint32_t)TX_MAX_PRIORITIES)
        return EDGE_EINVAL;
    const ULONG stack_bytes = (ULONG)((size_t)stack_words * sizeof(uint32_t));
    if (stack_bytes < EDGE_THREADX_MIN_STACK_BYTES || stack_bytes > EDGE_THREADX_STACK_BYTES)
        return EDGE_EINVAL;

    for (uint32_t i = 0u; i < EDGE_THREADX_MAX_TASKS; ++i) {
        edge_threadx_task_t *task = &g_tasks[i];
        if (task->buffered)
            continue;
        task->fn = fn;
        task->arg = arg;
        task->priority = (UINT)priority;
        task->stack_bytes = stack_bytes;
        task->buffered = true;
        if (name != NULL) {
            for (size_t c = 0u; c < sizeof(task->name) - 1u && name[c] != '\0'; ++c)
                task->name[c] = name[c];
        }
        /* Before the kernel starts it is buffered for tx_application_define(); after it
         * has started the kernel is running and the thread can be created right here -
         * buffering it then would leave a task that never runs. */
        if (g_started)
            create_now(task, i);
        return EDGE_OK;
    }
    return EDGE_ENOSPC;
}

void edge_rtos_start(void) {
    g_started = true;
    tx_kernel_enter(); /* creates the buffered tasks, then never returns */
}

uint32_t edge_rtos_task_stack_high_water(void) {
    /* Unavailable: ThreadX exposes `tx_thread_stack_highest_ptr` only with stack
     * checking, and documents it as an approximation. The contract defines 0 as
     * unavailable, never as "plenty". */
    return 0u;
}

void edge_rtos_wake_target_set_self(void) {
    /* The semaphore is the rendezvous; the port keeps no handle. */
}

void edge_rtos_wake_from_isr(void) {
    if (g_started)
        (void)tx_semaphore_put(&g_wake);
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
    if (!g_started)
        return false;
    return tx_semaphore_get(&g_wake, (ULONG)timeout_ticks) == TX_SUCCESS;
}

/*
 * ThreadX calls this from inside `tx_kernel_enter()`, after its own initialization and
 * before the scheduler starts: the only place where application objects may be created.
 * With no heap, `first_unused_memory` is unused.
 */
void tx_application_define(void *first_unused_memory) {
    (void)first_unused_memory;
    if (tx_semaphore_create(&g_wake, "wake", 0) != TX_SUCCESS)
        return;
    for (uint32_t i = 0u; i < EDGE_THREADX_MAX_TASKS; ++i) {
        edge_threadx_task_t *task = &g_tasks[i];
        if (!task->buffered)
            continue;
        create_now(task, i);
    }
}
