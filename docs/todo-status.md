# TODO implementation status

This file tracks the frozen architecture TODO against the current repository.

| Area | Status | Implementation |
|---|---|---|
| Thin framework | done | `edge_module` owns the module lifecycle contract and bounded event primitives. |
| Explicit composition | done | `product/example/main.c` constructs the app, binds ports, subscribes events, then starts `sys`. |
| Consumer-defined port | done | `app/dlt645` owns `dlt645_storage_if_t`; `product/example/glue.c` adapts `infra/flash`. |
| Scheduler | done | `sys/example` sorts by priority then `module_id`, validates required IDs, starts with rollback, polls cooperatively, and shuts down in reverse order. |
| Event routing | done | Board pushes into an injected sink; `sys_subscribe()` explicitly routes event IDs to app callbacks. |
| ISR safety boundary | done | Event payload is scalar-only; timestamp is injected; multi-IRQ sinks can inject `edge_irq_guard_t`. |
| Dependency isolation | done | CI checks app includes and forbidden RTOS/framework headers. |
| Event ID governance | done | Central `edge_module/include/edge/events.h`, `_Static_assert` guards, and CI collision checker. |
| Tests | done | CMocka covers queue timestamp/overflow and scheduler ordering/routing/rollback. |
| CI/CD | partial | GCC/Clang matrix, sanitizers, CMocka, clang-tidy, cppcheck, coverage, architecture checks and ELF size gate are implemented. IAR/iccarm and full family×board×app matrix remain. |
| Hardware validation | pending | Renode/HIL and real MCU IRQ validation remain target-specific. |
| Scheduler budgets | pending | Periodic division, execution budgets, idle/low-power and richer fault policy remain. |
| Distribution | pending | ABI/contract compatibility matrix, SDK packaging and compliance are separate follow-up work. |

## Important cutover

The obsolete descriptor/preamble manager, linker-section registration path, generic service/resource registry and old flat app/board APIs are no longer part of the active implementation. The repository follows the frozen explicit foreground architecture directly; there is no V2/V3 compatibility layer.
