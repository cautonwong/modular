cmake_minimum_required(VERSION 3.20)
project(edge_modular VERSION 0.5.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)
option(EDGE_MODULE_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})
option(EDGE_MODULE_BUILD_EXAMPLE "Build example product" ON)
option(EDGE_MODULE_ENABLE_WERROR "Treat compiler warnings as errors" ON)
option(EDGE_MODULE_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(EDGE_MODULE_ENABLE_COVERAGE "Enable GCC coverage" OFF)
option(EDGE_MODULE_BUILD_ARM_CORTEX_M0 "Build Cortex-M0 cross target" OFF)
option(EDGE_MODULE_BUILD_ARM_CORTEX_M4 "Build Cortex-M4 cross target" OFF)
option(EDGE_MODULE_BUILD_ARM_CORTEX_M4_QEMU "Build Cortex-M4 MPS2 AN386 target" OFF)
option(EDGE_MODULE_ARM_QEMU_SEMIHOSTING "Enable ARM semihosting exit for QEMU smoke tests" OFF)
option(EDGE_MODULE_BUILD_RISCV32_QEMU "Build RISC-V 32 QEMU (virt) target" OFF)
option(EDGE_MODULE_BUILD_FREERTOS_QEMU "Build FreeRTOS-hosted Cortex-M4 QEMU target" OFF)
set(EDGE_MODULE_FREERTOS_KERNEL_TAG "V11.1.0" CACHE STRING "FreeRTOS-Kernel git tag")

# When targeting bare-metal ARM, every compiled object (including static
# libraries) must use the same core and Thumb mode. Selecting it globally avoids
# linking ARM-mode library objects into a Thumb firmware image.
if(EDGE_MODULE_BUILD_ARM_CORTEX_M0)
    set(EDGE_ARM_CPU cortex-m0)
elseif(EDGE_MODULE_BUILD_ARM_CORTEX_M4 OR EDGE_MODULE_BUILD_ARM_CORTEX_M4_QEMU OR EDGE_MODULE_BUILD_FREERTOS_QEMU)
    set(EDGE_ARM_CPU cortex-m4)
endif()
if(DEFINED EDGE_ARM_CPU)
    add_compile_options(-mcpu=${EDGE_ARM_CPU} -mthumb -mno-unaligned-access -fstack-usage)
    add_link_options(-mcpu=${EDGE_ARM_CPU} -mthumb)
endif()

function(edge_enable_quality target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(EDGE_MODULE_ENABLE_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(EDGE_MODULE_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(EDGE_MODULE_ENABLE_COVERAGE)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

add_library(edge_module STATIC edge_module/src/event.c)
target_include_directories(edge_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include)
edge_enable_quality(edge_module)

add_library(sys_runtime STATIC sys/runtime/src/sys.c)
target_include_directories(sys_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sys/runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(sys_runtime PUBLIC edge_module)
edge_enable_quality(sys_runtime)

add_library(sys_example STATIC sys/example/sys_example.c)
target_include_directories(sys_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include)
target_link_libraries(sys_example PUBLIC sys_runtime)
edge_enable_quality(sys_example)

add_library(sys_meter STATIC sys/meter/sys_meter.c)
target_include_directories(sys_meter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sys/meter/include)
target_link_libraries(sys_meter PUBLIC sys_runtime)
edge_enable_quality(sys_meter)

# Product-line registries consumed by edge_add_product(). Keep each in sync with
# the matching add_library() target below.
set(EDGE_SYS_TARGET_example sys_example)
set(EDGE_SYS_TARGET_meter sys_meter)

add_library(app_dlt645 STATIC app/dlt645/src/dlt645.c)
target_include_directories(app_dlt645 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/dlt645/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(app_dlt645 PUBLIC edge_module)
edge_enable_quality(app_dlt645)
set(EDGE_APP_TARGET_dlt645 app_dlt645)

add_library(app_relay STATIC app/relay/src/relay.c)
target_include_directories(app_relay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/relay/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(app_relay PUBLIC edge_module)
edge_enable_quality(app_relay)
set(EDGE_APP_TARGET_relay app_relay)

add_library(app_modbus_slave STATIC app/modbus_slave/src/modbus_slave.c)
target_include_directories(app_modbus_slave PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/modbus_slave/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(app_modbus_slave PUBLIC edge_module)
edge_enable_quality(app_modbus_slave)
set(EDGE_APP_TARGET_modbus_slave app_modbus_slave)

add_library(app_meter_core STATIC app/meter_core/src/meter_core.c)
target_include_directories(app_meter_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/meter_core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(app_meter_core PUBLIC edge_module)
edge_enable_quality(app_meter_core)
set(EDGE_APP_TARGET_meter_core app_meter_core)

add_library(infra_flash STATIC infra/flash/src/flash.c)
target_include_directories(infra_flash PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/flash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(infra_flash PUBLIC edge_module)
edge_enable_quality(infra_flash)
set(EDGE_INFRA_TARGET_flash infra_flash)

add_library(infra_gpio STATIC infra/gpio/src/gpio.c)
target_include_directories(infra_gpio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/gpio/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(infra_gpio PUBLIC edge_module)
edge_enable_quality(infra_gpio)
set(EDGE_INFRA_TARGET_gpio infra_gpio)

add_library(infra_uart STATIC infra/uart/src/uart.c)
target_include_directories(infra_uart PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/uart/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(infra_uart PUBLIC edge_module)
edge_enable_quality(infra_uart)
set(EDGE_INFRA_TARGET_uart infra_uart)

add_library(pal_os STATIC pal/os/src/idle.c)
target_include_directories(pal_os PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/pal/os/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(pal_os PUBLIC edge_module)
edge_enable_quality(pal_os)

add_library(pal_host STATIC pal/host/src/host.c)
target_include_directories(pal_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/pal/host/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(pal_host PUBLIC edge_module pal_os)
edge_enable_quality(pal_host)

if(EDGE_MODULE_BUILD_FREERTOS_QEMU)
    include(FetchContent)
    FetchContent_Declare(freertos_kernel
        GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
        GIT_TAG ${EDGE_MODULE_FREERTOS_KERNEL_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(freertos_kernel)
    if(NOT freertos_kernel_POPULATED)
        FetchContent_Populate(freertos_kernel)
    endif()

    add_library(freertos_kernel STATIC
        ${freertos_kernel_SOURCE_DIR}/tasks.c
        ${freertos_kernel_SOURCE_DIR}/list.c
        ${freertos_kernel_SOURCE_DIR}/queue.c
        ${freertos_kernel_SOURCE_DIR}/portable/GCC/ARM_CM3/port.c
        ${freertos_kernel_SOURCE_DIR}/portable/MemMang/heap_4.c
    )
    target_include_directories(freertos_kernel PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/pal/rtos/freertos/include
        ${freertos_kernel_SOURCE_DIR}/include
        ${freertos_kernel_SOURCE_DIR}/portable/GCC/ARM_CM3
    )

    add_library(pal_rtos_freertos STATIC pal/rtos/freertos/src/freertos_rtos.c)
    target_include_directories(pal_rtos_freertos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/pal/rtos/include
        ${CMAKE_CURRENT_SOURCE_DIR}/pal/os/include
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
    )
    target_link_libraries(pal_rtos_freertos PUBLIC edge_module pal_os freertos_kernel)
endif()

add_library(board_example STATIC board/example/src/board.c)
target_include_directories(board_example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/board/example/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(board_example PUBLIC edge_module)
edge_enable_quality(board_example)
set(EDGE_BOARD_TARGET_example board_example)

add_library(board_mps2 STATIC board/mps2/src/board.c)
target_include_directories(board_mps2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/board/mps2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/soc/mps2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(board_mps2 PUBLIC edge_module)
edge_enable_quality(board_mps2)
set(EDGE_BOARD_TARGET_mps2 board_mps2)
if(EDGE_MODULE_ARM_QEMU_SEMIHOSTING OR EDGE_MODULE_BUILD_FREERTOS_QEMU)
    target_compile_definitions(board_mps2 PUBLIC EDGE_QEMU_SEMIHOSTING=1)
endif()

if(EDGE_MODULE_BUILD_RISCV32_QEMU)
    add_library(board_riscv_virt STATIC board/riscv_virt/src/board.c)
    target_include_directories(board_riscv_virt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/board/riscv_virt/include
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
    )
    target_link_libraries(board_riscv_virt PUBLIC edge_module)
    edge_enable_quality(board_riscv_virt)
    set(EDGE_BOARD_TARGET_riscv_virt board_riscv_virt)
endif()

# Legal family x board combinations (D39). An unknown family/board fails above;
# a known-but-unlisted pair is a product composition error and fails here.
set(EDGE_LEGAL_FAMILY_BOARD "example:example;meter:example;meter:mps2;meter:riscv_virt")

function(edge_add_product name)
    cmake_parse_arguments(P "" "family;board" "apps;infra" ${ARGN})
    if(NOT P_family OR NOT P_board OR NOT P_apps)
        message(FATAL_ERROR "edge_add_product(${name}): family, board and apps are required")
    endif()
    if(NOT DEFINED EDGE_SYS_TARGET_${P_family})
        message(FATAL_ERROR "edge_add_product(${name}): unknown family '${P_family}'")
    endif()
    if(NOT DEFINED EDGE_BOARD_TARGET_${P_board})
        message(FATAL_ERROR "edge_add_product(${name}): unknown board '${P_board}'")
    endif()
    if(NOT "${P_family}:${P_board}" IN_LIST EDGE_LEGAL_FAMILY_BOARD)
        message(FATAL_ERROR "edge_add_product(${name}): illegal family/board combination '${P_family}' + '${P_board}'")
    endif()

    set(dep_targets ${EDGE_SYS_TARGET_${P_family}} ${EDGE_BOARD_TARGET_${P_board}})
    foreach(app IN LISTS P_apps)
        if(NOT DEFINED EDGE_APP_TARGET_${app})
            message(FATAL_ERROR "edge_add_product(${name}): unknown app '${app}'")
        endif()
        list(APPEND dep_targets ${EDGE_APP_TARGET_${app}})
    endforeach()
    foreach(infra IN LISTS P_infra)
        if(NOT DEFINED EDGE_INFRA_TARGET_${infra})
            message(FATAL_ERROR "edge_add_product(${name}): unknown infra '${infra}'")
        endif()
        list(APPEND dep_targets ${EDGE_INFRA_TARGET_${infra}})
    endforeach()

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product/${name}/main.c")
        message(FATAL_ERROR "product/${name}/main.c is missing")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product/${name}/glue.c")
        message(FATAL_ERROR "product/${name}/glue.c is missing")
    endif()

    # Record dependencies so edge_add_arm_firmware() can reuse the same product
    # definition without re-parsing the composition.
    set(EDGE_PRODUCT_${name}_TARGETS "${dep_targets}" CACHE INTERNAL "Product ${name} dependencies")
    if(NOT EDGE_MODULE_BUILD_EXAMPLE)
        return()
    endif()

    add_executable(${name}
        product/${name}/main.c
        product/${name}/glue.c
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/product/${name}
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
    )
    target_link_libraries(${name} PRIVATE edge_module ${dep_targets})
    edge_enable_quality(${name})
endfunction()

# T7b neutrality fixture: one minimal product, parameterised over board + runner
# (and app). The app is never modified for a variant.
if(NOT CMAKE_CROSSCOMPILING)
    find_package(Threads REQUIRED)
endif()
function(edge_add_minimal_variant name board runner app)
    if(NOT DEFINED EDGE_BOARD_TARGET_${board})
        message(FATAL_ERROR "edge_add_minimal_variant(${name}): unknown board '${board}'")
    endif()
    if(NOT DEFINED EDGE_APP_TARGET_${app})
        message(FATAL_ERROR "edge_add_minimal_variant(${name}): unknown app '${app}'")
    endif()
    if(runner STREQUAL "baremetal")
        set(main_src tests/integration/minimal_product/main_baremetal.c)
    elseif(runner STREQUAL "thread")
        set(main_src tests/integration/minimal_product/main_thread.c)
    else()
        message(FATAL_ERROR "edge_add_minimal_variant(${name}): unknown runner '${runner}'")
    endif()

    add_executable(${name} ${main_src} tests/integration/minimal_product/glue.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/minimal_product
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
        ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/${app}/include
    )
    target_link_libraries(${name} PRIVATE
        edge_module sys_example ${EDGE_BOARD_TARGET_${board}} ${EDGE_APP_TARGET_${app}}
        infra_flash
    )
    if(runner STREQUAL "thread")
        target_link_libraries(${name} PRIVATE Threads::Threads)
    endif()
    edge_enable_quality(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "host")
endfunction()

edge_add_product(example family example board example infra flash apps dlt645)
edge_add_product(meter_host family meter board example infra flash gpio apps dlt645 relay)
edge_add_product(meter_mps2 family meter board mps2 infra flash gpio apps dlt645 relay)
edge_add_product(meter_gateway_host family meter board example infra flash uart apps dlt645 modbus_slave meter_core)

# Host neutrality matrix: board example x {baremetal, thread} runner, same app.
if(NOT CMAKE_CROSSCOMPILING)
    edge_add_minimal_variant(minimal_host_baremetal example baremetal dlt645)
    edge_add_minimal_variant(minimal_host_thread example thread dlt645)
endif()
if(EDGE_MODULE_BUILD_RISCV32_QEMU)
    edge_add_product(riscv_meter family meter board riscv_virt infra flash apps dlt645)
endif()
if(EDGE_MODULE_BUILD_FREERTOS_QEMU)
    edge_add_product(meter_mps2_freertos family meter board mps2 infra flash apps dlt645)
endif()

# Negative validation for the combinator: configuring with
# -DEDGE_MODULE_NEGATIVE_CASE=<family|board|app|infra> must fail. Consumers live
# in tests/negative_products.cmake so the positive product list stays clean.
set(EDGE_MODULE_NEGATIVE_CASE "" CACHE STRING "Force an invalid product combination")
if(EDGE_MODULE_NEGATIVE_CASE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/tests/negative_products.cmake)
endif()

function(edge_add_arm_firmware name product cpu linker_script)
    if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi-gcc$")
        message(FATAL_ERROR "${name} requires arm-none-eabi-gcc toolchain")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_TARGETS)
        message(FATAL_ERROR "${name}: unknown product '${product}'")
    endif()

    add_executable(${name}
        product/${product}/main.c
        product/${product}/glue.c
        .github/arm/startup.c
    )
    target_link_libraries(${name} PRIVATE edge_module ${EDGE_PRODUCT_${product}_TARGETS})
    target_compile_definitions(${name} PRIVATE EDGE_TARGET_ARM32=1)
    if(EDGE_MODULE_ARM_QEMU_SEMIHOSTING)
        target_compile_definitions(${name} PRIVATE EDGE_QEMU_SEMIHOSTING=1)
    endif()
    if(ARGC GREATER 4)
        target_compile_definitions(${name} PRIVATE EDGE_BOARD_TIMER_ISR=${ARGV4})
    endif()
    target_compile_options(${name} PRIVATE
        -mcpu=${cpu} -mthumb -mno-unaligned-access
        -ffreestanding -fdata-sections -ffunction-sections
        -fno-common -Os
    )
    target_link_options(${name} PRIVATE
        -mcpu=${cpu} -mthumb
        -T${CMAKE_CURRENT_SOURCE_DIR}/${linker_script}
        -nostartfiles
        --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

if(EDGE_MODULE_BUILD_ARM_CORTEX_M0)
    edge_add_arm_firmware(example_cortex_m0 example cortex-m0 .github/arm/cortex-m0.ld)
endif()

if(EDGE_MODULE_BUILD_ARM_CORTEX_M4)
    edge_add_arm_firmware(example_cortex_m4 example cortex-m4 .github/arm/cortex-m4.ld)
endif()

if(EDGE_MODULE_BUILD_ARM_CORTEX_M4_QEMU)
    edge_add_arm_firmware(meter_mps2_fw meter_mps2 cortex-m4 .github/arm/cortex-m4-qemu.ld board_mps2_irq_timer0)
endif()

function(edge_add_riscv_firmware name product linker_script)
    if(NOT CMAKE_C_COMPILER MATCHES "riscv.*-gcc$")
        message(FATAL_ERROR "${name} requires a RISC-V gcc toolchain")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_TARGETS)
        message(FATAL_ERROR "${name}: unknown product '${product}'")
    endif()

    add_executable(${name}
        product/${product}/main.c
        product/${product}/glue.c
        .github/riscv/startup.S
        .github/riscv/support.c
    )
    target_link_libraries(${name} PRIVATE edge_module ${EDGE_PRODUCT_${product}_TARGETS} -lgcc)
    target_compile_definitions(${name} PRIVATE EDGE_TARGET_RISCV32=1)
    target_compile_options(${name} PRIVATE
        -ffreestanding -fdata-sections -ffunction-sections -fno-common -Os
    )
    target_link_options(${name} PRIVATE
        -nostdlib
        -T${CMAKE_CURRENT_SOURCE_DIR}/${linker_script}
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

if(EDGE_MODULE_BUILD_RISCV32_QEMU)
    edge_add_riscv_firmware(riscv_meter riscv_meter .github/riscv/riscv-virt.ld)
endif()

function(edge_add_arm_freertos_firmware name product)
    if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi-gcc$")
        message(FATAL_ERROR "${name} requires arm-none-eabi-gcc toolchain")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_TARGETS)
        message(FATAL_ERROR "${name}: unknown product '${product}'")
    endif()

    add_executable(${name}
        product/${product}/main.c
        product/${product}/glue.c
        .github/arm/startup.c
    )
    target_link_libraries(${name} PRIVATE
        edge_module ${EDGE_PRODUCT_${product}_TARGETS} pal_rtos_freertos freertos_kernel
    )
    target_compile_definitions(${name} PRIVATE
        EDGE_TARGET_ARM32=1
        EDGE_QEMU_SEMIHOSTING=1
        EDGE_SVC_HANDLER=vPortSVCHandler
        EDGE_PENDSV_HANDLER=xPortPendSVHandler
        EDGE_SYSTICK_HANDLER=xPortSysTickHandler
    )
    target_compile_options(${name} PRIVATE
        -mcpu=cortex-m4 -mthumb -mno-unaligned-access -mfloat-abi=soft
        -ffreestanding -fdata-sections -ffunction-sections -fno-common -Os
    )
    target_link_options(${name} PRIVATE
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
        -T${CMAKE_CURRENT_SOURCE_DIR}/.github/arm/cortex-m4-qemu.ld
        -nostartfiles --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

if(EDGE_MODULE_BUILD_FREERTOS_QEMU)
    edge_add_arm_freertos_firmware(meter_mps2_freertos_fw meter_mps2_freertos)
endif()

if(EDGE_MODULE_BUILD_TESTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

    function(edge_add_host_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
            ${CMAKE_CURRENT_SOURCE_DIR}/sys/runtime/include
            ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include
            ${CMAKE_CURRENT_SOURCE_DIR}/sys/meter/include
            ${CMAKE_CURRENT_SOURCE_DIR}/app/dlt645/include
            ${CMAKE_CURRENT_SOURCE_DIR}/app/relay/include
            ${CMAKE_CURRENT_SOURCE_DIR}/app/modbus_slave/include
            ${CMAKE_CURRENT_SOURCE_DIR}/app/meter_core/include
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/flash/include
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/gpio/include
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/uart/include
            ${CMAKE_CURRENT_SOURCE_DIR}/board/example/include
            ${CMAKE_CURRENT_SOURCE_DIR}/pal/host/include
            ${CMAKE_CURRENT_SOURCE_DIR}/pal/os/include
        )
        target_link_libraries(${name} PRIVATE
            edge_module sys_example sys_meter app_dlt645 app_relay app_modbus_slave app_meter_core
            infra_flash infra_gpio infra_uart board_example pal_host pal_os
            PkgConfig::CMOCKA Threads::Threads
        )
        edge_enable_quality(${name})
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES LABELS "host")
    endfunction()

    edge_add_host_test(test_event tests/test_event.c)
    edge_add_host_test(test_sys tests/test_sys.c)
    edge_add_host_test(test_architecture tests/test_architecture.c)
    edge_add_host_test(test_app_dlt645 tests/test_app_dlt645.c)
    edge_add_host_test(test_app_relay tests/test_app_relay.c)
    edge_add_host_test(test_infra_gpio tests/test_infra_gpio.c)
    edge_add_host_test(test_modbus_slave tests/test_modbus_slave.c)
    edge_add_host_test(test_meter_core tests/test_meter_core.c)
    edge_add_host_test(test_app_interaction tests/test_app_interaction.c)
    edge_add_host_test(test_ports tests/test_ports.c)
    edge_add_host_test(test_pal_host tests/test_pal_host.c)
    edge_add_host_test(test_pal_os tests/test_pal_os.c)
    edge_add_host_test(test_runner_threads tests/test_runner_threads.c)
    edge_add_host_test(test_flash_glue tests/test_flash_glue.c product/example/glue.c)
endif()
