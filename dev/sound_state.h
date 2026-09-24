/* The C sound driver's state (game/sound/sound.h) against where the original
 * keeps the same things in sound RAM: a debugging aid for ct_dev
 * --sound-lockstep, which prints the fields that differ at the first tick the
 * two drivers' outputs do, to show where they parted. The tracks compared are
 * those playing. */
#ifndef CT_DEV_SOUND_STATE_H
#define CT_DEV_SOUND_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "sound/sound.h"

/* How many fields differ; the first few, as "name port/original;", to `out`. */
unsigned sound_state_differences(const SoundDriver *d, const uint8_t *ram, char *out, size_t size);

#endif
