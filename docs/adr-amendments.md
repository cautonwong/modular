# ADR amendment log

`docs/adr.md` is the decision record; `docs/adr-conformance.md` is the status
view. This file is the **history** of the record.

## The rule

A frozen decision row is **not rewritten to mean something else**. When a decision
changes, the change is appended here with the date, the ADR id, a one-line
description of what changed, and the issue/PR that triggered it. If the row text
must move to stay truthful, it moves *and* the move is recorded here.

Implementation progress is **not** an amendment: "D55 is now implemented" belongs
in `adr-conformance.md`, not here.

## Log

| Date | ADR | Change | Trigger |
|---|---|---|---|
| 2026-09-17 | D30 | `❌ not implemented` → **`🚫 wontfix`**: `edge_util` is deliberately not built; revisit only if duplication appears | conformance pass (`a874336`) |
| 2026-09-18 | D48 | **Tightened.** `infra -> soc` is denied with **no exception** (the `INFRA_SOC_BOUND` whitelist was removed); register/HAL code lives in `soc/<soc>`, OS device models in `pal/<os>`, and the binding in product glue. The §24 directory examples were corrected to match | #66, PR #69 (`704004c`) |
| 2026-09-18 | **D86** | **New.** `product/<name>` binds exactly one board, the binding is recorded at configure time and cannot be overridden by a build parameter; a product name is registered once | #68, PR #69 (`704004c`) |
| 2026-09-18 | **D87** | **New.** The FreeRTOS configuration is composed by the product (`soc/` + `board/` + product), the PAL only declares the contract and the required invariants (`#error`), and one compiled kernel serves one configuration | #67, PR #70 (`83c9eb2`) |
| 2026-09-19 | D15, D51 | **Satisfied, by changing the contract to match the ADR.** `init`/`deinit` are no longer in `edge_module_t`; the composition root calls `<app>_init(self)` / `<app>_deinit(self)`. ABI: 104 → 88 bytes (LP64), 64 → 56 bytes (ARM32/RV32). This was the last open contract conflict | #9, PR #74 (`4c78e24`) |
| 2026-09-19 | D53 | **Scope narrowed.** Assembly-time `init` failure policy (skip/record, or fatal) moved to the composition root, where D51 puts the call. `sys` keeps the runtime half (isolation after a `poll`/`on_event` failure), and the D53 gate marker follows that code | #9, PR #74 (`4c78e24`) |

## Not amendments

Housekeeping edits that keep the record truthful without changing a decision are
not logged here. The known example: the T7b neutrality fixture path was corrected
from `test/minimal_product` to `tests/integration/minimal_product` to match where
the fixture actually lives.

## Adding an entry

1. Add the row above with the date, the ADR id, what changed, and the trigger.
2. If the ADR row text must change to stay truthful, change it **and** say so in
   the row here.
3. Keep `docs/adr-conformance.md` and, when a gate moves, `ci/adr-gates.json` in
   step — `check_adr_gates.py` rejects a gate whose ADR has no conformance row.
