# Error model (D19 / D68)

Every layer returns `edge_status_t` (`edge/errors.h`): `0` = success, negative =
errno-style failure. Error codes are allocated centrally, like event and module
IDs, so a module can never silently collide with the framework or another module.

## Allocation rule

```c
#define EDGE_ERR(mod, code) \
    (-(int32_t)(((uint32_t)(mod) & 0xFF00u) | ((uint32_t)(code) & 0xFFu)))
```

- **Framework range `-1 .. -99`** is reserved for the `edge_status_t` enum.
- **Module range** is `EDGE_ERR(segment, code)` where `segment` is the high byte
  of a central module ID (`edge/modules.h`, `0xNN00`) and `code` is `1..255`.
  The result is always `<= -0x0100`, strictly outside the framework range.
- A zero segment (`0x0000`) is a bug: it resolves into the framework range.

## What each layer may return

| Layer | Allowed to return | Must not return |
|---|---|---|
| `edge_module` | `EDGE_OK`, `EDGE_EINVAL`, `EDGE_ENOENT`, `EDGE_EBUSY`, `EDGE_ESTATE`, `EDGE_EDEPEND`, `EDGE_EOVERFLOW`, `EDGE_ENOSPC`, `EDGE_EIO`, `EDGE_ENOTSUP` | module-specific codes |
| `sys` | the framework set above; may aggregate a module's error | a fabricated module code |
| `board` | the framework set above | business/app codes |
| `infra` | the framework set above plus its own `EDGE_ERR(segment, code)` | app business codes |
| `app` | the framework set above plus its own `EDGE_ERR(segment, code)` | another app's codes |
| `product` (glue) | the framework set; may translate between port and infra errors | business state |

A layer must not swallow an error silently: translate it to one of its own
allowed codes, or propagate it unchanged.

## Enforcement

- `edge/errors.h` carries `_Static_assert` guards for the framework range and
  `EDGE_ERR` composition.
- `check_error_ids.py` (CI + guard self-test) rejects duplicate framework values,
  values outside `-99 .. 0`, zero-segment `EDGE_ERR` allocations, and collisions
  between module error allocations and the framework or each other.
- `tests/guards/errors_{good,bad,bad_segment}` are the positive/negative fixtures.

## Adding a module error

1. Allocate the module ID in `edge/modules.h` (e.g. `EDGE_MOD_DLT645 0x1000u`).
2. Define the code next to the module: `#define DLT645_E_BAD_CRC EDGE_ERR(EDGE_MOD_DLT645, 1u)`.
3. Run `python3 .github/scripts/check_error_ids.py`; CI enforces it.
