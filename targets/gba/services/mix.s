@ The mixer's loops (services/mixer.c), ARM in fast RAM.
@
@ The PCM is offset by 128 (host/samples.py: 0-255, its silence 128), so a
@ sample is read with the unsigned byte load that takes the position's
@ whole part as its index, with no shift of its own. Each voice's mix then
@ holds 128 times its gains more than it says, which to_8_bits takes off with
@ the chunk's offset (mixer.c).
@
@ mix_wave adds a voice's wave into the chunk's mix, four samples a turn
@ while its end is further off than four steps: the four read from the
@ cartridge, the mix's four words loaded and stored with one load-multiple
@ and one store-multiple, each added with one multiply-accumulate whose
@ second operand is the sample, so the multiply stops after its first eight
@ bits. Then a sample at a time, looping or ending the wave where it reaches
@ its end. What it adds is what a sample at a time adds.
@
@ mix_pair adds two voices at once, where neither wave ends in the chunk,
@ so that the mix's words are loaded and stored once for the two.
@
@ to_8_bits turns the mix into each side's 8 bits, four samples to a word
@ stored into the sound's ring. Where none of the four is clipped, as
@ nearly every four are, each side's bytes are masked out of the words and
@ put together without a sample's tests.

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

@ bool mix_wave(int32_t *mix (r0), unsigned count (r1), const MixerWave *w (r2),
@               uint32_t *position (r3), uint32_t step, int32_t gains)
@ `position` and `step` in 4096ths of a sample; false where the wave ended
@ (and `position` is left as it was), the rest of the chunk given its
@ silence (128 times `gains`) so that the chunk's offset still holds.
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
    add     r12, r4, r4, lsl #1
    subs    lr, r6, r12             @ four samples start before this, or none
    movlo   lr, #0
    push    {r3, r7}                @ [sp]: where the position goes, [sp, #4]: back
    ldr     r3, [r3]                @ at
.Lnext:
    cmp     r1, #4
    cmphs   lr, r3
    bls     .Lone
.Lfour:
    ldm     r0, {r7-r10}
    ldrb    r11, [r2, r3, lsr #12]
    add     r3, r3, r4
    ldrb    r12, [r2, r3, lsr #12]
    add     r3, r3, r4
    mla     r7, r5, r11, r7
    ldrb    r11, [r2, r3, lsr #12]
    add     r3, r3, r4
    mla     r8, r5, r12, r8
    ldrb    r12, [r2, r3, lsr #12]
    add     r3, r3, r4
    mla     r9, r5, r11, r9
    mla     r10, r5, r12, r10
    stmia   r0!, {r7-r10}
    sub     r1, r1, #4
    cmp     r1, #4
    cmphs   lr, r3
    bhi     .Lfour
.Lone:
    cmp     r1, #0
    beq     .Ldone
    ldrb    r11, [r2, r3, lsr #12]
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
    mov     r5, r5, lsl #7          @ the silence, for what is left of the chunk
    subs    r1, r1, #1
    bmi     .Lsilent
.Lsilence:
    ldr     r12, [r0]
    add     r12, r12, r5
    str     r12, [r0], #4
    subs    r1, r1, #1
    bpl     .Lsilence
.Lsilent:
    add     sp, sp, #8
    mov     r0, #0
    pop     {r4-r11, lr}
    bx      lr
    .size mix_wave, . - mix_wave

@ void mix_pair(int32_t *mix (r0), unsigned count (r1), const MixerPair *pair (r2))
@ Two voices' waves added into the mix together, three samples a turn, so
@ that the mix's words are loaded and stored once for both: neither wave
@ reaches its end in `count` samples (mixer.c sees to it, and moves the
@ positions on itself). MixerPair: each voice's PCM, position and step (in
@ 4096ths of a sample) and gains.
    .global mix_pair
    .type mix_pair, %function
mix_pair:
    push    {r4-r11, lr}
    ldm     r2, {r2-r9}             @ the first voice in r2-r5, the second in r6-r9
    subs    r1, r1, #3
    bmi     .Lpair_rest
.Lpair_three:
    ldm     r0, {r10-r12}
    ldrb    lr, [r2, r3, lsr #12]
    add     r3, r3, r4
    mla     r10, r5, lr, r10
    ldrb    lr, [r6, r7, lsr #12]
    add     r7, r7, r8
    mla     r10, r9, lr, r10
    ldrb    lr, [r2, r3, lsr #12]
    add     r3, r3, r4
    mla     r11, r5, lr, r11
    ldrb    lr, [r6, r7, lsr #12]
    add     r7, r7, r8
    mla     r11, r9, lr, r11
    ldrb    lr, [r2, r3, lsr #12]
    add     r3, r3, r4
    mla     r12, r5, lr, r12
    ldrb    lr, [r6, r7, lsr #12]
    add     r7, r7, r8
    mla     r12, r9, lr, r12
    stmia   r0!, {r10-r12}
    subs    r1, r1, #3
    bpl     .Lpair_three
.Lpair_rest:
    adds    r1, r1, #3              @ the last one or two
    beq     .Lpair_done
.Lpair_one:
    ldr     r10, [r0]
    ldrb    lr, [r2, r3, lsr #12]
    add     r3, r3, r4
    mla     r10, r5, lr, r10
    ldrb    lr, [r6, r7, lsr #12]
    add     r7, r7, r8
    mla     r10, r9, lr, r10
    str     r10, [r0], #4
    subs    r1, r1, #1
    bne     .Lpair_one
.Lpair_done:
    pop     {r4-r11, lr}
    bx      lr
    .size mix_pair, . - mix_pair

@ void mix_start(int32_t *mix (r0), int32_t offset (r1), unsigned count (r2))
@ The chunk's mix set to its offset, eight words to a store.
    .global mix_start
    .type mix_start, %function
mix_start:
    push    {r4-r8}
    mov     r3, r1
    mov     r4, r1
    mov     r5, r1
    mov     r6, r1
    mov     r7, r1
    mov     r8, r1
    mov     r12, r1
.Lstart_eight:
    stmia   r0!, {r1, r3-r8, r12}
    subs    r2, r2, #8
    bhi     .Lstart_eight
    pop     {r4-r8}
    bx      lr
    .size mix_start, . - mix_start

@ void to_8_bits(const int32_t *mix (r0), int8_t *left (r1), int8_t *right (r2), unsigned count (r3))
@ Each word of the mix holds both sides (mixer.c's gains_of): the bottom
@ half is the right side's sum, what is left above it the left's. With the
@ chunk's offset it started from, each side's sum is 0x4000 more (mixer.c's MIX_ZERO): one
@ in 8 bits' range over 2^7 is then 0 to 0x7FFF, its top bit clear, and its
@ byte bits 7-14 of its half with the top bit turned over. Otherwise it is
@ clipped, each sum to the end of the range on its side. `left` and `right`
@ are word-aligned and `count` a multiple of four, not 0.
    .global to_8_bits
    .type to_8_bits, %function
to_8_bits:
    push    {r4-r11, lr}
    ldr     r12, =0x7F80            @ a side's byte, in its half
    mov     lr, #0x80
    orr     lr, lr, lr, lsl #8
    orr     lr, lr, lr, lsl #16     @ each byte's top bit
.Lword:
    ldm     r0!, {r4-r7}
    orr     r10, r4, r5
    orr     r10, r10, r6
    orr     r10, r10, r7
    tst     r10, #0x8000            @ either side's top bit
    tsteq   r10, #0x80000000
    bne     .Lclipped
    and     r9, r12, r4             @ the right's bytes, the first at the bottom
    mov     r9, r9, lsr #7
    and     r10, r12, r5
    orr     r9, r9, r10, lsl #1
    and     r10, r12, r6
    orr     r9, r9, r10, lsl #9
    and     r10, r12, r7
    orr     r9, r9, r10, lsl #17
    and     r8, r4, r12, lsl #16    @ the left's
    mov     r8, r8, lsr #23
    and     r10, r5, r12, lsl #16
    orr     r8, r8, r10, lsr #15
    and     r10, r6, r12, lsl #16
    orr     r8, r8, r10, lsr #7
    and     r10, r7, r12, lsl #16
    orr     r8, r8, r10, lsl #1
.Lstore:
    eor     r8, r8, lr
    eor     r9, r9, lr
    str     r8, [r1], #4
    str     r9, [r2], #4
    subs    r3, r3, #4
    bhi     .Lword
    pop     {r4-r11, lr}
    bx      lr

@ Four words, one or more of them clipped: each side's half on its own,
@ 0x4000 taken off, over 2^7, the end of the range where its bits above
@ the eighth aren't all its sign, then turned over as the fast path's.
.Lclipped:
    push    {r0-r3}
    mov     r0, #4
.Lsample:
    ldr     r10, =0x40004000
    sub     r4, r4, r10             @ the sums as they were
    mov     r10, r4, lsl #16
    sub     r1, r4, r10, asr #16    @ the left's sum, in the top half
    mov     r10, r10, asr #23       @ the right's over 2^7
    mov     r1, r1, asr #23         @ the left's
    mov     r2, r10, asr #7
    teq     r2, r10, asr #31
    movne   r10, #0x7F
    eorne   r10, r10, r2, asr #31   @ 127, or -128 where negative
    mov     r2, r1, asr #7
    teq     r2, r1, asr #31
    movne   r1, #0x7F
    eorne   r1, r1, r2, asr #31
    eor     r10, r10, #0x80         @ turned over, as the fast path leaves them
    eor     r1, r1, #0x80
    and     r10, r10, #0xFF
    and     r1, r1, #0xFF
    mov     r8, r8, lsr #8          @ the word's samples, the first at the bottom
    orr     r8, r8, r1, lsl #24
    mov     r9, r9, lsr #8
    orr     r9, r9, r10, lsl #24
    mov     r4, r5                  @ the next of the four
    mov     r5, r6
    mov     r6, r7
    subs    r0, r0, #1
    bne     .Lsample
    pop     {r0-r3}
    b       .Lstore
    .ltorg
    .size to_8_bits, . - to_8_bits
