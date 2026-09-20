# EOS: the first-party runtime

## What the name means here

Two senses, deliberately kept apart:

1. **`pal/eos/` — the first-party runtime kernel** (this document's subject): a
   static, zero-allocation, fixed-rate priority executive. It is a *module inside
   the `pal` layer*, a sibling of `pal/rtos/freertos`, not a new layer. Both
   implement the same neutral contract (`pal_rtos/rtos.h` + `edge_pal_port_t`),
   which is what makes that contract a contract rather than a description of one
   kernel.
2. **The EOS metric set** — the neutral core composition (`edge_module`, `sys`,
   `pal`, `board`, `soc`, `infra`) whose footprint a build's map report should
   roll up as `eos`. That rollup does not exist yet; see "Open" below, which
   starts with a real attribution bug.

When one word has to mean one thing, EOS means the `pal/eos` module.

## What the current slice guarantees

| guarantee | how, and how it is checked |
|---|---|
| zero runtime allocation (D21) | the task table and every counter live in the caller-owned `edge_eos_t`; `check_no_dynamic_memory.py` covers the images |
| bounded work per round | one round per due task, and a missed deadline is not replayed, so work per round is at most the task count |
| no starvation by construction | one round per task per pass, in priority order — not "run until done"; `tests/test_eos.c` asserts a priority-3 task makes the same progress as a priority-0 task |
| deterministic | priority order and period accounting only; deadlines use D72's modular compare, so a tick wrap does not read as "not due yet" |
| priority direction pinned | **0 is the highest priority**. The neutral contract deliberately does not pin this (an independent review of the ThreadX port found the same gap), so it is pinned here, in public |
| host-testable | no assembly, no per-task stacks, no context switch — the whole executive runs under cmocka |

## What it does not have (slice 1)

- **No preemption.** No PendSV context switch and no per-task stacks. There is
  therefore **no context-switch latency to report** — that number does not exist
  in this repository for any target.
- **No MPU, no privilege separation, no mutual exclusion primitives.**
- **Not wired into any product.** No firmware image links `pal/eos` yet, so it
  contributes zero bytes to every measured image below. Wiring it into a product
  is a separate slice.

## Measured metrics

Repo-recorded numbers, with the command that produces each. Nothing here is
hand-written except the record itself, which lives in `ci/`.

| metric | value | how to reproduce | status |
|---|---|---|---|
| host test suites | **25** (`test_eos` added) | `ctest --test-dir <build>` | measured |
| guard refutation cases | **53** | `python3 tests/guards/run_guard_selftest.py` | measured |
| Cortex-M0 firmware flash / RAM | **5282 B / 16 B** (8.1 % / 0.1 % of the part) | `check_size.py` + `check_map_budget.py --report` | recorded baseline in `ci/size-baseline.json`, not re-measured here |
| tightest binding budget | `edge_module` **1024 B flash**, 560 B headroom (1.8x) | `ci/size-budget.json` | recorded |
| worst framework stack frame | **≤ 128 B** against a 512 B gate | `check_stack_usage.py` | recorded |
| dynamic-allocation symbols | **0** in all four images | `check_no_dynamic_memory.py` | recorded |
| coverage floor | **95 % lines**, gated | `gcovr --fail-under-line 95` | recorded |

## Against PX5's published claims

PX5 RTOS is a closed-source commercial safety RTOS. It is used here as a
**published-claim benchmark**, not as a design source: this project must not read
its source code or derive from its documentation, and it does not.

| metric | PX5 published | ours | verdict |
|---|---|---|---|
| minimal kernel flash | "as little as 1KB" (vendor) vs "2 KB Flash / 1 KB RAM" (ST partner blog) | 5282 B for a whole example firmware; `edge_module` alone 464 B | **category mismatch, do not score**: theirs is a kernel minimum with no configuration stated, ours includes startup, libc and a product |
| minimal RAM | 1 KB | **16 B** static in the M0 image | **not comparable**: everything here is caller-owned (D21), so "16 B" is a different quantity, not a win |
| context-switch latency | "typically < 1 µs" at 80 MHz, no table | **nothing measured; no harness exists** | **behind, and unmeasured** — the single most actionable gap |
| certification | SGS-TÜV Saar: IEC 61508 SIL 4, ISO 26262 ASIL D, IEC 62304 Class C, EN 50128 SW-SIL 4 (announced 2024-04-04; corroborated by partner catalogues) | none | **unscorable**: a certificate is issued to a product and its evidence chain, and the integrator still validates assumptions of use in their own context. "SIL 4 vs none" states that a workstream exists on one side, not a score |
| certified scope split | generic C code certified; ~10 small binding functions excluded | the D45 core/app boundary exists on paper only | **behind on artefact, ahead on mechanism**: the layer matrices already define the boundary mechanically; the file list was never generated (see #136) |
| architecture breadth | ~27 cores (M/R/A, RISC-V, RX, MicroBlaze, TriCore) | Cortex-M0 built in CI, Cortex-M4 QEMU, RISC-V 32 QEMU, host | **behind on breadth, ahead on structure**: a new arch is one `pal/` port plus one board, and the refusal path for a wrong arch is itself tested |
| toolchain breadth | IAR, GCC, Arm tools | GCC + Clang in CI; IAR toolchain file exists, not in CI | **behind**: the IAR claim is a file, not a green job |
| third-party benchmark | Beningo 2026, public methodology, pinned versions | never benchmarked by anyone | **behind on external validation** |
| licence / price | from $5K, source in a private portal, evaluation gated | no fee, no gate — and **no `LICENSE` file at the repo root** | **ahead on access, behind on legal clarity** |
| enforced invariants | MISRA C:2012 292/297, statement+branch coverage 100 %, C-STAT clean — all behind the licence | zero allocation, per-function stack ceiling, per-layer map budgets, reproducibility, layer/RTOS neutrality — all machine-checked in CI, with 53 refutation cases | **ahead on inspectability, behind on certification-grade rigour**; neither substitutes for the other |

Sources for the PX5 column: the vendor's product and FAQ pages, the ST partner
blog post, the Renesas partner catalogue entry, and the 2024-04-04 certification
announcement. Third-party benchmark numbers come from Beningo's published RTOS
performance reports, which state the methodology and pin kernel versions.

## Non-claims

Copy this block into anything that describes EOS:

> **EOS — scope and non-claims.** EOS is the first-party runtime module in
> `pal/eos`: a static, zero-allocation, fixed-rate priority executive. It is
> **not** a certified functional-safety element: no certificate, safety manual,
> safety case or assessment exists for it, and none is issued to a design — a
> certificate is issued to a product or safety element out of context together
> with its evidence chain, and any integrator must still validate the assumptions
> of use in their own context. EOS makes **no execution-time performance claim**:
> there is no latency, throughput or benchmark number in this repository and no
> independent party has benchmarked it. EOS makes **no low-power claim**: tickless
> sleep is enabled and verified in QEMU, and STOP mode, clock-tree control,
> peripheral gating and retained-memory verification are board/SoC work that is
> not implemented. EOS makes **no multi-task scheduling claim** in this slice: it
> has no preemption, no priority inheritance and no SMP/AMP behaviour; the
> framework's runner remains a single task by construction (D47). EOS is **not a
> distributed product**: there is no SDK, no ABI compatibility matrix, no support
> commitment and no licence statement here. EOS is **not** related to, derived
> from, or compatible with any commercial RTOS; PX5 is a trademark of its owner
> and is referenced only as a published-claim benchmark.

## Open

1. **The metric rollup, and the attribution bug it depends on.**
   `check_map_budget.py`'s `layer_of()` does not recognise `libpal_host.a`,
   `libpal_os.a` or `libpal_cortex_m_bare.a`, so they fall into the `other`
   bucket — 764 B, 14 % of the M0 image. Any "EOS footprint" derived from today's
   report silently omits the non-RTOS PAL. Fix the attribution and emit an `eos`
   rollup of `eos_layers` (edge_module, sys, pal, board, soc, infra) before
   quoting any footprint.
2. **Only one image has a recorded baseline.** `ci/size-baseline.json` covers
   `example_cortex_m0`; the M4, RISC-V and FreeRTOS images have none, so "EOS
   measured" currently means "measured on one target".
3. **The latency harness does not exist.** Until there is a cycle-counter harness
   with a deliberately-slow refutation variant, the comparison with any published
   latency claim is not merely unfavourable, it is absent.
4. **Slice 2: preemption** (PendSV context switch, per-task stacks) — the
   prerequisite for a context-switch latency number and for `edge_rtos_task_create`
   parity.
5. **Slice 3: a product** (`product/meter_mps2_eos`) running the same capsule on
   EOS, with a QEMU smoke — the only way to claim EOS is a runtime rather than a
   scheduler library.
