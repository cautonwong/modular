# Add or change a layer

Layers are executable topology here, not prose. Adding one touches the guard, the
documented matrix, and the decision record at the same time — that is deliberate,
because an unguarded layer is a layer that will be crossed.

## Steps

1. **Directory** — `edge_module/`, `app/`, `sys/`, `board/`, `infra/`, `soc/`,
   `pal/`, `product/` are the current layers. A new one is a structural decision,
   so start from an ADR entry, not from the directory.
2. **Guard** — in `.github/scripts/check_layer_dependencies.py`:
   - add the name to `LAYER_DIRS`
   - add its allowed targets to `ALLOWED` (what may it include?)
   - add it to `SUBDIR_LAYERS` if it has per-owner subdirectories
3. **Documented matrix** — update the matrix in
   [`../governance.md`](../governance.md) so the prose and the code agree.
4. **Decision record** — add the decision to `docs/adr.md` (a new `D<n>`, or an
   amendment to an existing row following
   [`../adr.md`](../adr.md) conventions), a row to `docs/adr-conformance.md`, and
   a gate to `ci/adr-gates.json`. Core invariants also go into `must_gate`.
5. **Fixtures, both polarities** — extend `tests/guards/layer_good*` and
   `layer_bad*` and add the cases to `tests/guards/run_guard_selftest.py`.
   `check_guard_coverage.py` rejects a checker that is missing either a positive
   or a negative case, so this is enforced rather than remembered.

## Completion criterion

```bash
python3 .github/scripts/check_layer_dependencies.py
python3 .github/scripts/check_adr_gates.py
python3 .github/scripts/check_guard_coverage.py
python3 tests/guards/run_guard_selftest.py
python3 .github/scripts/check_docs.py
```

## Common traps

- **Adding the layer without a gate.** `governance.md` states the target: every
  core invariant has a gate, and every un-gated decision is visible as tracked
  debt in `ci/adr-gates.json`.
- **Letting the documented matrix drift.** The guard and `governance.md` are two
  statements of the same rule; when they disagree, the reader loses.
- **A one-directional exception.** `infra -> soc` was once allowed through a
  whitelist and the whitelist was removed (D48). Prefer restructuring over an
  exception: an exception is a rule that will grow.
- **Forgetting `check_adr_gates.py`.** It rejects a gate that references an ADR
  which has no conformance row, so the three files must move together.
