# ADR-002: Event ID Ownership and Domain Allocation

## Status
Accepted

## Context
Centralizing all business event IDs in `edge_module/include/edge/events.h` required the framework to know every domain event (watch touch/gestures, ZMK matrix/keycodes/layers, meter pulses, DLT645 frames). This broke modular boundaries and prevented independent evolution of application modules.

## Decision
1. **Framework vs Business Events**: `edge/events.h` defines strictly generic framework-level events (`EDGE_EVT_SYSTEM_START`, `EDGE_EVT_TIMER`, `EDGE_EVT_ERROR`).
2. **Domain Event Segments**: Each subsystem domain owns an allocated segment (e.g. `0x2100` for ZMK, `0x1600` for Watch) and defines its local events within domain headers (`0x2101`, `0x2102`, etc.).
3. **CI Validation**: `check_event_ids.py` scans headers across the repo to enforce uniqueness, valid segment ranges, and scalar payload compliance.

## Consequences
- Business domains can introduce new events in their domain headers without modifying the framework core.
