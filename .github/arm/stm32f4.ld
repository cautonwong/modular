ENTRY(_start)

/*
 * STM32F405/407 as the VESC6 uses it: 1 MB of flash at 0x08000000, 128 KB of SRAM at
 * 0x20000000. The part also has 64 KB of CCM at 0x10000000, which nothing here places anything
 * in yet - it is not DMA-reachable, so it is the wrong home for buffers that a driver will want
 * to hand to the ADC or the timer, and the right one only for a stack or scratch area that has a
 * reason to be there. That reason arrives with the drivers (D2); until then the region is
 * declared in soc_stm32f4's header and unused here, rather than mapped and left empty.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_start_thumb = _start + 1;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data : AT(_etext)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4);
    } > RAM

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }

    _end = .;
}

ASSERT(SIZEOF(.text) + SIZEOF(.isr_vector) + SIZEOF(.data) <= LENGTH(FLASH), "FLASH overflow")
ASSERT(SIZEOF(.data) + SIZEOF(.bss) + SIZEOF(.noinit) <= LENGTH(RAM), "RAM overflow")
