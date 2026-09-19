# QEMU RISC-V 32 (virt) board.
if(NOT EDGE_MODULE_BUILD_RISCV32_QEMU)
    return()
endif()

edge_add_board(riscv_virt SOURCES src/board.c DEPS edge_module)
