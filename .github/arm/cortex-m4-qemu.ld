ENTRY(_start)

/* QEMU MPS2 AN386 exposes the low-memory boot region through ZBT SSRAM1.
 * Keep the firmware linked at the emulated PSRAM address so absolute
 * references remain valid when QEMU loads the ELF. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x21000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
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
