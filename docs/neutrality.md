# Neutrality acceptance (T7a / T7b / T7c)

"One app, many boards, many runners" is only credible if it is executable.
These gates make it so (ADR D57, section 17.21).

## T7a — transitive include closure

`check_app_transitive_includes.py` resolves the quoted include closure of every
app through `app/<name>/include` + `edge_module/include` and rejects any header
that reaches a concrete layer (`board/infra/sys/soc/pal/product`), another app,
an RTOS header or a SoC vendor header. It complements the direct-include guard by
covering transitive leakage. Fixtures: `tests/guards/transitive_{good,bad}`.

## T7b — parameterised minimal product

`tests/integration/minimal_product/` is one product built against different
`BOARD`/`RUNNER`/`APP` combinations via `edge_add_minimal_variant`:

| board | runner | target |
|---|---|---|
| example (host) | baremetal (`main` super-loop) | `minimal_host_baremetal` |
| example (host) | thread (runner step in one thread) | `minimal_host_thread` |
| mps2 (QEMU M4) | baremetal | `meter_mps2_fw` |
| mps2 (QEMU M4) | FreeRTOS task | `meter_mps2_freertos_fw` |

That is 2 boards × 2 runner models. The CI `neutrality` job also asserts
`git diff --exit-code -- app/` after building the matrix, so the app is never
modified for a variant.

## T7c — host task model

`tests/test_runner_threads.c` runs the runner step in one thread while another
thread pushes events through the same SPSC sink; it asserts every event is
handled with zero drops, validating the runner decomposition and the lock-free
queue without an RTOS.

## `soc/` layer

`soc/mps2` is a real SoC package (memory map + IRQ numbers) selected by
`board/mps2`, so a board no longer hard-codes SoC addresses (D48/D49).

## Deferred

A *second* `infra/<device>/<impl>` implementation (multiple driver models for one
device) is new-instance work and is tracked under the instance-expansion epic
#33, gated on this acceptance.
