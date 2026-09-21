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
4. **Vector table and more than one IRQ** — the board owns the vector table (one
   per board; `soc/<soc>/` supplies reusable startup/link templates). Each IRQ
   *line* gets one vector entry that calls the board's dispatcher
   (`board_<board>_irq_dispatch(irq)`), not one function per handler, so a second
   consumer of the same line needs no new vector (D84):
   ```c
   edge_status_t board_<board>_irq_attach(uint32_t irq, board_<board>_irq_fn cb, void *ctx);
   void board_<board>_irq_dispatch(uint32_t irq);   /* registration order */
   ```
   Handlers run in **registration order**, the table is fixed and static (D21),
   and the walk is bounded by its capacity (D75). The board's own consumers
   register through the same entry point, so the built-in path exercises the
   dispatcher rather than reaching around it. `board/mps2` is the reference.
   Interrupt-priority policy is split: the SoC package owns the encoding and the
   legal range, the board owns the per-board assignment.
   *Transitional:* until #125 moves startup out of `.github/`, the CMake firmware
   helper wires a single timer vector through `EDGE_BOARD_TIMER_ISR`; the
   per-board `vectors.c` from §24 is the target shape.

5. **CMake** — create `board/<board>/CMakeLists.txt` (D88); the top-level
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

## Proving the contract

An interrupt entry point is the one place that cannot be debugged after the fact,
so its behaviour is a reusable suite (D75/D84/D57):

```c
const edge_board_contract_t contract = {
    .name = "board/my_board uart0_rx",
    .init = board_my_board_init,
    .irq = board_my_board_irq_uart0_rx,
    .expected_event_id = EDGE_EVT_UART0_RX,
};
edge_contract_board_run(&contract);
```

It checks that one interrupt publishes **exactly one** event carrying the IRQ
argument, and that a burst is bounded by the queue while anything that does not
fit is counted as dropped instead of being lost silently. It does not assume the
queue's capacity. `board/example` is covered in `tests/test_contract.c`;
`tests/contract_violations/board.c` proves the suite rejects an IRQ that
publishes nothing. A real board is covered by Renode/HIL (D62/D63).

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
