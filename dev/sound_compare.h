/* What ct_dev --sound-lockstep compares after each of the sound driver's
 * ticks, between the original driver on the emulated SPC700 and the C driver
 * (game/sound/), and how (docs/SOUND.md, "Validating the port"). Kept apart
 * from the harness (dev/sound_lockstep.h) so that it can be tested without a
 * ROM (tests/port/sound_test.c).
 *
 * A side is what one driver left when the tick (and the commands after it)
 * was done: the S-DSP's registers, every write it made to key on and key
 * off, in order, the status it showed the game, and the driver's own sample
 * in sound memory. */
#ifndef CT_DEV_SOUND_COMPARE_H
#define CT_DEV_SOUND_COMPARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SOUND_KEY_WRITES = 32,
    SOUND_COMPARED_MEMORY = 49   /* the driver's own sample, and its directory entry */
};

typedef struct {
    uint8_t reg, value;
} SoundKeyWrite;

typedef struct {
    uint8_t regs[128];
    SoundKeyWrite keys[SOUND_KEY_WRITES];
    uint16_t key_writes;                /* all of them; the first SOUND_KEY_WRITES are kept */
    uint8_t status[4];
    bool has_status;                    /* a tick that shows the status ran */
    uint8_t memory[SOUND_COMPARED_MEMORY];
} SoundSide;

/* How much is compared: the status alone, then the key writes as well, then
 * everything the driver puts out (the stages the port was built in); and
 * with STATE, the drivers' own state too (dev/sound_state.h), which finds
 * the tick where they part rather than the one where it shows. */
typedef enum { SOUND_COMPARE_STATUS, SOUND_COMPARE_KEYS, SOUND_COMPARE_DSP, SOUND_COMPARE_STATE } SoundCompareLevel;

typedef enum {
    SOUND_SAME,
    SOUND_STATUS_DIFFERS,      /* at: the port */
    SOUND_KEYS_DIFFER,         /* at: the write's index; want/got its value, reg its register */
    SOUND_REGISTER_DIFFERS,    /* at: the register */
    SOUND_MEMORY_DIFFERS,      /* at: the byte of SoundSide.memory */
    SOUND_STATE_DIFFERS        /* at: how many fields (the harness describes them) */
} SoundDifference;

typedef struct {
    SoundDifference kind;
    uint16_t at;
    uint8_t reg;
    int16_t want, got;         /* -1: no such write */
} SoundDivergence;

/* The S-DSP registers the S-DSP itself changes as it plays (each voice's
 * envelope and output levels, and the voices that reached their samples'
 * ends), which no driver writes and which aren't compared. */
bool sound_register_compared(uint8_t reg);

/* The first difference between the two sides at `level`, into *first; false
 * if they are the same. */
bool sound_compare(const SoundSide *reference, const SoundSide *port, SoundCompareLevel level, SoundDivergence *first);

/* One line saying what differed. */
void sound_divergence_describe(const SoundDivergence *d, char *out, size_t size);

void sound_side_clear_writes(SoundSide *side);
void sound_side_key_write(SoundSide *side, uint8_t reg, uint8_t value);

#endif
