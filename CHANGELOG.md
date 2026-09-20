# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `pal/eos`: the first-party runtime, a static zero-allocation fixed-rate priority
  executive (D46/D85). It is a module inside the `pal` layer, sibling to
  `pal/rtos/freertos`, and implements the same neutral contract - which is what
  makes that contract a contract rather than a description of one kernel. Slice 1
  has no preemption (no PendSV context switch, so no context-switch latency to
  report), no MPU, and is not yet linked into any product.
- `docs/eos.md`: what EOS is, what it guarantees, the measured metrics with the
  command that produces each, a published-claim benchmark against a commercial
  safety RTOS, and a non-claims block that can be copied into a README.

### Added

- `edge_rtos_wait_for_work()` / `edge_rtos_wake_from_isr()` /
  `edge_rtos_wake_target_set_self()`: the runner can park until work arrives, and
  the ISR path can wake it, yielding only when a task was actually made ready
  (D47/D71). Without this the runner could only poll, which `docs/low-power.md`
  puts ~1800x over a ten-year battery budget on the fixed cost per wake alone.
- `docs/rtos-runner.md`: the idle/tickless decision, the wake path end to end, the
  module-suspend versus task-suspend boundary, the module-to-kernel priority rule,
  the bounded yield policy, and how the on-target exit status aligns with the
  in-process stats.

### Changed

- The FreeRTOS product's capsule parks on a notification instead of polling, and
  tickless sleep is on (`configUSE_TICKLESS_IDLE`), which is only reachable now
  that the runner blocks. The sink guard is composed in the composition root as
  mask + wake (D14), so the board ISR stays RTOS-free (D85).
- A lowest-priority witness task plus a five-tick yield probe make the yield policy
  a refutation test: blocking passes, and the same probe with a bare `taskYIELD()`
  fails with `rc=16` (a lower-priority task was starved). The probe measures
  progress inside its window, because measuring the accumulated counter let the
  starving variant pass.
- The PAL config contract now requires `configUSE_TASK_NOTIFICATIONS` and
  `INCLUDE_xTaskGetCurrentTaskHandle`, the two kernel features the wake path needs.

### Fixed

- `pal/cortex-m-bare` SysTick read race: `monotonic_ticks` read `SYSTICK_CTRL`
  (which clears COUNTFLAG) and then `SYSTICK_VAL`, so a wrap landing between the
  two reads was never counted and the clock jumped *backwards* by a whole period
  (~0.67 s at 25 MHz). The wrap is now detected from the counter itself - SysTick
  counts down, so a sample above the previous one means it reloaded - which does
  not depend on `COUNTFLAG` read semantics (the MPS2 model under QEMU does not
  report the flag as documented, so the flag-based detector silently lost the
  wrap). The MPS2 smoke build samples a long window and requires the clock never
  to go backwards and to advance; the deterministic wrap-crossing refutation is
  the host test.
- `pal/cortex-m-bare` wrap accounting had two undocumented preconditions that
  could silently corrupt the 64-bit timeline: the sample interval (COUNTFLAG is
  one bit, so two wraps between samples look like one) and a variable
  `SYSTICK_LOAD`. The period is now read once in
  `edge_pal_cortex_m_bare_init` and cached in the state, and both preconditions
  are stated in the header.
- `pal/cortex-m-bare` could be selected by a non-ARM target, where it silently
  fell back to a call-counting fake clock that compiled, linked and passed.
  The port now refuses to compile without `__arm__` or the explicit host-test
  macro `EDGE_PAL_CORTEX_M_HOST_TEST`, and the CMake target is not created for a
  non-Cortex-M toolchain. Enforced by `check_pal_arch_binding.py`, which compiles
  the port both ways and requires the refusal to keep working.

### Added

- `edge_pal_cortex_m_extend()`: the 64-bit SysTick extension arithmetic is now a
  pure function, so the part of the port that cannot run on the host is still
  covered by host tests - wrap boundary, VAL == LOAD, the wrap race in both
  directions, and an observable `anomalies` counter when `SYSTICK_LOAD` changes
  after init (a contract violation is reported, never absorbed).

- FreeRTOS architecture PAL (D46/D50/D85): `pal/rtos/freertos` implements the
  full `edge_pal_port_t` - task-context `taskENTER_CRITICAL`/`EXIT` critical
  sections (mask-based, nesting), a hardware `DSB` barrier, the kernel tick
  extended to 64 bit, `xPortIsInsideInterrupt` context detection, and a `WFI`
  idle - plus `edge_rtos_irq_guard()` as the ISR-side half (BASEPRI save/restore
  around the event push). No FreeRTOS header escapes the port.
- `pal/os`: `edge_tick64_extend()`, the neutral 32-bit to 64-bit tick extension
  used by the kernel-tick clock (D72). Host-tested, including three consecutive
  wraps.
- `docs/low-power.md`: the requirement analysis for battery targets (water
  meters, watches) - the per-wake energy budget that rules out polling, the
  32-bit tick wrap horizon per `configTICK_RATE_HZ`, the three idle layers and
  their owners, posting atomicity, and watchdog policy during sleep.
- `board/mps2` gains `board_mps2_timer_set_priority()`, and the SoC gains the
  NVIC priority encoder: an RTOS ISR handler must sit at or below
  `configMAX_SYSCALL_INTERRUPT_PRIORITY` before the IRQ is enabled.

- Low-power closure (D9/D52/D71): PAL gains an `idle` primitive (Cortex-M `WFI`),
  `pal/os` provides the atomic `edge_os_idle_wait` (critical enter -> re-check ->
  wait -> release), boards gain enter-low-power / feed-watchdog / reset actions,
  `sys` exposes `edge_sys_healthy` (watchdog input) and `edge_sys_pending` (the
  re-check), and the MPS2 product wires the idle hook to them.
- Bare-metal Cortex-M PAL (`pal/cortex-m-bare`, D46/D85): PRIMASK critical
  sections (nesting counted), DSB memory barrier, free-running SysTick extended to
  64-bit monotonic time, and IPSR ISR detection, with a host fallback so the port
  is analyzable and contract-tested off target. The MPS2 product uses it as the
  event-sink guard and clock.
- Stack usage gate (D75): `-fstack-usage` on ARM/RISC-V, `check_stack_usage.py`
  (per-function budget + JSON report, wired into all four firmware jobs), plus
  FreeRTOS `configCHECK_FOR_STACK_OVERFLOW 2` and a task high-water assertion.
  See `docs/stack-usage.md`.
- App-to-app interaction example (D78): `app/meter_core` (provider, concrete API)
  consumed by `app/modbus_slave` (consumer-defined `modbus_store_if`) through a
  composition-root adapter in `product/meter_gateway_host`; a unit test and an
  interaction test prove neither app includes the other. New central
  `EDGE_MOD_METER`. See `docs/app-interaction.md`.
- Closed the `infra -> soc` backdoor: the layer matrix now denies it by default
  (`infra -> infra + pal + edge_module`); a register-level, SoC-named implementation
  must be explicitly listed in `INFRA_SOC_BOUND`. This registers the `soc/` + `pal/`
  regularisation and the `tests/integration/minimal_product/` fixture in the log.
- Layer dependency guard `check_layer_dependencies.py` enforcing the topology
  matrix (`soc -> soc`, `board -> soc/pal`, `infra -> soc`, `app -> self + edge_module`, ...)
  with positive/negative fixtures; README topology and `adr-conformance.md` now
  include `soc/` and `pal/`. Unified test layout by moving `test/minimal_product/`
  to `tests/integration/minimal_product/`.
- Neutrality acceptance (D57 / section 17.21): `check_app_transitive_includes.py`
  (T7a), a parameterised `tests/integration/minimal_product/` with baremetal + thread host
  runners (T7b), a host thread-model test (T7c), and a CI `neutrality` job that
  builds the board x runner matrix and asserts `git diff --exit-code -- app/`.
  New real `soc/mps2` SoC package selected by `board/mps2`. See `docs/neutrality.md`.
- Second domain app `app/modbus_slave` (Modbus RTU slave: coils/holding registers,
  function codes 0x01/0x03/0x05/0x06/0x10, exception responses, CRC16) on
  consumer-defined store/transport ports, plus the dual-protocol
  `product/meter_gateway_host` running `dlt645` + `modbus_slave` on one runner.
  New central `EDGE_MOD_MODBUS` and `EDGE_EVT_MODBUS_RX` IDs.
- ADR -> gate governance: `ci/adr-gates.json` + `check_adr_gates.py` (27 gated,
  58 paper-only tracked), `check_event_payload.py` (D18 scalar payload) and
  `check_no_dynamic_memory.py` (D21, wired into all four firmware jobs). See
  `docs/governance.md`.
- `check_error_ids.py` (D68) rejecting duplicate framework values, values outside
  `-99 .. 0`, zero-segment `EDGE_ERR` allocations and module-error collisions;
  positive/negative fixtures wired into CI and the guard self-test. Per-layer
  error sets are documented in `docs/error-model.md`.
- RISC-V 32 (`rv32imc_zicsr`) bare-metal target: `cmake/toolchains/riscv-elf.cmake`,
  `board/riscv_virt` (CLINT machine timer + QEMU test finisher), `product/riscv_meter`
  and a `riscv32-qemu` CI job that builds and runs it under `qemu-system-riscv32 -M virt`.
  It reuses the same `app/dlt645` source with zero app changes.
- `pal/os` contract (`edge_os_port_t` yield/sleep) and `edge_os_idle_hook`, which
  bridges it to the sys idle hook, with a tested host implementation.
- FreeRTOS host: a neutral `pal/rtos` contract (`edge_rtos_task_create`/`edge_rtos_start`)
  with a FreeRTOS implementation, and `product/meter_mps2_freertos` running the
  sys capsule as a single task on QEMU Cortex-M4 (sibling task injects events
  through the same sink). RTOS headers stay confined to `pal/rtos`.
- Central module ID table (`edge/modules.h`) and error allocation table
  (`edge/errors.h`) with `EDGE_ERR(segment, code)` composition and static asserts.
- Canonical narrow port shapes (`edge/ports.h`): byte reader/writer and storage KV,
  and now `edge_uart_port_t`/`edge_gpio_port_t` with an `infra/uart` host fake and
  `gpio_read`, exercised by `tests/test_ports.c`.
- Runtime API completion for the ADR catalogue: `edge_sys_step`/`edge_sys_run`,
  `edge_sys_idle` plus an injectable idle hook, `edge_sys_publish` with a
  runner-owned deferred queue (`edge_sys_bind_pending_queue`),
  `edge_sys_unsubscribe`, `edge_sys_suspend_all`/`edge_sys_resume_all`, and
  `edge_sys_stats_reset`.
- Optional `suspend`/`resume` module callbacks and a `fatal` module flag.
- `check_module_ids.py` with positive/negative fixtures, wired into the guard
  self-test and CI.
- App isolation guard now rejects raw register access (N3) and heavy libc use
  (N5), with negative fixtures.

### Changed

- The FreeRTOS product now takes its time from the kernel tick (the fake
  `++tick` clock is gone), installs the ISR guard on its event sink, and is fed
  by the real TIMER0 interrupt instead of a sibling injecting task. The FreeRTOS
  firmware wires `EDGE_BOARD_TIMER_ISR`, which it previously left unset - the
  first interrupt landed in `default_handler`'s `WFI` loop.
- `board_mps2_exit()` uses `SYS_EXIT_EXTENDED`, so a target run reports its real
  exit status (an assert at 9 no longer arrives as a generic 1). The FreeRTOS
  QEMU smoke step asserts status 0.

- `init` failures are now skipped and recorded by default; only modules marked
  `fatal` roll back the already started modules and fail the product (D53).
- Product module IDs come from the central table instead of magic numbers.
- `edge_add_product()` enforces a legal family x board whitelist; a new negative
  CI case covers a known-but-illegal pair.
- Runtime stats gained `idle_calls` and `pending_high_water`.
- `edge_module_t` grew append-only (`suspend`, `resume`, `suspended`, `fatal`);
  the ABI size assertions were updated (104 bytes LP64 / 64 bytes ARM32).

### Docs

- Added `docs/adr-conformance.md`; refreshed README, `todo.md` and
  `docs/todo-status.md` to match the implemented state.

### Added

- Shared `sys/runtime` scheduler with per-module `period`/`budget`, injected clock,
  bounded event dispatch, fault isolation and runtime statistics; `sys/example`
  and `sys/meter` are thin family wrappers.
- Generic `edge_add_product(family board infra apps)` product combinator with
  sys/board/infra/app registries, three legal products (`example`, `meter_host`,
  `meter_mps2`) and CMake negative validation for illegal combinations.
- New axes: `board/mps2` (Cortex-M4 MPS2 AN386), `app/relay` + `infra/gpio`.
- Map-file per-layer size budgets (`check_map_budget.py`, `ci/size-budget.json`)
  in addition to the ELF total gate.
- Host PAL implementation (`pal/host`) with an event-sink bridge test, and an
  IAR/iccarm CMake toolchain file (`cmake/toolchains/iar-arm.cmake`).
- CI: `product-matrix` job, map budgets on ARM targets, host-PAL static analysis,
  and coverage gate raised to 95% (currently ~99.8%).

### Fixed

- Cortex-M4 QEMU smoke: correct timer vector index (IRQ 8 -> `handlers[22]`) and
  make the ELF entry check accept the Thumb address; compile all ARM library
  objects in Thumb mode so the firmware no longer faults at reset.

### Changed

- Moved the scheduler implementation from `sys/example` to `sys/runtime` and
  rewrote the ARM firmware factory to link product targets instead of listing
  sources.

### Added

- Full architecture decision record `docs/adr.md` (D1-D85).
- Industrial CI/CD: build caching, coverage gate, JUnit test reports,
  reproducible build metadata, pinned GitHub Actions, and a tag-driven
  release pipeline.
- Governance and quality configuration: `CODEOWNERS`, `CONTRIBUTING.md`,
  `SECURITY.md`, `.clang-format`, `.clang-tidy`, `.editorconfig`, Dependabot.
- Architecture guard self-tests and a CMake/main app-list consistency check.

### Fixed

- Restored the default (warnings-as-errors) build by adding explicit port
  adapter trampolines in `product/example/glue.c`.
- Made the Cortex-M0 cross build reproducible by removing the implicit
  newlib dependency in `infra/flash`.
- Removed obsolete V2/V3 architecture leftovers (manifest/loader docs,
  linker-section registry, descriptor-based demo and tests).

## [0.5.0]

### Added

- Thin `edge_module` framework contract: module lifecycle, bounded ISR event
  queue with timestamping, central event IDs, injected clock/log ports.
- Deterministic `sys` scheduler: priority + module-id ordering, required-set
  validation, transactional init rollback, explicit event subscription and
  reverse-order shutdown.
- Board IRQ-to-event forwarding boundary and consumer-defined DLT645 port with
  a product composition-root adapter.
- GCC/Clang, sanitizer, coverage, static-analysis and Cortex-M0 cross-build CI
  with a firmware size budget.
