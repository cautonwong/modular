# Low-power targets: water meters, watches, and the contract they drive

This is the requirement analysis behind the power work (#28, #84, #90). It exists
because "low power" is not a tuning flag: on a battery meter it decides whether
the architecture (poll vs wake, 32-bit vs 64-bit time, who owns the sleep) is
usable at all.

Numbers below are worked examples, not a spec. The per-wake energy is
board-specific; every conclusion is stated as arithmetic you can redo with your
own board's figures.

## 1. The budget that shapes the design

Example cell and duty cycle:

| quantity | value |
|---|---|
| cell | 2.4 Ah over 10 years (CR17450-class) |
| allowed average current | `2400 mAh / 87600 h` = **27 µA** |
| deep-sleep current | ~1.5 µA |
| headroom for work | ~26 µA |
| active current | ~5 mA |
| **fixed cost per wake** (assumed: oscillator start, regulator settle, context restore) | ~50 µJ |

CPU-time is not the binding constraint. At 5 mA the headroom buys ~18 s of CPU
per hour, far more than a meter needs to read a sensor. The binding constraint is
**how often you wake**:

| wake rate | energy of the fixed per-wake cost | vs the 27 µA budget |
|---|---|---|
| 1 kHz (a 1 ms poll loop) | 50 mW | ~1800x over |
| 1 Hz | 50 µW | ~1.8x over |
| 1 per hour | 14 nW | ~0.05% |

Consequences the framework must respect:

1. **Polling is not an option.** Even 1 Hz blow the budget on the fixed per-wake
   cost alone, before any CPU time. The system must wake on an event (meter
   pulse, IR/UART frame, RTC tick), not on a fast loop.
2. Therefore the idle path is the *normal* state, not an optimization: the CPU
   spends years in it.
3. Therefore anything that silently adds a wake (an unconditional yield per IRQ,
   a fast tick, a timer that keeps firing) is a product-level defect, not a
   style issue.

## 2. Tick rate vs the 32-bit wrap horizon

A 32-bit tick counter wraps; the device is in the field for years:

| `configTICK_RATE_HZ` | one 32-bit tick period | note |
|---|---|---|
| 1000 | 49.7 days | |
| 100 | 497 days | typical meter tick |
| 32 (32768 Hz RTC / 1024) | 4.25 years | tickless territory |
| 1 | 136 years | needs an RTC to be useful |

So **any 32-bit tick wraps inside the warranty**, and the wrap always happens
long after the developer is gone. `edge_rtos_pal_port()`'s `monotonic_ticks`
therefore extends the kernel tick to 64 bits through `edge_tick64_extend()`
(`pal/os/include/pal_os/tick64.h`), and the accumulator:

- is **not** reset by a sleep or by enabling tickless sleep (suppressing ticks
  must not reset the epoch), and
- is updated under a BASEPRI mask, because a task and an ISR can sample it
  during the same wrap.

At 100 Hz the wrap lands on day 497, 994, 1491, ... - once inside a two-year
warranty, then every 497 days. It must be a non-event.

### Two different wrap problems

Deliberately solved in two places, and mixing them up is how a clock ends up
running backwards:

| problem | correct form | where |
|---|---|---|
| is a *deadline* due? | modular compare `(int32_t)(now - due) >= 0` | `sys/runtime` `tick_due` |
| extend a *counter* across a wrap | unsigned `now < last` then carry | `pal/os` `edge_tick64_extend` |

`edge_tick64_extend()`'s documented ceiling: it must be sampled at least once
per 2^31 ticks. At 100 Hz that is 248 days between samples, so every sys
iteration samples long before it matters. The ceiling is about *sampling gaps*,
not tick frequency - at 1 Hz it becomes 68 years, still fine.

## 3. Idle has three layers, with three different owners

| layer | who | what it saves |
|---|---|---|
| `WFI` | `pal/*` (`edge_pal_port_t.idle`) | core clocks while still powered |
| tickless sleep | scheduler idle task (`configUSE_TICKLESS_IDLE`, `vPortSuppressTicksAndSleep`) - **on since #90** | the SysTick wakeup itself, plus system clock during suppression |
| STOP / deep sleep + peripheral gating | `board/*` + `soc/*` | RAM retention mode, regulator, peripheral clocks |

Layers 1 and 2 are wired: `edge_os_idle_wait()` + `pal->idle` for the atomic wait,
and tickless sleep turned on because the runner now parks on a notification instead
of polling (docs/rtos-runner.md sections 1 and 5). Layer 3 is board/SoC work and
lands with the first real platform (#78, #75); until then "tickless is on" means
the framework no longer prevents sleep, not that a product is low power.

**Explicit non-goals**: the framework does not choose STOP vs SLEEP, does not
touch the clock tree, and does not gate peripherals. The board owns those; the
framework only depends on minimal primitives (D46).

## 4. Posting atomicity: why `edge_os_idle_wait()` exists

The meter's worst bug is not a crash, it is a **lost reading**: a pulse or an
IR frame that arrives in the window between "nothing to do, go to sleep" and
"actually asleep". The user-visible result is a meter that misses a reading once
and gets replaced.

`edge_os_idle_wait(pal, pending, ctx)` closes that window: mask, re-check
`pending` (`edge_sys_pending`), only then wait, then release. A fact posted
inside the window is seen by the re-check and the wait is skipped.

On a FreeRTOS product the other half is the ISR side: `edge_rtos_irq_guard()`
brackets the push in a BASEPRI save/restore so an ISR-delivered event cannot
tear a queue read happening in task context. Both halves are needed; either one
alone loses events under load.

### The ISR yield, and why it is not here yet

`portYIELD_FROM_ISR` has no place in the guard today because the capsule polls
the queue: the push wakes no task, so an unconditional yield would only add a
PendSV to every IRQ - the exact silent wake cost section 1 forbids. The yield
belongs with the **blocking consumer** that makes it meaningful (a capsule that
waits on a notification instead of polling), which is tracked with the idle
strategy in #90. The guard documents this rather than paying for a wake it has
no use for.

## 5. Watchdog policy

- **Feed only while healthy.** `edge_sys_healthy()` is the gate (#28). A meter
  that keeps feeding the watchdog while an app module is failed runs for years
  in a bad state; not feeding it is a reset, a clean re-init, and a counted
  event. The second behavior is the correct one for an unattended device.
- **A watchdog during sleep is a board decision with no free option.** An
  independent watchdog's maximum timeout is usually far shorter than a reading
  period (ms to tens of seconds vs hours). Either (a) run it at its slowest
  setting and guarantee the wake period is shorter than the timeout, or (b)
  freeze/disable it before deep sleep and feed it immediately on wake. Both are
  defensible; leaving it unstated is not, or the first real sample randomly
  resets.
- **Reset cause must be readable.** A reset that cannot be attributed makes
  field failures unfixable. `board_example_reset_count()`-style observability on
  the host, RCC reset flags on real silicon.

## 6. Retention assumptions

Cumulative volume, the tick accumulator, and reset counters must survive both
the sleep and a power cut. That means retained/backup RAM or a flush to flash
before sleeping. The framework's types assume RAM lives until the next boot
(`edge_sys_stats_t`, `edge_tick64_t`); a real platform must verify that
assumption explicitly instead of inheriting it (#78).

## 7. Where the repo stands (honest list)

- Have: `WFI` primitive, atomic wait, health signal, board power/watchdog/reset
  actions (#28); FreeRTOS PAL with 64-bit kernel-tick time and the ISR guard,
  verified on a real IRQ in the QEMU smoke (#84); a runner that parks on a
  notification and wakes from the ISR, with tickless sleep on and a lower-priority
  task proven not to starve (#90).
- Missing: real STOP-mode and peripheral gating (#78, #75); retained-memory
  verification (#78); an `edge_sys_next_due()` so the runner sleeps exactly until
  the next deadline instead of for the smallest app period, which costs one
  periodic wake per period even when nothing is due (docs/rtos-runner.md section
  7).
- The largest term is gone: section 1 put the old 1 ms polling runner ~1800x over
  a ten-year budget on the fixed cost per wake alone. The runner no longer polls;
  what remains is the residual periodic wake above and anything a board fails to
  gate.
