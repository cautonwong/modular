# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Before exploring, read these

- **`CONTEXT.md`** at the repo root (single-context). Created lazily; if absent, proceed silently.
- **`docs/adr.md`** — the extended architecture decision record (D1-D85).
- **`docs/adr-conformance.md`** — how far the code matches the ADR, plus the open conflicts.
- **`todo.md`** — archived early D1-D45 subset (superseded by `docs/adr.md`).
- **`docs/architecture.md`** — the implemented runtime and boundary summary.

If any of these files don't exist, **proceed silently**. Don't flag their absence; don't suggest creating them upfront. The `/domain-modeling` skill creates `CONTEXT.md` lazily when terms or decisions actually get resolved.

## File structure

Single-context repo:

```
/
├── CONTEXT.md                     ← lazily created
├── todo.md                        archived D1-D45 (superseded by docs/adr.md)
├── docs/
│   ├── adr.md                     extended decision record (D1-D85)
│   ├── adr-conformance.md         ADR vs implementation status
│   ├── architecture.md
│   └── todo-status.md
├── edge_module/                   framework contract
├── sys/  board/  infra/  app/     layers
└── product/                       composition root
```

This repo keeps one decision-record file (`docs/adr.md`) rather than a `docs/adr/` directory of numbered ADRs. Cite decisions by their `D<n>` id (e.g. `D53`), and check `docs/adr-conformance.md` for whether that decision is implemented, partial, or in conflict.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in `CONTEXT.md`. Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal: either you're inventing language the project doesn't use (reconsider) or there's a real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing decision, surface it explicitly rather than silently overriding:

> _Contradicts D65 (one SPSC queue per producer), but worth reopening because…_
