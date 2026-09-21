# Flake ledger

Non-deterministic failures have to be **visible**. The failure mode this file
exists to prevent is the quiet one: a job fails, someone re-runs it, it passes,
and nobody ever learns that the suite is not deterministic.

So: a re-run that passes **still gets an entry here**.

## What counts as a flake

All three must hold:

1. It failed, then passed on a re-run of the **same commit** with no code change.
2. The same job has failed on unrelated commits.
3. The cause is not a real defect, an upstream outage, or a network fault outside
   the repository's control.

Anything else is a **deterministic failure**. Those are recorded here too, so the
line between the two is written down rather than argued about later.

## Retry policy

- **No automatic retries.** A third-party retry action costs more in supply chain
  than it saves for a single maintainer; `Re-run failed jobs` in the Actions UI is
  the mechanism.
- A re-run that passes still requires an entry (see above).
- **Quarantine** is available but never silent: a job that flakes twice within ten
  runs, or a test that flakes twice, may be quarantined only with an entry naming
  an owner and the condition for removing the quarantine. There is no "skip it and
  move on".
- A quarantined job must not be a **required** check. If a required check has to
  be quarantined, that is a protection change and follows the emergency-exit
  procedure in [`../CONTRIBUTING.md`](../CONTRIBUTING.md).

## Entries

| Date | Job / test | Symptom | Reproduction attempt | Classification | Disposition | Ref |
|---|---|---|---|---|---|---|
| 2026-09-18 | `static-analysis` / Formatting check | `clang-format --dry-run --Werror` rejected two files | Reproduced locally with clang-format 18.1.3, the same version CI uses | **Deterministic** | Fixed by applying clang-format | #69 |
| 2026-09-21 | Embedded Smoke / Cortex-M4 QEMU smoke | `rc=137` - the firmware was killed by the step's 10 s `timeout`, while the same commit passed locally | Re-ran locally: 1.06 s per run, 3/3 pass. The step log shows the child was killed, not that the firmware exited non-zero | **Deterministic on a slow runner, not a flake**: the on-target clock self-check added in #126 sampled 400k times with 64 spins per sample (~25M iterations). That fits a fast machine (~1 s) and not a shared runner on a 10 s budget - a correctness check turned into a timeout | Fixed in #152: two cheap deterministic checks instead - a 256-sample non-decreasing burst over real reads, plus a synthetic wrap driven through the extension arithmetic, which pins the exact delta and the wrap count. ~70 ms locally, and the defect is caught 3/3 instead of 2/3 | #126 |
| 2026-09-20 | Embedded Smoke / FreeRTOS QEMU smoke | `rc=16` (the runner-starvation probe) in 1 of 7 runs of the same commit | Re-ran the identical firmware binary; measured 1/7, then 6/6 clean | **Flake in the check, not the product**: the probe measured its window as a kernel-tick difference, and #90 also enabled tickless sleep in that product - a suppressed-tick jump can satisfy a tick window before the lower-priority witness is ever scheduled | Fixed in the same branch: the probe counts *blocking sleeps* instead of ticks. Re-measured 10/10 clean with the blocking policy, and 5/5 `rc=16` with a bare `taskYIELD()`, so the refutation still holds | #90 |
| 2026-09-18 | `static-analysis` / cppcheck | cppcheck exited non-zero on a real finding | Reproduced locally (`cppcheck 2.7` initially reported the same class of finding) | **Deterministic** | Fixed; a later differential run showed findings 3 -> 1 | #69 |
| 2026-09-19 | `Embedded Smoke` / FreeRTOS build | `pal_rtos/rtos.h` pulled `pal_os/os.h`, which was not on the kernel's include path | Reproduced with the kernel's exact include set: same `fatal error` | **Deterministic** | Fixed by moving the assert contract into a dependency-free header | #70 |
| 2026-09-19 | `pull_request` workflows | No CI ran at all on PR #74 (`mergeable_state: dirty`) | Confirmed the branch contained already squash-merged commits, so GitHub could not build the merge commit | **Deterministic** | Cherry-picked onto `main`; the branch discipline and the `pre-push` warning were added by #96 | #74, #105 |

## Current state

**No non-deterministic flake has been observed in this repository so far.** Every
failure above was reproducible, which is why they are classified as deterministic.
The quarantine path is therefore documented but not yet exercised; the first real
flake goes in the table above, and the re-run that absorbed it is exactly what this
file is for.
