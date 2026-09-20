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

# Third-party sources, so deliberately outside edge_enable_quality().
add_library(freertos_kernel STATIC
    ${freertos_kernel_SOURCE_DIR}/tasks.c
    ${freertos_kernel_SOURCE_DIR}/list.c
    ${freertos_kernel_SOURCE_DIR}/queue.c
    ${freertos_kernel_SOURCE_DIR}/portable/GCC/ARM_CM3/port.c
    ${freertos_kernel_SOURCE_DIR}/portable/MemMang/heap_4.c
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
