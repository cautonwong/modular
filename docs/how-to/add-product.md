# Add a product

A product is the composition root: it selects the family, board, infra and apps,
constructs them, adapts them, and starts the scheduler. It contains **no business
logic** — if you are writing an `if` about behaviour, it belongs in an app or in
the family's `sys`.

## Steps

1. **Files** — `product/<name>/main.c` and `product/<name>/glue.c` (the manifest
   of a product is CMake plus `main()`, D7; there is no manifest file).
2. **CMake** —
   ```cmake
   edge_add_product(<name> family <family> board <board> infra <i...> apps <a...>)
   ```
   The `family:board` pair must be in `EDGE_LEGAL_FAMILY_BOARD`. A product name is
   registered exactly once and is permanently bound to that board (D86); a second
   registration or a re-bind fails at configure time.
3. **`glue.c`** — adapters only: take a concrete infra provider and expose the
   consumer-defined port an app declared.
4. **`main.c` order** (see `product/example/main.c` as the reference):
   ```
   board_init(sink)            -> interrupts and safe hardware actions
   infra construct             -> caller-owned storage (D21)
   adapters                    -> glue, no business logic
   <app>_construct(self, id, priority, deps)
   <app>_init(self)            -> D51: the composition root starts the module;
                                  the skip/fatal policy (D53) is decided here
   apps[] = { <app>_module(&app), ... }
   sys init, required set, clock, subscribe
   sys_start, then the loop (sys_run_once / sys_step / sys_run)
   ```
5. **Shutdown, reverse order** —
   `sys_power_off` (modules save state, reverse priority) →
   `<app>_deinit(self)` for each app, reverse construction order → `sys_deinit`.
6. **Firmware wrapper** (only if it targets real hardware) —
   `edge_add_arm_firmware`, `edge_add_riscv_firmware` or
   `edge_add_arm_freertos_firmware`. A FreeRTOS product must also provide
   `product/<name>/include/FreeRTOSConfig.h` composing the `soc/`, `board/` and
   product layers (D87).

## Completion criterion

```bash
cmake -S . -B build -G Ninja -DEDGE_MODULE_BUILD_TESTS=ON && cmake --build build
ctest --test-dir build
./build/<name>                                          # host products exit 0
python3 .github/scripts/check_cmake_apps.py             # apps list == constructs
python3 .github/scripts/check_product_board_binding.py
python3 .github/scripts/check_layer_dependencies.py     # product -> all
```

For a firmware target: the ELF architecture check, the size budget and the map
layer budget all pass.

## Common traps

- **The apps list drifting from `main.c`.** Declaring an app in CMake but
  constructing a different set (or the reverse) is caught by
  `check_cmake_apps.py`; it is the easiest mistake to make when copying a
  product.
- **Leaving out `_init`.** With D51 the scheduler no longer calls it, so a module
  that is never initialised simply runs with unvalidated dependencies. A required
  module that the product forgets to add is still caught by
  `edge_sys_validate_required`.
- **Forgetting the reverse `_deinit`.** Nothing else tears module state down any
  more.
- **Business logic in `main.c` or `glue.c`.** Selection and wiring only.
- **Re-pointing a product at another board.** Not possible by design (D86) — make
  a new product entry instead.
