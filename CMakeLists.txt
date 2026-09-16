cmake_minimum_required(VERSION 3.20)
project(edge_modular VERSION 0.5.0 LANGUAGES C)

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

# When targeting bare-metal ARM, every compiled object (including static
# libraries) must use the same core and Thumb mode. Selecting it globally avoids
# linking ARM-mode library objects into a Thumb firmware image.
if(EDGE_MODULE_BUILD_ARM_CORTEX_M0)
    set(EDGE_ARM_CPU cortex-m0)
elseif(EDGE_MODULE_BUILD_ARM_CORTEX_M4 OR EDGE_MODULE_BUILD_ARM_CORTEX_M4_QEMU)
    set(EDGE_ARM_CPU cortex-m4)
endif()
if(DEFINED EDGE_ARM_CPU)
    add_compile_options(-mcpu=${EDGE_ARM_CPU} -mthumb -mno-unaligned-access)
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

add_library(pal_host STATIC pal/host/src/host.c)
target_include_directories(pal_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/pal/host/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(pal_host PUBLIC edge_module)
edge_enable_quality(pal_host)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(board_mps2 PUBLIC edge_module)
edge_enable_quality(board_mps2)
set(EDGE_BOARD_TARGET_mps2 board_mps2)

# Legal family x board combinations (D39). An unknown family/board fails above;
# a known-but-unlisted pair is a product composition error and fails here.
set(EDGE_LEGAL_FAMILY_BOARD "example:example;meter:example;meter:mps2")

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

edge_add_product(example family example board example infra flash apps dlt645)
edge_add_product(meter_host family meter board example infra flash gpio apps dlt645 relay)
edge_add_product(meter_mps2 family meter board mps2 infra flash gpio apps dlt645 relay)

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
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/flash/include
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/gpio/include
            ${CMAKE_CURRENT_SOURCE_DIR}/board/example/include
            ${CMAKE_CURRENT_SOURCE_DIR}/pal/host/include
        )
        target_link_libraries(${name} PRIVATE
            edge_module sys_example sys_meter app_dlt645 app_relay
            infra_flash infra_gpio board_example pal_host
            PkgConfig::CMOCKA
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
    edge_add_host_test(test_pal_host tests/test_pal_host.c)
    edge_add_host_test(test_flash_glue tests/test_flash_glue.c product/example/glue.c)
endif()
