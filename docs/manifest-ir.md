# Manifest -> IR

The intended source of truth is a product manifest, not hand-maintained C metadata.

```yaml
modules:
  - id: 0x1001
    instances: [meter0, meter1]
    dependencies: [bus]
    resources: [RS485_0]
    events: [RX, TIMER]
    services:
      requires:
        - id: 0x2001
          major: 1
          minor: 2
```

The compiler normalizes this into ModuleIR, validates dependency/resource/service compatibility, then emits static C descriptors or loadable-image metadata.
