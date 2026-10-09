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

#ifdef __cplusplus
}
#endif

#endif /* EDGE_IRQ_H */
