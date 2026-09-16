# TODO implementation status

This file tracks the frozen architecture TODO against the current repository.

| Area | Status | Implementation |
|---|---|---|
| Thin framework | done | `edge_module` owns the module lifecycle contract and bounded event primitives. |
| Explicit composition | done | `product/example/main.c` constructs the app, binds ports, subscribes events, then starts `sys`. |
| Consumer-defined port | done | `app/dlt645` owns `dlt645_storage_if_t`; `product/example/glue.c` adapts `infra/flash`. |
| Scheduler | done | `sys/runtime` provides the shared runtime (priority + `module_id` ordering, required validation, rollback, injected clock, per-module `period`/`budget`, bounded dispatch, fault isolation, stats); `sys/example` and `sys/meter` are family wrappers. |
| Event routing | done | Board pushes into an injected sink; `sys_subscribe()` explicitly routes event IDs to app callbacks. |
| ISR safety boundary | done | Event payload is scalar-only; timestamp is injected; multi-IRQ sinks can inject `edge_irq_guard_t`. |
| Dependency isolation | done | CI checks app includes and forbidden RTOS/framework headers. |
| Event ID governance | done | Central `edge_module/include/edge/events.h`, `_Static_assert` guards, and CI collision checker. |
| Runtime budget / isolation | done | Bounded event dispatch, injected-clock execution budget accounting, per-module fault isolation, and runtime statistics are implemented without RTOS/thread dependencies. |
| Tests | done | CMocka covers queue timestamp/overflow, scheduler ordering/routing/rollback, runtime budget, bounded dispatch, statistics, fault isolation, DLT645 and relay app contracts, GPIO/flash infra, and the host PAL <-> event-sink bridge. |
| Product matrix | done | `edge_add_product(family board infra apps)` resolves targets from registries; `example`, `meter_host`, `meter_mps2` build and run, and illegal family/board/app/infra combinations are rejected by CMake with negative CI cases. |
| Size budget | done | ELF Flash/RAM totals plus map-file per-layer attribution (`edge_module/sys/board/infra/app/product/startup/other`) via `check_map_budget.py` and `ci/size-budget.json`. |
| Platform abstraction | partial | `edge/pal.h` contract plus a tested `pal/host` implementation wired through the event sink. Architecture-specific (cortex-m-bare) and RTOS-neutral PALs remain. |
| CI/CD | done | GCC/Clang Debug+Release, ASan/UBSan, CMocka with JUnit reports, gcovr line gate (95%), clang-format/clang-tidy/cppcheck, architecture guards with self-tests, Cortex-M0/Cortex-M4 cross builds, MPS2 QEMU IRQ smoke, map-level size budgets, reproducible firmware + provenance/SBOM, pinned Actions, CodeQL and a tag-driven release pipeline. IAR/iccarm remains an opt-in toolchain file. |
| Hardware validation | in progress | Cortex-M4 firmware is built and exercised on QEMU MPS2 AN386 (timer IRQ -> event -> superloop). Renode needs a custom MPS2/CMSDK platform description; real MCU HIL needs a self-hosted runner and is not yet wired. |
| Scheduler budgets | partial | Periodic scheduling, execution-budget accounting, bounded event dispatch, fault isolation and statistics are implemented. Idle/low-power policy, high-water marks and watchdog policy remain. |
| Target portability | partial | Cortex-M0 and Cortex-M4 ARM GNU builds are present, plus an IAR/iccarm CMake toolchain file. RV32 and RTOS-neutral PAL remain. |
| Distribution | pending | ABI/contract compatibility matrix, SDK packaging and compliance are separate follow-up work. |

## Current runtime contract

- `period == 0` means event-driven module; it is not polled by the foreground scheduler.
- `budget == 0` disables execution-budget checking; otherwise elapsed injected monotonic ticks above the budget are reported as `EDGE_EOVERFLOW` while the scheduler continues.
- A module returning an error from `init`, `poll`, or `on_event` is isolated by marking it failed; other modules continue to run.
- Event dispatch is bounded by `max_events_per_run` (default 8) so a continuously non-empty queue cannot starve periodic foreground work.
- The current implementation has one caller-owned event queue per `edge_sys_t`; multi-SPSC producer queues remain future work.

## Important cutover

The obsolete descriptor/preamble manager, linker-section registration path, generic service/resource registry and old flat app/board APIs are no longer part of the active implementation. The repository follows the frozen explicit foreground architecture directly; there is no V2/V3 compatibility layer.
