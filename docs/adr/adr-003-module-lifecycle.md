# ADR-003: Module Lifecycle Contracts and Descriptors

## Status
Accepted

## Context
Modules previously mixed initialization logic across ad-hoc `construct()` functions and direct property assignment, risking partial initialization states and silent startup failures.

## Decision
1. **Lifecycle Phases**: Formalize the 5-stage lifecycle contract:
   - `init`: Allocate state, bind dependencies, configure hardware registers. Returns `edge_status_t`.
   - `start`: Begin periodic scheduling or event listening.
   - `stop`: Gracefully terminate execution and release temporary resources.
   - `suspend`: Prepare state for power saving / low power mode.
   - `resume`: Restore clocks, peripherals, and operational state upon wakeup.
2. **Module Descriptor**: Provide `edge_module_desc_t` (`edge/desc.h`) declaring standard metadata and lifecycle function pointers.
3. **Rollback Guarantee**: If `init` fails for module $N$, previously initialized modules $1 \dots N-1$ are automatically stopped in reverse order (`N-1 \dots 1`), preventing resource leaks.

## Consequences
- Deterministic initialization and rollback across all host and target platforms.
