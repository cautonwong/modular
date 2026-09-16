# TODO implementation status

This file tracks the uploaded architecture TODO against the current repository.

| Item | Status | Implementation |
|---|---|---|
| S1 | done | D1-D14 are treated as frozen architecture decisions. |
| S2 | done | `docs/architecture.md` and the layer rules are reflected in the source layout. |
| S3 | done | `app/dlt645` defines its consumer port; `infra/flash` provides the concrete API; `product/example` contains the adapter and explicit construction; `edge_sys` runs the foreground loop. |
| S4 | partial | `edge/board_irq.h` and `board/example/irq.c` define IRQ-to-event forwarding; `docs/event_ids.csv` defines ownership ranges. Queue overflow remains an explicit `EDGE_EOVERFLOW` condition. A real MCU IRQ/HIL test is hardware-specific and is not fabricated here. |
| S5 | done | `edge_module`, `app`, `sys`, `board`, `infra`, and `product` monorepo areas now exist, with a buildable example product. |
| S6 | done | `.github/scripts/check_app_isolation.py` rejects app includes of concrete `infra/`, `product/`, `board/`, or `sys/` headers. |
| S7 | done | The CI isolation job executes the negative-boundary checker on every push/PR. |
| S8 | partial | `edge_sys` implements priority ordering with `module_id` ascending tie-break and mandatory-module validation. A generated CMake/main consistency checker is still product-specific and intentionally not inferred. |

## CI quality gates

The CI matrix covers GCC and Clang, Debug and Release, and ASan/UBSan configurations. Tests use CMocka. A separate static-analysis job runs clang-tidy and cppcheck, and the application dependency boundary is checked independently.

## Explicit architectural rules

- Product composition is explicit in `product/<name>/main.c`.
- App interfaces are owned by the consuming app.
- Concrete infrastructure is kept out of app include paths.
- The sys module schedules; it does not construct concrete infrastructure.
- Board code owns hardware IRQ entry points and hardware-level actions; application policy stays above it.
- Foreground callbacks must return; they must not become private infinite loops.
