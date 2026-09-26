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
| D47 | Single runner context; `sys_step()` decomposable | ✅ | `edge_sys_step()` + `edge_sys_run()`; on an RTOS the runner parks on a notification and is woken from the ISR instead of polling, with the yield policy, the module-vs-task suspend boundary and the priority mapping written down in `docs/rtos-runner.md` |
| D51 | `poll`/`on_event` (+ optional `suspend`/`resume`); `init`/`deinit` outside the struct | ✅ | `edge_module_t` carries `poll`/`on_event`/`power_off`/`suspend`/`resume` only; the composition root calls `<app>_init(self)` / `<app>_deinit(self)` (products do, reverse order at shutdown). Enforced by `check_module_contract.py`. `power_off` stays in the struct because *ordering* is a `sys` duty (D11) |
| D52 | Event-driven + periodic mixed scheduling; idle -> board | ✅ | `period`/`budget`/`edge_sys_idle` hook; the product idle hook feeds the watchdog and calls the board low-power action, then the atomic PAL wait. On the RTOS the idle hook is reached while the runner is parked, and the scheduler's tickless sleep sits below it |
| D53 | Non-fatal init failure skipped and recorded | ✅ | `fatal` flag: default skip, `fatal` rolls back |
| D54 | Drop-newest + counter; multi-subscriber; unsubscribe | 🟡 | All done, except multi-SPSC producer queues |
| D55 | Central `edge/modules.h`, `0xNN00` segment, allocated in blocks per owning layer | ✅ | Table-driven `EDGE_MODULE_IDS` (one line per ID; assertions generated from it); `check_module_ids.py` rejects duplicates, misaligned values, out-of-block values and layer tokens without a block |
| D56 | ABI version object | 🟡 | `_Static_assert` layout only |
| D65 | One SPSC queue per producer | ❌ | Single caller-owned queue + injected `edge_irq_guard_t` |
| D66 | Scalar event + token; payload read through a port | ✅ | The rule is written where implementers look (`edge/event.h` next to `edge_event_t`, plus `docs/payload-token.md`): the payload stays in the producer's buffer, the consumer reads it through a port it defines (D14; `edge_byte_reader_t` is the canonical byte shape), and a frame is invalidated by the next event with the same id. `tests/test_payload_token.c` pins both halves, including the *checkable* form: when the token is a generation the consumer's port can refuse a stale one, so the lifecycle rule stops being a discipline. Not enforced by a gate, and cannot be: `check_event_payload.py` sees scalar fields and cannot tell a length from an address, which is why the anti-pattern is documented rather than detected |
| D67 | API catalogue (framework/runtime/platform/ports) | 🟡 | `errors.h`/`modules.h`/`ports.h` present; naming is `edge_sys_*`, not `sys_*` |
| D69 | Bounded runner step | ✅ | `max_events_per_run` (default 8), one due poll per module per step |
| D70 | Reentrant `sys_publish` deferred into a runner queue | ✅ | `edge_sys_publish()` + `pending_high_water` |
| D71 | Atomic idle/low-power sequence in board/PAL | ✅ | `edge_os_idle_wait(pal, pending, ctx)` = critical-enter -> re-check (`edge_sys_pending`) -> `pal->idle` (Cortex-M `WFI`) -> critical-exit; board provides enter-low-power / feed-watchdog / reset; `edge_sys_healthy` is the watchdog input. The boundary is explicit: `pal->idle` is the raw wait primitive and must not contain the sequence (it would double-mask and re-check outside its own protection). ISR half: `edge_rtos_irq_guard()` brackets the push in a BASEPRI save/restore |
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
| D45 | Certification boundary (core vs app) | 🟡 | Documented boundary. The first enforcement artefact now exists: every quality-gate exemption (`RAW`) must be registered in `ci/exemptions.json` with an owner, a reason and an exit condition, enforced by `check_exemptions.py` in both directions (an unregistered `RAW` target fails, and so does an entry whose directory no longer declares one). Still missing: the generated core file list (#136) |
| D46/D85 | `pal/` per (architecture x RTOS); board selects | 🟡 | Architecture/RTOS PALs exist behind `edge/pal.h`: `pal/cortex-m-bare`, `pal/rtos/freertos`, `pal/rtos/zephyr` (#82), and `pal/rtos/rtems` (#135). Layering enforced by `check_layer_dependencies.py`. Open: the RTOS PAL is still selected inside the firmware target rather than by an explicit board/product binding (#76), and the host-POSIX port does not exist yet (#88). The architecture half of the binding is enforced: `pal/cortex-m-bare` compiles only for `__arm__` or with the explicit host-test macro `EDGE_PAL_CORTEX_M_HOST_TEST`, and `check_pal_arch_binding.py` requires refusal to work. |
| D48 | Driver model neutral; infra depends on narrow ports only | ✅ | `infra -> soc` is denied with **no exception** (`check_layer_dependencies.py`); SoC-bound (register/HAL) code lives in `soc/<soc>/`, OS device models in `pal/<os>/`, binding in product glue. A second portable implementation is tracked in #57 |
| D50 | RTOS neutrality: the framework calls no RTOS API, RTOS appears only in `product/main` and `pal/` | ✅ | No FreeRTOS, Zephyr, or RTEMS header escapes `pal/rtos/*`: `pal_rtos_freertos.h`, `pal_rtos_zephyr.h`, and `pal_rtos_rtems.h` expose only `edge_*` types. `check_app_isolation.py` rejects `FreeRTOS.h` / `task.h` / `cmsis_os.h` / `zephyr/` / `rtems.h` in any app |
| D49 | Framework and `sys` have zero SoC knowledge; `soc/` is a support package selected by the board | ✅ | `soc/mps2` selected by `board/mps2`; `soc -> soc` only, enforced by `check_layer_dependencies.py` |
| D57-D61 | Host unit tests, cmocka, fakes, fake clock/PAL | 🟡 | cmocka + host PAL, and D57's host half is executable now: reusable contract suites in `tests/contract/` (app lifecycle, port shapes, board IRQ) are run by every app test and by `tests/test_contract.c`, with `tests/contract_violations/` making CTest prove the suites reject a broken implementation. Still open: a central `test/fakes/` (D60), Renode/HIL (D62/D63, #22/#30). D61's "fake clock / empty PAL for the host" is no longer a convention: the host fallback inside `pal/cortex-m-bare` is opt-in per build, so a fake clock cannot reach a target build by accident |
| D62/D63 | Renode for board/infra; HIL for the rest | ❌ | QEMU MPS2 smoke only |
| D64 | 4-stage CI (host -> target -> Renode -> HIL) | 🟡 | Stages 1-2 done, 3-4 missing |
| D73 | 1 app may expose 1..n modules | 🟡 | Model supports it; no multi-module example |
| D78 | App-to-app interaction only via a consumer-defined interface or an event | ✅ | `app/meter_core` (provider) + `app/modbus_slave` (consumer `modbus_store_if`) adapted in `product/meter_gateway_host`; enforced by `check_app_isolation.py` / `check_layer_dependencies.py` |
| D75 | Bounded ISR (clear + push only) | ✅ | Board ISRs only clear and push; stack usage is gated by `check_stack_usage.py` + FreeRTOS overrun detection (`docs/stack-usage.md`) |
| D84 | Shared IRQ multi-handler via `board_irq_attach` | ✅ | `board_mps2_irq_attach()` + `board_mps2_irq_dispatch()`: one line carries several handlers, dispatched in **registration order** from a fixed static table (no allocation, D21) with a bounded walk (D75); the line's vector entry dispatches instead of calling one handler, and the board's own timer consumer registers through the same public entry point so the built-in path exercises it. Host-tested (`tests/test_board_irq_attach.c`) and exercised by both QEMU smokes. Priority policy: `soc/<soc>` owns the encoding and legal range, `board/` the assignment. Remaining: the vector table itself is still the transitional `.github/arm/startup.c` (#125) |
| D86 | `product/<name>` binds exactly one board; the binding cannot be overridden by build parameters | ✅ | `edge_add_product()` records `EDGE_PRODUCT_<name>_BOARD` and rejects duplicate registration or re-binding; enforced by `check_product_board_binding.py` with negative fixtures. The neutrality fixture `edge_add_minimal_variant` is a test fixture and exempt |
| D87 | FreeRTOS config is composed by the product (soc/board/product); the PAL only declares the contract and the required invariants | ✅ | `FreeRTOSConfig.h` is a composer carrying `#error` invariants; `soc/mps2` + `board/mps2` + `product/meter_mps2_freertos/edge_freertos_config.h` supply the values; `configASSERT` routes to `edge_rtos_assert_failed()` + an observable product hook |
| D34/D35/D76 | Distribution, compliance, OTA | ❌ | Separate workstreams, out of scope by decision |
| D88 | Every area directory declares its own build target; the top-level file only discovers areas and composes products | ✅ | `cmake/EdgeTargets.cmake` defines the module shape once; `app/*`, `infra/*`, `board/*`, `soc/*`, `sys/*`, `pal/*` each carry a `CMakeLists.txt`; host tests name their own dependencies; enforced by `check_area_registration.py` with both fixtures, and `check_cmake_apps.py` / `check_product_board_binding.py` were re-pointed at the area files |

## BLDC / VESC port (phases A-E, in progress)

The `bldc` family ports `vendor/bldc` (VESC) into this architecture. Plan, the six
completion criteria and the phase route: [`bldc-migration.md`](bldc-migration.md).
This table is the drift view required by that plan, not a second plan. A decision
number for the family is deliberately left to the maintainer.

Porting rule applied throughout: the reference source is the only authority, and a
claim of "1:1" is only made where a differential harness compiles the reference and
compares it (14/14 in the util harness, 8/8 in the motor harness at the time of
writing).

| Area | Status | Evidence / divergence |
|---|---|---|
| FOC math (Clarke/Park/SVPWM, sincos, atan2) | ✅ | Differential: `foc_svm` matches over 6561 vectors (max duty difference 1.5 integer counts, the reference's own rounding). The earlier `v/(v_bus*sqrt(3)/2)` normalisation was a sqrt(3) error, fixed in `cf5baaf` |
| Observer family (7 types) | 🟡 | Differential: bit-identical to `foc_observer_update` for all seven (max abs phase and state delta 0.000000 over 4000 steps each). Parameter compensation is ported as well and verified with it ENABLED: `foc_observer_adjust_params` reproduces the reference's saturation, temperature and saliency block bit-exactly for FOC_SAT_COMP_FACTOR and FOC_SAT_COMP_LAMBDA_AND_FACTOR, with and without temperature compensation. Still open: nothing in this row - `foc_core` carries the two configuration fields and the product supplies the motor temperature (B4) |
| PLL | ✅ | Differential: bit-identical to `foc_pll_run`; the non-reference phase differencing it replaced is deleted |
| Decoded app inputs, statistics, energy counters, tachometer | ✅ | Wire bytes pinned per command; reset/read-reset semantics covered by tests |
| Average sampler tick | 🟡 | The reference samples on a dedicated periodic thread and keeps summing while the motor is idle (reusing the last vd/vq); this port accumulates in `foc_core`'s periodic `poll` and adds nothing to vd/vq while idle |
| Energy counter tick | 🟡 | Reference accumulates in the MC timer ISR with that timer's dt (`mc_interface.c:2036`); this port accumulates in the FOC loop with the loop dt. Same integral, finer sampling |
| Statistics inputs | 🟡 | Power statistic uses the unfiltered bus voltage; `count_time` returns 0 (needs a clock this module is not given); the motor-temperature statistics report the filtered NTC reading the product's sampler hands in (B4) |
| Fields with no source | 🟡 | Three MOSFET temperatures and the timeout/kill-switch status return 0; the motor NTC now has one (the product's sampler, B4), which is also how the reference's own `m_temp_motor` is produced. Both temperature seeds start at the reference's 0: this port used to start the FET one at 25 degC, and that choice is gone. Input current is the reference's power-balance estimate, not a DC measurement. Same policy in COMM_GET_VALUES_SETUP: odometer_m (needs a persisted counter, and the reference's own per-motor accumulation site has not been located, so a number would be invented) and uptime_ms (this module is given no clock) return 0; num_vescs stays 1 because the reference aggregates unexpired CAN status frames and this product has no CAN status receive path; controller_id is 1 until the app configuration reaches that adapter. Its battery level uses the bus voltage where the reference filters a slower input voltage |
| Speed and position control loops | 🟡 | The speed loop is now the reference's `foc_run_pid_speed` fed by `s_pid_*`, bit-identical over four configurations (`26e805c`). Still open: the position loop, which the reference drives with `p_pid_*` while this port uses a fixed `err_pos * 0.1f` |
| Current-command path | 🟡 | DIR_MULT (`m_invert_direction`) is now applied where the reference applies it - current, brake, duty, pid_speed - and the port's invented clamp to `[current_min_a, current_max_a]` is gone, since `mc_interface_set_current` never limits (`mc_interface.c`). `COMM_SET_CURRENT_REL` follows `mc_interface_set_current_rel`: the limit base is chosen from the duty's sign, which is why it lives in the motor side (the codec has no duty). One divergence inside that: the reference chooses between the *effective* limits `lo_current_max` / `lo_current_min`, which `update_override_limits()` derives at runtime as the smallest of the current, rpm, acceleration, temperature, duty and input-current limits, whereas this port uses the stored `l_current_min`. They agree only while no other limit binds, so the effective-limit computation is still to do. `lo_current_max/min` are runtime values and are deliberately not serialised here (the reference does not serialise them either). Still open: the reference's trailing `set_current_off_delay(0.1)`, whose only reader is the field-weakening modulation extension (B3); and brake, which needs its control mode: the reference's brake is not a negative current but `CONTROL_MODE_CURRENT_BRAKE`, whose meaning is spread over the loop - the braking current opposes the speed (`mcpwm_foc.c:3450`), its magnitude is capped at the magnitude of lo_current_min (`:3328`), the duty-based limit base switches to the positive limit (`:3391`), min-rpm hysteresis is cleared (`:4091`), and a 10-cycle state machine keeps all phases shorted until the braking current reaches its target (`:3350`) - with field weakening involved (`foc_math.c:722`), so it shares state with B3. The port keeps its approximate negative current rather than flipping the sign without that mode. Handbrake is done: `FOC_STATE_HANDBRAKE`, the setpoint on the q axis, no abs and no DIR_MULT, the electrical phase forced to zero (`mcpwm_foc.c:3602`) so it locks the rotor instead of driving it, and `COMM_SET_HANDBRAKE` on the wire at 1e3 |
| Reported duty | ✅ | `COMM_GET_VALUES`' duty field now carries `SIGN(vq) * NORM2_f(mod_d, mod_q) * p_duty_norm` (`mcpwm_foc.c:3818`) instead of the phase-A duty, which is a different quantity. `p_duty_norm` is `TWO_BY_SQRT3` because `foc_overmod_factor` defaults to 1.0 and is not a configuration field here yet |
| Saturation and temperature compensation | 🟡 | Saturation compensation (all three `foc_sat_comp_mode` branches) and the saliency term are ported and verified bit-exactly against the reference's block for FOC_SAT_COMP_FACTOR and FOC_SAT_COMP_LAMBDA_AND_FACTOR. Temperature compensation is now carried end to end: `timer_update`'s cycle-time recomputation (mcpwm_foc.c:3939-3948) with its `-30` degC floor and the `0.00386` coefficient, consumed by the observer's resistance and by the current loop's `ki`, both gated on `foc_temp_comp`. The factor's expression keeps the reference's double literals, which a test pins with a value where float literals give a different last bit. Open: the motor-temperature *derating* (`l_temp_motor_start/end`, `l_temp_accel_dec`) and the FET-temperature derating share this input but are limit logic the port does not compute |
| Configuration byte stream | 🟡 | The mc_configuration stream is now the reference's own, byte for byte: 488 bytes including the 4-byte signature, generated from `datatypes.h` / `confgenerator.c` / `mcconf_default.h` by `tools/gen_mcconf_from_reference.py`, with a static assert pinning the length and a test comparing the port's output against the reference serialiser's for a default configuration. The generator refuses to emit if a serialiser statement fails to parse, and it caught two of my own errors on the way: the observer default (3, MXLEMMING_LAMBDA_COMP, not 0) and `l_abs_current_max` having a global default of 130 A under the macro name `MCCONF_L_MAX_ABS_CURRENT`. Still open: none of this row. The app_configuration stream is the reference's own 290-byte layout too, A7's COMM_GET/SET_MCCONF and COMM_GET/SET_APPCONF are wired, and the port's own flash envelope (signature + version + length + CRC) has been deleted with nothing replacing it - a stored configuration is now the reference's variable table plus the struct's own crc member, exactly what `conf_general_read/store_mc_configuration` uses |
| Commands not yet handled | 🟡 | 160 ids are declared; the handled set is listed in `vesc_comm_process_command`. Unhandled ids answer nothing, as an unknown command does in the reference. `COMM_GET_MCCONF` / `SET_MCCONF` / `GET_APPCONF` / `SET_APPCONF` are handled now, in the reference's framing: the reply is the command id followed by the configuration stream (489 and 291 bytes), and a SET passes the request's stream straight through to the aggregate, which decodes into a staging copy first exactly as the reference copies the live configuration. COMM_TERMINAL_CMD and COMM_FORWARD_CAN are handled. The terminal runs inline here because this port is single-threaded and cooperative, which is what the reference's blocking thread achieves; the forward goes through vesc_can_send_buffer, whose three branches (short frame, seven-byte split, long split past index 255, closing frame with length and CRC) are ported and tested. The reference's dual-motor branch of FORWARD_CAN does not apply: this port has one motor. Still unhandled, deliberately, and recorded in docs/bldc-migration.md rather than left to be discovered: the detection family (B5) and the BMS flash / IMU / CAN-baud commands, each of which needs a write path its module does not have yet. A8 |
| `soc/stm32f4`, `board/vesc6` | ❌ | Arithmetic adapters only (scaling, deadtime encoding); no register-level PWM/ADC drivers. D2 |
| `product/vesc6_stm32f4` | ❌ | Does not exist, though issue #203 claimed Phase 4 delivered it. D1 |
| Boards with no product | 🟡 | `board/vesc4` and `board/vesc_unity` build but no product binds them. E4 |

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
