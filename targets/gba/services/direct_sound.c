/* The sound out (services/direct_sound.h). */
#include "services/direct_sound.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "system/interrupts.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))
#define SOUNDCNT_L REG16(0x04000080)
#define SOUNDCNT_H REG16(0x04000082)
#define SOUNDCNT_X REG16(0x04000084)
#define FIFO_A 0x040000A0u
#define FIFO_B 0x040000A4u
#define DMA1SAD REG32(0x040000BC)
#define DMA1DAD REG32(0x040000C0)
#define DMA1CNT_H REG16(0x040000C6)
#define DMA2SAD REG32(0x040000C8)
#define DMA2DAD REG32(0x040000CC)
#define DMA2CNT_H REG16(0x040000D2)
#define TM0CNT_L REG16(0x04000100)
#define TM0CNT_H REG16(0x04000102)
#define TM1CNT_L REG16(0x04000104)
#define TM1CNT_H REG16(0x04000106)
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define REG_IME REG16(0x04000208)

enum {
    SOUND_ON = 0x0080,
    /* both at full volume, A to the left and B to the right, both on timer 0 */
    DIRECT_SOUND = 0x0004 | 0x0008 | 0x0200 | 0x1000,
    FIFOS_EMPTIED = 0x0800 | 0x8000,
    /* on, as its FIFO asks, again and again, words, to the FIFO again and again */
    SOUND_DMA = 0x8000 | 0x3000 | 0x0200 | 0x0400 | 0x0040,
    TIMER_ON = 0x0080, TIMER_IRQ = 0x0040, TIMER_COUNTS_THE_LAST = 0x0004,
    QUARTERS = DIRECT_SOUND_RING / DIRECT_SOUND_QUARTER,
};
_Static_assert(DIRECT_SOUND_QUARTER % DIRECT_SOUND_FILL == 0 && DIRECT_SOUND_FILL % 4 == 0,
               "a fill never wraps the ring, the quarters fall between fills, and a fill is whole words");

DirectSound ct_gba_sound;
static void (*filler)(void);
static volatile bool filling;
static unsigned quarter;

/* The DMA given the ring's start with the FIFOs emptied: what they held is
 * what the DMA read on past the ring's end, the sample just played being the
 * ring's last. Once every two GBA frames: in the cartridge, as Thumb, where
 * fast RAM does more as the mixer's. */
__attribute__((noinline)) static void play_from_the_start(void)
{
    DMA1CNT_H = 0;
    DMA2CNT_H = 0;
    SOUNDCNT_H = DIRECT_SOUND | FIFOS_EMPTIED;
    DMA1SAD = (uint32_t)(uintptr_t)ct_gba_sound.left;
    DMA1DAD = FIFO_A;
    DMA2SAD = (uint32_t)(uintptr_t)ct_gba_sound.right;
    DMA2DAD = FIFO_B;
    DMA1CNT_H = SOUND_DMA;
    DMA2CNT_H = SOUND_DMA;
}

/* What is playing now, to `needed`, wasn't filled in time: silence from
 * where it was. Seldom, so in the cartridge, as Thumb. */
__attribute__((noinline)) static void silence_unfilled(DirectSound *s, uint32_t played, uint32_t needed)
{
    uint32_t queued = s->queued;
    uint32_t from = (int32_t)(played - queued) > 0 ? played : queued;
    uint32_t at = from % DIRECT_SOUND_RING;
    for (uint32_t n = needed - from; n; n--) {
        s->left[at] = s->right[at] = 0;
        if (++at == DIRECT_SOUND_RING)
            at = 0;
    }
    s->silent += needed - queued;
    s->queued = needed;
}

/* A quarter of the ring played: the DMA back to its start after the last
 * quarter, silence where the quarter now playing wasn't filled, and the
 * mixer asked to fill what was played. In ARM, in fast RAM, as the
 * interrupts are. */
__attribute__((target("arm"), section(".iwram"), noinline)) static void quarter_played(void)
{
    REG_IF = GBA_IRQ_TIMER1;
    DirectSound *s = &ct_gba_sound;
    if (++quarter == QUARTERS) {
        quarter = 0;
        play_from_the_start();
    }
    uint32_t played = s->played + DIRECT_SOUND_QUARTER, queued = s->queued;
    s->played = played;
    /* the quarter now playing, which ends where a fill does */
    uint32_t needed = played + DIRECT_SOUND_QUARTER;
    if (!filling && (int32_t)(needed - queued) > 0)
        silence_unfilled(s, played, needed);
    if (filler && !filling && direct_sound_room() >= DIRECT_SOUND_FILL) {
        filling = true;
        gba_interrupt_nested(filler);
        filling = false;
    }
}

/* The ring is named in the debug log for a runner that records it
 * (targets/gba/host/mgba_run.c --stream). It starts silent, its first fill
 * coming as its first quarter is played. */
void direct_sound_start(void (*fill)(void))
{
    DirectSound *s = &ct_gba_sound;
    fprintf(stderr, "ct_gba: sound ring at %08X, %u samples, left at +%u, right at +%u, %u a second\n",
            (unsigned)(uintptr_t)s, (unsigned)DIRECT_SOUND_RING, (unsigned)offsetof(DirectSound, left),
            (unsigned)offsetof(DirectSound, right), (unsigned)DIRECT_SOUND_RATE);
    filler = fill;
    memset(s->left, 0, sizeof s->left);
    memset(s->right, 0, sizeof s->right);
    s->queued = s->played = s->silent = 0;
    SOUNDCNT_X = SOUND_ON;
    SOUNDCNT_L = 0;
    play_from_the_start();
    gba_interrupt_handler(GBA_IRQ_TIMER1, quarter_played);
    TM1CNT_H = 0;
    TM1CNT_L = (uint16_t)(0x10000 - DIRECT_SOUND_QUARTER);
    TM1CNT_H = TIMER_ON | TIMER_IRQ | TIMER_COUNTS_THE_LAST;
    TM0CNT_H = 0;
    TM0CNT_L = (uint16_t)(0x10000 - DIRECT_SOUND_CYCLES);
    REG_IF = GBA_IRQ_TIMER1;
    REG_IE = (uint16_t)(REG_IE | GBA_IRQ_TIMER1);
    REG_IME = 1;
    TM0CNT_H = TIMER_ON;
}

void direct_sound_next(int8_t **left, int8_t **right)
{
    uint32_t at = ct_gba_sound.queued % DIRECT_SOUND_RING;
    *left = &ct_gba_sound.left[at];
    *right = &ct_gba_sound.right[at];
}

void direct_sound_queued(void) { ct_gba_sound.queued += DIRECT_SOUND_FILL; }
