#ifndef EDGE_IRQ_H
#define EDGE_IRQ_H

#include "errors.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 4-Class Interrupt Classification (ADR-006, IRQ-003)
 */
typedef enum edge_irq_class {
    EDGE_IRQ_CLASS_EVENT = 0,     /**< Control plane: GPIO, RTC, Buttons, Touch, Gestures */
    EDGE_IRQ_CLASS_DATA = 1,      /**< Data plane: UART, SPI DMA, ADC stream, RingBuffer */
    EDGE_IRQ_CLASS_SCHEDULER = 2, /**< Timebase / SysTick / Scheduler wake */
    EDGE_IRQ_CLASS_FAULT = 3      /**< Fault / Crash: HardFault, NMI, Watchdog, Brown-out */
} edge_irq_class_t;

/**
 * @brief Fault Context Capture Record (FAULT-002 ~ 005)
 */
typedef struct edge_fault_record {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t reset_reason;
} edge_fault_record_t;

/**
 * @brief ISR Execution Statistics (IRQ-EVT-007, BACKPRESSURE-005)
 */
typedef struct edge_irq_stats {
    uint32_t event_irq_count;
    uint32_t data_irq_count;
    uint32_t scheduler_irq_count;
    uint32_t fault_irq_count;
    uint32_t event_dropped_count;
    uint32_t buffer_overflow_count;
} edge_irq_stats_t;

typedef void (*edge_fault_handler_fn)(const edge_fault_record_t *fault, void *ctx);

typedef struct edge_fault_manager {
    edge_fault_record_t last_fault;
    bool has_fault;
    edge_fault_handler_fn handler;
    void *ctx;
} edge_fault_manager_t;

static inline void edge_fault_manager_init(edge_fault_manager_t *mgr, edge_fault_handler_fn handler,
                                           void *ctx) {
    if (mgr == NULL) {
        return;
    }
    mgr->has_fault = false;
    mgr->handler = handler;
    mgr->ctx = ctx;
}

static inline void edge_fault_capture_isr(edge_fault_manager_t *mgr,
                                          const edge_fault_record_t *fault) {
    if (mgr == NULL || fault == NULL) {
        return;
    }
    mgr->last_fault = *fault;
    mgr->has_fault = true;
    if (mgr->handler != NULL) {
        mgr->handler(fault, mgr->ctx);
    }
}

static inline bool edge_fault_occurred(const edge_fault_manager_t *mgr) {
    return mgr != NULL && mgr->has_fault;
}

static inline void edge_fault_clear(edge_fault_manager_t *mgr) {
    if (mgr != NULL) {
        mgr->has_fault = false;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* EDGE_IRQ_H */
