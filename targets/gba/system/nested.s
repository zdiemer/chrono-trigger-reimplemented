@ Work an interrupt hands on with the other interrupts on (system/interrupts.h):
@ the sound's mixing, which a frame's line interrupts mustn't wait behind.
@ ARM, in fast RAM, as the interrupts are.
    .section .iwram, "ax", %progbits
    .arm
    .align 2

@ r0: the function, called in system mode with interrupts on, on the nested
@ stack (r1: its top). The interrupt's return and its saved status stay on
@ the interrupts' stack, which an interrupt taken meanwhile writes over in
@ the registers; the interrupted code's stack pointer and return address are
@ kept on the nested stack and put back.
    .global gba_run_nested
    .type gba_run_nested, %function
gba_run_nested:
    mrs     r2, spsr
    stmfd   sp!, {r2, lr}
    mrs     r3, cpsr
    bic     r3, r3, #0x9F
    orr     r3, r3, #0x1F          @ system mode, interrupts on
    msr     cpsr_c, r3
    mov     r2, sp
    mov     sp, r1
    stmfd   sp!, {r2, lr}
    mov     lr, pc
    bx      r0
    ldmfd   sp!, {r2, lr}
    mov     sp, r2
    mrs     r3, cpsr
    bic     r3, r3, #0x9F
    orr     r3, r3, #0x92          @ interrupt mode, interrupts off
    msr     cpsr_c, r3
    ldmfd   sp!, {r2, lr}
    msr     spsr_fsxc, r2
    bx      lr

@ r0: the interrupts' stack's top, from the BIOS's 160 bytes to room of the
@ program's, which a nested interrupt needs.
    .global gba_interrupts_stack
    .type gba_interrupts_stack, %function
gba_interrupts_stack:
    mrs     r1, cpsr
    bic     r2, r1, #0x1F
    orr     r2, r2, #0x92          @ interrupt mode, interrupts off
    msr     cpsr_c, r2
    mov     sp, r0
    msr     cpsr_c, r1
    bx      lr
