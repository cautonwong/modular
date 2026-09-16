# TODO implementation status

This file tracks the frozen architecture TODO against the current repository.

| Area | Status | Implementation |
|---|---|---|
| Thin framework | done | `edge_module` owns the module lifecycle contract and bounded event primitives. |
| Explicit composition | done | `product/example/main.c` constructs the app, binds ports, subscribes events, then starts `sys`. |
| Consumer-defined port | done | `app/dlt645` owns `dlt645_storage_if_t`; `product/example/glue.c` adapts `infra/flash`. |
| Scheduler | done | `sys/example` sorts by priority then `module_id`, validates required IDs, starts with rollback, polls cooperatively, supports per-module `period`, and shuts down in reverse order. |
| Event routing | done | Board pushes into an injected sink; `sys_subscribe()` explicitly routes event IDs to app callbacks. |
| ISR safety boundary | done | Event payload is scalar-only; timestamp is injected; multi-IRQ sinks can inject `edge_irq_guard_t`. |
| Dependency isolation | done | CI checks app includes and forbidden RTOS/framework headers. |
| Event ID governance | done | Central `edge_module/include/edge/events.h`, `_Static_assert` guards, and CI collision checker. |
| Runtime budget / isolation | done | Bounded event dispatch, injected-clock execution budget accounting, per-module fault isolation, and runtime statistics are implemented without RTOS/thread dependencies. |
| Tests | done | CMocka covers queue timestamp/overflow and scheduler ordering/routing/rollback plus runtime budget, bounded dispatch, statistics and fault isolation. |
| CI/CD | done | GCC/Clang Debug+Release, ASan/UBSan, CMocka with JUnit reports, gcovr coverage gate, clang-format/clang-tidy/cppcheck, architecture guards with self-tests, Cortex-M0 cross build with size gate, reproducible firmware + provenance/SBOM, pinned Actions, CodeQL and a tag-driven release pipeline. Cortex-M4 + QEMU smoke is now a separate workflow; IAR/iccarm and the full family×board×app matrix remain. |
| Hardware validation | in progress | Cortex-M4 firmware is built and exercised on QEMU MPS2 AN386. Renode/HIL and real MCU IRQ validation remain target-specific. |
| Scheduler budgets | partial | Periodic scheduling, execution-budget accounting, bounded event dispatch and fault isolation are implemented. Idle/low-power policy, high-water marks and richer diagnostics remain. |
| Target portability | partial | Cortex-M0 and Cortex-M4 ARM GNU builds are present. RV32 target and emulator smoke remain. |
| Distribution | pending | ABI/contract compatibility matrix, SDK packaging and compliance are separate follow-up work. |

## Current runtime contract

- `period == 0` means event-driven module; it is not polled by the foreground scheduler.
- `budget == 0` disables execution-budget checking; otherwise elapsed injected monotonic ticks above the budget are reported as `EDGE_EOVERFLOW` while the scheduler continues.
- A module returning an error from `init`, `poll`, or `on_event` is isolated by marking it failed; other modules continue to run.
- Event dispatch is bounded by `max_events_per_run` (default 8) so a continuously non-empty queue cannot starve periodic foreground work.
- The current implementation has one caller-owned event queue per `edge_sys_t`; multi-SPSC producer queues remain future work.

## Important cutover

The obsolete descriptor/preamble manager, linker-section registration path, generic service/resource registry and old flat app/board APIs are no longer part of the active implementation. The repository follows the frozen explicit foreground architecture directly; there is no V2/V3 compatibility layer.
