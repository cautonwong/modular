# Application module. It defines itself and never depends on another app or on a
# concrete infra driver (D14); the product supplies the ports.
edge_add_app(relay SOURCES src/relay.c DEPS edge_module)
