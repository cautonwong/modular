# ADR-001: Module ID Ownership and Decentralized Declarations

## Status
Accepted

## Context
In early framework versions, `edge_module/include/edge/modules.h` maintained a monolithic central X-macro table of all system module IDs. As applications (DLT645, Modbus, ZMK keyboard apps, InfiniTime watch apps, BLE services) scaled, every new module forced edits to the core framework header, creating cross-layer coupling and PR merge contention.

## Decision
1. **Decentralized Ownership**: Each application or module declares its own stable module ID within its module domain/namespace header (e.g. `app/<module>/include/<module>/module.h`).
2. **Segment Alignment**: Module IDs preserve the `0xNN00` high-byte segment rule, aligning with `EDGE_ERR(mod, code)` error domain mapping.
3. **CI Collision Verification**: Centralized verification is maintained via CI static scanner (`check_module_ids.py`), ensuring uniqueness, valid segment ranges, and preventing namespace collisions.
4. **Stability Guarantee**: Module IDs are persistent identity tokens used in error reporting, logging, and diagnostics; they never renumber based on file ordering.

## Consequences
- The core framework header `edge/module.h` remains generic and decoupled from domain-specific modules.
- New modules can be added in their own directories without touching framework files.
