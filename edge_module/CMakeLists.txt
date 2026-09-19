# The foundation layer: events, module contract, ports. Nothing depends on
# nothing here -- everything else depends on this.
edge_add_foundation(edge_module SOURCES src/event.c)
