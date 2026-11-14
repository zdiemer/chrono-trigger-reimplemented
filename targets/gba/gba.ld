/* The GBA build's memory (docs/PLATFORM.md, "The GBA build, as built").
 *
 * The cartridge (ROM, from 0x08000000) holds the code, the constants and
 * the initial values of the data; the packing step (targets/gba/pack.py)
 * appends the cartridge's resources and the movie after `__rom_end`. Work
 * RAM (EWRAM, 256 KiB at 0x02000000) holds the data and the zeroed state,
 * then the arena (port/arena.h) up to its end. Fast RAM (IWRAM, 32 KiB at
 * 0x03000000) holds the driver's own stack at its top. Video RAM holds the
 * game's second bank of work RAM (targets/gba/memory.c), as no picture is
 * drawn. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
    rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
    ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
    iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}

__ewram_end = ORIGIN(ewram) + LENGTH(ewram);
__iwram_end = ORIGIN(iwram) + LENGTH(iwram);
/* The driver's stack: the top of fast RAM, below the BIOS's own 256 bytes. */
__stack_top = __iwram_end - 0x100;

SECTIONS
{
    .text : {
        KEEP(*(.header))
        *(.text .text.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
    } > rom

    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > rom

    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > rom

    .iwram : {
        __iwram_start = .;
        *(.iwram .iwram.*)
        . = ALIGN(4);
        __iwram_data_end = .;
    } > iwram AT > rom
    __iwram_load = LOADADDR(.iwram);

    /* Zeroed fast RAM: the fibers' stacks (targets/gba/fiber.c). */
    .iwram_bss (NOLOAD) : {
        __iwram_bss_start = .;
        *(.iwram_bss .iwram_bss.*)
        . = ALIGN(8);
        __iwram_bss_end = .;
    } > iwram
    ASSERT(__iwram_bss_end <= __stack_top - 0x1800, "fast RAM leaves the driver's stack under 6 KiB")

    .data : {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > ewram AT > rom
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ewram

    /* What the arena hands out, to the end of work RAM. */
    __arena_start = .;

    __rom_end = LOADADDR(.data) + SIZEOF(.data);

    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
