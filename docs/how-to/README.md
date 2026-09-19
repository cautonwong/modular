# How to add things

Ordered checklists for the changes that happen most often. Each one lists the
files a change touches, the registry/gate touchpoints that are easy to forget,
and a **completion criterion** you can check instead of hoping.

Read [`../governance.md`](../governance.md) first: it defines the layer matrix,
the ADR-to-gate rule, and the ticket conventions these checklists assume.

| Task | Checklist |
|---|---|
| Add an application module | [`add-app.md`](add-app.md) |
| Add a board (and its SoC) | [`add-board.md`](add-board.md) |
| Add an infrastructure driver | [`add-driver.md`](add-driver.md) |
| Add a product (composition root) | [`add-product.md`](add-product.md) |
| Add or change a layer | [`add-layer.md`](add-layer.md) |

## The gate that applies to all of them

```bash
python3 tests/guards/run_guard_selftest.py        # the guards still work
python3 tests/guards/run_generator_selftest.py    # the generators still work
cmake -S . -B build -G Ninja -DEDGE_MODULE_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

The pre-commit hook (`.githooks/pre-commit`) runs the source-level half of this
before the commit exists; CI runs all of it plus the target builds.
