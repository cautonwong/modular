# Architecture governance: ADR -> gate

`docs/adr.md` (D1-D85) is the decision record; [`adr-conformance.md`](adr-conformance.md)
is the status view. This file defines how decisions become **executable gates**.

## Principle

A rule that cannot be automatically checked is a soft rule that will eventually
be broken. But not every decision is mechanically checkable: "interfaces follow
the consumer" is a design intent, not a predicate. So the target is **not**
85/85 scripted; it is:

> every **core invariant** has a gate, and every un-gated ADR is **visible** as
> tracked debt rather than an invisible risk.

## The matrix

`ci/adr-gates.json` maps `ADR id -> { file, marker? }`. `check_adr_gates.py`:

- verifies each gate file exists and its marker (if any) is present;
- verifies every `must_gate` ADR has a gate entry;
- rejects gates that reference an ADR absent from `adr-conformance.md`;
- prints the paper-only set (currently 58 of 85) as a ratchet backlog.

`must_gate` = D7, D14, D18, D21, D26, D31, D37, D44, D48, D55, D68, D86, D87.

## Layer dependency matrix

The topology is executable, not prose. `check_layer_dependencies.py` resolves every
quoted include in a layer to the layer that owns the target file and enforces:

```text
edge_module -> edge_module only
app     -> app/<self> + edge_module
sys     -> sys + edge_module
board   -> board + soc + pal + edge_module
infra   -> infra + pal + edge_module
soc     -> soc
pal     -> pal + edge_module
product -> all
```

`infra -> soc` is denied with **no exception**: a reusable infra module only ever
depends on narrow ports. SoC-bound (register/HAL) code belongs in `soc/<soc>/`,
OS/RTOS-bound code in `pal/<os>/`, and the binding happens in `product/<name>/glue`.
Negative fixtures live in `tests/guards/layer_*` (e.g. `soc -> board`, `app -> infra`,
`infra -> soc`).

## Product-to-board binding (D86)

`edge_add_product()` records `EDGE_PRODUCT_<name>_BOARD` and rejects a second
registration or a re-bind of an existing product; firmware targets take a product
name only, so they cannot override the binding. `check_product_board_binding.py`
re-checks the same rules from the parsed `CMakeLists.txt`, with fixtures in
`tests/guards/binding_*`. The T7b neutrality fixture (`edge_add_minimal_variant`)
is a test artifact and is exempt.

## RTOS configuration ownership (D87)

`pal/` ships **no** `FreeRTOSConfig.h`: `pal -> product`/`board`/`soc` is forbidden by
the layer rule. Instead the product owns `product/<name>/include/FreeRTOSConfig.h`,
which composes the layers it owns (`soc/<soc>` priorities, `board/<board>` clock,
product resources) and then includes the PAL-owned `edge_rtos_config.h`. That PAL
header applies the defaults and fails the build (`#error`) if the product disabled
stack-overflow detection, the stack high-water API, `configASSERT`, or left dynamic
allocation unstated. The composition root puts the product include dir on the
include path of `freertos_kernel`, `pal_rtos_freertos` and the firmware, so all
three share one configuration instance; a second RTOS product needs its own kernel
target (`edge_add_arm_freertos_firmware` rejects a second owner).

## Gates added for previously paper-only invariants

- **D18 (events are scalar facts, no bare pointers)** — `check_event_payload.py`
  asserts the exact field set and types of `struct edge_event`; a `sizeof` assert
  alone cannot catch a field being retyped to a pointer.
- **D21 (zero runtime allocation)** — `check_no_dynamic_memory.py` rejects
  `malloc/calloc/realloc/free/strdup` symbols in the firmware `nm` output. It runs
  on all four firmware images (M0, M4, RISC-V, FreeRTOS). The FreeRTOS kernel's
  own `pvPortMalloc`/`vPortFree` are not libc allocation and are allowed.

## Adding a decision

1. Add the ADR to `docs/adr.md` and its status row to `adr-conformance.md`.
2. Add a gate to `ci/adr-gates.json`; if it is a core invariant, add it to
   `must_gate`.
3. Add positive/negative fixtures and a `run_guard_selftest.py` case.
   `check_guard_coverage.py` rejects any `check_*.py` that is missing either a
   positive or a negative case (or whose fixture does not exist), so this step is
   enforced rather than remembered. Generators are not gates and get a smoke test
   instead (`run_generator_selftest.py`).

## Ticket conventions

Tickets are the unit of work; the tracker is GitHub. `.github/ISSUE_TEMPLATE/`
carries issue forms that require the same three sections the ADR work has used:
**What to build**, **Acceptance criteria**, **Blocked by**. The forms exist so the
structure survives being written by someone who has not read this file.

### Priorities

| Label | Means | Test |
|---|---|---|
| `P0` | The repo is red, or everything else is waiting on it | Would a second person be blocked today? |
| `P1` | High leverage and executable now | Does finishing it unblock or de-risk another ticket? |
| `P2` | Supporting work: proofs, examples, docs-level closure | None of the above |
| `P3` | Not now: blocked on infrastructure, or a separately chartered workstream | Do not start without new information |

Priority is **importance**, not readiness. Readiness is carried by `Blocked by`.

### `ready-for-agent`

Apply the label only when **all** of these hold:

1. `Blocked by` is empty (or every blocker is closed).
2. What to build and the acceptance criteria are complete enough to verify
   without asking a question.
3. The verification is expressible as a test, a guard, or a CI job.

A blocked ticket must **not** carry the label: the label is a claim that the work
can be started now, and a false claim is worse than a missing one. When a blocker
closes, re-read the ticket and apply the label then.

### Sizing

One logical change per ticket, sized to a single review, with its own test
coverage. If a ticket cannot be verified on its own, it is either two tickets or
it belongs as an acceptance criterion of another one.
