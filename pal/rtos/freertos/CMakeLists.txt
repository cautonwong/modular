# FreeRTOS: the kernel plus our port on top of the neutral RTOS interface
# (D46/D85). Only the QEMU product uses it today.
if(NOT EDGE_MODULE_BUILD_FREERTOS_QEMU)
    return()
endif()

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

# The pin is verified where it is used, not only in ci/dependencies.json: fetching by
# tag keeps the clone shallow and fast, and this catches the case the whole exercise
# is about - a tag that moved, or a pin that was edited in one place only.
if(NOT DEFINED EDGE_MODULE_FREERTOS_KERNEL_COMMIT)
    message(FATAL_ERROR
        "EDGE_MODULE_FREERTOS_KERNEL_COMMIT is not set; the kernel pin is mandatory "
        "(ci/dependencies.json, docs/dependencies.md)")
endif()
execute_process(
    COMMAND git -C ${freertos_kernel_SOURCE_DIR} rev-parse HEAD
    OUTPUT_VARIABLE EDGE_FREERTOS_HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT EDGE_FREERTOS_HEAD STREQUAL "${EDGE_MODULE_FREERTOS_KERNEL_COMMIT}")
    message(FATAL_ERROR
        "FreeRTOS-Kernel revision drift: the checkout is ${EDGE_FREERTOS_HEAD} but the pin is "
        "${EDGE_MODULE_FREERTOS_KERNEL_COMMIT} (tag ${EDGE_MODULE_FREERTOS_KERNEL_TAG}). Either the "
        "tag moved or the pin was changed in one place only; see docs/dependencies.md")
endif()
message(STATUS "FreeRTOS-Kernel pinned at ${EDGE_FREERTOS_HEAD} (${EDGE_MODULE_FREERTOS_KERNEL_TAG})")

# Third-party sources, so deliberately outside edge_enable_quality().
add_library(freertos_kernel STATIC
    ${freertos_kernel_SOURCE_DIR}/tasks.c
    ${freertos_kernel_SOURCE_DIR}/list.c
    ${freertos_kernel_SOURCE_DIR}/queue.c
    ${freertos_kernel_SOURCE_DIR}/portable/GCC/ARM_CM3/port.c
    # No heap_4.c: the port owns static task storage and the image must not be able
    # to allocate (#172).
)
target_include_directories(freertos_kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${freertos_kernel_SOURCE_DIR}/include
    ${freertos_kernel_SOURCE_DIR}/portable/GCC/ARM_CM3
)

# RAW: this port has never been compiled with the warning gate. Turning it on is
# part of the RTOS hardening work (#84, #85), not of this refactor, so the flag
# set of every existing target stays exactly as it was.
edge_add_pal(rtos_freertos RAW SOURCES src/freertos_rtos.c src/freertos_pal.c
    DEPS edge_module pal_os pal_rtos freertos_kernel)
