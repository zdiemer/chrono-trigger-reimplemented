/* The voices (platform/audio.h) as S-DSP registers: their first implementation.
 *
 * Each call is made as the register writes that make it, through `write`, and
 * what a call changes in part (the envelope's switch, the echo's writes) is
 * read back through `read` first. Where the registers go is the host's: the
 * emulated S-DSP when the C driver plays through it (frontend/sound_host.h,
 * SOUND_MIXER_DSP), or a copy of its registers when ct_dev --sound-lockstep
 * compares the C driver with the original. The samples and the directory go
 * into sound memory through `memory_write` (and a move reads it back through
 * `memory_read`), where the original driver keeps them.
 *
 * This is the host side of the service: game code sees AuVoices only as the
 * opaque handle audio.h declares. */
#ifndef CT_PLATFORM_VOICES_DSP_H
#define CT_PLATFORM_VOICES_DSP_H

#include "platform/voices.h"

typedef struct {
    AuVoices voices;
    void *ctx;
    uint8_t (*read)(void *ctx, uint8_t reg);
    void (*write)(void *ctx, uint8_t reg, uint8_t value);
    void (*memory_write)(void *ctx, uint16_t at, uint8_t value);
    uint8_t (*memory_read)(void *ctx, uint16_t at);
} VoicesDsp;

void voices_dsp_init(VoicesDsp *v, void *ctx, uint8_t (*read)(void *ctx, uint8_t reg),
                     void (*write)(void *ctx, uint8_t reg, uint8_t value),
                     void (*memory_write)(void *ctx, uint16_t at, uint8_t value),
                     uint8_t (*memory_read)(void *ctx, uint16_t at));

#endif
