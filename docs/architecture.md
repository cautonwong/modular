# Architecture

One foreground loop owns lifecycle and bounded `poll()` calls. Ten or more modules are scheduler clients, not tasks.

```text
hardware ISR -> driver acknowledge/capture -> bounded event queue -> foreground dispatch -> module poll()
```

Modules use logical resources; drivers own registers, DMA and interrupt acknowledgement. Shared buses should use a state-machine arbiter rather than a mutex in this execution model.

Cross-module calls use versioned services/contracts. Dependencies are explicit and should be resolved by a manifest/IR compiler into a deterministic topological startup order; linker object order is not a semantic dependency mechanism.

Initialization and power-on roll back already-started modules in reverse order. Production integration should add fault policy, retry budget, watchdog policy and per-module timing budgets.
