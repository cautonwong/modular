# ADR-005: Module Dependency Model and Deterministic Ordering

## Status
Accepted

## Context
Modules often depend on other modules or platform services (e.g. `watch_ui` depends on `watch_time` and `watch_power`; `zmk_studio` depends on `zmk_keymap`). Relying on array ordering creates fragile implicit dependencies.

## Decision
1. **Explicit Dependency Metadata**: Module descriptors declare an array of prerequisite module IDs via `dependencies` and `dependency_count`.
2. **Topological Order**: Framework and product loaders resolve module dependency DAGs prior to startup.
3. **Deterministic Tie-Breaking**: When multiple modules have identical dependency depth, Module ID ascending order serves as the deterministic tie-breaker.
4. **Cycle Rejection**: Circular dependencies are detected and rejected at assembly/configuration time.

## Consequences
- Predictable startup ordering without implicit positional assumptions.
