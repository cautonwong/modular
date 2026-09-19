# Application module. It defines itself and never depends on another app or on a
# concrete infra driver (D14); the product supplies the ports.
edge_add_app(dlt645 SOURCES src/dlt645.c DEPS edge_module)
