# Application module. It defines itself and never depends on another app or on a
# concrete infra driver (D14); the product supplies the ports.
edge_add_app(modbus_slave SOURCES src/modbus_slave.c DEPS edge_module)
