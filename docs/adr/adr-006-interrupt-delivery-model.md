# ADR-006: 4-Class Interrupt Delivery Architecture

## Status
Accepted

## Context
Routing all hardware interrupts indiscriminately through a single global event queue creates queue congestion for high-bandwidth streaming (UART/SPI DMA) and prevents proper handling of critical faults (HardFault/NMI) and scheduling timebases (SysTick).

## Decision
1. **4-Class Interrupt Paths**: Formally distinguish four independent ISR execution paths:
   - **Event IRQ**: Control plane discrete facts (GPIO edge, RTC alarm, touch gesture). Minimal capture $\to$ `edge_event_queue` $\to$ background dispatch.
   - **Data IRQ**: High-throughput byte/sample streams (UART RX, SPI DMA, ADC buffers). Data goes to driver-owned ring buffers; ISR emits single `DATA_READY` event only when threshold reached.
   - **Scheduler IRQ**: Timebase / SysTick / high-resolution timer. Directly wakes scheduler runner without entering event queue.
   - **Fault IRQ**: HardFault, NMI, Watchdog panic, Brown-out. Captures registers to `edge_fault_record_t`, switches to safe state, and resets without touching normal runtime queues.
2. **EventQueue Role**: The EventQueue is strictly an Event/Control Plane, never a Data Plane.

## Consequences
- High-rate serial and DMA streaming will not overflow the system control event queue.
- Fault handling is isolated from memory/queue corruption.
