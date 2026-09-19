# Host unit tests. Each test names the modules it exercises, so adding an app or
# a driver never edits a shared list again (D88). The framework is cmocka (D59).
find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

edge_add_host_test(test_event SOURCES test_event.c DEPS edge_module)
edge_add_host_test(test_sys SOURCES test_sys.c DEPS edge_module sys_example sys_meter)
edge_add_host_test(test_architecture SOURCES test_architecture.c DEPS edge_module)
edge_add_host_test(test_app_dlt645 SOURCES test_app_dlt645.c DEPS app_dlt645)
edge_add_host_test(test_app_relay SOURCES test_app_relay.c DEPS app_relay)
edge_add_host_test(test_infra_gpio SOURCES test_infra_gpio.c DEPS infra_gpio)
edge_add_host_test(test_modbus_slave SOURCES test_modbus_slave.c DEPS app_modbus_slave)
edge_add_host_test(test_meter_core SOURCES test_meter_core.c DEPS app_meter_core)
edge_add_host_test(test_app_interaction SOURCES test_app_interaction.c
    DEPS app_meter_core app_modbus_slave)
edge_add_host_test(test_ports SOURCES test_ports.c DEPS infra_gpio infra_uart)
edge_add_host_test(test_pal_host SOURCES test_pal_host.c DEPS pal_host)
edge_add_host_test(test_pal_os SOURCES test_pal_os.c DEPS sys_example pal_host pal_os)
edge_add_host_test(test_runner_threads SOURCES test_runner_threads.c DEPS sys_example)
edge_add_host_test(test_flash_glue SOURCES test_flash_glue.c ../product/example/glue.c
    DEPS app_dlt645 infra_flash)

# Every product's glue is in the coverage scope, so every trampoline is exercised
# here instead of living only inside a product executable.
edge_add_host_test(test_product_glue SOURCES test_product_glue.c
    ../product/meter_host/glue.c
    ../product/meter_mps2/glue.c
    ../product/meter_gateway_host/glue.c
    DEPS app_dlt645 app_meter_core app_modbus_slave app_relay infra_flash infra_gpio infra_uart)
target_include_directories(test_product_glue PRIVATE ${EDGE_ROOT}/product/meter_gateway_host)
