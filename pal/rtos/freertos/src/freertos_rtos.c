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
