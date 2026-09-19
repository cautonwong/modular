# EdgeTargets.cmake -- the shape of a layer module, defined once (D88).
#
# Before D88 every app, driver, board, sys and pal target was declared in the
# top-level CMakeLists.txt: five areas working in parallel all edited the same
# ~170 lines, and the host-test helper repeated a hard-coded list of every
# include directory and every library in the project. Now each area directory
# owns a CMakeLists.txt that declares itself through one of the helpers below,
# and the top-level file only discovers areas and composes products.
#
# What a module looks like -- static library, public `include/`, edge_module on
# the include path, quality flags, registry entry consumed by edge_add_product()
# -- lives here exactly once, so an author cannot get it subtly wrong.
#
# Every helper is called from the area directory, so CMAKE_CURRENT_SOURCE_DIR is
# that directory and source files are written relative to it.

get_filename_component(EDGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# The foundation layer (edge_module) is the only library with no dependencies and
# is not registered as a product-line module.
function(edge_add_foundation target)
    cmake_parse_arguments(F "" "" "SOURCES" ${ARGN})
    if(NOT F_SOURCES)
        message(FATAL_ERROR "edge_add_foundation(${target}): at least one SOURCES entry is required")
    endif()
    add_library(${target} STATIC ${F_SOURCES})
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    edge_enable_quality(${target})
endfunction()

# Internal: create <layer>_<name> and register it. `HEADERS` declares an
# interface-only module (a header package such as pal/rtos or soc/mps2);
# `RAW` keeps a target out of the quality gate (see pal/rtos/freertos);
# `FAMILY` marks a sys module as a selectable product family.
function(edge_add_layer_module layer name)
    cmake_parse_arguments(M "HEADERS;RAW;FAMILY" "" "SOURCES;DEPS" ${ARGN})
    set(target ${layer}_${name})

    if(NOT M_DEPS)
        message(FATAL_ERROR
            "edge_add_${layer}(${name}): DEPS is required; name every module this one "
            "includes, so the dependency is visible in the area file (D27)")
    endif()

    if(M_HEADERS)
        if(M_SOURCES)
            message(FATAL_ERROR "edge_add_${layer}(${name}): HEADERS and SOURCES are exclusive")
        endif()
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${EDGE_ROOT}/edge_module/include
        )
        target_link_libraries(${target} INTERFACE ${M_DEPS})
    else()
        if(NOT M_SOURCES)
            message(FATAL_ERROR "edge_add_${layer}(${name}): at least one SOURCES entry is required")
        endif()
        add_library(${target} STATIC ${M_SOURCES})
        target_include_directories(${target} PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${EDGE_ROOT}/edge_module/include
        )
        target_link_libraries(${target} PUBLIC ${M_DEPS})
        if(NOT M_RAW)
            edge_enable_quality(${target})
        endif()
    endif()

    if(layer STREQUAL "app")
        set(EDGE_APP_TARGET_${name} ${target} CACHE INTERNAL "app registry: ${name}")
    elseif(layer STREQUAL "infra")
        set(EDGE_INFRA_TARGET_${name} ${target} CACHE INTERNAL "infra registry: ${name}")
    elseif(layer STREQUAL "board")
        set(EDGE_BOARD_TARGET_${name} ${target} CACHE INTERNAL "board registry: ${name}")
    elseif(layer STREQUAL "sys" AND M_FAMILY)
        set(EDGE_SYS_TARGET_${name} ${target} CACHE INTERNAL "sys family registry: ${name}")
    endif()
endfunction()

function(edge_add_app name)
    edge_add_layer_module(app ${name} ${ARGN})
endfunction()

function(edge_add_infra name)
    edge_add_layer_module(infra ${name} ${ARGN})
endfunction()

function(edge_add_board name)
    edge_add_layer_module(board ${name} ${ARGN})
endfunction()

function(edge_add_soc name)
    edge_add_layer_module(soc ${name} ${ARGN})
endfunction()

function(edge_add_sys name)
    edge_add_layer_module(sys ${name} ${ARGN})
endfunction()

function(edge_add_pal name)
    edge_add_layer_module(pal ${name} ${ARGN})
endfunction()

# Host unit test. The test names the modules it exercises, so adding an app or a
# driver never edits a shared list again.
function(edge_add_host_test name)
    cmake_parse_arguments(T "" "" "SOURCES;DEPS" ${ARGN})
    if(NOT T_SOURCES)
        message(FATAL_ERROR "edge_add_host_test(${name}): SOURCES is required")
    endif()
    if(NOT T_DEPS)
        message(FATAL_ERROR "edge_add_host_test(${name}): DEPS is required")
    endif()
    add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE ${T_DEPS} PkgConfig::CMOCKA Threads::Threads)
    edge_enable_quality(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "host")
endfunction()
