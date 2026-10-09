# ADR-007: Event Plane vs Data Plane Separation

## Status
Accepted

## Context
Embedding raw byte arrays or streaming buffers inside fixed-size events (`edge_event_t`) wastes memory, violates the D66 payload token rule, and forces unnecessary copying.

## Decision
1. **Fact vs Data**:
   - **Events (Control Plane)**: Transport discrete scalar facts with timestamps and source tokens (fixed 24-byte struct).
   - **Buffers (Data Plane)**: High-throughput payloads reside in driver-owned lockless SPSC ring buffers (`edge_ring_buffer_t` in `edge/ring_buffer.h`) or DMA ping-pong buffers.
2. **Buffer Notification**: ISR pushes data into the driver ring buffer and emits a lightweight notification token event. Consumer modules drain data directly from the driver port.

## Consequences
- Zero-copy data streaming with fixed bounded memory overhead.
