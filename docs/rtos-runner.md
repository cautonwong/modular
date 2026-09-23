# RTOS runner semantics (D47/D51/D52/D71)

What the runner does on an RTOS: when it sleeps, how it wakes, what suspend means,
how module priorities relate to kernel priorities, and what the smoke can prove.

## 1. idle and tickless: adopted, with a caveat

**Decision: tickless sleep is on** (`configUSE_TICKLESS_IDLE 1`,
`configEXPECTED_IDLE_TIME_BEFORE_SLEEP 2` in the product's `FreeRTOSConfig.h`).

The reason it is worth turning on *now* is that it only became useful in this
change: the runner parks on a notification instead of polling, so the scheduler's
idle task actually gets the CPU and there is real suppressed time to account for.
With a polling runner, tickless idle would have been configured and never reached.

- The ARM_CM3 port's `vPortSuppressTicksAndSleep()` advances the kernel tick by the
  suppressed amount, so D52's `period`/`budget` accounting and the 64-bit
  kernel-tick clock (`edge_rtos_pal_port`, D72) stay correct across a sleep.
- It is verified in the QEMU smoke (three consecutive runs, exit 0), not on
  silicon.

**What it does not do.** Tickless suppresses the tick and executes `WFI`. It does
not reduce current by itself in this repo: `board_mps2_enter_low_power()` is still
a placeholder, so the regulator/clock-tree/peripheral-gating win is board and SoC
work and lands with the first real platform (#78, #75). Treat "tickless is on" as
"the framework no longer prevents sleep", not "the product is low power".

**Why the parking matters more than the tickless flag.** `docs/low-power.md`
section 1 quantifies it: a 1 ms polling runner is roughly 1800x over a ten-year
battery budget on the fixed cost per wake alone. Removing the poll is the large
term; suppressing the tick is the smaller one.

## 2. The wake path, end to end

```
runner parks (edge_rtos_wait_for_work)
   timer IRQ
     board ISR              clears the flag, pushes the event          (RTOS-free)
       sink guard enter     PAL: BASEPRI save/restore                 (D71)
       sink guard exit      PAL restore, then edge_rtos_wake_from_isr()
         notify             vTaskNotifyGiveFromISR
         yield              portYIELD_FROM_ISR(woken)  <- only if woken
runner resumes, runs the capsule, parks again
```

Two deliberate choices:

- The **composition root** composes the sink guard (mask + wake) as an adapter
  (D14). The board's ISR therefore stays RTOS-free (D85) while still being what
  delivers the wake.
- The yield is **conditional**: `portYIELD_FROM_ISR(woken)` only sets PendSV when a
  task was actually made ready. An unconditional yield would add a PendSV to every
  interrupt - the silent wake cost section 1 of `docs/low-power.md` rules out.

**A park is not a yield.** `edge_rtos_wait_for_work()` returns immediately when a
notification is already pending, so "how long did we wait" cannot be observed from
it. The first version of the witness check assumed otherwise and was flaky
(`rc=16` on some runs); the fix was to measure progress with a real block. This is
recorded because the same trap will catch the next person observing runner timing.

## 3. Module suspend is not task suspend

Two different things, deliberately never mapped onto each other:

| | module suspend (`D51`) | RTOS task suspend |
|---|---|---|
| API | `edge_sys_suspend_all()` / `resume_all()`, `module.suspend`/`resume` | `vTaskSuspend()` (kernel) |
| scope | one module inside the runner | every module in that task, plus the runner's idle path |
| effect | stops polling it and stops delivering events to it | stops executing the task entirely |
| who calls | the framework, on the module's own lifecycle hook | the composition root only |
| ISR wake | n/a | not possible directly; the task must be resumed by another task |

The runner never maps a module's suspension onto a task suspension. A suspended
module must not take the watchdog feed, the idle path, or the other modules down
with it, and those all live in the same task. Suspending a task is a product-level
decision with product-level consequences; if a composition root ever does it, it
owns the watchdog and the remaining modules.

Module suspension is power-relevant, not just a filter: `edge_sys_pending()`
(`#28`) ignores suspended modules, so a suspended module does not keep the runner
awake.

## 4. Priority mapping

- `edge_module_t.priority` is **order inside the runner** (D11): it decides the
  poll and dispatch order within one task, nothing else. It is never mapped onto a
  kernel priority - that would turn one module's tick rate into another module's
  latency.
- Kernel priorities are a **product** decision, chosen per task *role*. The rule:
  a task that must make progress while the runner is parked has to be *below* the
  runner, because only a lower-priority task runs while the runner is blocked. In
  the pinned contract convention (`0` is highest, see
  [`rtos-ports.md`](rtos-ports.md)) "below" means a **numerically larger** value.
- Concrete in `product/meter_mps2_freertos`: `witness` = 2, `capsule` = 1. The
  witness is deliberately the lower-priority task precisely so that it can only run
  when the runner behaves; the port inverts both numbers for FreeRTOS, which counts
  the other way.
- There is no automatic mapping, on purpose. A future product with several tasks
  writes its own mapping down; this section is the rule it has to respect.

## 5. Yield policy, bounded

**Rule: yield by blocking.** The runner parks on
`edge_rtos_wait_for_work(period_ticks)` at the end of every round. It never relies
on `taskYIELD()`: a bare yield is not a fairness primitive (it does not run
lower-priority tasks) and it burns current at the same priority.

- **Bound**: the runner yields at least once per app period (the park timeout) and
  immediately on every delivered event. Nothing in the runner loop spins without a
  bound.
- The park timeout is the smallest app period, not a fixed tick count, because
  every wake has a fixed energy cost. In the FreeRTOS product it is
  `CAPSULE_PERIOD_TICKS` (100), the same value the app is constructed with.
- **Verified as a refutation, not a demonstration**: over a five-tick window the
  product requires a lower-priority witness task to make progress *inside that
  window*. Blocking → passes 3/3; the same window with a bare `taskYIELD()` →
  fails 3/3 with `rc=16`. Measuring the differential matters: an earlier version
  checked the accumulated counter and let the starving variant pass.

## 6. Observation alignment (#28, #81)

- `edge_sys_stats_t` remains the in-process view: `idle_calls` counts runner idle
  hook entries, `pending_high_water` the re-check high-water, and they are asserted
  by host tests (`tests/test_low_power.c`, `tests/test_sys.c`).
- On target there is no output channel, so the observable is the **exit status**:
  `0` ok, `9` kernel assert, `12` capsule stack nearly exhausted, `14`/`15` clock
  anomalies (`#126`), `16` a lower-priority task was starved. `board_mps2_exit()`
  and `.github/arm/startup.c` use `SYS_EXIT_EXTENDED`, so these codes survive to
  the harness and the CI smoke asserts `0`.
- Keeping the two views separate is deliberate: the in-process counters are for
  debugging and host tests, the exit status is for CI, and neither is inferred
  from the other.

## 7. Open

- **Next deadline dynamic sleep**: `edge_sys_next_due()` (`sys/runtime/include/runtime/sys.h`)
  is now implemented (#184). It inspects all registered modules and pending events to
  compute the exact earliest wake deadline, allowing the runner to sleep until necessary
  rather than waking periodically on the minimum period.
- Tickless is verified in QEMU and host simulators; the current-reduction claim belongs to the
  first real platform (#78, #75).
