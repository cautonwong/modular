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
        if(app STREQUAL "dlt645")
            list(APPEND app_targets app_dlt645)
        else()
            message(FATAL_ERROR "edge_add_product(${name}): unknown app '${app}'")
        endif()
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

if(EDGE_MODULE_BUILD_ARM_CORTEX_M0)
    if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi-gcc$")
        message(FATAL_ERROR "EDGE_MODULE_BUILD_ARM_CORTEX_M0 requires arm-none-eabi-gcc toolchain")
    endif()

    add_executable(example_cortex_m0
        product/example/main.c
        product/example/glue.c
        board/example/src/board.c
        infra/flash/src/flash.c
        app/dlt645/src/dlt645.c
        sys/example/src/sys.c
        edge_module/src/event.c
        .github/arm/startup.c
    )
    target_include_directories(example_cortex_m0 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
        ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/dlt645/include
        ${CMAKE_CURRENT_SOURCE_DIR}/infra/flash/include
        ${CMAKE_CURRENT_SOURCE_DIR}/board/example/include
        ${CMAKE_CURRENT_SOURCE_DIR}/product/example
    )
    target_compile_definitions(example_cortex_m0 PRIVATE EDGE_TARGET_ARM32=1)
    target_compile_options(example_cortex_m0 PRIVATE
        -mcpu=cortex-m0 -mthumb -mno-unaligned-access
        -ffreestanding -fdata-sections -ffunction-sections
        -fno-common -Os
    )
    target_link_options(example_cortex_m0 PRIVATE
        -mcpu=cortex-m0 -mthumb
        -T${CMAKE_CURRENT_SOURCE_DIR}/.github/arm/cortex-m0.ld
        --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/example_cortex_m0.map
    )
    set_target_properties(example_cortex_m0 PROPERTIES OUTPUT_NAME example_cortex_m0)
    add_custom_command(TARGET example_cortex_m0 POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:example_cortex_m0> ${CMAKE_CURRENT_BINARY_DIR}/example_cortex_m0.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:example_cortex_m0>
        VERBATIM
    )
endif()

if(EDGE_MODULE_BUILD_TESTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

    add_executable(test_event tests/test_event.c)
    target_link_libraries(test_event PRIVATE edge_module PkgConfig::CMOCKA)
    target_include_directories(test_event PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include)
    edge_enable_quality(test_event)
    add_test(NAME event COMMAND test_event)

    add_executable(test_sys tests/test_sys.c)
    target_link_libraries(test_sys PRIVATE edge_module PkgConfig::CMOCKA)
    target_include_directories(test_sys PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include ${CMAKE_CURRENT_SOURCE_DIR}/sys/example/include)
    target_link_libraries(test_sys PRIVATE sys_example)
    edge_enable_quality(test_sys)
    add_test(NAME sys COMMAND test_sys)

    add_executable(test_architecture tests/test_architecture.c)
    target_link_libraries(test_architecture PRIVATE edge_module PkgConfig::CMOCKA)
    target_include_directories(test_architecture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include)
    edge_enable_quality(test_architecture)
    add_test(NAME architecture COMMAND test_architecture)
endif()
