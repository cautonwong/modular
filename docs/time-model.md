# The time model: which clock is which

Three time domains exist in this repository, and only one of them is a wall clock.
Getting them confused is silent: the numbers still add up, they just mean something
else. This is the page that says which is which, and what a legacy time function
becomes.

## The three domains

| domain | port | unit | owner |
|---|---|---|---|
| **monotonic** | `edge_pal_port_t.monotonic_ticks` | **depends on the PAL** - see the table below | `pal/` (chosen by the board), D46/D85 |
| **kernel tick** | FreeRTOS `configTICK_RATE_HZ`; on an RTOS product this *is* the PAL's monotonic source | one tick | product config, D87 |
| **wall time** | `edge_clock_port_t.wall_time` | seconds since an epoch | the board's RTC. The RTOS does **not** provide it; every clock port in the tree passes `NULL` today |

## What one unit is, per configuration

| product / PAL | monotonic source | one unit | wrap behaviour |
|---|---|---|---|
| bare metal (`pal/cortex-m-bare`, `board/mps2`) | free-running SysTick, `LOAD = 0x00FFFFFF`, processor clock | **one processor cycle** - 40 ns at 25 MHz | `wrap` counter plus `edge_tick64_extend`; the period is 16 777 216 cycles ≈ **0.671 s**; must be sampled at least once per 2^31 units (248 days at 100 Hz equivalent) |
| FreeRTOS (`pal/rtos/freertos`, `board/mps2`) | kernel tick (`configTICK_RATE_HZ = 1000`, `configCPU_CLOCK_HZ = 25 MHz`) | **one tick = 1 ms** | the 32-bit kernel tick extended to 64 bits by the same helper |

## The consequence that bites

**`edge_module_t.period` and `.budget` are in the units of the injected clock**, and
the two products in this tree inject different ones. The same literal value means:

| `period = 100` with | physical interval |
|---|---|
| the bare-metal MPS2 product (cycles) | ≈ **4 µs** |
| the FreeRTOS MPS2 product (ticks) | **100 ms** |

Both products "work", which is exactly why this is worth writing down: a migrated
number that is not translated keeps compiling and silently changes behaviour by
four orders of magnitude. A product must therefore **state which clock it injects**,
and a migration must **translate its periods and timeouts**, not copy them.

For deadline comparison the rules are already fixed and are not repeated here: use
the modular compare (`(int64_t)(now - due) >= 0`, D72) for deadlines, and the
unsigned carry in `edge_tick64_extend` for extending a counter.

## What a legacy time function becomes

| legacy | becomes | note |
|---|---|---|
| `HAL_GetTick()` / `millis()` used to compute an elapsed interval | the injected clock port (D20), with the interval **converted** to that clock's units | a cycle-based clock turns `> 500 ms` into `> 500 cycles` if you copy the literal |
| `delay_ms(10)` inside a periodic task | **nothing inside the runner** | blocking is prohibited in the runner (`docs/how-to/intake.md`); it goes in the code's own RTOS task, or becomes a state machine step |
| a busy-wait calibration loop | the same loop, but the *timeout* comes from the clock port | and it is a power bug in a battery product (`docs/low-power.md`) |
| an RTC read for a timestamp | `edge_clock_port_t.wall_time`, provided by the board | unsupported everywhere today, so the product must either supply it or not need it |

## Two prohibitions, both learned the hard way

1. **Do not measure elapsed time on an emulated target to decide whether a time
   source works.** A QEMU virtual clock does not advance over a short burst, so
   "did it advance" is not a portable assertion; the deterministic coverage for
   time arithmetic lives in host tests. This has cost three separate fixes
   (`docs/flake-ledger.md`).
2. **Do not use a tick difference as a measurement window when tickless sleep is
   enabled.** A suppressed-tick jump satisfies the window without any scheduling
   having happened - the same ledger entry.

## Where this leaves the open work

The units are documented; the *conversion helpers* are not. If a product needs to
map between microseconds, cycles and ticks it currently does the arithmetic by
hand, from `configCPU_CLOCK_HZ` and `configTICK_RATE_HZ`. A conversion helper
belongs with the clock port (D20) and is tracked in #91 rather than invented here.
