#include "audio_replay.h"
#include "port/state_host.h"
#include <stdio.h>
#include <stdlib.h>

typedef enum { BOOT, COMMAND, WRITE, READ, PASS, FRAME } Call;
typedef struct Event {
    struct Event *previous;
    size_t refs, count;
    Call call;
    uint32_t a, b, answer;
    StateHostWork work;
} Event;
typedef struct { SoundNative *sound; Event *last; } Checkpoint;
struct AudioReplay {
    GameState *game;
    SoundNative *sound;
    AuHost real;
    Event *last;
    const Event **replay;
    size_t count, at;
    bool replaying;
};

static Event *retain(Event *event) { if (event) event->refs++; return event; }
static void release(Event *event)
{
    while (event && --event->refs == 0) {
        Event *previous = event->previous;
        free(event);
        event = previous;
    }
}

static uint32_t invoke(AudioReplay *r, Call call, uint32_t a, uint32_t b)
{
    if (r->replaying) {
        const Event *event = r->at < r->count ? r->replay[r->at++] : NULL;
        if (!event || event->call != call || event->a != a || event->b != b) {
            fprintf(stderr, "native audio replay differs at call %zu\n", r->at);
            exit(1);
        }
        state_host_add_work(r->game, event->work);
        return event->answer;
    }
    StateHostWork before = state_host_work(r->game);
    uint32_t answer = 0;
    switch (call) {
    case BOOT: answer = r->real.boot(r->real.ctx); break;
    case COMMAND: {
        AudioCommand c = { (uint8_t)a, (uint8_t)(a >> 8), (uint8_t)(a >> 16), (uint8_t)(a >> 24), (uint8_t)(b >> 8) };
        answer = r->real.command(r->real.ctx, c, (uint8_t)b);
        break;
    }
    case WRITE: r->real.write(r->real.ctx, (uint8_t)a, (uint8_t)b); break;
    case READ: answer = r->real.read(r->real.ctx, (uint8_t)a); break;
    case PASS: r->real.pass(r->real.ctx, a); break;
    case FRAME: sound_native_frame(r->sound); sound_native_discard_audio(r->sound); break;
    }
    StateHostWork after = state_host_work(r->game);
    Event *event = malloc(sizeof *event);
    if (!event) { fprintf(stderr, "native audio history: out of memory\n"); exit(2); }
    *event = (Event){ r->last, 1, r->last ? r->last->count + 1 : 1, call, a, b, answer,
                     { after.raw_bus - before.raw_bus, after.owed - before.owed, after.milli - before.milli } };
    r->last = event; /* transfers the live reference to the new node's parent */
    return answer;
}

/* send is the CPU half: it must still update the game's command buffer and
 * charge its accesses. Its nested host calls are the recorded boundary. */
static void send(void *ctx, AudioCommand c)
{
    AudioReplay *r = ctx;
    r->real.send(r->real.ctx, c);
}
static uint32_t boot(void *ctx) { return invoke(ctx, BOOT, 0, 0); }
static uint32_t command(void *ctx, AudioCommand c, uint8_t effects)
{
    uint32_t a = (uint32_t)c.command | (uint32_t)c.p1 << 8 | (uint32_t)c.p2 << 16 | (uint32_t)c.p3 << 24;
    return invoke(ctx, COMMAND, a, (uint32_t)effects | (uint32_t)c.count << 8);
}
static void write_port(void *ctx, uint8_t port, uint8_t v) { (void)invoke(ctx, WRITE, port, v); }
static uint8_t read_port(void *ctx, uint8_t port) { return (uint8_t)invoke(ctx, READ, port, 0); }
static void pass(void *ctx, uint32_t cycles) { (void)invoke(ctx, PASS, cycles, 0); }

AudioReplay *audio_replay_create(void) { return calloc(1, sizeof(AudioReplay)); }
void audio_replay_destroy(AudioReplay *r)
{
    if (!r) return;
    release(r->last);
    free(r->replay);
    free(r);
}
void audio_replay_bind(AudioReplay *r, GameState *game, Platform *pf, SoundNative *sound)
{
    release(r->last);
    free(r->replay);
    *r = (AudioReplay){ .game = game, .sound = sound, .real = pf->audio };
    pf->audio = (AuHost){ r, send, boot, command, write_port, read_port, pass };
}
void audio_replay_frame(AudioReplay *r) { (void)invoke(r, FRAME, 0, 0); }
void *audio_replay_save(AudioReplay *r)
{
    Checkpoint *c = malloc(sizeof *c);
    if (!c) return NULL;
    c->sound = sound_native_copy(r->sound);
    if (!c->sound) { free(c); return NULL; }
    c->last = retain(r->last);
    return c;
}
void audio_replay_drop(void *checkpoint)
{
    Checkpoint *c = checkpoint;
    if (!c) return;
    release(c->last);
    free(c->sound);
    free(c);
}
bool audio_replay_begin(AudioReplay *r, const void *checkpoint)
{
    const Checkpoint *c = checkpoint;
    size_t count = c->last ? c->last->count : 0;
    const Event **events = count ? malloc(count * sizeof *events) : NULL;
    if (count && !events) return false;
    size_t i = count;
    for (const Event *event = c->last; event; event = event->previous) events[--i] = event;
    free(r->replay);
    r->replay = events;
    r->count = count;
    r->at = 0;
    r->replaying = true;
    return true;
}
bool audio_replay_end(AudioReplay *r, const void *checkpoint)
{
    const Checkpoint *c = checkpoint;
    if (r->at != r->count) return false;
    sound_native_restore(r->sound, c->sound);
    release(r->last);
    r->last = retain(c->last);
    free(r->replay);
    r->replay = NULL;
    r->replaying = false;
    return true;
}
