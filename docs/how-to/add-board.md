# Add a board

A board owns the hardware facts of one board: interrupt vectors, pin/clock
configuration, the SoC it selects, and pure hardware actions. It never knows an
app, a product family, or business state (D3/D9/D49).

Most boards need an SoC support package first; see
[`add-driver.md`](add-driver.md) for where register-level code belongs.

## Steps

1. **SoC support package** (if the SoC is new) — `soc/<soc>/include/soc_<soc>/`
   with the memory map, peripheral base addresses, IRQ numbers and SoC-level
   init, plus `soc/<soc>/CMakeLists.txt` declaring the header package (D88):
   ```cmake
   edge_add_soc(<soc> HEADERS DEPS edge_module)
   ```
   A board links `soc_<soc>`; `check_layer_dependencies.py` allows `soc -> soc` only.
2. **Board header** — `board/<board>/include/<board>/board.h`:
   `void board_<board>_init(edge_event_sink_t *sink);` plus one entry point per
   IRQ the board forwards.
3. **Board implementation** — `board/<board>/src/board.c`. It may `#include` its
   SoC header. Each ISR may only: clear the flag, capture the minimum, and push
   an event to the injected sink (D75). No loops over data, no allocation, no
   logging, no call to an app.
4. **CMake** — create `board/<board>/CMakeLists.txt` (D88); the top-level
   `CMakeLists.txt` is not edited:
   ```cmake
   edge_add_board(<board> SOURCES src/board.c DEPS edge_module soc_<soc>)
   ```
   Listing `soc_<soc>` in `DEPS` is what puts the SoC headers on the include path.
   If the board's configuration is read by the FreeRTOS kernel (D87), keep the SoC
   include root on the board's own PUBLIC interface as well — see
   [`board/mps2/CMakeLists.txt`](../../board/mps2/CMakeLists.txt) for the one case
   that does this, and why.
5. **Legal combinations** — add every `family:board` pair this board may be
   paired with to `EDGE_LEGAL_FAMILY_BOARD`. A known-but-unlisted pair fails at
   configure time, which is the point.
6. **Bind a product** — `edge_add_product(<product> family <f> board <board> ...)`.
   D86: a product is bound to exactly one board, the binding is recorded, and it
   cannot be overridden by a build parameter.
7. **Firmware target** (for real hardware) — `edge_add_arm_firmware`,
   `edge_add_riscv_firmware` or `edge_add_arm_freertos_firmware`, with the linker
   layout and startup for that SoC.

## Completion criterion

```bash
python3 .github/scripts/check_area_registration.py       # the directory declares itself (D88)
python3 .github/scripts/check_layer_dependencies.py      # board -> board+soc+pal+edge_module
python3 .github/scripts/check_product_board_binding.py   # binding recorded, no re-bind
python3 .github/scripts/check_cmake_apps.py
cmake --build build --target <firmware>                   # links, with a size report
```

Plus: the size and stack budgets still pass for the firmware that now includes
the board.

## Common traps

- **Device access in the board.** UART/ADC/relay work is `infra`, not `board`
  (D3). If you find yourself writing a register driver here to make an app work,
  the driver belongs in `infra/<device>` (portable core) or `soc/<soc>`
  (register-level), wired through product glue.
- **An ISR that does real work.** The bounded-ISR rule (D75) is what keeps the
  runner deterministic; a slow ISR shows up as lost events, not as a compile
  error.
- **Exposing module state or an app list from the board.** That is a boundary
  violation regardless of how convenient it is.
- **Forgetting the whitelist entry.** The failure is a configure error naming the
  illegal pair, not a silent fallback.
