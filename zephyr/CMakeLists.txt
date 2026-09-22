# Zephyr module CMakeLists.txt (Issue #82)
if(CONFIG_MODULAR)
    zephyr_library_named(modular_core)

    zephyr_include_directories(
        ../edge_module/include
        ../sys/runtime/include
        ../sys/meter/include
        ../pal/os/include
        ../pal/rtos/include
        ../pal/rtos/zephyr/include
        ../infra/flash/include
        ../infra/gpio/include
        ../infra/uart/include
        ../app/dlt645/include
        ../app/meter_core/include
        ../app/modbus_slave/include
        ../app/relay/include
    )

    zephyr_library_sources(
        ../edge_module/src/event.c
        ../sys/runtime/src/sys.c
        ../sys/meter/sys_meter.c
        ../pal/os/src/tick64.c
        ../pal/os/src/idle.c
        ../pal/rtos/src/assert.c
        ../pal/rtos/zephyr/src/zephyr_pal.c
        ../pal/rtos/zephyr/src/zephyr_rtos.c
        ../app/dlt645/src/dlt645.c
        ../app/meter_core/src/meter_core.c
        ../app/modbus_slave/src/modbus_slave.c
        ../app/relay/src/relay.c
        ../infra/flash/src/flash.c
        ../infra/gpio/src/gpio.c
        ../infra/uart/src/uart.c
    )
endif()
