cmake_minimum_required(VERSION 3.16)
project(edge_modular VERSION 0.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)
option(EDGE_MODULE_BUILD_TESTS "Build tests" ${BUILD_TESTING})
option(EDGE_MODULE_BUILD_EXAMPLE "Build example" ON)
option(EDGE_MODULE_ENABLE_WERROR "Treat compiler warnings as errors" OFF)
option(EDGE_MODULE_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(EDGE_MODULE_ENABLE_COVERAGE "Enable GCC coverage" OFF)

add_library(edge_modular STATIC
    src/module_manager.c
    src/event.c
    src/resource.c
    src/service.c)
target_include_directories(edge_modular PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(edge_modular PRIVATE -Wall -Wextra -Wpedantic)

if(EDGE_MODULE_ENABLE_WERROR)
    target_compile_options(edge_modular PRIVATE -Werror)
endif()

if(EDGE_MODULE_ENABLE_SANITIZERS)
    target_compile_options(edge_modular PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(edge_modular INTERFACE -fsanitize=address,undefined)
endif()

if(EDGE_MODULE_ENABLE_COVERAGE)
    target_compile_options(edge_modular PRIVATE --coverage -O0 -g)
    target_link_options(edge_modular INTERFACE --coverage)
endif()

if(EDGE_MODULE_BUILD_EXAMPLE)
    add_executable(edge_modular_example examples/superloop_main.c)
    target_link_libraries(edge_modular_example PRIVATE edge_modular)
endif()

if(EDGE_MODULE_BUILD_TESTS)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CMOCKA IMPORTED_TARGET cmocka)
    endif()
    if(NOT CMOCKA_FOUND)
        message(FATAL_ERROR "CMocka is required for tests. Install libcmocka-dev.")
    endif()

    add_executable(test_event tests/test_event.c)
    target_link_libraries(test_event PRIVATE edge_modular PkgConfig::CMOCKA)
    add_test(NAME event COMMAND test_event)

    add_executable(test_abi tests/test_abi.c)
    target_link_libraries(test_abi PRIVATE edge_modular PkgConfig::CMOCKA)
    add_test(NAME abi COMMAND test_abi)

    add_executable(test_manager tests/test_manager.c)
    target_link_libraries(test_manager PRIVATE edge_modular PkgConfig::CMOCKA)
    add_test(NAME manager COMMAND test_manager)
endif()
