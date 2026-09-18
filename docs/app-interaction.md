# App-to-app interaction (D78)

Apps never include each other. When one app needs data from another:

1. **Consumer** defines the interface it needs (D14: interfaces follow the consumer).
2. **Provider** exposes a concrete API — it is *not* converted into events.
3. **Composition root** (product glue, or a test) adapts provider to consumer.
4. `main()` explicitly wires the two.

## Concrete example

`app/modbus_slave` (consumer) defines `modbus_store_if`; `app/meter_core`
(provider) exposes `meter_core_read_register` / `..._write_register` /
`..._read_coil` / `..._write_coil`; `product/meter_gateway_host/glue.c` is the
composition root that adapts one to the other:

```c
static edge_status_t mb_read_holding(void *self, uint16_t addr, uint16_t *value) {
    return meter_core_read_register((const meter_core_t *)self, addr, value);
}
```

Neither app includes the other. This is enforced by:

- `check_app_isolation.py` (no direct cross-app include),
- `check_app_transitive_includes.py` (no transitive cross-app reach),
- `check_layer_dependencies.py` (`app -> app/<self> + edge_module`).

## Events vs. services

Use the event bus for **facts that have already happened** (e.g. "a frame was
received"); use a consumer-defined interface for **commands and data pulls**
(e.g. "read holding register 3"). Do not turn a data service into an event (D16).

## Files

| Role | Location |
|---|---|
| Provider (concrete API) | `app/meter_core/` |
| Consumer (defines the interface) | `app/modbus_slave/` (`modbus_store_if`) |
| Adapter (composition root) | `product/meter_gateway_host/glue.c` |
| Interaction test | `tests/test_app_interaction.c` |
