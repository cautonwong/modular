# ADR-009: EventQueue Backpressure and Overflow Diagnostics

## Status
Accepted

## Context
Bursts of external stimuli (e.g. rapid touch gestures, button bounces, serial bursts) can fill the fixed-capacity event queue. Silent event drops prevent diagnostics and lead to erratic device behavior.

## Decision
1. **Drop Policy**: When the event queue is saturated:
   - Non-critical telemetry/sampling events follow a drop-newest policy with atomic increment of `overflow_count`.
   - Critical events (power panic, emergency stop, fault alarms) utilize reserved head slots.
2. **Event Coalescing**: Consecutive redundant events from identical sources (e.g. duplicate `UART_RX_READY` or repeated IMU motion triggers) are coalesced if prior events are still pending in the queue.
3. **Overflow Diagnostics**: Overflow counts and high-water marks are exposed to telemetry and health monitoring.

## Consequences
- Guaranteed system stability under event storms without buffer overrun crashes.
