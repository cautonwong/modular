# Stack usage gate (D75)

Bare-metal has no MMU/MPU: the most damaging memory failure is a call stack that
overruns into globals. The map budget covers `.text/.data/.bss` but not stack, so
this gate makes stack usage measurable and bounded.

## Static: `-fstack-usage`

Every ARM and RISC-V build is compiled with `-fstack-usage`, so GCC emits one
`.su` file per object. `check_stack_usage.py <build-dir>` parses them and:

- prints the worst per-function frames (top 10);
- fails if any **non-product** function exceeds `--max-bytes 512`;
- writes a JSON report.

Composition-root `main()` functions are excluded (`--exclude-substr /product/`):
by D21 they own the caller-provided storage (queues, buffers), so their frame is
intentionally large. The framework/app/sys/board/infra functions stay small
(currently <= 128 bytes).

This runs on all four firmware jobs (M0, M4, RISC-V, FreeRTOS) and the report is
uploaded as an artifact. The FreeRTOS job also excludes the third-party kernel
(`/_deps/`).

## Why not a pure static worst-case call graph?

A trustworthy worst-case stack depth needs a call graph, but this architecture
dispatches through **function pointers** everywhere (`edge_module_t` callbacks,
`edge_sys_idle_fn`, port vtables). A static call graph cannot resolve those edges
soundly, so it would *under-estimate* the depth. Hence the gate is a **sound
per-function bound**, and the dynamic checks below cover the executed paths.

## Dynamic: task high-water and overrun detection

- `pal/rtos/freertos` enables `configCHECK_FOR_STACK_OVERFLOW 2`; the overflow
  hook halts, turning silent corruption into a CI failure.
- `edge_rtos_task_stack_high_water()` (neutral API) exposes
  `uxTaskGetStackHighWaterMark`; the FreeRTOS product fails the smoke if the
  capsule task has less than 64 bytes left.
