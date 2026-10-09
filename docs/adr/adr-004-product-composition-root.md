# ADR-004: Product Composition Root and Module Encapsulation

## Status
Accepted

## Context
In early designs, `product/<product>/main.c` directly manipulated raw internal runtime array pointers (`edge_module_t *apps[]`). This exposed scheduler internals to the composition root and prevented automated lifecycle management.

## Decision
1. **Composition Root Role**: `product/<product>` is the sole composition root responsible for:
   - Module instantiation (caller-owned static allocation).
   - Port adapter binding & dependency injection.
   - Platform initialization and clean shutdown.
2. **Encapsulated Product Descriptor**: Introduce `edge_product_t` (`edge/product.h`) to declare the product composition as a high-level collection of `edge_product_module_t` descriptors.
3. **No Business Logic**: Product composition roots contain zero business logic or state machine evaluation.

## Consequences
- Clean separation between assembly topology and runtime scheduling mechanics.
