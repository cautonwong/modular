# Add an infrastructure driver

`infra/<device>` holds the **portable core** of a device: protocol, buffering and
policy, depending only on narrow ports. It is not where register-level code
lives, and it must never know a SoC (D48).

```
SoC / vendor bound (registers, HAL)   -> soc/<soc>/
OS / RTOS bound (e.g. Zephyr model)   -> pal/<os>/
protocol / policy / buffering         -> infra/<device>/     (this checklist)
binding + adaptation                  -> product/<name>/glue.c
```

## Steps

1. **Public header** — `infra/<device>/include/<device>/<device>.h`, the
   provider's own concrete API (what `flash_read`/`flash_write` are for
   `infra/flash`). This is not the consumer interface; the consumer defines that
   (D14).
2. **Implementation** — `infra/<device>/src/<device>.c`. It may `#include`
   `edge/*` and `pal/*`. It may **not** `#include` `soc/*`, another layer, or a
   register address.
3. **Register-level part** (if the device needs one) — put it in `soc/<soc>/`
   or, when it is OS-bound, in `pal/<os>/`. The product glue binds it to the
   narrow port the portable core consumes.
4. **CMake** — create `infra/<device>/CMakeLists.txt` (D88). Nothing central
   lists the driver, and the top-level `CMakeLists.txt` is not edited:
   ```cmake
   edge_add_infra(<device> SOURCES src/<device>.c DEPS edge_module)
   ```
   Add `pal_<os>` to `DEPS` only if it genuinely needs a PAL primitive; that is
   what puts the PAL headers on the include path, so a hardcoded include list is
   never needed. The module shape lives in
   [`cmake/EdgeTargets.cmake`](../../cmake/EdgeTargets.cmake), and
   `check_area_registration.py` requires the helper argument to equal the
   directory name.
5. **Adapter** — in `product/<product>/glue.c`, adapt the concrete API to the
   consumer-defined port of the app that needs it. See
   [`add-product.md`](add-product.md).
6. **Host fake and test** — a deterministic fake in the driver's own test (or in
   `tests/`), registered with `edge_add_host_test(... SOURCES ... DEPS infra_<device>)`
   in `tests/CMakeLists.txt`.

## Completion criterion

```bash
python3 .github/scripts/check_area_registration.py       # the directory declares itself (D88)
python3 .github/scripts/check_layer_dependencies.py      # infra -> infra+pal+edge_module
python3 .github/scripts/check_no_dynamic_memory.py <firmware>   # if it lands in firmware
python3 .github/scripts/check_stack_usage.py <build-dir> --max-bytes 512 --exclude-substr /product/
python3 .github/scripts/check_map_budget.py <map-file>
```

## Proving the port contract

A port's shape says nothing about what a zero-length write means or whether a
NULL buffer is rejected or dereferenced, so the behaviour is a reusable suite
(D57). Run it against the adapter you wrote, wherever it lives (glue, driver or
fake):

```c
const edge_storage_contract_t contract = {
    .name = "my_product/storage",
    .read = port.read, .write = port.write, .self = port.self,
};
edge_contract_storage_run(&contract);
```

`edge_contract_storage_run` checks: a value written reads back equal, a
zero-length read/write is a no-op rather than an error, and a NULL buffer is
rejected with `EDGE_EINVAL`. `edge_contract_byte_writer_run` does the same for
the `write(buf, len)` shape (transport, UART). A new shape gets a new
`edge_contract_*_run` in `tests/contract/`.

Every product's storage trampoline and the gateway transport are run against
these suites in `tests/test_contract.c`, and `tests/contract_violations/port.c`
proves the suite rejects a port that accepts a NULL buffer.

## Common traps

- **`infra -> soc` is denied with no exception.** There is no whitelist any more
  (D48): the guard fails, and that is intended. If a driver seems to need SoC
  knowledge, split it — portable core in `infra`, register access in `soc`, bound
  at the composition root.
- **Allocating at runtime.** Buffers are caller-owned and zero allocation is a
  hard rule (D21); the `nm` gate rejects `malloc`/`calloc`/`realloc`/`free`.
- **Inventing a consumer interface here.** If the interface only exists because
  this driver wanted it, it is the wrong interface; the consumer defines the
  shape.
- **Assuming one instance.** Address instances through the port handle or the
  event `source`, not through a file-scope singleton.
