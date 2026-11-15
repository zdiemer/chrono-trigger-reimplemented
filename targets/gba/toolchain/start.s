@ The cartridge's first bytes and the processor's start (targets/gba/toolchain/gba.ld).
@
@ The header's fields are left for the packing step (targets/gba/host/pack.py) to
@ fill: the title, the game code and the complement check, and the options
@ word after it, where `pack.py --fast-rom` asks for the cartridge's faster
@ waits. The start sets the waits, puts the
@ driver's stack at the top of fast RAM, copies the data's initial values out
@ of the cartridge, zeroes the rest and calls main, in Thumb, which never
@ returns.
    .section .header, "ax"
    .arm
    .global _start
_start:
    b       start
    .fill   156, 1, 0          @ the logo the BIOS checks; mGBA's own BIOS does not
    .fill   12, 1, 0           @ title
    .fill   4, 1, 0            @ game code
    .byte   0x30, 0x31         @ maker
    .byte   0x96               @ fixed
    .byte   0x00               @ unit
    .byte   0x00               @ device
    .fill   7, 1, 0
    .byte   0x00               @ version
    .byte   0x00               @ complement check, the packing step's
    .fill   2, 1, 0
options:
    .word   0                  @ the packing step's: bit 0, the faster waits (pack.py's OPTIONS_AT, FAST_ROM)

start:
    mov     r0, #0x1F          @ system mode, interrupts off
    msr     cpsr_c, r0
    ldr     sp, =__stack_top
    ldr     r0, =0x04000204    @ the cartridge's waits: 3 then 1, with the prefetch, as commercial games set them,
    ldr     r1, =0x4314        @ and the SRAM's 4, the GBA's own from its start (work_ram.c keeps pages there)
    ldr     r2, options        @ or 2 then 1, which flash carts and the Analogue Pocket take and some others don't
    tst     r2, #1
    ldrne   r1, =0x4318
    strh    r1, [r0]

    ldr     r0, =__iwram_load  @ the fast RAM's code and data
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_data_end
    bl      copy
    ldr     r0, =__data_load   @ the data
    ldr     r1, =__data_start
    ldr     r2, =__data_end
    bl      copy
    ldr     r1, =__bss_start   @ the zeroed state
    ldr     r2, =__bss_end
    bl      zero
    ldr     r1, =__iwram_bss_start   @ and the fast RAM's, the driver's stack too (main.c reports its deepest use)
    ldr     r2, =__stack_top
    bl      zero

    ldr     r0, =main
    mov     lr, pc
    bx      r0
2:  b       2b

@ Words from r0 to r1 up to r2.
copy:
    cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     copy
    bx      lr

@ Words from r1 up to r2 set to zero.
zero:
    mov     r3, #0
1:  cmp     r1, r2
    strlo   r3, [r1], #4
    blo     1b
    bx      lr
    .pool
