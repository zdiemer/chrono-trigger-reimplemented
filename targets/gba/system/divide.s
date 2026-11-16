@ The C library's division for the GBA (the run-time ABI's __aeabi_uidiv,
@ __aeabi_uidivmod, __aeabi_idiv and __aeabi_idivmod), in place of libgcc's:
@ ARM code in fast RAM. The ARM7TDMI has no divide instruction, and Thumb
@ has no multiply that keeps a product's high word either, so every / and %
@ the Thumb code makes, by a constant too, calls one of these; libgcc's are
@ Thumb, the signed pair in the cartridge (docs/PLATFORM.md, "The GBA build,
@ as built").
@
@ The quotient is worked out four bits a turn with ARM's conditional
@ execution, the divisor first moved up under the dividend four bits and
@ then one bit at a time, as libgcc's ARM build does; the remainder is the
@ dividend less the quotient times the divisor. The signed pair divides the
@ two sizes and gives the results their signs. A division by zero answers
@ as libgcc's does: unsigned, all ones for a dividend other than 0; signed,
@ the largest value of the dividend's sign; the remainder 0.

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

@ unsigned r0 / r1 into r0, and r0 % r1 into r1
    .global __aeabi_uidiv, __aeabi_uidivmod, __udivsi3
    .type __aeabi_uidiv, %function
    .type __aeabi_uidivmod, %function
    .type __udivsi3, %function
__aeabi_uidiv:
__aeabi_uidivmod:
__udivsi3:
    subs    r2, r1, #1
    beq     .Lby_one
    bcc     .Lby_zero
    cmp     r0, r1
    bls     .Lat_most
    push    {r0, r1}
    mov     r3, #1
1:  cmp     r1, #0x10000000
    cmplo   r1, r0
    movlo   r1, r1, lsl #4
    movlo   r3, r3, lsl #4
    blo     1b
1:  cmp     r1, #0x80000000
    cmplo   r1, r0
    movlo   r1, r1, lsl #1
    movlo   r3, r3, lsl #1
    blo     1b
    mov     r2, #0
1:  cmp     r0, r1
    subhs   r0, r0, r1
    orrhs   r2, r2, r3
    cmp     r0, r1, lsr #1
    subhs   r0, r0, r1, lsr #1
    orrhs   r2, r2, r3, lsr #1
    cmp     r0, r1, lsr #2
    subhs   r0, r0, r1, lsr #2
    orrhs   r2, r2, r3, lsr #2
    cmp     r0, r1, lsr #3
    subhs   r0, r0, r1, lsr #3
    orrhs   r2, r2, r3, lsr #3
    cmp     r0, #0
    movsne  r3, r3, lsr #4
    movne   r1, r1, lsr #4
    bne     1b
    mov     r0, r2
    pop     {r2, r3}
    mul     r1, r0, r3
    sub     r1, r2, r1
    bx      lr
.Lby_one:
    mov     r1, #0
    bx      lr
.Lat_most:
    moveq   r0, #1
    moveq   r1, #0
    movne   r1, r0
    movne   r0, #0
    bx      lr
.Lby_zero:
    cmp     r0, #0
    mvnne   r0, #0
    bx      lr
    .size __aeabi_uidiv, . - __aeabi_uidiv
    .size __aeabi_uidivmod, . - __aeabi_uidivmod
    .size __udivsi3, . - __udivsi3

@ signed r0 / r1 into r0, toward zero, and the remainder, of r0's sign, into r1
    .global __aeabi_idiv, __aeabi_idivmod, __divsi3
    .type __aeabi_idiv, %function
    .type __aeabi_idivmod, %function
    .type __divsi3, %function
__aeabi_idiv:
__aeabi_idivmod:
__divsi3:
    cmp     r1, #0
    beq     .Lsigned_by_zero
    eor     r12, r0, r1
    and     r12, r12, #0x80000000
    orr     r12, r12, r0, lsr #31      @ the quotient's sign in bit 31, the dividend's in bit 0
    cmp     r0, #0
    rsblt   r0, r0, #0
    cmp     r1, #0
    rsblt   r1, r1, #0
    push    {r12, lr}
    bl      __aeabi_uidivmod
    pop     {r12, lr}
    tst     r12, #1
    rsbne   r1, r1, #0
    tst     r12, #0x80000000
    rsbne   r0, r0, #0
    bx      lr
.Lsigned_by_zero:
    cmp     r0, #0
    mvngt   r0, #0x80000000
    movlt   r0, #0x80000000
    bx      lr
    .size __aeabi_idiv, . - __aeabi_idiv
    .size __aeabi_idivmod, . - __aeabi_idivmod
    .size __divsi3, . - __divsi3
