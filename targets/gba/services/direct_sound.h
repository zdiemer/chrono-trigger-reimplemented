/* The GBA's sound out: the mixer's 8-bit samples played on Direct Sound A
 * (left) and B (right) at DIRECT_SOUND_RATE, 224 a GBA frame.
 *
 * The GBA plays it by itself. The mixer writes what it mixes into a ring of
 * two frames; timer 0 plays a sample from each channel's FIFO at the rate,
 * and DMA 1 and 2 feed the FIFOs from the ring as they run low, as the
 * hardware's sound DMA does, with no processor's work a sample. Timer 1,
 * counting timer 0's samples, interrupts every quarter of the ring: there
 * the ring's start is given to the DMA again once it has played to the end,
 * and the mixer fills what has been played (the fill given to
 * direct_sound_start), with the other interrupts on, so the sound is made as
 * fast as it plays whatever the game's frame takes. Where a quarter about to
 * play wasn't filled in time, it is silence, never the ring's last turn
 * heard again. */
#ifndef CT_GBA_DIRECT_SOUND_H
#define CT_GBA_DIRECT_SOUND_H

#include <stdbool.h>
#include <stdint.h>

enum {
    DIRECT_SOUND_CYCLES = 1254,                      /* cycles a sample: 16,777,216 / 1254 = 13,379 a second */
    DIRECT_SOUND_FRAME = 224,                        /* samples a GBA frame (280,896 cycles) */
    DIRECT_SOUND_RING = 2 * DIRECT_SOUND_FRAME,      /* samples in the ring, each channel */
    DIRECT_SOUND_QUARTER = DIRECT_SOUND_RING / 4,    /* samples between timer 1's interrupts */
    DIRECT_SOUND_FILL = 32                           /* samples the mixer queues at a time, a power of two dividing the ring */
};

/* The ring, where a runner that records the sound finds it
 * (targets/gba/host/mgba_run.c --stream, from the debug log's line naming
 * it, its length and where each side starts): `queued` counts every sample
 * queued since the start, the newest at ring[(queued - 1) % DIRECT_SOUND_RING],
 * and `played` every sample the DMA has played, as of the last quarter. */
typedef struct {
    volatile uint32_t queued, played;
    volatile uint32_t silent;                        /* samples of silence played where the ring wasn't filled */
    int8_t left[DIRECT_SOUND_RING] __attribute__((aligned(4)));
    int8_t right[DIRECT_SOUND_RING] __attribute__((aligned(4)));
} DirectSound;
extern DirectSound ct_gba_sound;

/* Playing, with `fill` called from timer 1's interrupt (gba_interrupt_nested)
 * whenever the ring has room for DIRECT_SOUND_FILL samples and no fill is
 * running; it queues what it can. */
void direct_sound_start(void (*fill)(void));
/* The samples the ring has room for. */
static inline uint32_t direct_sound_room(void)
{
    uint32_t ahead = ct_gba_sound.queued - ct_gba_sound.played;
    return ahead < DIRECT_SOUND_RING ? DIRECT_SOUND_RING - ahead : 0;
}
/* Where the next DIRECT_SOUND_FILL samples of each side go, after those
 * queued, with room for them: the place never wraps. Once they are written
 * there, direct_sound_queued(). */
void direct_sound_next(int8_t **left, int8_t **right);
void direct_sound_queued(void);

#endif
