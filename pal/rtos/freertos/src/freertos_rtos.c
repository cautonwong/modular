#include "pal_rtos/rtos.h"

#include "FreeRTOS.h"
#include "task.h"

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

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority) {
    if (fn == NULL)
        return EDGE_EINVAL;
    if (xTaskCreate((TaskFunction_t)fn, name, (configSTACK_DEPTH_TYPE)stack_words, arg,
                    (UBaseType_t)priority, NULL) != pdPASS)
        return EDGE_ENOSPC;
    return EDGE_OK;
}

void edge_rtos_start(void) {
    vTaskStartScheduler();
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

static edge_rtos_assert_fn g_assert_fn;
static void *g_assert_ctx;
static uint32_t g_assert_count;

void edge_rtos_set_assert_hook(edge_rtos_assert_fn fn, void *ctx) {
    g_assert_fn = fn;
    g_assert_ctx = ctx;
}

uint32_t edge_rtos_assert_count(void) {
    return g_assert_count;
}

void edge_rtos_assert_failed(const char *file, int line) {
    (void)file;
    (void)line;
    ++g_assert_count;
    if (g_assert_fn != NULL)
        g_assert_fn(g_assert_ctx, file, line);
    /* No hook (or a hook that returned): halt rather than continue silently. */
    for (;;) {
    }
}
