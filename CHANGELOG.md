# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
- Central module ID table (`edge/modules.h`) and error allocation table
  (`edge/errors.h`) with `EDGE_ERR(segment, code)` composition and static asserts.
- Canonical narrow port shapes (`edge/ports.h`).
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
