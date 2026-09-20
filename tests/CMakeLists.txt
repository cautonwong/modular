# Host unit tests. Each test names the modules it exercises, so adding an app or
# a driver never edits a shared list again (D88). The framework is cmocka (D59).
find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

# The contract suites (D57) are shared by several tests, so they are one small
# library rather than a copy per test. They assert through cmocka, so an adopter
# keeps its own test binary and its own fixtures.
add_library(edge_test_contract STATIC
    contract/app_contract.c
    contract/port_contract.c
    contract/board_contract.c
)
target_include_directories(edge_test_contract PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${EDGE_ROOT}/edge_module/include
)
target_link_libraries(edge_test_contract PUBLIC edge_module PkgConfig::CMOCKA)
edge_enable_quality(edge_test_contract)

edge_add_host_test(test_contract SOURCES test_contract.c
    ../product/example/glue.c
    ../product/meter_host/glue.c
    ../product/meter_mps2/glue.c
    ../product/meter_gateway_host/glue.c
    DEPS edge_test_contract app_dlt645 app_meter_core app_modbus_slave app_relay
         board_example infra_flash infra_gpio infra_uart)
target_include_directories(test_contract PRIVATE ${EDGE_ROOT}/product/meter_gateway_host)

# Each contract suite is also run against a broken implementation, and CTest is
# told to expect that failure. If a suite ever stops rejecting its violation the
# binary passes and CTest reports an error, so the proof cannot rot silently.
function(edge_add_contract_violation_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE edge_test_contract)
    edge_enable_quality(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "host" WILL_FAIL TRUE)
endfunction()

edge_add_contract_violation_test(test_contract_violation_app contract_violations/app.c)
edge_add_contract_violation_test(test_contract_violation_port contract_violations/port.c)
edge_add_contract_violation_test(test_contract_violation_board contract_violations/board.c)

edge_add_host_test(test_event SOURCES test_event.c DEPS edge_module)
edge_add_host_test(test_sys SOURCES test_sys.c DEPS edge_module sys_example sys_meter)
edge_add_host_test(test_architecture SOURCES test_architecture.c DEPS edge_module)
edge_add_host_test(test_app_dlt645 SOURCES test_app_dlt645.c DEPS app_dlt645 edge_test_contract)
edge_add_host_test(test_app_relay SOURCES test_app_relay.c DEPS app_relay edge_test_contract)
edge_add_host_test(test_infra_gpio SOURCES test_infra_gpio.c DEPS infra_gpio)
edge_add_host_test(test_modbus_slave SOURCES test_modbus_slave.c DEPS app_modbus_slave edge_test_contract)
edge_add_host_test(test_meter_core SOURCES test_meter_core.c DEPS app_meter_core edge_test_contract)
edge_add_host_test(test_app_interaction SOURCES test_app_interaction.c
    DEPS app_meter_core app_modbus_slave)
edge_add_host_test(test_ports SOURCES test_ports.c DEPS infra_gpio infra_uart)
edge_add_host_test(test_pal_host SOURCES test_pal_host.c DEPS pal_host)
edge_add_host_test(test_pal_os SOURCES test_pal_os.c DEPS sys_example pal_host pal_os)
edge_add_host_test(test_pal_cortex_m SOURCES test_pal_cortex_m.c DEPS pal_cortex_m_bare)
edge_add_host_test(test_low_power SOURCES test_low_power.c
    DEPS sys_example board_example pal_host pal_os)
edge_add_host_test(test_tick64 SOURCES test_tick64.c DEPS pal_os)
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
