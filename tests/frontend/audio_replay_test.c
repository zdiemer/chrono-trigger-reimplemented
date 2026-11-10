/* A made-up sound host: cached replies must preserve CPU-side sends,
 * timing-account deltas, branch histories and the sound state after load. */
#include "audio_replay.h"
#include "port/state_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GameState { StateHostWork work; unsigned sends; };
struct SoundNative { GameState *game; Platform *pf; uint32_t tick; uint8_t ports[4]; };
static unsigned calls, failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
StateHostWork state_host_work(const GameState *g) { return g->work; }
void state_host_add_work(GameState *g, StateHostWork w)
{
    g->work.raw_bus += w.raw_bus; g->work.owed += w.owed; g->work.milli += w.milli;
}
static void charged(SoundNative *s, unsigned n)
{
    state_host_add_work(s->game, (StateHostWork){n, n * 3, n * 1007}); calls++;
}
void sound_native_frame(SoundNative *s) { charged(s, 3); s->tick = s->tick * 3 + 7; s->ports[0] = (uint8_t)s->tick; }
void sound_native_discard_audio(SoundNative *s) { (void)s; }
SoundNative *sound_native_copy(const SoundNative *s)
{
    SoundNative *copy = malloc(sizeof *copy); if (copy) *copy = *s; return copy;
}
void sound_native_restore(SoundNative *s, const SoundNative *copy)
{
    GameState *g = s->game; Platform *pf = s->pf;
    *s = *copy; s->game = g; s->pf = pf;
}
static uint32_t boot(void *ctx) { SoundNative *s = ctx; charged(s, 1); s->tick = 11; return 123; }
static uint32_t command(void *ctx, AudioCommand c, uint8_t effects)
{
    SoundNative *s = ctx; charged(s, 5);
    s->tick += c.command + c.p1 + c.p2 + c.p3 + c.count + effects;
    return s->tick;
}
static void send(void *ctx, AudioCommand c)
{
    SoundNative *s = ctx; s->game->sends++;
    s->pf->audio.command(s->pf->audio.ctx, c, 9);
}
static void write_port(void *ctx, uint8_t p, uint8_t v) { SoundNative *s = ctx; charged(s, 7); s->ports[p] = v; }
static uint8_t read_port(void *ctx, uint8_t p) { SoundNative *s = ctx; charged(s, 2); return s->ports[p]; }
static void pass(void *ctx, uint32_t cycles) { SoundNative *s = ctx; charged(s, 4); s->tick += cycles; }
static void fresh(AudioReplay *r, SoundNative *s, GameState *g, Platform *pf)
{
    *g = (GameState){ .work = {UINT32_MAX - 5, UINT32_MAX - 10, UINT64_MAX - 2000} };
    *s = (SoundNative){ .game = g, .pf = pf };
    pf->audio = (AuHost){s, send, boot, command, write_port, read_port, pass};
    audio_replay_bind(r, g, pf, s);
}
static uint8_t sequence(AudioReplay *r, Platform *pf)
{
    AuHost *a = &pf->audio;
    CHECK(a->boot(a->ctx) == 123);
    a->send(a->ctx, (AudioCommand){ 1, 2, 3, 4, 3 });
    audio_replay_frame(r);
    a->write(a->ctx, 2, 99);
    CHECK(a->read(a->ctx, 2) == 99);
    a->pass(a->ctx, 300);
    audio_replay_frame(r);
    return a->read(a->ctx, 0);
}
int main(void)
{
    AudioReplay *r = audio_replay_create();
    CHECK(r != NULL);
    if (!r) return 1;
    GameState game; Platform pf = {0}; SoundNative sound;
    fresh(r, &sound, &game, &pf);
    uint8_t answer = sequence(r, &pf);
    GameState saved = game;
    uint32_t tick = sound.tick;
    void *a = audio_replay_save(r);
    CHECK(a != NULL);
    audio_replay_frame(r);
    void *b = audio_replay_save(r);
    uint32_t next = sound.tick;
    unsigned ran = calls;
    fresh(r, &sound, &game, &pf);
    CHECK(audio_replay_begin(r, a));
    CHECK(sequence(r, &pf) == answer);
    CHECK(audio_replay_end(r, a));
    CHECK(calls == ran); /* no sound execution while rebuilding game stacks */
    CHECK(game.sends == saved.sends && game.work.raw_bus == saved.work.raw_bus
          && game.work.owed == saved.work.owed && game.work.milli == saved.work.milli);
    CHECK(sound.tick == tick);
    audio_replay_frame(r);
    CHECK(sound.tick == next && calls == ran + 1);
    audio_replay_drop(a); /* shared prefix remains owned by the other branch */
    fresh(r, &sound, &game, &pf);
    CHECK(audio_replay_begin(r, b));
    CHECK(sequence(r, &pf) == answer);
    audio_replay_frame(r);
    CHECK(audio_replay_end(r, b));
    CHECK(sound.tick == next && calls == ran + 1);
    audio_replay_drop(b);
    audio_replay_destroy(r);
    printf("audio_replay_test: %u failures\n", failures);
    return failures != 0;
}
