@ The SNES's VRAM taken a 32-byte unit at a time (services/video.c,
@ vram_take_or_fill): ARM code in fast RAM that compares a unit's words
@ with what VRAM holds four at a time, by load-multiples, and stores them
@ only where they differ, answering which units changed. A send of bytes
@ VRAM already holds stores nothing and marks nothing; one that changes
@ them costs a unit's load and store rather than a byte's compare and a
@ copy. The video service's sends are much of a location's or a menu's
@ load (docs/PLATFORM.md, "Where the GBA's frame goes").

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

@ uint32_t vram_units_take(uint32_t *to (r0), const uint32_t *from (r1), uint32_t units (r2), uint32_t step (r3))
@ `units` (1 to 32) units into `to`, each stored where it differs from
@ `to`'s, from `from` on: its next 16 bytes for each half unit where `step`
@ is 16, the same 16 again and again (a fill) where it is 0. Bit k of the
@ answer is set where the kth unit changed.
    .global vram_units_take
    .type vram_units_take, %function
vram_units_take:
    push    {r4-r11, lr}
    mov     r12, r3
    mov     lr, #0
    mov     r3, #1
.Ltake_unit:
    ldmia   r1, {r4-r7}
    add     r1, r1, r12
    ldmia   r0, {r8-r11}
    cmp     r4, r8
    cmpeq   r5, r9
    cmpeq   r6, r10
    cmpeq   r7, r11
    stmiane r0, {r4-r7}
    orrne   lr, lr, r3
    add     r0, r0, #16
    ldmia   r1, {r4-r7}
    add     r1, r1, r12
    ldmia   r0, {r8-r11}
    cmp     r4, r8
    cmpeq   r5, r9
    cmpeq   r6, r10
    cmpeq   r7, r11
    stmiane r0, {r4-r7}
    orrne   lr, lr, r3
    add     r0, r0, #16
    mov     r3, r3, lsl #1
    subs    r2, r2, #1
    bne     .Ltake_unit
    mov     r0, lr
    pop     {r4-r11, lr}
    bx      lr
    .size vram_units_take, . - vram_units_take
