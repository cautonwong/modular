# ARM MPS2 (AN385/AN386) board: timer tick, IRQ routing, SoC bring-up.
edge_add_board(mps2 SOURCES src/board.c DEPS edge_module soc_mps2 pal_cortex_m_bare)

# D87: the FreeRTOS kernel and the PAL are compiled against this board's
# configuration, which reads the SoC config header through the product's
# FreeRTOSConfig.h. edge_add_arm_freertos_firmware() copies this target's own
# PUBLIC include roots into both, so the SoC root has to be one of them, not
# only part of the link closure.
target_include_directories(board_mps2 PUBLIC ${EDGE_ROOT}/soc/mps2/include)

if(EDGE_MODULE_ARM_QEMU_SEMIHOSTING OR EDGE_MODULE_BUILD_FREERTOS_QEMU)
    target_compile_definitions(board_mps2 PUBLIC EDGE_QEMU_SEMIHOSTING=1)
endif()
