# ADR-008: ISR Coding Rules and Execution Constraints

## Status
Accepted

## Context
Interrupt service routines executing non-reentrant libc routines, unbounded loops, or invoking business callbacks directly cause priority inversions, deadlocks, and corrupted module state.

## Decision
1. **Mandatory ISR Rules**:
   - **ISR-001**: ISRs must NEVER invoke application callbacks directly.
   - **ISR-002**: ISRs must NEVER call blocking or waiting functions.
   - **ISR-003**: ISRs must NEVER invoke dynamic memory allocation (`malloc`, `free`).
   - **ISR-004**: ISRs must NEVER call formatted I/O (`printf`).
   - **ISR-005**: ISRs must perform minimal hardware capture and acknowledge interrupts immediately.
   - **ISR-006**: ISRs must have strict execution time budgets (sub-microsecond for fast paths).
2. **Static & CI Enforcement**: CI rules detect forbidden function calls inside ISR compilation units.

## Consequences
- Bounded, predictable ISR latency across all MCU architectures.
