<!--
Thanks for contributing! Please fill in the sections below.
Keep one logical change per PR and make sure every required check is green.
-->

## Summary

<!-- What does this change do and why? -->

## Layer(s) touched

- [ ] `edge_module` (framework contract — highest review bar)
- [ ] `sys` / `board` / `infra` (certifiable core)
- [ ] `app`
- [ ] `product` (composition root / glue)
- [ ] Build / CI / docs only

## Architecture checklist

- [ ] Dependency direction respected (`app -> edge_module`; `product -> *`).
- [ ] No runtime allocation, no RTOS/threads, no service locator.
- [ ] Events carry scalar facts only; commands use explicit ports.
- [ ] No architecture-specific code leaked into framework layers.
- [ ] New/changed behaviour is covered by tests.
- [ ] `CHANGELOG.md` updated for user-visible changes.

## Verification

<!-- Commands you ran, e.g. ctest / clang-tidy / cross build + resulting size. -->

## Related ticket

<!-- Required, e.g. "Closes #123". The commit subject carries the same `(#123)` reference; see CONTRIBUTING.md. -->

Closes #
