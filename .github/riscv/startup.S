/* RISC-V RV32 bare-metal startup for the QEMU `virt` machine.
 *
 * Entered in machine mode with an undefined stack, so the very first thing we
 * do is load `sp`; only then may any C code (or compiler prologue) run. */
    .section .text.start, "ax"
    .globl _start
    .type _start, @function
_start:
    la sp, _estack

    /* Zero .bss */
    la t0, _sbss
    la t1, _ebss
1:
    bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    /* Copy .data from its load address to its run address */
    la t0, _sdata
    la t1, _edata
    la t2, _sidata
3:
    bgeu t0, t1, 4f
    lw t3, 0(t2)
    sw t3, 0(t0)
    addi t0, t0, 4
    addi t2, t2, 4
    j 3b
4:
    call main
5:
    j 5b
    .size _start, . - _start
