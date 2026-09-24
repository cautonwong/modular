# ThreadX PAL (D46/D85, issue #134).
#
# Fetched like the FreeRTOS port: by tag and shallow, then the resolved revision is
# verified against the pin, because a tag can be moved. Which vendor port gets built
# depends on the target: the Cortex-M3 GNU port for a Cortex-M firmware (it runs on
# the M4 we build for, soft float, no FPU lazy stacking to configure), and the Linux
# GNU port for a host product - which ships its own `tx_initialize_low_level.c`, so the
# first ThreadX verification needs no bare-metal bring-up at all.
if(NOT EDGE_MODULE_BUILD_THREADX)
    return()
endif()

set(EDGE_MODULE_THREADX_TAG "v6.5.1.202602a_rel"
    CACHE STRING "ThreadX tag - for humans; the commit below is the pin")
set(EDGE_MODULE_THREADX_COMMIT "b91b03b9e75fa523b17127f9e0eca09dca916459"
    CACHE STRING "ThreadX commit - the immutable pin (see ci/dependencies.json)")

if(CMAKE_CROSSCOMPILING)
    set(EDGE_THREADX_PORT_DIR ports/cortex_m3/gnu)
else()
    set(EDGE_THREADX_PORT_DIR ports/linux/gnu)
endif()

include(FetchContent)
FetchContent_Declare(threadx
    GIT_REPOSITORY https://github.com/eclipse-threadx/threadx.git
    GIT_TAG ${EDGE_MODULE_THREADX_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(threadx)
if(NOT threadx_POPULATED)
    FetchContent_Populate(threadx)
endif()

execute_process(
    COMMAND git -C ${threadx_SOURCE_DIR} rev-parse HEAD
    OUTPUT_VARIABLE EDGE_THREADX_HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT EDGE_THREADX_HEAD STREQUAL "${EDGE_MODULE_THREADX_COMMIT}")
    message(FATAL_ERROR
        "ThreadX revision drift: the checkout is ${EDGE_THREADX_HEAD} but the pin is "
        "${EDGE_MODULE_THREADX_COMMIT} (tag ${EDGE_MODULE_THREADX_TAG}); see docs/dependencies.md")
endif()
message(STATUS "ThreadX pinned at ${EDGE_THREADX_HEAD} (${EDGE_MODULE_THREADX_TAG}, ${EDGE_THREADX_PORT_DIR})")

# Third-party sources, so deliberately outside edge_enable_quality(). Globbing the
# vendor's kernel and port is the one place a glob belongs: the list is theirs, it is
# pinned, and CONFIGURE_DEPENDS keeps it honest on reconfigure. Our own sources stay
# explicit.
file(GLOB THREADX_COMMON CONFIGURE_DEPENDS ${threadx_SOURCE_DIR}/common/src/*.c)
file(GLOB THREADX_PORT CONFIGURE_DEPENDS ${threadx_SOURCE_DIR}/${EDGE_THREADX_PORT_DIR}/src/*.c
    ${threadx_SOURCE_DIR}/${EDGE_THREADX_PORT_DIR}/src/*.S)

add_library(threadx_kernel STATIC ${THREADX_COMMON} ${THREADX_PORT})
target_include_directories(threadx_kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${threadx_SOURCE_DIR}/common/inc
    ${threadx_SOURCE_DIR}/${EDGE_THREADX_PORT_DIR}/inc
)
if(NOT CMAKE_CROSSCOMPILING)
    # The Linux port needs GNU source visibility and reports its scheduling through a
    # debug channel; both are the vendor's requirements, not ours.
    # Both come from the vendor Linux port: _GNU_SOURCE for the feature macros it uses,
    # and TX_LINUX_MULTI_CORE because the port otherwise leaves concurrent threads
    # unconstrained on a multi-core host. Deliberately *not* the vendor's
    # TX_LINUX_DEBUG_ENABLE trace: it turns TX_DISABLE into a plain non-recursive
    # mutex, so any kernel call made inside a critical section self-deadlocks (measured:
    # edge_sys_start -> tx_time_get -> _tx_linux_debug_entry_insert blocked on the mutex
    # our own TX_DISABLE held).
    target_compile_definitions(threadx_kernel PUBLIC _GNU_SOURCE TX_LINUX_MULTI_CORE)
    find_package(Threads REQUIRED)
    target_link_libraries(threadx_kernel PUBLIC Threads::Threads)
endif()

edge_add_pal(rtos_threadx RAW SOURCES src/threadx_rtos.c src/threadx_pal.c
    DEPS edge_module pal_os pal_rtos threadx_kernel)
