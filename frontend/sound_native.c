#include "sound_native.h"

#include <stdlib.h>
#include <string.h>

#include "core/sound_driver.h"
#include "platform/native/mixer.h"
#include "sound/sound.h"

/* The driver's time is the SPC700's, whose timers count at 8 kHz: the
 * original driver ticks about 190 times a second (8 kHz when fast-forwarding,
 * command $F6), and a step of the echo's delay is 16 ms of buffer. The mixer
 * makes a frame of sound every 32 cycles. */
enum {
    SPC_CYCLES_PER_FRAME = 17040,   /* 1.024 MHz over the 60.1 frames a second the picture runs at */
    TIMER_STEP_CYCLES = 128,
    TICK_CYCLES = 0x2A * TIMER_STEP_CYCLES,
    FAST_TICK_CYCLES = 1 * TIMER_STEP_CYCLES,
    ECHO_STEP_CYCLES = 0x80 * TIMER_STEP_CYCLES,
    MIX_CYCLES = 32,
};

enum {
    RELOAD_COMMAND = 0xFF,
    RELOAD = 0xF0,                  /* $FF's parameter that stops the driver for the game to boot it again */
    SONG_COMMANDS = 0x10,           /* $10-$17 on the ports: a song's start waits out the echo */
    FIRST_AFTER_SONGS = 0x18
};

typedef enum { WAITING, ECHO_DRAINING, ECHO_FILLING } Phase;

/* A command that came while the driver waited for its echo: the game's (as
 * the CPU half took it) or one written to the ports. */
typedef struct {
    SoundCommand command;
    bool game;
    uint8_t effects_asked;
} Held;

struct SoundNative {
    GameState *game;
    Platform *pf;
    SoundDriver driver;
    Mixer mixer;
    bool booted;
    Phase phase;
    uint32_t timer;          /* cycles since the driver's last tick was due */
    uint32_t mix_clock;      /* cycles toward the mixer's next frame */
    uint32_t wait_left, fill_cycles;
    Held held[SOUND_QUEUE];
    uint8_t queued;
    uint8_t in[4];           /* the ports as the game wrote them */
    uint8_t out[4];          /* ... and as the driver shows them */
};

/* ---- The driver ------------------------------------------------------------------------------- */

/* The status the driver shows on the ports after a tick; a stopped driver
 * shows nothing new. */
static void show_status(SoundNative *n)
{
    if (n->driver.stopped)
        return;
    SoundStatus status = sound_status(&n->driver);
    memcpy(n->out, status.port, sizeof status.port);
}

/* The echo's wait after a change of its delay: the old delay's time for the
 * old buffer to drain, then the new one's to fill (a song's command waits for
 * the whole new buffer, and goes straight to that with no echo before). */
static void wait_for_echo(SoundNative *n, uint8_t old_delay, bool song)
{
    uint8_t new_delay = n->driver.echo_delay;
    uint32_t fill = song ? new_delay * ECHO_STEP_CYCLES
                         : new_delay > old_delay ? (uint32_t)(new_delay - old_delay) * ECHO_STEP_CYCLES : 0;
    if (song && !old_delay) {
        sound_echo_drained(&n->driver);
        n->phase = ECHO_FILLING;
        n->wait_left = fill;
        return;
    }
    n->phase = ECHO_DRAINING;
    n->wait_left = old_delay * ECHO_STEP_CYCLES;
    n->fill_cycles = fill;
}

static void carry_out(SoundNative *n, Held h)
{
    SoundDriver *d = &n->driver;
    SoundCommand c = h.command;
    uint8_t old_delay = mixer_echo_delay(&n->mixer);
    bool was_draining = d->echo_draining;
    if (h.game)
        sound_game_command(d, c, h.effects_asked);
    else if (c.command >= SONG_COMMANDS && c.command < FIRST_AFTER_SONGS)
        sound_play_song(d, c, d->samples.effect_set);
    else
        sound_run_command(d, c);
    if (c.command == RELOAD_COMMAND && c.p1 == RELOAD) {
        n->phase = WAITING;
        n->queued = 0;
        return;
    }
    if (d->echo_draining && !was_draining)
        wait_for_echo(n, old_delay, d->song_voices_waiting);
}

/* A command that comes while the driver waits for its echo waits its turn;
 * the game never sends more than the queue holds in one wait. */
static void hold(SoundNative *n, Held h)
{
    if (n->queued < SOUND_QUEUE)
        n->held[n->queued++] = h;
    else
        n->held[SOUND_QUEUE - 1] = h;
}

static void take(SoundNative *n, Held h)
{
    if (n->phase == WAITING)
        carry_out(n, h);
    else
        hold(n, h);
}

/* The commands that waited, until one starts another wait. */
static void run_held(SoundNative *n)
{
    Held held[SOUND_QUEUE];
    uint8_t count = n->queued, i = 0;
    memcpy(held, n->held, sizeof held);
    n->queued = 0;
    while (i < count && n->phase == WAITING)
        carry_out(n, held[i++]);
    for (; i < count; i++)
        hold(n, held[i]);
}

/* A command the game wrote to the ports: taken, and echoed on port 0 until
 * the next tick shows the status there. A song's start waits on the port for
 * the echo's wait to end. Whether there was one. */
static bool take_port_command(SoundNative *n)
{
    uint8_t command = n->in[0];
    if (!command)
        return false;
    bool song = command >= SONG_COMMANDS && command < FIRST_AFTER_SONGS;
    if (n->phase != WAITING && song)
        return false;
    SoundCommand c = { command, n->in[1], n->in[2], n->in[3] };
    memset(n->in, 0, sizeof n->in);
    n->out[0] = command;
    take(n, (Held){ c, false, 0 });
    return true;
}

/* ---- Its time ---------------------------------------------------------------------------------- */

static uint32_t tick_cycles(const SoundNative *n) { return sound_ticking_fast(&n->driver) ? FAST_TICK_CYCLES : TICK_CYCLES; }

static void echo_wait(SoundNative *n, uint32_t cycles)
{
    if (n->wait_left > cycles) {
        n->wait_left -= cycles;
        return;
    }
    if (n->phase == ECHO_DRAINING) {
        sound_echo_drained(&n->driver);
        n->phase = ECHO_FILLING;
        n->wait_left = n->fill_cycles;
        return;
    }
    sound_echo_ready(&n->driver);
    n->phase = WAITING;
    run_held(n);
}

static void mix(SoundNative *n, uint32_t cycles)
{
    int16_t frame[2];
    for (n->mix_clock += cycles; n->mix_clock >= MIX_CYCLES; n->mix_clock -= MIX_CYCLES)
        mixer_frame(&n->mixer, frame);
}

/* The driver's time in steps of at most a timer step: a tick where one is
 * due, a command from the ports between them, the echo's wait counting down. */
static void pass(void *ctx, uint32_t cycles)
{
    SoundNative *n = ctx;
    if (!n->booted)
        return;
    while (cycles) {
        uint32_t step = cycles < TIMER_STEP_CYCLES ? cycles : TIMER_STEP_CYCLES;
        cycles -= step;
        mix(n, step);
        take_port_command(n);
        if (n->phase != WAITING) {
            echo_wait(n, step);
            continue;
        }
        n->timer += step;
        if (n->timer >= tick_cycles(n)) {
            n->timer %= tick_cycles(n);
            sound_tick(&n->driver);
            show_status(n);
        }
    }
}

/* ---- The hooks --------------------------------------------------------------------------------- */

static void send(void *ctx, AudioCommand c)
{
    SoundNative *n = ctx;
    sound_driver_send(n->game, n->pf, c);
}

static void boot(void *ctx)
{
    SoundNative *n = ctx;
    uint8_t old_delay = mixer_echo_delay(&n->mixer);
    if (!n->booted)
        memset(&n->driver, 0, sizeof n->driver);
    n->driver.echo_waits = true;
    memset(n->in, 0, sizeof n->in);
    memset(n->out, 0, sizeof n->out);
    sound_boot(&n->driver, n->game, mixer_voices(&n->mixer));
    n->booted = true;
    n->phase = WAITING;
    n->timer = 0;
    n->queued = 0;
    if (n->driver.echo_draining)
        wait_for_echo(n, old_delay, false);
}

static void command(void *ctx, AudioCommand c, uint8_t effects_asked)
{
    SoundNative *n = ctx;
    if (n->booted)
        take(n, (Held){ { c.command, c.p1, c.p2, c.p3 }, true, effects_asked });
}

static void write_port(void *ctx, uint8_t port, uint8_t v)
{
    SoundNative *n = ctx;
    n->in[port & 3] = v;
    if ((port & 3) == 0)
        take_port_command(n);
}

static uint8_t read_port(void *ctx, uint8_t port)
{
    SoundNative *n = ctx;
    return n->out[port & 3];
}

SoundNative *sound_native_create(GameState *g, Platform *pf)
{
    SoundNative *n = calloc(1, sizeof *n);
    if (!n)
        return NULL;
    n->game = g;
    n->pf = pf;
    mixer_init(&n->mixer);
    pf->audio = (AuHost){ n, send, boot, command, write_port, read_port, pass };
    return n;
}

void sound_native_destroy(SoundNative *n)
{
    if (!n)
        return;
    if (n->pf->audio.ctx == n)
        n->pf->audio = (AuHost){ 0 };
    free(n);
}

void sound_native_frame(SoundNative *n) { pass(n, SPC_CYCLES_PER_FRAME); }
