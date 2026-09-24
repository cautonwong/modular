# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **A fault in an RTOS firmware now reports itself instead of parking the CPU.** The
  shared startup table gained `EDGE_FAULT_HANDLER`, both firmware products point it at a
  handler that calls the shared assert contract, and the product's existing hook turns
  that into its own exit code. Verified the way it should be: a deliberate `udf` exits 9,
  and the normal path still exits 0. That test caught a real defect - `handlers[i]` in the
  table is hardware vector `i + 2`, so the fault vectors are indices 1 to 4, not 3 to 6,
  and at the wrong index the fault is silently delivered to `default_handler` (#134).

- **ThreadX is integrated, and verified on the host and on Cortex-M.**
  `pal/rtos/threadx` fetches the kernel like the FreeRTOS port does (by tag, shallow,
  then the resolved revision is checked against the pin in `ci/dependencies.json`),
  builds it, and implements both contracts. Two products run the same starvation probe
  on it: `product/meter_threadx` on the vendor's Linux port, and
  `product/meter_mps2_threadx` on the MPS2 model, which is what covers the vectors, the
  interrupt priorities and the tick. Five port facts had to be measured rather than
  assumed, and are recorded in `docs/rtos-ports.md`: the kernel clock is not readable
  before `tx_kernel_enter()`, a thread entry parameter is a 32-bit `ULONG` on the Linux
  port (so the pool index is passed, not an address), the vendor debug trace makes
  `TX_DISABLE` a non-recursive mutex that self-deadlocks on any kernel call inside it, a
  fast peripheral interrupt sharing the tick's priority starves the tick outright, and a
  QEMU firmware built without the semihosting option turns its exit into a spin loop
  (#134).

- `check_layer_dependencies.py` now checks the **declared build dependencies** as well
  as the includes. The matrix was enforced on `#include` only, so a library could
  declare a dependency it never included: the dependency's `PUBLIC` include
  directories reach the compile line, and the forbidden include would have worked the
  day someone wrote it with no gate noticing (#173). `edge_module` is treated as
  bookkeeping rather than a dependency - the helper requires a `DEPS` and puts
  `edge_module/include` on every target, which is why `soc` can name it without
  weakening `soc -> soc` for real includes. A negative fixture covers the case, and
  the check found the `soc -> edge_module` contradiction on its first run.

### Fixed

- Tests reset their module state **before every case** instead of at the end of a
  function: a cmocka assertion longjmps out of the test, so an end-of-test reset is
  skipped exactly when the state has already gone wrong and the next case inherits it.
  `cmocka_unit_test_setup` does this per case - a group setup runs once, which the
  first version of the fix demonstrated by still leaking state (#171). The IRQ-table
  test keeps its documented order dependence, and its capacity case queries the count
  rather than assuming one.

### Tests

- The event-ring test now fills the queue **after** the indices have wrapped, which is
  where the full-queue branch and its counter live (verified: disabling the check
  fails two cases). The wraparound it already did was real, so #167 is closed with
  that correction plus this case (#167).
- A shutdown-order case with three modules asserts that `power_off` runs in reverse
  construction order (D82) - the existing test used one module and could not observe
  an order at all (verified: a forward loop fails it) (#165).
- #169 was closed as stale: `tests/test_tick64.c` already covers the 32-bit wrap
  across `UINT32_MAX -> 0` (`test_wrap_extends_the_epoch`, `test_three_wraps_stay_monotonic`).
### Fixed

- The gateway's transport sink was 64 bytes while the protocol can build 69 (32
  registers: 2 + 64 PDU + unit id + CRC), so a large read response copied past the end
  of it (`uart_write` copies `len` bytes into the caller's buffer and has no notion of
  its size). The sink is now derived from the slave's own response capacity and the
  adapter refuses anything larger with `EDGE_ENOSPC`; the test writes the worst case
  and then one byte more (#170).
- The FreeRTOS port allocated its tasks (`xTaskCreate` with `heap_4.c` compiled in),
  which contradicted the zero-allocation rule the rest of the repository is gated on -
  and the gate could not see it, because its forbidden-symbol list held only the libc
  `malloc` family. The port now owns static task storage (`xTaskCreateStatic`, a fixed
  pool, an idle-task memory provider), the product states
  `configSUPPORT_STATIC_ALLOCATION 1` / `configSUPPORT_DYNAMIC_ALLOCATION 0`, `heap_4.c`
  is no longer compiled in, and `check_no_dynamic_memory.py` rejects `pvPortMalloc` and
  friends. Verified by `arm-none-eabi-nm`: the image contains **no** allocator symbol
  at all (#172).

### Note

- #166 and #168 were closed as **not defects**: `app/dlt645` has no frame parser to
  validate a length in, and the Modbus response builders already bound `qty` against
  `MODBUS_SLAVE_MAX_QTY` with PDU arrays sized for that maximum and a bounded
  `send_pdu` (see the closing comments for the indices).
### Removed

- `docs/todo-status.md`: a **second** area-level status table, while `CONTRIBUTING.md`
  declares `docs/adr-conformance.md` the *only* decision-vs-implementation view. Two
  tables that answer the same question drift apart, and the one nobody declared
  authoritative is the one that rots - the file's own header already said
  "summary view only". Its content is derivable from the conformance table and the
  code, and the tracker holds the work state. `todo.md` stays where it is: the repo
  archives superseded documents, and that one is declared an archive.
### Fixed

- The Cortex-M clock's sample and accumulator update are now one critical section
  (#175). An ISR preempting between the `SYSTICK_VAL` read and
  `edge_pal_cortex_m_extend()` advanced the accumulator's last sample, and the
  interrupted read was then evaluated against that newer value, read as a wrap and
  incremented `wrap` for a wrap that never happened - system time corruption. The
  clock *is* read from an ISR (the event sink stamps timestamps from it), so the two
  contexts were always concurrent; the port's own documentation demanded masking and
  the implementation did not do it.
- The event queue publishes its payload before the index that makes it visible
  (#161): a release fence after the slot write and an acquire fence before the reader
  reads it. Without the pair, a consumer can observe the new head and read a slot that
  has not been written yet - after any compiler reordering, and on a weakly ordered
  core even without one.
- Event dispatch walks the subscription array in reverse (#176). A callback that
  unsubscribes removes an entry and shifts the array left, so forward iteration moved
  an unvisited subscription into a slot the loop had already passed and **silently
  dropped its event** - invisible until someone relies on two handlers for one event.
  The regression test registers three subscribers, has the middle one unsubscribe
  during delivery, and fails against the old loop.
- An out-of-range task priority is **rejected with `EDGE_EINVAL` rather than clamped**
  (#174). Clamping turned a caller's mistake into a scheduling surprise nobody could
  trace back; the contract now states the range rule, and the port suite checks it by
  supplying the port's own maximum.
- The port suite now executes the pinned priority direction instead of describing it
  (#163): the harness supplies a bounded "advance one round" hook - deliberately
  outside the product contract, whose `start()` does not return - and the suite
  asserts that a priority-0 task runs before a priority-1 task. Verified by
  inverting the order in the reference port: the suite fails.
### Added

- `tests/contract/rtos_contract.{c,h}`: the RTOS port contract as an executable
  suite, with its refutation in `tests/contract_violations/rtos.c` - a port that
  accepts a NULL entry point (a mistake that becomes a jump to address zero the
  first time the scheduler runs a task) must be rejected, and CTest is told to expect
  that failure so the suite cannot quietly stop rejecting it. The suite runs on the
  host against `pal/eos`, which now implements the neutral contract with the same
  function names a kernel port uses. That is the point of the change: without a
  host-side port, every assertion in the contract would need a target and a kernel to
  mean anything, and the three ports would be "conforming" only in prose.

### Changed

- The assert contract (`edge_rtos_assert_failed`, its hook and its counter) moved out
  of the FreeRTOS port into `pal/rtos/src/assert.c`, so every port shares one
  implementation and the suite tests it once. It was never kernel-specific: a product
  maps its kernel's assert onto it, and what follows - count it, tell the composition
  root, halt rather than continue - is the same everywhere.
- `pal/eos` gained `edge_eos_bind_rtos()` and the port implementation. Its
  differences from a kernel are documented where they are decided, because they are
  the contract's edge cases rather than gaps: no per-task stack, so `stack_words` is
  accepted and ignored and the high-water mark is 0, which the contract defines as
  *unavailable* rather than *plenty*; and no blocking, so the wake pair is a no-op and
  `wait_for_work()` reports work instead of waiting - honest for a fixed-rate
  executive, and not to be mistaken for power management.
- The suite also records what it deliberately cannot check and why: anything needing
  the scheduler to run. `edge_rtos_start()` does not return by contract, and adding a
  test-only "step" entry point would put a test primitive into the product contract.
  Priority direction is therefore evidenced by construction - EOS's own test asserts
  that priority 0 runs first, and a kernel port proves it through its product's
  behaviour test.

### Added

- `ci/dependencies.json` plus `check_dependencies.py`: every fetched third-party
  component is pinned to an immutable revision, and the record is enforced in two
  places. The checker validates the shape (a 40-hex commit, a tag, a url, a licence,
  an existing `used_by`), rejects duplicates and an empty record, and **requires the
  CMake pin to carry exactly the commit the manifest names** - two records that
  disagree are worse than one. The fetching port additionally compares the resolved
  checkout against that commit at configure time, which is what catches the case the
  whole exercise is about: a tag that moved. The fetch stays shallow and by tag, so
  no CI run pays for a full clone.
- `docs/dependencies.md`: the bump procedure, what a bump has to pass (including the
  starvation probe in both directions for a kernel, because that is what a scheduling
  change breaks), the rollback path and why the commit is what makes it reliable, and
  the named gaps (no upstream watch, no content hashes).
- The SBOM and the provenance metadata now **read that manifest** instead of listing
  hard-coded components, so FreeRTOS appears as a `library` component with its
  `pin:commit`, and `build-metadata.json` carries a `dependencies` array. A second and
  third kernel therefore become one manifest entry each - which is why this landed
  before ThreadX and Zephyr (#134, #82) rather than after them.

### Changed

- `EDGE_MODULE_FREERTOS_KERNEL_COMMIT` is now the pin next to the existing
  `..._TAG`, which stays as the human-readable label. The configure fails with
  `FreeRTOS-Kernel revision drift` if the two disagree with reality.
### Added

- `docs/rtos-ports.md`: the preparation for a second and third kernel. It records
  what `pal_rtos/rtos.h` now **pins** rather than inherits, and lays ThreadX and
  Zephyr beside FreeRTOS as a capability table (task storage, start semantics,
  priority direction, stack high-water, assert contract, configuration, build
  integration, tickless) with the two hard parts named: ThreadX is a
  kernel-shaped port with four translations, Zephyr is a build-system integration
  where the *board* is described by devicetree rather than `board.c`.

### Changed

- The neutral RTOS contract no longer inherits FreeRTOS's conventions. It now pins
  four semantics explicitly - **priority direction (0 is highest)**, task storage
  owned by the implementation, creation before `start`, and "an absent capability
  reports 0, which means unavailable and never plenty" - because the three kernels
  disagree about all four, and a contract that silently follows one is that
  kernel's shape wearing a neutral name. The FreeRTOS port translates instead of
  forwarding: its priority is inverted, since FreeRTOS counts the other way *and*
  reserves 0 for the idle task. The product's priorities follow the pinned
  convention (capsule 1, witness 2), and the runner documentation and the
  starvation probe still hold in both directions.

### Added

- `docs/time-model.md`: the three time domains (the PAL's monotonic counter, the
  RTOS kernel tick, and the board's wall clock), who owns each, and **what one unit
  is** in every configuration this tree builds. The finding worth the page: `period`
  and `budget` are in the units of the *injected* clock, so the same `period = 100`
  is about 4 us on the bare-metal MPS2 product (processor cycles) and 100 ms on the
  FreeRTOS one (kernel ticks) - a migrated literal that is not translated keeps
  compiling and silently changes behaviour by four orders of magnitude. The page
  also maps `HAL_GetTick()`/`millis()`/`delay_ms` and the two time-related
  prohibitions this repository learned the hard way. `docs/how-to/intake.md` no
  longer lists the time base as not ready.

### Added

- `docs/payload-token.md` and the rule in `edge/event.h`: how a payload travels
  without a pointer (D66). The event carries scalars only, the payload stays in the
  producer's buffer, and the consumer reads it through a port it defines itself
  (D14). The document also gives the **checkable form**: make the token a
  generation and the consumer's port can refuse a stale one, which turns "read
  before the next event with the same id" from a discipline into a precondition.
  `tests/test_payload_token.c` pins the round trip, the refusal of a stale token,
  and why the consumer copies - the three parts a legacy `on_frame(const uint8_t *,
  size_t)` callback has to become. This closes the structural blocker #147 named
  for legacy interfaces that pass buffers, and the intake guide no longer lists it
  as not ready.

### Added

- The SoC package carries **code**, not only headers: `soc/mps2` declares
  `SOURCES src/soc_mps2.c` and the NVIC priority encoder moved there from a header
  inline. The target layout (`docs/adr.md` section 24) puts register drivers and
  vendor HALs in the SoC package, so a header-only package would have left that
  shape untested until the first real SoC arrived - the first real SoC package is
  exactly the case that has to be cheap (#147).

### Changed

- `ci/toolchain.json` declares the **toolchain files**, not just the installed
  tools, and `check_toolchain.py` now fails when a `*.cmake` under the declared
  directories is missing from the manifest, or when a declared file does not
  exist. A vendor toolchain (IAR today; armclang or a vendor GCC tomorrow) can
  therefore be named by a product without becoming an undeclared build dependency,
  and neither direction can drift silently. Vendor toolchains are never *required*
  to be present: absence is skipped, like the ARM toolchain on a host job. A
  negative fixture adds the undeclared-file case (guard self-test 57 -> 58 cases).

### Fixed

- The commit-message gate checked the branch but not the pull request title, and a
  squash merge does not write the branch's subject onto `main`: it writes the PR
  title plus ` (#<number>)`. A 97-character title therefore passed every check on
  the pull request and failed on `main` with 104 characters, which is how `main`
  went red on `8b0a332`. `check_commit_messages.py` gained
  `--pr-title <title> --pr-number <number>`, which validates the *prospective*
  squash subject with the same rules and reports the composed string instead of an
  abstract length limit; the pull request job runs it next to the range check, so
  the gate now checks the object it actually claims to check. Two guard cases
  reproduce the incident (57 cases total). The offending subject on `main` cannot
  be rewritten on a protected branch; this prevents recurrence, and the next merge
  puts a green HEAD on `main`.

### Fixed

- The Cortex-M4 smoke's on-target clock self-check was too expensive to keep: 400k
  samples with 64 spins each (~25M iterations) took ~1 s locally and exceeded the
  CI step's 10 s budget, so the firmware was killed (`rc=137`) and a correctness
  check became a timeout on shared runners. It is replaced by two cheap
  deterministic checks - a 256-sample non-decreasing burst over real reads, plus a
  synthetic wrap driven through the extension arithmetic that pins the exact delta
  and the wrap count. ~70 ms locally, and the wrap defect is now caught 3/3
  instead of 2/3. Recorded in `docs/flake-ledger.md`, with the rule this is the
  third instance of: an assertion must not measure elapsed time on an emulated
  target to decide whether the time source works.

### Added

- `board_mps2_irq_attach()` + `board_mps2_irq_dispatch()` (D84): one interrupt line
  can carry several handlers, dispatched in registration order from a fixed static
  table (no allocation, D21) with a bounded walk (D75). A line's vector entry is
  now the dispatcher rather than a single handler, so a second consumer of the
  same line needs no new vector - and the board's own timer consumer registers
  through the same public entry point, so the built-in path exercises the
  mechanism instead of reaching around it. Host-tested in
  `tests/test_board_irq_attach.c`.

### Changed

- Resolved three contradictions between frozen documents that would have blocked
  the first real board (recorded in `docs/adr-amendments.md`): vendor HAL lives in
  `soc/<soc>/` and glue only adapts (`docs/phase1.md` reworded to match §24); the
  vector table belongs to the board, with startup/link templates in the SoC
  package; and D84's `board_irq_attach` is implemented. `docs/how-to/add-board.md`
  gained the vector-ownership and multi-IRQ steps, and the D84 conformance row
  moved to implemented.

### Added

- `ci/exemptions.json` + `check_exemptions.py`: every quality-gate exemption
  (`RAW`, which keeps a target out of the warning and static-analysis gate) must
  be registered with an owner, a reason, an exit condition and a ticket. The
  checker enforces it in both directions - an unregistered `RAW` target fails, and
  so does an entry whose directory no longer declares one, because a decoration
  hides the next hole. The one existing exemption (`pal_rtos_freertos`) is now
  registered, and D45 gains its first enforcement artefact (ADR gate matrix
  36 -> 37; guards 21 checkers / 53 cases -> 22 / 55).
- `docs/how-to/intake.md`: the procedure for bringing in code that was **not**
  written to these rules - destination chosen by what the code knows rather than
  where it came from, three compliance paths in priority order (rewrite, wrap
  behind a consumer-defined port, quarantine as `RAW`), the gate-by-gate remedy
  table, and the blocking/time-model policy (two allowed containers, one
  prohibition, because the runner is a single cooperative task by D47).

### Fixed

- The FreeRTOS runner's starvation probe was flaky: it measured its window as a
  kernel-tick difference after the same change enabled tickless sleep, so a
  suppressed-tick jump could satisfy the window before the lower-priority witness
  was scheduled (measured: `rc=16` about once in seven runs of an unmodified
  build). The probe now counts *blocking sleeps* instead of ticks: 10/10 clean
  with the blocking policy, and 5/5 `rc=16` with a bare `taskYIELD()`, so the
  refutation it exists for still holds. Recorded in `docs/flake-ledger.md`.
- `pal/cortex-m-bare`: the critical-section design now states the invariant it
  rests on instead of implying it. `critical_enter`/`critical_exit` are a
  void -> void pair, so the saved PRIMASK lives in the caller's state - one slot,
  written on the outermost enter - and that is only sufficient because a critical
  section masks every maskable interrupt, so no masking context can interleave
  inside one. NMI and HardFault are not masked by PRIMASK and are therefore
  excluded: an enter/exit pair inside a held section would overwrite the saved
  slot and leave the interrupted section running with interrupts enabled. The two
  shared fields are volatile, and a host test pins the counter discipline,
  including a nested context that enters and leaves while the outer section is
  held. `idle` is documented as the raw wait primitive whose atomic wrapper is
  `edge_os_idle_wait()` (D71) - putting the sequence in `idle` as well would
  double-mask and move the re-check outside its own protection. The port also
  declares that it takes over SysTick and is therefore mutually exclusive with an
  RTOS that owns the tick.

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
