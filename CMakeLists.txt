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
option(EDGE_MODULE_ARM_QEMU_SEMIHOSTING "Enable ARM semihosting exit for QEMU smoke tests" OFF)

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

add_library(sys_example STATIC sys/example/sys_example.c sys/example/src/sys.c)
target_include_directories(sys_example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(sys_example PUBLIC edge_module)
edge_enable_quality(sys_example)

add_library(app_dlt645 STATIC app/dlt645/src/dlt645.c)
target_include_directories(app_dlt645 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/dlt645/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(app_dlt645 PUBLIC edge_module)
edge_enable_quality(app_dlt645)
set(EDGE_APP_TARGET_dlt645 app_dlt645)

add_library(infra_flash STATIC infra/flash/src/flash.c)
target_include_directories(infra_flash PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/infra/flash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(infra_flash PUBLIC edge_module)
edge_enable_quality(infra_flash)

add_library(board_example STATIC board/example/src/board.c)
target_include_directories(board_example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/board/example/include
    ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
)
target_link_libraries(board_example PUBLIC edge_module)
edge_enable_quality(board_example)

function(edge_add_product name)
    cmake_parse_arguments(P "" "family;board" "apps" ${ARGN})
    if(NOT P_family OR NOT P_board OR NOT P_apps)
        message(FATAL_ERROR "edge_add_product(${name}): family, board and apps are required")
    endif()

    set(app_targets)
    foreach(app IN LISTS P_apps)
        if(NOT DEFINED EDGE_APP_TARGET_${app})
            message(FATAL_ERROR "edge_add_product(${name}): unknown app '${app}'")
        endif()
        list(APPEND app_targets ${EDGE_APP_TARGET_${app}})
    endforeach()

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product/${name}/main.c")
        message(FATAL_ERROR "product/${name}/main.c is missing")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product/${name}/glue.c")
        message(FATAL_ERROR "product/${name}/glue.c is missing")
    endif()

    add_executable(${name}
        product/${name}/main.c
        product/${name}/glue.c
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/product/${name}
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
    )
    target_link_libraries(${name} PRIVATE
        edge_module sys_example board_example infra_flash ${app_targets}
    )
    edge_enable_quality(${name})
endfunction()

if(EDGE_MODULE_BUILD_EXAMPLE)
    edge_add_product(example family example board example apps dlt645)
endif()

function(edge_add_arm_firmware name cpu linker_script)
    if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi-gcc$")
        message(FATAL_ERROR "${name} requires arm-none-eabi-gcc toolchain")
    endif()

    add_executable(${name}
        product/example/main.c
        product/example/glue.c
        board/example/src/board.c
        infra/flash/src/flash.c
        app/dlt645/src/dlt645.c
        sys/example/sys_example.c
        sys/example/src/sys.c
        edge_module/src/event.c
        .github/arm/startup.c
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
        ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/dlt645/include
        ${CMAKE_CURRENT_SOURCE_DIR}/infra/flash/include
        ${CMAKE_CURRENT_SOURCE_DIR}/board/example/include
        ${CMAKE_CURRENT_SOURCE_DIR}/product/example
    )
    target_compile_definitions(${name} PRIVATE EDGE_TARGET_ARM32=1)
    if(EDGE_MODULE_ARM_QEMU_SEMIHOSTING)
        target_compile_definitions(${name} PRIVATE EDGE_QEMU_SEMIHOSTING=1)
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
    edge_add_arm_firmware(example_cortex_m0 cortex-m0 .github/arm/cortex-m0.ld)
endif()

if(EDGE_MODULE_BUILD_ARM_CORTEX_M4)
    edge_add_arm_firmware(example_cortex_m4 cortex-m4 .github/arm/cortex-m4.ld)
endif()

if(EDGE_MODULE_BUILD_TESTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

    function(edge_add_host_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
            ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include
            ${CMAKE_CURRENT_SOURCE_DIR}/app/dlt645/include
            ${CMAKE_CURRENT_SOURCE_DIR}/infra/flash/include
            ${CMAKE_CURRENT_SOURCE_DIR}/board/example/include
        )
        target_link_libraries(${name} PRIVATE
            edge_module sys_example app_dlt645 infra_flash board_example
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
    edge_add_host_test(test_flash_glue tests/test_flash_glue.c product/example/glue.c)
endif()
