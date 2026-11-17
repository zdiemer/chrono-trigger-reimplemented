@ A SNES tile as the GBA's (services/video.c): ARM code in fast RAM that
@ turns a tile's bitplanes into the GBA's packed nibbles a word of the
@ SNES's at a time, with no table. A row's four bitplanes are one word, a
@ plane a byte, its leftmost dot the byte's top bit; the GBA's row is a word
@ of eight nibbles, its leftmost dot the lowest. Each bit of the one goes
@ to its place in the other by four exchanges of bit groups and a swap of
@ the halves, about twenty instructions a row. The background's tiles as a
@ scene's first picture names them, and the sprites' as they change
@ (docs/PLATFORM.md, "The GBA build, as built").

    .syntax unified
    .section .iwram, "ax", %progbits
    .arm
    .align 2

@ A bit's place in the row's word is 8 * plane + 7 - dot; in the GBA's,
@ 4 * dot + plane. Exchanging the bits of the place's index two by two
@ takes the one to the other: its bits 4 and 2 (shift 12), 3 and 0 (7),
@ 2 and 1 (2), then 3 and 2 each turned over (12, the places where both
@ are clear with those where both are set), and bit 4 turned over (the
@ halves swapped). An exchange moves the bits `mask` picks up by `shift`
@ and those `shift` above them down.
.macro exchange row, scratch, shift, mask
    eor     \scratch, \row, \row, lsr #\shift
    and     \scratch, \scratch, \mask
    eor     \row, \row, \scratch
    eor     \row, \row, \scratch, lsl #\shift
.endm

.macro gba_row row, scratch
    exchange \row, \scratch, 12, r8
    exchange \row, \scratch, 7, r9
    exchange \row, \scratch, 2, r10
    exchange \row, \scratch, 12, r11
    mov     \row, \row, ror #16
.endm

@ void tile_to_gba(volatile uint32_t *to (r0), const uint32_t *tile (r1), uint32_t deep (r2))
@ The tile at `tile` (word-aligned: planes 0 and 1 a halfword a row, then,
@ where `deep`, planes 2 and 3 the same 16 bytes on) into the GBA's 4bpp
@ tile at `to`, eight words; a 2bpp tile's planes 2 and 3 are clear.
    .global tile_to_gba
    .type tile_to_gba, %function
tile_to_gba:
    push    {r4-r11}
    ldr     r8, =0x0000F0F0
    ldr     r9, =0x00AA00AA
    ldr     r10, =0x0C0C0C0C
    ldr     r11, =0x000F000F
    mov     r3, #4             @ two rows at a time
.Lrows:
    ldr     r4, [r1], #4       @ planes 0 and 1 of this row and the next
    cmp     r2, #0
    ldrne   r5, [r1, #12]      @ planes 2 and 3, 16 bytes on
    moveq   r5, #0
    mov     r6, r4, lsl #16    @ this row: its planes 0 and 1 low, 2 and 3 high
    mov     r6, r6, lsr #16
    orr     r6, r6, r5, lsl #16
    mov     r12, r5, lsr #16   @ the next row the same
    mov     r12, r12, lsl #16
    orr     r12, r12, r4, lsr #16
    gba_row r6, r7
    gba_row r12, r7
    str     r6, [r0], #4
    str     r12, [r0], #4
    subs    r3, r3, #1
    bne     .Lrows
    pop     {r4-r11}
    bx      lr
    .pool
    .size tile_to_gba, . - tile_to_gba
