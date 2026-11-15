/* The GBA's sound out: the mixer's 8-bit samples played on Direct Sound A
 * (left) and B (right) at DIRECT_SOUND_RATE, 224 a GBA frame.
 *
 * The mixer queues what it mixes in a ring; timer 0 plays a sample from
 * each channel's FIFO at the rate, and timer 1, counting timer 0, interrupts
 * every 16 samples to move the next 16 of each channel from the ring to its
 * FIFO, so the ring can wrap anywhere. The playback keeps the ring full: as
 * the interrupt finds room in it, it has the mixer fill it (the fill given
 * to direct_sound_start), with the other interrupts on, so the sound is
 * made as fast as it plays whatever the game's frame takes. Where the ring
 * runs dry all the same (the mixer couldn't fill it in time), the FIFOs get
 * silence until a frame's worth is queued again. */
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
    volatile uint32_t silent;                        /* silence played with the ring dry */
    int8_t left[DIRECT_SOUND_RING], right[DIRECT_SOUND_RING];
} DirectSound;
extern DirectSound ct_gba_sound;

/* Playing, with `fill` called from the interrupt (gba_interrupt_nested)
 * whenever the ring has room for DIRECT_SOUND_FILL samples and no fill is
 * running; it queues what it can. */
enum { DIRECT_SOUND_FILL = 32 };
void direct_sound_start(void (*fill)(void));
/* The samples the ring has room for. */
static inline uint32_t direct_sound_room(void) { return DIRECT_SOUND_RING - (ct_gba_sound.queued - ct_gba_sound.played); }
/* Where the next `count` samples of each side go, after those queued, or
 * false with the ring too full to take them (they're dropped). `count`
 * divides the ring, and every count queued is the same, so the place never
 * wraps; once they are written there, direct_sound_queued(count). */
bool direct_sound_space(uint32_t count, int8_t **left, int8_t **right);
void direct_sound_queued(uint32_t count);

#endif
