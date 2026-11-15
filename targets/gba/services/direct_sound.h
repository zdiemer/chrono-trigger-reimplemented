/* The GBA's sound out: the mixer's 8-bit samples played on Direct Sound A
 * (left) and B (right) at DIRECT_SOUND_RATE, 224 a GBA frame.
 *
 * The mixer queues what it mixes in a ring; timer 0 plays a sample from
 * each channel's FIFO at the rate, and timer 1, counting timer 0, interrupts
 * every 16 samples to move the next 16 of each channel from the ring to its
 * FIFO, so the ring can wrap anywhere. Where the ring runs dry (the game's
 * frame took longer than the GBA's), the FIFOs get silence until a frame's
 * worth is queued again: what is heard is the game's sound, gapped where the
 * game is late, never repeated. */
#ifndef CT_GBA_DIRECT_SOUND_H
#define CT_GBA_DIRECT_SOUND_H

#include <stdbool.h>
#include <stdint.h>

enum {
    DIRECT_SOUND_CYCLES = 1254,                      /* cycles a sample: 16,777,216 / 1254 = 13,379 a second */
    DIRECT_SOUND_FRAME = 224,                        /* samples a GBA frame (280,896 cycles) */
    DIRECT_SOUND_RING = 512                          /* samples queued at most, each channel: two frames and more */
};

/* The ring, where a runner that records the sound finds it
 * (targets/gba/host/mgba_run.c --stream, from the debug log's line naming it): `queued` counts every sample
 * queued since the start, the newest at ring[(queued - 1) % DIRECT_SOUND_RING]. */
typedef struct {
    volatile uint32_t queued, played;
    volatile uint32_t dropped;                       /* queued with the ring full: not played */
    int8_t left[DIRECT_SOUND_RING], right[DIRECT_SOUND_RING];
} DirectSound;
extern DirectSound ct_gba_sound;

void direct_sound_start(void);
/* Where the next `count` samples of each side go, after those queued, or
 * false with the ring too full to take them (they're dropped). `count`
 * divides the ring, and every count queued is the same, so the place never
 * wraps; once they are written there, direct_sound_queued(count). */
bool direct_sound_space(uint32_t count, int8_t **left, int8_t **right);
void direct_sound_queued(uint32_t count);

#endif
