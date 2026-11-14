/* The GBA build's memory (docs/PLATFORM.md, "The GBA build, as built").
 *
 * The cartridge (ROM, from 0x08000000) holds the code, the constants and
 * the initial values of the data; the packing step (targets/gba/host/pack.py)
 * appends the cartridge's resources and the movie after `__rom_end`. Work
 * RAM (EWRAM, 256 KiB at 0x02000000) holds the data and the zeroed state,
 * then the arena (port/arena.h) up to its end. Fast RAM (IWRAM, 32 KiB at
 * 0x03000000) holds the code that must be fast (the picture's line
 * interrupt), the fibers' stacks and the driver's own stack at its top.
 * Video RAM is the picture's (targets/gba/services/video.c). */
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
    .header : { KEEP(*(.header)) } > rom

    /* Fast RAM's code, copied there at the start: before the cartridge's
     * code, since a function goes to the first place that names it. */
    .iwram : {
        __iwram_start = .;
        *(.iwram .iwram.*)
        INCLUDE fast_code.ld
        . = ALIGN(4);
        __iwram_data_end = .;
    } > iwram AT > rom
    __iwram_load = LOADADDR(.iwram);

    .text : {
        *(.text .text.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
    } > rom

    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > rom

    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > rom

    /* Zeroed fast RAM: the fibers' stacks (targets/gba/system/fiber.c). */
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
