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
edge_add_host_test(test_sys SOURCES test_sys.c DEPS edge_module sys_example sys_meter sys_watch sys_keyboard)
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

edge_add_host_test(test_infra_battery_adc SOURCES test_infra_battery_adc.c DEPS infra_battery_adc)
edge_add_host_test(test_infra_cst816s SOURCES test_infra_cst816s.c DEPS infra_cst816s)
edge_add_host_test(test_app_touch_gesture SOURCES test_app_touch_gesture.c DEPS app_touch_gesture)
edge_add_host_test(test_app_button_handler SOURCES test_app_button_handler.c DEPS app_button_handler)
edge_add_host_test(test_app_watch_power SOURCES test_app_watch_power.c DEPS app_watch_power)
edge_add_host_test(test_infra_hrs3300 SOURCES test_infra_hrs3300.c DEPS infra_hrs3300)
edge_add_host_test(test_app_heart_rate SOURCES test_app_heart_rate.c DEPS app_heart_rate)
if(NOT WIN32)
    target_link_libraries(test_app_heart_rate PRIVATE m)
endif()
edge_add_host_test(test_infra_bma421 SOURCES test_infra_bma421.c DEPS infra_bma421)
edge_add_host_test(test_app_step_counter SOURCES test_app_step_counter.c DEPS app_step_counter)
edge_add_host_test(test_app_watch_time SOURCES test_app_watch_time.c DEPS app_watch_time)
edge_add_host_test(test_infra_st7789 SOURCES test_infra_st7789.c DEPS infra_st7789)
edge_add_host_test(test_infra_flash_spi SOURCES test_infra_flash_spi.c DEPS infra_flash_spi)
edge_add_host_test(test_infra_haptic SOURCES test_infra_haptic.c DEPS infra_haptic)
edge_add_host_test(test_board_pinetime SOURCES test_board_pinetime.c DEPS board_pinetime soc_nrf52)
edge_add_host_test(test_soc_nrf52_spim SOURCES test_soc_nrf52_spim.c DEPS soc_nrf52)
edge_add_host_test(test_soc_nrf52_twim SOURCES test_soc_nrf52_twim.c DEPS soc_nrf52)
edge_add_host_test(test_soc_nrf52_saadc SOURCES test_soc_nrf52_saadc.c DEPS soc_nrf52)
edge_add_host_test(test_soc_nrf52_rtc SOURCES test_soc_nrf52_rtc.c DEPS soc_nrf52)
edge_add_host_test(test_soc_nrf52_wdt SOURCES test_soc_nrf52_wdt.c DEPS soc_nrf52)
edge_add_host_test(test_soc_nrf52_nvmc SOURCES test_soc_nrf52_nvmc.c DEPS soc_nrf52)
edge_add_host_test(test_app_ble_services SOURCES test_app_ble_services.c DEPS app_ble_services)
edge_add_host_test(test_infra_rle SOURCES test_infra_rle.c DEPS infra_rle)
edge_add_host_test(test_app_alarm SOURCES test_app_alarm.c DEPS app_alarm)
edge_add_host_test(test_app_stopwatch SOURCES test_app_stopwatch.c DEPS app_stopwatch)
edge_add_host_test(test_app_timer SOURCES test_app_timer.c DEPS app_timer)
edge_add_host_test(test_app_watch_settings SOURCES test_app_watch_settings.c DEPS app_watch_settings)
edge_add_host_test(test_app_ble_weather SOURCES test_app_ble_weather.c DEPS app_ble_weather)
edge_add_host_test(test_app_ble_music SOURCES test_app_ble_music.c DEPS app_ble_music)
edge_add_host_test(test_app_ble_nav SOURCES test_app_ble_nav.c DEPS app_ble_nav)
edge_add_host_test(test_app_ble_notifications SOURCES test_app_ble_notifications.c DEPS app_ble_notifications)
edge_add_host_test(test_app_ble_motion SOURCES test_app_ble_motion.c DEPS app_ble_motion)
edge_add_host_test(test_app_ble_fs SOURCES test_app_ble_fs.c DEPS app_ble_fs)
edge_add_host_test(test_app_ble_dfu SOURCES test_app_ble_dfu.c DEPS app_ble_dfu)
edge_add_host_test(test_app_watch_ui SOURCES test_app_watch_ui.c DEPS app_watch_ui)
edge_add_host_test(test_app_metronome SOURCES test_app_metronome.c DEPS app_metronome)
edge_add_host_test(test_app_calculator SOURCES test_app_calculator.c DEPS app_calculator)
edge_add_host_test(test_app_dice SOURCES test_app_dice.c DEPS app_dice)
edge_add_host_test(test_app_ble_passkey SOURCES test_app_ble_passkey.c DEPS app_ble_passkey)
edge_add_host_test(test_app_flashlight SOURCES test_app_flashlight.c DEPS app_flashlight)
edge_add_host_test(test_app_game_paddle SOURCES test_app_game_paddle.c DEPS app_game_paddle)
edge_add_host_test(test_app_game_twos SOURCES test_app_game_twos.c DEPS app_game_twos)
edge_add_host_test(test_app_paint SOURCES test_app_paint.c DEPS app_paint)
edge_add_host_test(test_app_firmware_validator SOURCES test_app_firmware_validator.c DEPS app_firmware_validator)

# Every product's glue is in the coverage scope, so every trampoline is exercised
# here instead of living only inside a product executable.
edge_add_host_test(test_product_glue SOURCES test_product_glue.c
    ../product/meter_host/glue.c
    ../product/meter_mps2/glue.c
    ../product/meter_gateway_host/glue.c
    ../product/meter_rtems/glue.c
    ../product/watch_host/glue.c
    ../product/pinetime/glue.c
    ../product/keyboard_host/glue.c
    ../product/corne_split/glue.c
    DEPS app_dlt645 app_meter_core app_modbus_slave app_relay app_touch_gesture app_button_handler app_watch_power app_heart_rate app_step_counter app_watch_time app_ble_services app_alarm app_stopwatch app_timer app_watch_settings app_ble_weather app_ble_music app_ble_nav app_ble_notifications app_ble_motion app_ble_fs app_ble_dfu app_firmware_validator app_watch_ui app_metronome app_calculator app_dice app_ble_passkey app_flashlight app_game_paddle app_game_twos app_paint infra_battery_adc infra_cst816s infra_hrs3300 infra_bma421 infra_st7789 infra_flash_spi infra_haptic infra_rle infra_flash infra_gpio infra_uart board_pinetime app_zmk_matrix app_zmk_keymap app_zmk_behavior app_zmk_split infra_hid_report)
target_include_directories(test_product_glue PRIVATE ${EDGE_ROOT}/product ${EDGE_ROOT}/product/meter_gateway_host ${EDGE_ROOT}/product/watch_host ${EDGE_ROOT}/product/pinetime ${EDGE_ROOT}/product/meter_rtems ${EDGE_ROOT}/product/keyboard_host ${EDGE_ROOT}/product/corne_split)

# ZMK Keyboard test suites
edge_add_host_test(test_infra_hid_report SOURCES test_infra_hid_report.c DEPS infra_hid_report)
edge_add_host_test(test_infra_kscan SOURCES test_infra_kscan.c DEPS infra_kscan)
edge_add_host_test(test_app_zmk_matrix SOURCES test_app_zmk_matrix.c DEPS app_zmk_matrix edge_test_contract)
edge_add_host_test(test_app_zmk_keymap SOURCES test_app_zmk_keymap.c DEPS app_zmk_keymap edge_test_contract)
edge_add_host_test(test_app_zmk_combo SOURCES test_app_zmk_combo.c DEPS app_zmk_combo edge_test_contract)
edge_add_host_test(test_app_zmk_behavior SOURCES test_app_zmk_behavior.c DEPS app_zmk_behavior edge_test_contract)
edge_add_host_test(test_app_zmk_endpoints SOURCES test_app_zmk_endpoints.c DEPS app_zmk_endpoints edge_test_contract)
edge_add_host_test(test_app_zmk_split SOURCES test_app_zmk_split.c DEPS app_zmk_split edge_test_contract)
edge_add_host_test(test_app_zmk_pointing SOURCES test_app_zmk_pointing.c DEPS app_zmk_pointing edge_test_contract)
edge_add_host_test(test_app_zmk_rgb SOURCES test_app_zmk_rgb.c DEPS app_zmk_rgb edge_test_contract)
edge_add_host_test(test_app_zmk_pm SOURCES test_app_zmk_pm.c DEPS app_zmk_pm edge_test_contract)
edge_add_host_test(test_app_zmk_wpm SOURCES test_app_zmk_wpm.c DEPS app_zmk_wpm edge_test_contract)
edge_add_host_test(test_board_keyboard_mock SOURCES test_board_keyboard_mock.c DEPS board_keyboard_mock edge_test_contract)
edge_add_host_test(test_app_zmk_sensors SOURCES test_app_zmk_sensors.c DEPS app_zmk_sensors edge_test_contract)
edge_add_host_test(test_app_zmk_behavior_queue SOURCES test_app_zmk_behavior_queue.c DEPS app_zmk_behavior_queue edge_test_contract)
edge_add_host_test(test_app_zmk_settings SOURCES test_app_zmk_settings.c DEPS app_zmk_settings edge_test_contract)
edge_add_host_test(test_app_zmk_ext_power SOURCES test_app_zmk_ext_power.c DEPS app_zmk_ext_power edge_test_contract)
edge_add_host_test(test_app_zmk_leds SOURCES test_app_zmk_leds.c DEPS app_zmk_leds edge_test_contract)
edge_add_host_test(test_app_zmk_pointing_processors SOURCES test_app_zmk_pointing_processors.c DEPS app_zmk_pointing_processors edge_test_contract)
edge_add_host_test(test_app_zmk_display SOURCES test_app_zmk_display.c DEPS app_zmk_display edge_test_contract)
edge_add_host_test(test_app_zmk_studio SOURCES test_app_zmk_studio.c DEPS app_zmk_studio edge_test_contract)
edge_add_host_test(test_app_zmk_backlight SOURCES test_app_zmk_backlight.c DEPS app_zmk_backlight edge_test_contract)
edge_add_host_test(test_app_zmk_battery SOURCES test_app_zmk_battery.c DEPS app_zmk_battery edge_test_contract)
edge_add_host_test(test_app_zmk_boot SOURCES test_app_zmk_boot.c DEPS app_zmk_boot edge_test_contract)
