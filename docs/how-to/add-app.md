# Add an application module

An app is reusable business logic. It depends on `edge_module` and on the ports
**it defines itself** (D14) — never on another app, a concrete `infra`, a board,
or a register address.

## Steps

1. **Public header** — `app/<name>/include/<name>/<name>.h`:
   - the consumer-defined ports the app needs (plain function-pointer structs,
     e.g. what `dlt645_storage_if_t` is for `app/dlt645`)
   - `<name>_t` holding an `edge_module_t module` plus its own state
   - `void <name>_construct(<name>_t *self, uint32_t module_id, uint32_t priority, ...deps);`
   - `edge_status_t <name>_init(<name>_t *self);` and
     `edge_status_t <name>_deinit(<name>_t *self);` — D51: the **composition root**
     calls these, not the scheduler
   - `edge_module_t *<name>_module(<name>_t *self);` — ADR 20.3.5 shape
2. **Implementation** — `app/<name>/src/<name>.c`. `construct` fills the module
   struct with `poll`, `on_event`, `power_off` (and optionally `suspend`/`resume`)
   and sets `private_data = self`. Do **not** put `init`/`deinit` in the struct.
3. **Module id** — add `EDGE_MOD_<NAME>` to `edge_module/include/edge/modules.h`
   on a `0xNN00` segment. `check_module_ids.py` rejects a duplicate or a
   non-aligned value.
4. **CMake** — in the top-level `CMakeLists.txt`, copy the shape of an existing
   app:
   ```cmake
   add_library(app_<name> STATIC app/<name>/src/<name>.c)
   target_include_directories(app_<name> PUBLIC
       ${CMAKE_CURRENT_SOURCE_DIR}/app/<name>/include
       ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include)
   target_link_libraries(app_<name> PUBLIC edge_module)
   edge_enable_quality(app_<name>)
   set(EDGE_APP_TARGET_<name> app_<name>)
   ```
5. **Assemble it somewhere** — add `<name>` to the `apps` list of the products
   that use it (`edge_add_product(... apps ...)`), and in that product's
   `main.c` call `<name>_construct(...)` then `<name>_init(...)`.
6. **Adapter** — if the app needs infrastructure, adapt it in
   `product/<product>/glue.c`. The app defines the interface; the product bridges
   it. See [`add-product.md`](add-product.md).
7. **Test** — `tests/test_app_<name>.c` plus, in the test block of
   `CMakeLists.txt`, `edge_add_host_test(test_app_<name> tests/test_app_<name>.c)`.

## Completion criterion

```bash
python3 .github/scripts/check_cmake_apps.py            # CMake apps list == main.c constructs
python3 .github/scripts/check_app_isolation.py         # no concrete-layer includes
python3 .github/scripts/check_app_transitive_includes.py
python3 .github/scripts/check_layer_dependencies.py
python3 .github/scripts/check_module_ids.py
ctest --test-dir build                                  # the new test passes
```

## Common traps

- **The apps list and `main.c` must agree.** `check_cmake_apps.py` compares the
  `edge_add_product(... apps ...)` list against `<app>_construct(` calls and fails
  on either kind of mismatch — including an app directory that no product uses.
- **One `#include "flash/flash.h"` breaks isolation.** The include path of an app
  target only carries `app/<name>/include` and `edge_module/include`; the guard
  catches it, and so does the compiler.
- **The app must not know `sys`.** It receives events and returns status codes; it
  never calls `edge_sys_*` and never reaches into the scheduler.
- **`period`/`budget` are the app's scheduling contract**, not an implementation
  detail: `period = 0` means event-driven only.
