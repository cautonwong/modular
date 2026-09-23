#include "pal_rtos/rtos.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * Task storage belongs to the port (the contract requires it), and the image has no
 * heap at all: `configSUPPORT_DYNAMIC_ALLOCATION` is 0 and `heap_4.c` is not compiled
 * in, so `xTaskCreate` does not exist and nothing can allocate at run time (#172).
 */
#ifndef EDGE_FREERTOS_MAX_TASKS
#define EDGE_FREERTOS_MAX_TASKS 4u
#endif
#ifndef EDGE_FREERTOS_STACK_WORDS
#define EDGE_FREERTOS_STACK_WORDS 512u /* the largest a product here asks for */
#endif

static StaticTask_t g_task_tcb[EDGE_FREERTOS_MAX_TASKS];
static StackType_t g_task_stack[EDGE_FREERTOS_MAX_TASKS][EDGE_FREERTOS_STACK_WORDS];
static uint32_t g_tasks_used;

static void os_yield(void *self) {
    (void)self;
    taskYIELD();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

edge_os_port_t edge_rtos_os_port(void) {
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = NULL,
    };
    return port;
}

/*
 * Priority translation. The contract pins 0 = highest (see pal_rtos/rtos.h);
 * FreeRTOS is the other way round and reserves 0 for the idle task. Forwarding the
 * number would silently invert every task's priority, so the port inverts it, and
 * contract priority 0 lands on the highest real priority rather than on idle.
 */
static UBaseType_t to_freertos_priority(uint32_t priority) {
    return (UBaseType_t)((uint32_t)configMAX_PRIORITIES - 1u - priority);
}

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority) {
    if (fn == NULL)
        return EDGE_EINVAL;
    /* Rejected, not clamped: silently turning an out-of-range priority into the
     * lowest one hides a caller's mistake behind a scheduling surprise. */
    if (priority >= (uint32_t)configMAX_PRIORITIES)
        return EDGE_EINVAL;
    /* Refused, not served by allocating: the port owns a fixed pool and there is no
     * heap to fall back on. */
    if (stack_words == 0u || stack_words > EDGE_FREERTOS_STACK_WORDS)
        return EDGE_EINVAL;
    if (g_tasks_used >= EDGE_FREERTOS_MAX_TASKS)
        return EDGE_ENOSPC;
    TaskHandle_t handle = xTaskCreateStatic(
        (TaskFunction_t)fn, name, (configSTACK_DEPTH_TYPE)stack_words, arg,
        to_freertos_priority(priority), g_task_stack[g_tasks_used], &g_task_tcb[g_tasks_used]);
    if (handle == NULL)
        return EDGE_ENOSPC;
    ++g_tasks_used;
    return EDGE_OK;
}

void edge_rtos_start(void) {
    vTaskStartScheduler();
}

/* With static allocation and no heap, the kernel asks the application for the idle
 * task's memory instead of allocating it. */
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *size) {
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb;
    *stack = idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

/* Overrun guard (configCHECK_FOR_STACK_OVERFLOW == 2): halt so a stack overflow
 * is an unmistakable CI failure rather than silent corruption. */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    (void)name;
    for (;;) {
    }
}

uint32_t edge_rtos_task_stack_high_water(void) {
    return (uint32_t)uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t);
}

/* ---- wake/block (D47/D71) ---------------------------------------------- */

static TaskHandle_t g_wake_target;

void edge_rtos_wake_target_set_self(void) {
    g_wake_target = xTaskGetCurrentTaskHandle();
}

void edge_rtos_wake_from_isr(void) {
    if (g_wake_target == NULL)
        return;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(g_wake_target, &woken);
    /* Yield only when a task was actually made ready: an unconditional yield here
     * would put a PendSV on every interrupt, which is the silent wake cost
     * docs/low-power.md section 1 rules out. */
    portYIELD_FROM_ISR(woken);
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
    return ulTaskNotifyTake(pdTRUE, (TickType_t)timeout_ticks) > 0u;
}
