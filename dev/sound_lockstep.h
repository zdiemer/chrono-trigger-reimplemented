/* ct_dev --sound-lockstep: the C sound driver (game/sound/) run beside the
 * original on the emulated SPC700, a tick for each of its ticks, fed what it
 * was fed, and compared after each (docs/SOUND.md, "Validating the port").
 *
 * The original's program is watched through the APU's per-instruction hook,
 * at the places that mark its structure, and the C driver is given the same
 * thing at the same place:
 *
 * - its boot ($0200): sound_boot;
 * - a tick's start ($02A0): the comparison of what the last tick left, then
 *   the tick's parts at the points the original reaches them (the first
 *   batch of queued commands at $02F2, the second at $036E, the end at
 *   $0371; the status it showed is read at $02E2);
 * - a command carried out ($0BAC, with the command in $8D-$90), wherever
 *   that is: in either batch, between ticks, or in the wait for the echo's
 *   buffer. A song's start is given once its upload is in ($0C08), after
 *   anything that upload loaded;
 * - the echo writing again after a delay change ($1397): sound_echo_drained;
 *   and its buffer ready ($0CE5 for a song's, $134B for command $FC's):
 *   sound_echo_ready;
 * - an upload's header ($1035): the set of effects (at $2F00, the set the CPU
 *   half's work RAM says it sent) or command $71's instrument (at $1F40
 *   outside a song's upload, the block its parameter names);
 * - the tick it runs while data is uploaded ($0415);
 * - the voices' output levels its meter reads ($145C), which change every
 *   sample, handed to the C driver when it reads them.
 *
 * The C driver plays into a copy of the S-DSP's registers
 * (platform/voices_dsp.h), compared with the real ones at the start of every
 * tick, with every key on and key off write between (dev/sound_compare.h).
 * Exact without modelling cycles: the original acts on a command only at the
 * points above, and the tick boundary is where both drivers agree on time. It
 * must start at the driver's boot (from power-on; a loaded state is refused
 * by ct_dev). */
#ifndef CT_DEV_SOUND_LOCKSTEP_H
#define CT_DEV_SOUND_LOCKSTEP_H

#include <stdio.h>

#include "sound_compare.h"
#include "snes/system.h"

typedef struct SoundLockstep SoundLockstep;

SoundLockstep *sound_lockstep_create(Snes *s, SoundCompareLevel level);
void sound_lockstep_destroy(SoundLockstep *l);

/* From the APU's hooks (Apu.on_insn and Apu.on_dsp_write). */
void sound_lockstep_insn(SoundLockstep *l, const Apu *apu);
void sound_lockstep_dsp_write(SoundLockstep *l, const Apu *apu, uint8_t reg, uint8_t value);

/* Whether the drivers have differed at a tick so far. */
bool sound_lockstep_diverged(const SoundLockstep *l);

/* A summary line to `summary`, and every differing tick to `path` if one is
 * given; false if the drivers diverged. */
bool sound_lockstep_report(SoundLockstep *l, const char *path, FILE *summary);

#endif
