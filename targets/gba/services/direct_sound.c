/* The sound out (services/direct_sound.h). */
#include "services/direct_sound.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "system/interrupts.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))
#define SOUNDCNT_L REG16(0x04000080)
#define SOUNDCNT_H REG16(0x04000082)
#define SOUNDCNT_X REG16(0x04000084)
#define FIFO_A REG32(0x040000A0)
#define FIFO_B REG32(0x040000A4)
#define TM0CNT_L REG16(0x04000100)
#define TM0CNT_H REG16(0x04000102)
#define TM1CNT_L REG16(0x04000104)
#define TM1CNT_H REG16(0x04000106)
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define REG_IME REG16(0x04000208)

enum {
    SOUND_ON = 0x0080,
    /* both at full volume, A to the left on timer 0, B to the right on timer 0, their FIFOs emptied */
    DIRECT_SOUND = 0x0004 | 0x0008 | 0x0200 | 0x0800 | 0x1000 | 0x8000,
    TIMER_ON = 0x0080, TIMER_IRQ = 0x0040, TIMER_COUNTS_THE_LAST = 0x0004,
    MOVE = 16,                         /* samples moved to each FIFO an interrupt, half of it */
};

DirectSound ct_gba_sound;
static bool playing;

/* The next 16 samples of each side to its FIFO, or silence where the ring
 * holds fewer; once dry, it plays again from a frame's worth queued. In ARM,
 * in fast RAM, as the interrupts are. */
__attribute__((target("arm"), section(".iwram"), noinline)) static void move_to_fifos(void)
{
    REG_IF = GBA_IRQ_TIMER1;
    DirectSound *s = &ct_gba_sound;
    uint32_t queued = s->queued, played = s->played;
    if (!playing && queued - played >= DIRECT_SOUND_FRAME)
        playing = true;
    if (playing && queued - played < MOVE)
        playing = false;
    if (!playing) {
        for (unsigned n = 0; n < MOVE / 4; n++)
            FIFO_A = 0, FIFO_B = 0;
        return;
    }
    const uint32_t *left = (const uint32_t *)&s->left[played % DIRECT_SOUND_RING];
    const uint32_t *right = (const uint32_t *)&s->right[played % DIRECT_SOUND_RING];
    for (unsigned n = 0; n < MOVE / 4; n++)
        FIFO_A = left[n], FIFO_B = right[n];
    s->played = played + MOVE;
}

/* The ring is named in the debug log for a runner that records it
 * (targets/gba/host/mgba_run.c --stream). */
void direct_sound_start(void)
{
    fprintf(stderr, "ct_gba: sound ring at %08X\n", (unsigned)(uintptr_t)&ct_gba_sound);
    SOUNDCNT_X = SOUND_ON;
    SOUNDCNT_L = 0;
    SOUNDCNT_H = DIRECT_SOUND;
    for (unsigned n = 0; n < 2 * MOVE / 4; n++)       /* the FIFOs full of silence to start */
        FIFO_A = 0, FIFO_B = 0;
    gba_interrupt_handler(GBA_IRQ_TIMER1, move_to_fifos);
    TM1CNT_H = 0;
    TM1CNT_L = (uint16_t)(0x10000 - MOVE);
    TM1CNT_H = TIMER_ON | TIMER_IRQ | TIMER_COUNTS_THE_LAST;
    TM0CNT_H = 0;
    TM0CNT_L = (uint16_t)(0x10000 - DIRECT_SOUND_CYCLES);
    REG_IF = GBA_IRQ_TIMER1;
    REG_IE = (uint16_t)(REG_IE | GBA_IRQ_TIMER1);
    REG_IME = 1;
    TM0CNT_H = TIMER_ON;
}

/* The ring wraps on a multiple of MOVE, so the interrupt's 16 never do. */
void direct_sound_queue(const int8_t *left, const int8_t *right, uint32_t count)
{
    DirectSound *s = &ct_gba_sound;
    uint32_t queued = s->queued;
    if (queued + count - s->played > DIRECT_SOUND_RING) {
        s->dropped += count;
        return;
    }
    uint32_t at = queued % DIRECT_SOUND_RING, first = DIRECT_SOUND_RING - at < count ? DIRECT_SOUND_RING - at : count;
    memcpy(&s->left[at], left, first);
    memcpy(&s->right[at], right, first);
    memcpy(s->left, left + first, count - first);
    memcpy(s->right, right + first, count - first);
    s->queued = queued + count;
}
