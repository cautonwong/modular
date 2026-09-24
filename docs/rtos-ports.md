# RTOS ports: one contract, three kernels

This repository runs a product on FreeRTOS today, wants ThreadX, and has Zephyr on
the list (#82). This page is the preparation for that: what the shared contract
pins, what each kernel provides, and what its port therefore has to build.

The rule it exists to enforce: **a contract that silently inherits one kernel's
convention is not neutral, it is that kernel's shape wearing a neutral name.**

## What the contract pins

`pal/rtos/include/pal_rtos/rtos.h` pins four semantics, each because the kernels
disagree about it:

| pinned | why it has to be pinned |
|---|---|
| **priority direction: 0 is highest** | FreeRTOS is the opposite *and* reserves 0 for the idle task; Zephyr's cooperative priorities are negative. Forwarding the number instead of translating silently inverts every task's priority - the port inverts |
| **task storage: the implementation owns it** | FreeRTOS allocates; ThreadX and static Zephyr require caller-owned control blocks and stacks. A port without a heap supplies a static pool sized by its own config macro and returns `EDGE_ENOSPC` when full |
| **creation before `start` works** | that is what a composition root does. A kernel whose entry call never returns buffers the requests and creates them from its entry callback; the port may not require the caller to move assembly there |
| **absent capabilities report 0** | `edge_rtos_task_stack_high_water()` returns 0 when the kernel cannot report it. 0 means *unavailable*, never *plenty* - a product must not read it as a safety margin |

## The kernels, side by side

FreeRTOS, ThreadX and RTEMS rows are facts from this tree. Zephyr rows come from the vendor
documentation reviewed in #134 and #82 and are **not verified here** - they are the
reason the preparation exists, not a substitute for trying it.

| concern | FreeRTOS (in tree) | ThreadX (#134) | Zephyr (#82) | RTEMS (#135) |
|---|---|---|---|---|
| task creation | `xTaskCreate`, kernel-allocated stack | `tx_thread_create` with a **caller-owned** `TX_THREAD` + stack; no heap | `k_thread_create` with a caller-owned stack | `rtems_task_create` with static task pool |
| start | `vTaskStartScheduler` after creation | `tx_kernel_enter` **never returns** | no entry call: started at boot | `rtems_initialize_executive` / `Init` task |
| priority | **higher number = higher priority**, 0 = idle | 0 = highest | lower number = higher; negative = cooperative | 1 = highest, 255 = lowest (`prio + 1`) |
| stack high-water | `uxTaskGetStackHighWaterMark` (real) | `tx_thread_stack_highest_ptr` | `k_thread_stack_space_get` (real) | stack checker hook |
| assert contract | `configASSERT` routed to `edge_rtos_assert_failed()` | fault handlers | `__ASSERT` / `k_panic`, hookable | `rtems_fatal_error_occurred` |
| configuration | product-owned `FreeRTOSConfig.h` (D87) | product-owned `tx_user.h` | **Kconfig + devicetree** | `<rtems/confdefs.h>` |
| build integration | `FetchContent` of the kernel sources | same shape as FreeRTOS | **west + `module.yml` + DTS** | CMake / Waf BSP link |
| tickless | `configUSE_TICKLESS_IDLE` | `TX_LOW_POWER` | `CONFIG_PM` / tickless idle | BSP idle power hook |

## The two hard parts, named

1. **ThreadX is a kernel-shaped port** - the same shape as the FreeRTOS one, with
   four translations instead of none: a static task pool, buffered creation because
   entry never returns, a preprocessor-only config file, and an assertion surface -
   though that last one is still open, because the shared startup table exposes no
   fault vector to hang it off (the same gap the other firmware products have, so it
   belongs in `.github/arm/startup.c`, not in this port). That is mechanical work, which is why #134
   recommends it as the second kernel. It is now integrated and host-verified
   (`pal/rtos/threadx`, `product/meter_threadx`), and three port facts were measured
   rather than assumed:
   - **The kernel's clock is not readable before `tx_kernel_enter()`** on the Linux
     port: `tx_time_get()` takes the kernel's own lock, which `_tx_initialize_low_level()`
     has not created yet, so an early read blocks forever. The product initialises the
     framework from its first thread instead.
   - **A thread's entry parameter is a `ULONG`**, which on the Linux port is 32 bits:
     passing a host pointer there truncates it and the first dereference faults. The
     port passes a pool index.
   - **The Linux port's `TX_LINUX_DEBUG_ENABLE` trace turns `TX_DISABLE` into a plain
     non-recursive mutex**, so any kernel call made inside a critical section
     self-deadlocks. The port does not enable the trace, and the host build does not
     mask a clock read that no interrupt can interleave.
   - **A fast peripheral interrupt at the same priority as the tick starves it.** The
     board's timer IRQ sits at library priority 5 and fires every 250 core cycles
     (~100 kHz on the MPS2); with the tick at the same priority neither can preempt the
     other, so the tick stopped and every kernel timeout stopped expiring (measured: the
     witness task froze at 20 ms). The ceiling is now 4, one step above the peripheral:
     the tick preempts it, and both stay maskable by a critical section, which is what
     the kernel needs from an interrupt that calls into it.
   - **The task pool is the product's to state.** It is the largest single consumer of
     RAM, and this board has 16 KiB in total: an unstated pool is either too small to
     create anything or too large to link.
   - **A QEMU firmware without `EDGE_MODULE_ARM_QEMU_SEMIHOSTING` compiles its exit into
     a spin loop** - the board's semihosting exit is behind that macro, so a missing
     option looks exactly like a firmware hang. The ThreadX firmware function refuses
     that build.
2. **Zephyr is a build-system integration, not a header swap.** Kconfig and
   devicetree describe the *board* as data, which touches D3/D9 ("the board owns
   the hardware facts") in a way the other two do not: a Zephyr board is a DTS
   overlay, not a `board.c`. Budget it as a different workstream, not "one more
   `pal/rtos/<os>` directory".

## Prerequisites, in order

1. **This page and the pinned contract.** Done.
2. **#89: pin the kernel to an immutable revision and list it in the SBOM.** Today
   FreeRTOS is pinned by a *movable tag* and `generate_sbom.py` lists **no kernel at
   all**, so adding a second and third kernel would widen an existing gap twice
   over. This is a prerequisite for every kernel after the first, and it is the
   reason #134 says not to start until it lands.
3. **A reusable port conformance suite.** `tests/contract/` already holds suites
   that several tests reuse and `tests/contract_violations/` makes CTest prove they
   reject a broken implementation. The RTOS port contract is the next candidate: a
   port that satisfies `pal_rtos/rtos.h` should be provable, not asserted in prose.
4. **Then one kernel at a time**, ThreadX first (kernel-shaped), Zephyr last
   (build-shaped).

## Where `pal/eos` fits

`pal/eos` is the first-party runtime and the fourth column of this table: it is the
port that has to satisfy the same contract with **no third-party kernel at all**,
and it is the only one that runs under host tests. That makes it the reference for
the contract rather than a competitor to the kernels - and it is why the priority
direction is already pinned there (`0` = highest) before being pinned in the
contract itself.

## Not in this preparation

- No third kernel is being added here; this is the contract and the map.
- No per-kernel priority *mapping policy*: kernel priorities are the product's
  business. The rule that a task which must progress while the runner is parked has
  to be numerically lower in the pinned convention stays in
  [`rtos-runner.md`](rtos-runner.md) section 4.
- No claim that ThreadX or Zephyr will be easy. The table above is where they are
  not.
