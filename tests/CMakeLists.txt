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
    contract/rtos_contract.c
)
target_include_directories(edge_test_contract PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${EDGE_ROOT}/edge_module/include
)
target_link_libraries(edge_test_contract PUBLIC edge_module pal_os pal_rtos PkgConfig::CMOCKA)
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
edge_add_contract_violation_test(test_contract_violation_rtos contract_violations/rtos.c)

edge_add_host_test(test_event SOURCES test_event.c DEPS edge_module)
edge_add_host_test(test_sys SOURCES test_sys.c DEPS edge_module sys_example sys_meter sys_bldc)
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
edge_add_host_test(test_eos SOURCES test_eos.c DEPS pal_eos pal_rtos)
edge_add_host_test(test_board_irq_attach SOURCES test_board_irq_attach.c
    DEPS board_mps2 edge_module)
edge_add_host_test(test_payload_token SOURCES test_payload_token.c DEPS edge_module)
edge_add_host_test(test_rtos_contract SOURCES test_rtos_contract.c
    DEPS edge_test_contract pal_eos)
edge_add_host_test(test_riscv_deadline SOURCES test_riscv_deadline.c DEPS edge_module)
# The RISC-V board target only exists for the RISC-V toolchain, so the header is
# reached directly: the pure deadline arithmetic is host-testable even though the
# rest of the board is not.
target_include_directories(test_riscv_deadline PRIVATE ${EDGE_ROOT}/board/riscv_virt/include)
edge_add_host_test(test_runner_threads SOURCES test_runner_threads.c DEPS sys_example)
edge_add_host_test(test_flash_glue SOURCES test_flash_glue.c ../product/example/glue.c
    DEPS app_dlt645 infra_flash)

edge_add_host_test(test_app_pulse_meter SOURCES test_app_pulse_meter.c DEPS app_pulse_meter edge_test_contract)
edge_add_host_test(test_foc_core SOURCES test_foc_core.c DEPS app_foc_core)
edge_add_host_test(test_app_vesc_comm SOURCES test_app_vesc_comm.c DEPS app_vesc_comm)
edge_add_host_test(test_motor_config SOURCES test_motor_config.c DEPS app_motor_config)
edge_add_host_test(test_app_timeout_guard SOURCES test_app_timeout_guard.c DEPS app_timeout_guard)
edge_add_host_test(test_app_throttle SOURCES test_app_throttle.c DEPS app_throttle)
edge_add_host_test(test_infra_drv83xx SOURCES test_infra_drv83xx.c DEPS infra_drv83xx)
edge_add_host_test(test_infra_encoder SOURCES test_infra_encoder.c DEPS infra_encoder)
edge_add_host_test(test_infra_imu SOURCES test_infra_imu.c DEPS infra_imu)
edge_add_host_test(test_board_vesc6 SOURCES test_board_vesc6.c DEPS board_vesc6 soc_stm32f4)
edge_add_host_test(test_pal_power SOURCES test_pal_power.c DEPS pal_host pal_os)
edge_add_host_test(test_app_motor_id SOURCES test_app_motor_id.c DEPS app_motor_id)
edge_add_host_test(test_app_ppm SOURCES test_app_ppm.c DEPS app_ppm)
edge_add_host_test(test_app_adc_input SOURCES test_app_adc_input.c DEPS app_adc_input)
edge_add_host_test(test_app_vesc_can SOURCES test_app_vesc_can.c DEPS app_vesc_can app_vesc_comm)
edge_add_host_test(test_board_vesc4 SOURCES test_board_vesc4.c DEPS board_vesc4 soc_stm32f4)
edge_add_host_test(test_board_vesc_unity SOURCES test_board_vesc_unity.c DEPS board_vesc_unity soc_stm32f4)
edge_add_host_test(test_infra_digital_filter SOURCES test_infra_digital_filter.c DEPS infra_digital_filter)
edge_add_host_test(test_app_nunchuk SOURCES test_app_nunchuk.c DEPS app_nunchuk)
edge_add_host_test(test_app_pas SOURCES test_app_pas.c DEPS app_pas)
edge_add_host_test(test_app_balance SOURCES test_app_balance.c DEPS app_balance)
edge_add_host_test(test_app_vesc_terminal SOURCES test_app_vesc_terminal.c DEPS app_vesc_terminal)
edge_add_host_test(test_infra_vesc_buffer SOURCES test_infra_vesc_buffer.c DEPS infra_vesc_buffer)
edge_add_host_test(test_app_vesc_bms SOURCES test_app_vesc_bms.c DEPS app_vesc_bms)

# Every product's glue is in the coverage scope, so every trampoline is exercised
# here instead of living only inside a product executable.
edge_add_host_test(test_product_glue SOURCES test_product_glue.c
    ../product/meter_host/glue.c
    ../product/meter_mps2/glue.c
    ../product/meter_gateway_host/glue.c
    ../product/water_meter_host/glue.c
    ../product/vesc_host/glue.c
    DEPS app_dlt645 app_meter_core app_modbus_slave app_pulse_meter app_relay app_foc_core app_vesc_comm app_motor_config app_ppm app_adc_input app_vesc_can app_motor_id app_nunchuk app_pas app_balance app_vesc_terminal app_vesc_bms infra_flash infra_gpio infra_uart infra_vesc_buffer)
target_include_directories(test_product_glue PRIVATE ${EDGE_ROOT}/product/meter_gateway_host ${EDGE_ROOT}/product/water_meter_host ${EDGE_ROOT}/product/vesc_host)
