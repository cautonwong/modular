# TODO implementation status

> Summary view only. [`docs/adr-conformance.md`](adr-conformance.md) is the
> single authoritative decision-vs-implementation diff view; `todo.md` is an
> archived D1-D45 subset.

| Area | Status | Implementation |
|---|---|---|
| Thin framework | done | `edge_module` owns the module lifecycle contract (incl. optional `suspend`/`resume`), bounded event primitives, the central event/module ID and error tables, and the canonical narrow port shapes. |
| Explicit composition | done | `product/example/main.c` constructs the app, binds ports, subscribes events, then starts `sys`. |
| Consumer-defined port | done | `app/dlt645` owns `dlt645_storage_if_t`; `product/example/glue.c` adapts `infra/flash`; `edge/ports.h` provides optional canonical shapes. |
| Scheduler | done | `sys/runtime` provides the shared runtime (priority + `module_id` ordering, required validation, rollback, injected clock, per-module `period`/`budget`, bounded dispatch, fault isolation, stats, idle hook); `sys/example` and `sys/meter` are family wrappers. |
| Event routing | done | Board pushes into an injected sink; `sys_subscribe()`/`sys_unsubscribe()` explicitly route event IDs to app callbacks; `edge_sys_publish()` defers runner-originated facts. |
| ISR safety boundary | done | Event payload is scalar-only; timestamp is injected; multi-IRQ sinks can inject `edge_irq_guard_t`. |
| Dependency isolation | done | CI checks app includes and forbidden RTOS/SoC headers, raw register access (N3) and heavy libc use (N5). |
| Event ID governance | done | Central `edge_module/include/edge/events.h`, `_Static_assert` guards, and CI collision checker. |
| Module ID governance | done | Central `edge_module/include/edge/modules.h`, `0xNN00` segment asserts, and `check_module_ids.py`. |
| Error allocation | done | Central `edge_module/include/edge/errors.h` with the `EDGE_ERR(segment, code)` composition rule. |
| Runtime budget / isolation | done | Bounded event dispatch, injected-clock execution budget accounting, non-fatal init skip with `fatal` rollback opt-in, `suspend`/`resume`, idle hook, fault isolation and runtime statistics without RTOS/thread dependencies. |
| Tests | done | CMocka covers queue timestamp/overflow, scheduler ordering/routing/rollback, runtime budget, bounded dispatch, statistics/reset, fault isolation, publish/unsubscribe/suspend/resume/idle, DLT645 and relay app contracts, GPIO/flash infra, and the host PAL <-> event-sink bridge. |
| Product matrix | done | `edge_add_product(family board infra apps)` resolves targets from registries and enforces a legal family x board whitelist; `example`, `meter_host`, `meter_mps2` build and run, and unknown components plus illegal pairs are rejected by CMake with negative CI cases. |
| Size budget | done | ELF Flash/RAM totals plus map-file per-layer attribution (`edge_module/sys/board/infra/app/product/startup/other`) via `check_map_budget.py` and `ci/size-budget.json`. |
| Platform abstraction | partial | `edge/pal.h` contract plus a tested `pal/host` implementation wired through the event sink. Architecture-specific (cortex-m-bare) and RTOS-neutral PALs remain. |
| CI/CD | done | GCC/Clang Debug+Release, ASan/UBSan, CMocka with JUnit reports, gcovr line gate (95%), clang-format/clang-tidy/cppcheck, architecture guards with self-tests, Cortex-M0/Cortex-M4 cross builds, MPS2 QEMU IRQ smoke, map-level size budgets, reproducible firmware + provenance/SBOM, pinned Actions, CodeQL and a tag-driven release pipeline. IAR/iccarm remains an opt-in toolchain file. |
| Hardware validation | in progress | Cortex-M4 firmware is built and exercised on QEMU MPS2 AN386 (timer IRQ -> event -> superloop). Renode needs a custom MPS2/CMSDK platform description; real MCU HIL needs a self-hosted runner and is not yet wired. |
| Scheduler budgets | partial | Periodic scheduling, execution-budget accounting, bounded event dispatch, fault isolation, idle hook and statistics are implemented. Low-power board wiring, watchdog policy and per-module high-water marks remain. |
| Target portability | partial | Cortex-M0 and Cortex-M4 ARM GNU builds are present, plus an IAR/iccarm CMake toolchain file. RV32 and RTOS-neutral PAL remain. |
| Distribution | pending | ABI/contract compatibility matrix, SDK packaging and compliance are separate follow-up work. |

## Current runtime contract

- `period == 0` means event-driven module; it is not polled by the foreground scheduler.
- `budget == 0` disables execution-budget checking; otherwise elapsed injected monotonic ticks above the budget are reported as `EDGE_EOVERFLOW` while the scheduler continues.
- A module returning an error from `init`, `poll`, or `on_event` is isolated by marking it failed; other modules continue to run.
- `init` failure with `fatal == 0` is skipped and counted; with `fatal == 1` the already started modules are rolled back and the product fails (`EDGE_SYS_FAILED`).
- Event dispatch is bounded by `max_events_per_run` (default 8) so a continuously non-empty queue cannot starve periodic foreground work.
- `edge_sys_publish()` is runner-context only and requires a caller-bound pending queue (`edge_sys_bind_pending_queue`); overflow drops the newest event and increments `stats.drops`, with `stats.pending_high_water` tracking the deepest backlog.
- `edge_sys_step()` is the decomposable runner step (alias of `edge_sys_run_once`); `edge_sys_run()` loops `while (state == RUNNING)` for bare-metal products.
- `edge_sys_idle()` runs when a step processed no event and no poll; it increments `stats.idle_calls` and calls the injected idle hook.
- `suspend`/`resume` are optional module callbacks; `edge_sys_suspend_all()`/`edge_sys_resume_all()` skip failed modules and are used for low-power entry/exit.
- `edge_sys_stats_reset()` clears the aggregate counters.
- The current implementation has one caller-owned event queue per `edge_sys_t`; multi-SPSC producer queues remain future work.

## Important cutover

The obsolete descriptor/preamble manager, linker-section registration path, generic service/resource registry and old flat app/board APIs are no longer part of the active implementation. The repository follows the frozen explicit foreground architecture directly; there is no V2/V3 compatibility layer.
