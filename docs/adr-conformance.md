# ADR conformance

Tracks how far the current implementation matches the decisions in
[`adr.md`](adr.md) (D1-D85). `adr.md` is the single decision source and this file
is the **only** decision-vs-implementation view; `todo.md` (D1-D45) is an archived
early subset.

Legend: ✅ implemented · 🟡 partial · ❌ not implemented · ⛔ contradicts the ADR · 🚫 wontfix (decided not to do).

## Framework and composition

| ADR | Decision | Status | Evidence |
|---|---|---|---|
| D5 | Explicit assembly, no linker-section registration | ✅ | `product/*/main.c` construct apps by hand; no registry section remains |
| D6 | Constructor injection, no service locator | ✅ | `dlt645_construct(self, id, prio, deps)` |
| D7 | No manifest; CMake + `main()` are the only product truth | ✅ | `check_cmake_apps.py` enforces the two lists match |
| D11 | priority sort + `module_id` tie-break | ✅ | `sort_apps()` in `sys/runtime/src/sys.c` |
| D14 | Consumer-defined interfaces, adapters in product glue | ✅ | `dlt645_storage_if_t` + `product/example/glue.c` |
| D15 | Extremely thin `edge_module` | ✅ | Lifecycle callbacks + event primitives + tables; `init`/`deinit` are no longer in the struct (D51), so only scheduling state remains beside the contract |
| D16-D18 | Events are facts; scalar payload; no pointers | ✅ | `edge_event_t{id,source,arg0,arg1,timestamp}` with `_Static_assert` |
| D19/D68 | Unified `edge_status_t` + central error allocation | ✅ | `edge/errors.h` + `check_error_ids.py` (dup / range / zero-segment) + `docs/error-model.md` |
| D20 | Injected clock port | ✅ | `edge/clock.h`, sink timestamps |
| D21 | Zero runtime allocation | ✅ | Only caller-owned storage; no `malloc` |
| D22/D23/D24 | Small, capability-narrow ports | ✅ | `edge/ports.h` (`byte_reader`/`byte_writer`/`storage_kv` + `uart`/`gpio`), with `infra/uart` and `infra/gpio` fakes adapted in tests |
| D30 | `edge_util` header-only library | 🚫 wontfix | Deliberately not built: no shared util is needed yet; revisit only if duplication appears (D30 closed as wontfix) |
| D31 | Single `edge/events.h` + compile-time uniqueness | ✅ | `_Static_assert` guards + `check_event_ids.py` |
| D32 | Explicit `sys_subscribe` in `main()` | ✅ | Products subscribe before `start` |
| D33 | Sink-side monotonic timestamp | ✅ | `edge_event_sink_push_isr()` |
| D36 | `include/<name>/` public + `src/` private | ✅ | All modules follow it |
| D40 | ABI append-only, versioned | 🟡 | Append-only fields + `_Static_assert`; no compatibility matrix |
| D41 | Injected `log_port_t` | ✅ | `edge/log.h` |
| D43 | `extern "C"` headers for C++ | ✅ | All public headers guarded |
| D44 | No architecture code in the framework | ✅ | Registers only in `board/*` |

## Runtime, events and faults

| ADR | Decision | Status | Evidence |
|---|---|---|---|
| D47 | Single runner context; `sys_step()` decomposable | ✅ | `edge_sys_step()` + `edge_sys_run()` |
| D51 | `poll`/`on_event` (+ optional `suspend`/`resume`); `init`/`deinit` outside the struct | ✅ | `edge_module_t` carries `poll`/`on_event`/`power_off`/`suspend`/`resume` only; the composition root calls `<app>_init(self)` / `<app>_deinit(self)` (products do, reverse order at shutdown). Enforced by `check_module_contract.py`. `power_off` stays in the struct because *ordering* is a `sys` duty (D11) |
| D52 | Event-driven + periodic mixed scheduling; idle -> board | ✅ | `period`/`budget`/`edge_sys_idle` hook; the product idle hook feeds the watchdog and calls the board low-power action, then the atomic PAL wait |
| D53 | Non-fatal init failure skipped and recorded | ✅ | `fatal` flag: default skip, `fatal` rolls back |
| D54 | Drop-newest + counter; multi-subscriber; unsubscribe | 🟡 | All done, except multi-SPSC producer queues |
| D55 | Central `edge/modules.h`, `0xNN00` segment, allocated in blocks per owning layer | ✅ | Table-driven `EDGE_MODULE_IDS` (one line per ID; assertions generated from it); `check_module_ids.py` rejects duplicates, misaligned values, out-of-block values and layer tokens without a block |
| D56 | ABI version object | 🟡 | `_Static_assert` layout only |
| D65 | One SPSC queue per producer | ❌ | Single caller-owned queue + injected `edge_irq_guard_t` |
| D66 | Scalar event + token; payload read through a port | ❌ | No buffer/token path yet |
| D67 | API catalogue (framework/runtime/platform/ports) | 🟡 | `errors.h`/`modules.h`/`ports.h` present; naming is `edge_sys_*`, not `sys_*` |
| D69 | Bounded runner step | ✅ | `max_events_per_run` (default 8), one due poll per module per step |
| D70 | Reentrant `sys_publish` deferred into a runner queue | ✅ | `edge_sys_publish()` + `pending_high_water` |
| D71 | Atomic idle/low-power sequence in board/PAL | ✅ | `edge_os_idle_wait(pal, pending, ctx)` = critical-enter -> re-check (`edge_sys_pending`) -> `pal->idle` (Cortex-M `WFI`) -> critical-exit; board provides enter-low-power / feed-watchdog / reset; `edge_sys_healthy` is the watchdog input. ISR half: `edge_rtos_irq_guard()` brackets the push in a BASEPRI save/restore |
| D72 | Wrap-safe tick comparison | ✅ | Deadlines use the modular compare `(int64_t)(now - due) >= 0` in `sys/runtime/src/sys.c`; extending a *counter* across a wrap is a different operation and is stated the other way in `pal/os/include/pal_os/tick64.h` (unsigned `now < last` carry, with the once-per-2^31-tick sampling ceiling), host-tested in `tests/test_tick64.c`. The two are documented together so they cannot be mixed up |
| D74 | `sys_stats` aggregate | ✅ | `edge_sys_stats_t` + `edge_sys_stats_reset()` |

## Platform, tooling and process

| ADR | Decision | Status | Evidence |
|---|---|---|---|
| D25 | CMake drives GCC + IAR, `.ewp` debug only | 🟡 | `cmake/toolchains/iar-arm.cmake` exists, not in CI |
| D26 | `edge_add_product(...)` | ✅ | CMake function + registries |
| D27/D28 | Include isolation + compile-time checks, refutation tests | 🟡 | Per-target includes, `check_app_isolation.py`, guard self-tests; heuristic, IAR not covered |
| D37 | Map-based flash/RAM budget | ✅ | `check_map_budget.py` + `ci/size-budget.json` |
| D38 | Reproducible build + metadata | ✅ | `SOURCE_DATE_EPOCH`, byte-compare, provenance, SBOM |
| D39 | Full legal family x board matrix build | 🟡 | Whitelist + 3 products + negative cases; not enumerated |
| D45 | Certification boundary (core vs app) | 🟡 | Documented boundary; no enforcement artefact |
| D46/D85 | `pal/` per (architecture x RTOS); board selects | 🟡 | Two architecture PALs are real, both behind `edge/pal.h` and selected by the product's board: `pal/cortex-m-bare` (PRIMASK critical sections, DSB, SysTick time, WFI) and `pal/rtos/freertos` (`pal_rtos_freertos.h`: `taskENTER_CRITICAL` task-context critical sections, DSB, kernel tick extended to 64 bit, `xPortIsInsideInterrupt`, WFI idle) with the ISR half as `edge_rtos_irq_guard()`. Layering enforced by `check_layer_dependencies.py`. Open: the RTOS PAL is still selected inside the firmware target rather than by an explicit board/product binding (#76), and the Zephyr / host-POSIX ports do not exist yet (#82, #88). The architecture half of the binding is now enforced rather than assumed: `pal/cortex-m-bare` compiles only for `__arm__` or with the explicit host-test macro `EDGE_PAL_CORTEX_M_HOST_TEST`, its CMake target is not created for a non-Cortex-M toolchain, and `check_pal_arch_binding.py` compiles the port both ways and requires the refusal to keep working |
| D48 | Driver model neutral; infra depends on narrow ports only | ✅ | `infra -> soc` is denied with **no exception** (`check_layer_dependencies.py`); SoC-bound (register/HAL) code lives in `soc/<soc>/`, OS device models in `pal/<os>/`, binding in product glue. A second portable implementation is tracked in #57 |
| D50 | RTOS neutrality: the framework calls no RTOS API, RTOS appears only in `product/main` and `pal/` | ✅ | No FreeRTOS header escapes `pal/rtos/freertos`: `pal_rtos_freertos.h` exposes only `edge_*` types, and every kernel call (critical sections, `xPortIsInsideInterrupt`, `xTaskGetTickCount*`, BASEPRI mask) lives in `pal/rtos/freertos/src/freertos_pal.c`. `check_app_isolation.py` rejects `FreeRTOS.h` / `task.h` / `cmsis_os.h` / `zephyr/` in any app |
| D49 | Framework and `sys` have zero SoC knowledge; `soc/` is a support package selected by the board | ✅ | `soc/mps2` selected by `board/mps2`; `soc -> soc` only, enforced by `check_layer_dependencies.py` |
| D57-D61 | Host unit tests, cmocka, fakes, fake clock/PAL | 🟡 | cmocka + host PAL, and D57's host half is executable now: reusable contract suites in `tests/contract/` (app lifecycle, port shapes, board IRQ) are run by every app test and by `tests/test_contract.c`, with `tests/contract_violations/` making CTest prove the suites reject a broken implementation. Still open: a central `test/fakes/` (D60), Renode/HIL (D62/D63, #22/#30). D61's "fake clock / empty PAL for the host" is no longer a convention: the host fallback inside `pal/cortex-m-bare` is opt-in per build, so a fake clock cannot reach a target build by accident |
| D62/D63 | Renode for board/infra; HIL for the rest | ❌ | QEMU MPS2 smoke only |
| D64 | 4-stage CI (host -> target -> Renode -> HIL) | 🟡 | Stages 1-2 done, 3-4 missing |
| D73 | 1 app may expose 1..n modules | 🟡 | Model supports it; no multi-module example |
| D78 | App-to-app interaction only via a consumer-defined interface or an event | ✅ | `app/meter_core` (provider) + `app/modbus_slave` (consumer `modbus_store_if`) adapted in `product/meter_gateway_host`; enforced by `check_app_isolation.py` / `check_layer_dependencies.py` |
| D75 | Bounded ISR (clear + push only) | ✅ | Board ISRs only clear and push; stack usage is gated by `check_stack_usage.py` + FreeRTOS overrun detection (`docs/stack-usage.md`) |
| D84 | Shared IRQ multi-handler via `board_irq_attach` | 🟡 | `edge_irq_guard_t` exists; no `board_irq_attach` API |
| D86 | `product/<name>` binds exactly one board; the binding cannot be overridden by build parameters | ✅ | `edge_add_product()` records `EDGE_PRODUCT_<name>_BOARD` and rejects duplicate registration or re-binding; enforced by `check_product_board_binding.py` with negative fixtures. The neutrality fixture `edge_add_minimal_variant` is a test fixture and exempt |
| D87 | FreeRTOS config is composed by the product (soc/board/product); the PAL only declares the contract and the required invariants | ✅ | `FreeRTOSConfig.h` is a composer carrying `#error` invariants; `soc/mps2` + `board/mps2` + `product/meter_mps2_freertos/edge_freertos_config.h` supply the values; `configASSERT` routes to `edge_rtos_assert_failed()` + an observable product hook |
| D34/D35/D76 | Distribution, compliance, OTA | ❌ | Separate workstreams, out of scope by decision |
| D88 | Every area directory declares its own build target; the top-level file only discovers areas and composes products | ✅ | `cmake/EdgeTargets.cmake` defines the module shape once; `app/*`, `infra/*`, `board/*`, `soc/*`, `sys/*`, `pal/*` each carry a `CMakeLists.txt`; host tests name their own dependencies; enforced by `check_area_registration.py` with both fixtures, and `check_cmake_apps.py` / `check_product_board_binding.py` were re-pointed at the area files |

## Open conflicts

1. **D65 (multi-SPSC)**: the implementation deliberately uses one queue plus an
   injected IRQ guard, which is the scheme the ADR chose *against*.
2. **API naming**: the ADR catalogue uses `sys_run/sys_step/sys_idle/...`; the
   implementation uses the `edge_sys_*` prefix.

## Maintenance

When a decision is implemented or a new conflict is found, update the matching
row here. `todo.md` is archived and is not updated; this file is the single
tracking entry point for ADR-vs-code drift.

Automatable decisions are also mapped to a gate in `ci/adr-gates.json` and
checked by `.github/scripts/check_adr_gates.py`; see [`governance.md`](governance.md)
for the matrix and the paper-only ratchet.
