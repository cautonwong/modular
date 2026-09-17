# Architecture

This repository implements the foreground/background architecture defined by
[`docs/adr.md`](adr.md) (the single decision source); [`adr-conformance.md`](adr-conformance.md)
tracks decision-vs-implementation drift.

```text
                    product/<name>
                  Composition Root
                 /       |       \
                /        |        \
        sys/<family> board/<board> infra/<name>
                \        |        /
                 \       |       /
                    app/<name>
                         |
                    edge_module
```

## Runtime

```text
hardware IRQ
    -> board ISR
    -> injected event sink
    -> bounded event queue
    -> sys event router
    -> explicit app subscription
    -> app.on_event()
    -> app.poll()
```

There are no RTOS tasks, worker threads, runtime allocation, linker-section auto-registration, or service locator in the core model.

## Boundaries

- `edge_module`: lifecycle contract and bounded event primitives.
- `sys`: product-family scheduling policy, deterministic priority ordering, lifecycle orchestration and event routing.
- `board`: IRQ ownership, minimal capture and safe board actions; never calls app callbacks.
- `app`: business state machine and consumer-defined ports; no concrete infrastructure dependency.
- `infra`: concrete driver/storage/device implementations.
- `product`: explicit construction and adapter wiring; no business logic.

## Scheduling

`sys` sorts by `priority`, then by ascending `module_id`. `required` is a separate validation set. `poll()` is a bounded cooperative step. Startup rolls back already initialized modules on failure; shutdown is reverse order.

## Events

The event payload is fixed scalar data:

```c
struct edge_event {
    uint32_t id;
    uint32_t source;
    uint32_t arg0;
    uint32_t arg1;
    uint64_t timestamp;
};
```

A single producer can push directly. If multiple IRQ sources share one sink, the board injects an `edge_irq_guard_t` implemented by the target platform to serialize producers without putting architecture-specific code in the framework.

Cross-module commands use explicit consumer-defined interfaces and direct calls. Events are reserved for facts that have already happened.
