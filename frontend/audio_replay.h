/* Reuse the audio host's recorded replies while rebuilding game stacks.
 * The checkpoint keeps the complete sound driver/mixer, so normal play
 * resumes with exactly the sound state that produced those replies. */
#ifndef CT_AUDIO_REPLAY_H
#define CT_AUDIO_REPLAY_H
#include "sound_native.h"

typedef struct AudioReplay AudioReplay;
AudioReplay *audio_replay_create(void);
void audio_replay_destroy(AudioReplay *r);
/* Bind a fresh sound host, before its first frame; discard the live history. */
void audio_replay_bind(AudioReplay *r, GameState *game, Platform *pf, SoundNative *sound);
void audio_replay_frame(AudioReplay *r);
void *audio_replay_save(AudioReplay *r);
void audio_replay_drop(void *checkpoint);
bool audio_replay_begin(AudioReplay *r, const void *checkpoint);
bool audio_replay_end(AudioReplay *r, const void *checkpoint);
#endif
