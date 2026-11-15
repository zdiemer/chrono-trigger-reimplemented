@ The mixer's loops (services/mixer.c), ARM in fast RAM.
@
@ mix_wave adds a voice's wave into the chunk's mix, four samples a turn
@ while the wave's end is further off than four steps: the four read from
@ the cartridge, the mix's four words loaded and stored with one
@ load-multiple and one store-multiple, each added with one
@ multiply-accumulate whose second operand is the sample, so the multiply
@ stops after its first eight bits. Then a sample at a time, looping or
@ ending the wave where it reaches its end. What it adds is what a sample at
@ a time adds.
@
@ to_8_bits turns the mix into each side's 8 bits, four samples to a word
@ stored into the sound's ring, rather than a byte at a time into work RAM.

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

@ bool mix_wave(int32_t *mix (r0), unsigned count (r1), const MixerWave *w (r2),
@               uint32_t *position (r3), uint32_t step, int32_t gains)
@ `position` and `step` in 4096ths of a sample; false where the wave ended
@ (and `position` is left as it was).
    .global mix_wave
    .type mix_wave, %function
mix_wave:
    push    {r4-r11, lr}
    ldr     r4, [sp, #36]           @ step
    ldr     r5, [sp, #40]           @ gains
    ldm     r2, {r2, r6, r7}        @ pcm, length, loop (MixerWave)
    mov     r6, r6, lsl #12         @ the end, in 4096ths
    cmn     r7, #1                  @ MIXER_NO_LOOP: the wave ends there
    moveq   r7, #0
    subne   r7, r6, r7, lsl #12     @ else what it goes back by to loop
    push    {r3, r7}                @ [sp]: where the position goes, [sp, #4]: back
    ldr     r3, [r3]                @ at
.Lnext:
    cmp     r1, #4
    addhs   r11, r3, r4, lsl #2
    cmphs   r6, r11
    bls     .Lone
.Lfour:
    mov     r11, r3, lsr #12
    ldrsb   r11, [r2, r11]
    add     r3, r3, r4
    mov     r12, r3, lsr #12
    ldrsb   r12, [r2, r12]
    add     r3, r3, r4
    mov     lr, r3, lsr #12
    ldrsb   lr, [r2, lr]
    add     r3, r3, r4
    ldm     r0, {r7-r10}
    mla     r7, r5, r11, r7
    mla     r8, r5, r12, r8
    mla     r9, r5, lr, r9
    mov     r11, r3, lsr #12
    ldrsb   r11, [r2, r11]
    add     r3, r3, r4
    mla     r10, r5, r11, r10
    stmia   r0!, {r7-r10}
    sub     r1, r1, #4
    cmp     r1, #4
    addhs   r11, r3, r4, lsl #2
    cmphs   r6, r11
    bhi     .Lfour
.Lone:
    cmp     r1, #0
    beq     .Ldone
    mov     r11, r3, lsr #12
    ldrsb   r11, [r2, r11]
    ldr     r12, [r0]
    mla     r12, r5, r11, r12
    str     r12, [r0], #4
    sub     r1, r1, #1
    add     r3, r3, r4
    cmp     r3, r6
    blo     .Lnext
    ldr     r7, [sp, #4]
    cmp     r7, #0
    beq     .Lended
.Lback:
    sub     r3, r3, r7
    cmp     r3, r6
    bhs     .Lback
    b       .Lnext
.Ldone:
    pop     {r12}
    str     r3, [r12]
    add     sp, sp, #4
    mov     r0, #1
    pop     {r4-r11, lr}
    bx      lr
.Lended:
    add     sp, sp, #8
    mov     r0, #0
    pop     {r4-r11, lr}
    bx      lr
    .size mix_wave, . - mix_wave

@ void to_8_bits(const int32_t *mix (r0), int8_t *left (r1), int8_t *right (r2), unsigned count (r3))
@ Each word of the mix holds both sides (mixer.c's gains_of): the bottom
@ half is the right side's sum, what is left above it the left's. Each sum
@ over 2^7, clipped to 8 bits: in range where its bits above the eighth are
@ all its sign, else the end of the range on its side. `left` and `right`
@ are word-aligned and `count` a multiple of four, not 0.
    .global to_8_bits
    .type to_8_bits, %function
to_8_bits:
    push    {r4-r10, lr}
    mov     r10, #0x7F
.Lword:
    mov     lr, #4
.Lsample:
    ldr     r4, [r0], #4
    mov     r5, r4, lsl #16
    sub     r6, r4, r5, asr #16     @ the left's sum, in the top half
    mov     r5, r5, asr #23         @ the right's over 2^7
    mov     r6, r6, asr #23         @ the left's
    mov     r7, r5, asr #7
    teq     r7, r5, asr #31
    eorne   r5, r10, r5, asr #31    @ 127, or -128 where negative
    mov     r7, r6, asr #7
    teq     r7, r6, asr #31
    eorne   r6, r10, r6, asr #31
    mov     r8, r8, lsr #8          @ the word's samples, the first at the bottom
    orr     r8, r8, r6, lsl #24
    mov     r9, r9, lsr #8
    orr     r9, r9, r5, lsl #24
    subs    lr, lr, #1
    bne     .Lsample
    str     r8, [r1], #4
    str     r9, [r2], #4
    subs    r3, r3, #4
    bhi     .Lword
    pop     {r4-r10, lr}
    bx      lr
    .size to_8_bits, . - to_8_bits
