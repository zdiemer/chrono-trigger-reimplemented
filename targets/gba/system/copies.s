@ The C library's copy and fill (memcpy, memset) for the GBA, in place of
@ newlib's: ARM code in fast RAM that moves eight words with one
@ load-multiple and one store-multiple, where newlib's are Thumb a word at a
@ time. The game, the sends and the picture copy and fill all the time
@ (docs/PLATFORM.md, "The GBA build, as built").
@
@ Neither writes a byte where newlib's wrote a word, since video RAM takes no
@ byte writes: memcpy moves words where both ends share their alignment and
@ at least 8 bytes go (newlib's: both aligned and at least 16), memset where
@ at least 4 bytes are left once the start is aligned (as newlib's).

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

@ void *memcpy(void *to (r0), const void *from (r1), size_t n (r2))
    .global memcpy
    .type memcpy, %function
memcpy:
    mov     r12, r0
    cmp     r2, #8
    blo     .Lcopy_bytes
    eor     r3, r0, r1
    tst     r3, #3
    bne     .Lcopy_bytes
.Lcopy_align:
    tst     r0, #3
    beq     .Lcopy_words
    ldrb    r3, [r1], #1
    strb    r3, [r0], #1
    sub     r2, r2, #1
    b       .Lcopy_align
.Lcopy_words:
    push    {r4-r10}
.Lcopy_block:
    subs    r2, r2, #32
    ldmhs   r1!, {r3-r10}
    stmhs   r0!, {r3-r10}
    bhs     .Lcopy_block
    add     r2, r2, #32
    pop     {r4-r10}
.Lcopy_word:
    subs    r2, r2, #4
    ldrhs   r3, [r1], #4
    strhs   r3, [r0], #4
    bhs     .Lcopy_word
    add     r2, r2, #4
.Lcopy_bytes:
    subs    r2, r2, #1
    ldrbhs  r3, [r1], #1
    strbhs  r3, [r0], #1
    bhs     .Lcopy_bytes
    mov     r0, r12
    bx      lr
    .size memcpy, . - memcpy

@ void *memset(void *to (r0), int value (r1), size_t n (r2))
    .global memset
    .type memset, %function
memset:
    mov     r12, r0
    and     r1, r1, #0xFF
.Lset_align:
    tst     r0, #3
    beq     .Lset_aligned
    subs    r2, r2, #1
    blo     .Lset_done
    strb    r1, [r0], #1
    b       .Lset_align
.Lset_aligned:
    cmp     r2, #4
    blo     .Lset_bytes
    orr     r1, r1, r1, lsl #8
    orr     r1, r1, r1, lsl #16
    push    {r4-r9}
    mov     r3, r1
    mov     r4, r1
    mov     r5, r1
    mov     r6, r1
    mov     r7, r1
    mov     r8, r1
    mov     r9, r1
.Lset_block:
    subs    r2, r2, #32
    stmhs   r0!, {r1, r3-r9}
    bhs     .Lset_block
    add     r2, r2, #32
    pop     {r4-r9}
.Lset_word:
    subs    r2, r2, #4
    strhs   r1, [r0], #4
    bhs     .Lset_word
    add     r2, r2, #4
.Lset_bytes:
    subs    r2, r2, #1
    strbhs  r1, [r0], #1
    bhs     .Lset_bytes
.Lset_done:
    mov     r0, r12
    bx      lr
    .size memset, . - memset

@ Runs into and out of work RAM's top in the cartridge's SRAM
@ (runtime/port/vars_host.h's vars_host_cold_write and vars_host_cold_read),
@ which takes a byte at a time: the ordinary memory's end read or written a
@ word at a time once aligned, four of the SRAM's bytes to each, where a
@ byte loop in C spent as long again on itself as on the SRAM. A world
@ map's load sends its graphics through the SRAM, about 20 KiB each way.

@ void vars_host_cold_write(volatile uint8_t *to (r0), const uint8_t *from (r1), uint32_t n (r2))
    .global vars_host_cold_write
    .type vars_host_cold_write, %function
vars_host_cold_write:
    cmp     r2, #0
    bxeq    lr
    tst     r1, #3
    ldrbne  r3, [r1], #1
    strbne  r3, [r0], #1
    subne   r2, r2, #1
    bne     vars_host_cold_write
    subs    r2, r2, #4
    blo     .Lcold_write_bytes
.Lcold_write_word:
    ldr     r3, [r1], #4
    strb    r3, [r0], #1
    lsr     r3, r3, #8
    strb    r3, [r0], #1
    lsr     r3, r3, #8
    strb    r3, [r0], #1
    lsr     r3, r3, #8
    strb    r3, [r0], #1
    subs    r2, r2, #4
    bhs     .Lcold_write_word
.Lcold_write_bytes:
    adds    r2, r2, #4
    bxeq    lr
.Lcold_write_byte:
    ldrb    r3, [r1], #1
    strb    r3, [r0], #1
    subs    r2, r2, #1
    bne     .Lcold_write_byte
    bx      lr
    .size vars_host_cold_write, . - vars_host_cold_write

@ void vars_host_cold_read(uint8_t *to (r0), const volatile uint8_t *from (r1), uint32_t n (r2))
    .global vars_host_cold_read
    .type vars_host_cold_read, %function
vars_host_cold_read:
    cmp     r2, #0
    bxeq    lr
    tst     r0, #3
    ldrbne  r3, [r1], #1
    strbne  r3, [r0], #1
    subne   r2, r2, #1
    bne     vars_host_cold_read
    subs    r2, r2, #4
    blo     .Lcold_read_bytes
.Lcold_read_word:
    ldrb    r3, [r1], #1
    ldrb    r12, [r1], #1
    orr     r3, r3, r12, lsl #8
    ldrb    r12, [r1], #1
    orr     r3, r3, r12, lsl #16
    ldrb    r12, [r1], #1
    orr     r3, r3, r12, lsl #24
    str     r3, [r0], #4
    subs    r2, r2, #4
    bhs     .Lcold_read_word
.Lcold_read_bytes:
    adds    r2, r2, #4
    bxeq    lr
.Lcold_read_byte:
    ldrb    r3, [r1], #1
    strb    r3, [r0], #1
    subs    r2, r2, #1
    bne     .Lcold_read_byte
    bx      lr
    .size vars_host_cold_read, . - vars_host_cold_read
