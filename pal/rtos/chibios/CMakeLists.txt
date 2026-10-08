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

# The HAL, in the shape the reference's own build asks for it (make/fw.mk): its core sources, its
# OSAL for the RT port, and the STM32F4xx platform. The low-level drivers are the one place a glob is
# right rather than a list: they are grouped per peripheral and version, each file is guarded by the
# family macro it belongs to, and a file for another family compiles to nothing rather than conflicting.
# The reference selects them through ChibiOS's make system, which is machinery this target does not
# need to reproduce when the preprocessor already answers the same question.
file(GLOB EDGE_CHIBIOS_HAL_SOURCES
    "${EDGE_CHIBIOS_ROOT}/os/hal/src/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/osal/rt/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/STM32F4xx/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/common/ARMCMx/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/*.c"
    # The variants, listed rather than globbed and the same eight the includes name: a variant directory
    # holds other families' files too, guarded by a family macro that is not what decides this build, so
    # globbing them compiles the wrong family's code and fails on its registers.
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/DACv1/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/GPIOv2/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/I2Cv1/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/OTGv1/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/RTCv2/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/SPIv1/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/TIMv1/*.c"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/USARTv1/*.c"
)
file(GLOB EDGE_CHIBIOS_CMSIS_SOURCES
    "${EDGE_CHIBIOS_ROOT}/os/ext/CMSIS/ST/system_stm32f4xx.c"
)
set(EDGE_CHIBIOS_PORT_SOURCES
    # The port is chosen, not globbed: the ARMCMx directory holds both the ARMv6-M and the ARMv7-M one,
    # they define the same port entry points, and only one of them can be compiled. This is a Cortex-M4,
    # which is ARMv7-M.
    "${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx/chcore.c"
    "${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx/chcore_v7m.c"
    "${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.s"
)

# Third-party sources, so deliberately outside edge_enable_quality(): warnings are off for this target
# alone, and the port's own files are the ones the repository wrote.
#
# The port's context-switch assembly is a lower-case .s file, which the assembler does not preprocess -
# and it must be preprocessed, because its floating-point save is behind a CORTEX_USE_FPU guard that the
# build is expected to answer. ChibiOS's own rules pass it through the preprocessor; here that is said
# per source file, since the flag belongs to this one rather than to the target.
set_source_files_properties("${EDGE_CHIBIOS_ROOT}/os/rt/ports/ARMCMx/compilers/GCC/chcoreasm_v7m.s"
    PROPERTIES COMPILE_OPTIONS "-x;assembler-with-cpp")

add_library(chibios_kernel STATIC EXCLUDE_FROM_ALL
              ${EDGE_CHIBIOS_RT_SOURCES} ${EDGE_CHIBIOS_PORT_SOURCES}
              ${EDGE_CHIBIOS_HAL_SOURCES} ${EDGE_CHIBIOS_CMSIS_SOURCES})
#
# EXCLUDE_FROM_ALL because the kernel is not yet the whole of what a ChibiOS firmware needs: its device
# parameters reach for board.h, and that board header is the HAL's, so the kernel, its port and their
# sources build as a library and stop there. Bringing the HAL in is the piece after this one; until then
# being out of the default build keeps the claim honest, which is that this target builds and no firmware
# does yet.
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
    # The CMSIS device headers ChibiOS carries: the STM32F4's own, which the device parameters include,
    # and the Cortex-M core's beside them.
    "${EDGE_CHIBIOS_ROOT}/os/ext/CMSIS/ST"
    "${EDGE_CHIBIOS_ROOT}/os/ext/CMSIS/include"
    "${EDGE_CHIBIOS_ROOT}/os/hal/include"
    "${EDGE_CHIBIOS_ROOT}/os/hal/osal/rt"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/STM32F4xx"
    # The low-level driver variants this family uses, read off ChibiOS's own platform.mk for it rather
    # than taken wholesale: several variants share a header name, and the first one on the include path
    # would answer for all of them.
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/DACv1"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/GPIOv2"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/I2Cv1"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/OTGv1"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/RTCv2"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/SPIv1"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/TIMv1"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32/LLD/USARTv1"
    # The HAL's shared port directories: the ARM Cortex-M one holds nvic.h, which the STM32 platform
    # includes, and the STM32 one holds the family-agnostic low-level headers beside the family's own.
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/common"
    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/common/ARMCMx"    "${EDGE_CHIBIOS_ROOT}/os/hal/ports/STM32"
    # The two the reference's own include line names beside the ones above (make/fw.mk:188).
    "${EDGE_CHIBIOS_ROOT}/os/hal/lib/streams"
    "${EDGE_CHIBIOS_ROOT}/os/various"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    # The five configuration headers are the reference's own, and are kept here rather than in include
    # for the same reason the trees beside them are: they are not this repository's code. They are the
    # kernel's, the HAL's and the chip's answer for this board, taken verbatim, and a path with a vendor
    # component is what the formatting gate already leaves alone - which one such header needs, its
    # hook bodies being inside macros a formatter cannot reach.
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
)
target_compile_definitions(chibios_kernel PUBLIC EDGE_TARGET_ARM32=1)
if(NOT MSVC)
    target_compile_options(chibios_kernel PRIVATE -w)
endif()

# The smallest firmware that runs on the kernel above, with the start-up and linker script this
# repository already uses for its own ARM firmware: a main that brings the hardware and the kernel up and
# creates one thread. Out of the default build for the same reason the kernel is - nothing depends on
# ChibiOS yet, and this is the evidence for the claim that it starts, not a product.
add_executable(chibios_smoke_fw EXCLUDE_FROM_ALL
    firmware/main.c
    # The board's own two symbols, from the reference's hwconf/board.c: boardInit and pal_default_config,
    # which the HAL's own initialisation calls and which nothing else can supply.
    vendor/board.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../.github/arm/startup.c"
)
target_link_libraries(chibios_smoke_fw PRIVATE chibios_kernel)
target_link_options(chibios_smoke_fw PRIVATE
    # The reference's own linker script, which is the one its build uses: it carries the kernel's stack
    # and heap symbols, and it knows where this board's memory is.
    -T${CMAKE_CURRENT_SOURCE_DIR}/vendor/ld_eeprom_emu.ld
    -nostartfiles -Wl,--gc-sections
)
target_compile_definitions(chibios_smoke_fw PRIVATE EDGE_TARGET_ARM32=1)
# The board file wants a board and a firmware name, and it reaches for the board's own header through a
# macro. The reference's answer for a board that is not a particular product is its example one
# (hwconf/example), whose header carries both names - so what this build passes is which board header to
# include, and not what to call it.
target_compile_definitions(chibios_smoke_fw PRIVATE HW_HEADER="hw_example_core.h")
target_include_directories(chibios_smoke_fw PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/example")
# The board file is the reference's, and its hardware header reaches for the STM32 standard peripheral
# library that ChibiOS carries for exactly this reason. Headers first: what a link still wants after this
# will say whether the library's sources are needed too.
target_include_directories(chibios_smoke_fw PRIVATE
    "${EDGE_CHIBIOS_ROOT}/ext/stdperiph_stm32f4"
    "${EDGE_CHIBIOS_ROOT}/ext/stdperiph_stm32f4/inc"
)
