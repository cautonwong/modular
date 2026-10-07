# ChibiOS: the kernel from the tree vendored beside this file, and our port on top of the neutral
# RTOS interface (D46/D85). The vendored copy lives at vendor/ChibiOS_3.0.5, which is the RTOS the
# reference firmware runs on - so unlike the FreeRTOS backend, whose kernel is fetched by tag, this one
# is a tree the repository already carries.
#
# The backend builds with the existing Cortex-M4 option rather than a new one of its own: that option
# already means "there is a cross toolchain and a chip to target", which is the whole of what this
# needs. When a product runs ChibiOS it will choose the option, as the FreeRTOS product does with its
# own.
if(NOT EDGE_MODULE_BUILD_ARM_CORTEX_M4)
    return()
endif()

set(EDGE_CHIBIOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/ChibiOS_3.0.5")

# The kernel's own sources, the port below the kernel, and the two start-up files the port needs. They
# are globbed rather than listed because the list belongs to the vendored tree, not to this file: a
# newer ChibiOS would add and remove them without this target being edited, and the import is already
# pinned as a whole tree.
file(GLOB EDGE_CHIBIOS_RT_SOURCES
    "${EDGE_CHIBIOS_ROOT}/os/rt/src/*.c"
)
file(GLOB EDGE_CHIBIOS_PORT_SOURCES
    "${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.s"
)

# Third-party sources, so deliberately outside edge_enable_quality(): warnings are off for this target
# alone, and the port's own files are the ones the repository wrote.
add_library(chibios_kernel STATIC EXCLUDE_FROM_ALL
              ${EDGE_CHIBIOS_RT_SOURCES} ${EDGE_CHIBIOS_PORT_SOURCES})
#
# EXCLUDE_FROM_ALL because the kernel alone is not yet the whole of what a ChibiOS build needs: its
# device parameters reach for board.h, and that board header is the HAL's. The kernel, its port and
# their sources compile as far as that boundary; finishing it means bringing the HAL in with the board
# that uses it, which is the piece after this one. Being out of the default build keeps that honest:
# nothing here claims to build when the target it belongs to cannot.
target_include_directories(chibios_kernel PUBLIC
    "${EDGE_CHIBIOS_ROOT}/os/rt/include"
    "${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx"
    "${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx/compilers/GCC"
    "${EDGE_CHIBIOS_ROOT}/os/common/portability/GCC"
    "${EDGE_CHIBIOS_ROOT}/os/common/oslib/include"
    "${EDGE_CHIBIOS_ROOT}/os/common/startup/ARMCMx/compilers/GCC"
    # The device parameters are per family, and this repository's chip is an STM32F4. A product
    # targeting another family overrides this with its own, which is the same division as the
    # configuration headers themselves: the kernel comes from the tree, the chip's numbers from the
    # product that runs on it.
    "${EDGE_CHIBIOS_ROOT}/os/common/ports/ARMCMx/devices/STM32F4xx"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_definitions(chibios_kernel PUBLIC EDGE_TARGET_ARM32=1)
if(NOT MSVC)
    target_compile_options(chibios_kernel PRIVATE -w)
endif()
