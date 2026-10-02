#include "sound_host.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu/dsp.h"
#include "platform/voices_dsp.h"
#include "port/vars_ram.h"
#include "sound/sound.h"

/* The SPC700's timers count at 8 kHz; the original driver sets timer 0 to
 * tick it about 190 times a second (8 kHz when fast-forwarding, command $F6)
 * and waits on timer 1, 62.5 Hz, which is one step of the echo's delay: the
 * time the S-DSP takes over 2 KiB of echo buffer. */
enum {
    TIMER_STEP_CYCLES = 128,
    TICK_CYCLES = 0x2A * TIMER_STEP_CYCLES,
    FAST_TICK_CYCLES = 1 * TIMER_STEP_CYCLES,
    ECHO_STEP_CYCLES = 0x80 * TIMER_STEP_CYCLES
};

/* A command the game writes to the ports itself (the scene's sends) is
 * answered at once, and the answer stays on port 0 until the next tick shows
 * the status there: the game would miss it with a tick due, or when ticking
 * fast (every 128 cycles). The host takes about the original's time over it
 * before it ticks again. */
enum { COMMAND_CYCLES = 256 };

enum {
    RELOAD_COMMAND = 0xFF,
    RELOAD = 0xF0,                  /* $FF's parameter that stops the driver for the game to boot it again */
    SONG_COMMANDS = 0x10,           /* $10-$17 through the ports: a song's start waits out the echo */
    FIRST_AFTER_SONGS = 0x18
};

/* Where the SPC700 is left while the host plays in its place: in the
 * original's main loop, which is where its program would be between ticks. */
enum { DRIVER_RUNNING = 0x0299 };

typedef enum {
    WAITING,          /* for a command, between ticks */
    ECHO_DRAINING,    /* after a change of the echo's delay: the old buffer's time */
    ECHO_FILLING      /* ... and the new one's */
} Phase;

/* A command that came while the driver waited for its echo: the game's (a
 * call, as the CPU half takes it) or one written to the ports. */
typedef struct {
    SoundCommand command;
    bool game;
    uint8_t effects_asked;
} Waiting;

struct SoundHost {
    Snes *snes;
    SoundDriver driver;
    AuVoices voices;
    bool booted;
    Phase phase;
    uint32_t timer;             /* SPC700 cycles since the driver's last tick was due */
    unsigned took;              /* cycles the host takes over what it did in this call */
    uint32_t wait_left;
    uint32_t fill_cycles;
    Waiting queue[SOUND_QUEUE];
    uint8_t queued;

    unsigned long samples_into_echo;
    unsigned long commands_dropped;
    SoundHost *loading;         /* a state read goes here first (sound_host_state) */
    SnesStateExtra extra;       /* what sound_host_state hands out */
};

/* ---- The voices, into the emulated S-DSP ------------------------------------------------ */

static Apu *apu_of(SoundHost *h) { return &h->snes->apu; }

static uint8_t read_register(void *ctx, uint8_t reg)
{
    SoundHost *h = ctx;
    return dsp_read(&apu_of(h)->dsp, reg);
}

static void write_register(void *ctx, uint8_t reg, uint8_t value)
{
    SoundHost *h = ctx;
    dsp_write(&apu_of(h)->dsp, reg, value);
}

/* Whether the S-DSP is writing its echo over an address: its writes are on,
 * and the address is in the buffer it writes now or the one its registers
 * name (which it moves to when its offset next wraps). */
enum { ECHO_WRITES_OFF = 0x20, ECHO_UNIT_BYTES = 0x800 };

static bool in_echo(const Dsp *dsp, uint16_t at)
{
    if (dsp->regs[DSP_FLG] & ECHO_WRITES_OFF)
        return false;
    uint16_t now = (uint16_t)(dsp->echo_esa << 8), next = (uint16_t)(dsp->regs[DSP_ESA] << 8);
    uint32_t now_length = dsp->echo_length ? dsp->echo_length : 4;
    uint32_t next_length = (dsp->regs[DSP_EDL] & 0x0F) ? (dsp->regs[DSP_EDL] & 0x0F) * ECHO_UNIT_BYTES : 4;
    return (uint16_t)(at - now) < now_length || (uint16_t)(at - next) < next_length;
}

static void write_memory(void *ctx, uint16_t at, uint8_t value)
{
    SoundHost *h = ctx;
    /* (a sample placed past where the places' echo starts is the
     * original's, which places a song's samples end to end whatever their
     * length, and forgets those past the echo once they are in) */
    if (in_echo(&apu_of(h)->dsp, at) && at >> 8 < h->driver.samples.echo_page)
        h->samples_into_echo++;
    apu_of(h)->ram[at] = value;
}

static uint8_t read_memory(void *ctx, uint16_t at)
{
    SoundHost *h = ctx;
    return apu_of(h)->ram[at];
}

/* ---- The driver ----------------------------------------------------------------------------- */

/* The driver reads the cartridge through the game's accessors, which must
 * leave the machine as it is: it runs in the middle of the CPU's time. */
static bool quiet(void)
{
    bool was = vars_peeking();
    vars_peek(true);
    return was;
}

static void answer(SoundHost *h, uint8_t port0) { apu_of(h)->cpu_out[0] = port0; }

static void show_status(SoundHost *h)
{
    if (h->driver.stopped)
        return;
    SoundStatus status = sound_status(&h->driver);
    memcpy(apu_of(h)->cpu_out, status.port, sizeof status.port);
}

static uint32_t tick_cycles(const SoundHost *h) { return sound_ticking_fast(&h->driver) ? FAST_TICK_CYCLES : TICK_CYCLES; }

/* Whether a tick is due; a tick late by more than one period is still one. */
static bool tick_due(SoundHost *h)
{
    uint32_t period = tick_cycles(h);
    if (h->timer < period)
        return false;
    h->timer %= period;
    return true;
}

static void tick(SoundHost *h)
{
    bool was = quiet();
    sound_tick(&h->driver);
    vars_peek(was);
    show_status(h);
}

/* The echo's wait after a change of its delay: the old delay's time for the
 * old buffer to drain, then the new one's for it to fill. A song's command
 * waits for the whole of the new buffer before the song takes its echo, and
 * goes straight to that if there was no echo before; a change of the delay
 * alone ($FC) waits for what the new buffer adds to the old. */
static void wait_for_echo(SoundHost *h, uint8_t old_delay, bool song)
{
    SoundDriver *d = &h->driver;
    uint8_t new_delay = d->echo_delay;
    uint32_t fill = song ? new_delay * ECHO_STEP_CYCLES
                         : new_delay > old_delay ? (uint32_t)(new_delay - old_delay) * ECHO_STEP_CYCLES : 0;
    if (song && !old_delay) {
        bool was = quiet();
        sound_echo_drained(d);
        vars_peek(was);
        h->phase = ECHO_FILLING;
        h->wait_left = fill;
        return;
    }
    h->phase = ECHO_DRAINING;
    h->wait_left = old_delay * ECHO_STEP_CYCLES;
    h->fill_cycles = fill;
}

/* A command carried out: the game's as the CPU half took it (game/sound/loads.c),
 * or one written to the ports, which the original's driver took as it is (a
 * song's start then loads it as the game's would). $FF $F0 stops the driver
 * for the game to boot it again. */
static void carry_out(SoundHost *h, Waiting w)
{
    SoundDriver *d = &h->driver;
    SoundCommand c = w.command;
    uint8_t old_delay = dsp_read(&apu_of(h)->dsp, DSP_EDL) & 0x0F;
    bool was_draining = d->echo_draining;
    bool was = quiet();
    if (w.game)
        sound_game_command(d, c, w.effects_asked);
    else if (c.command >= SONG_COMMANDS && c.command < FIRST_AFTER_SONGS)
        sound_play_song(d, c, d->samples.effect_set);
    else
        sound_run_command(d, c);
    vars_peek(was);
    if (c.command == RELOAD_COMMAND && c.p1 == RELOAD) {
        h->phase = WAITING;
        h->queued = 0;
        return;
    }
    if (d->echo_draining && !was_draining)
        wait_for_echo(h, old_delay, d->song_voices_waiting);
}

/* While the driver waits for its echo, commands wait in the order they came.
 * A full queue takes the new command in place of its last, as the driver's own
 * does (sound_queue_command); the game never sends that many in one wait, so
 * one dropped is counted as a fault (sound_host_commands_dropped). */
static void hold(SoundHost *h, Waiting w)
{
    if (h->queued < SOUND_QUEUE) {
        h->queue[h->queued++] = w;
        return;
    }
    h->commands_dropped++;
    h->queue[SOUND_QUEUE - 1] = w;
}

/* The commands that waited, until one starts another wait: the rest wait on
 * for the end of that one. (The original runs the whole queue again after
 * the next wait, which a second change of the delay queued behind the first
 * turns into a loop that never ends; the game never sends one.) */
static void run_held(SoundHost *h)
{
    uint8_t n = h->queued, i = 0;
    h->queued = 0;
    Waiting held[SOUND_QUEUE];
    memcpy(held, h->queue, sizeof held);
    while (i < n && h->phase == WAITING)
        carry_out(h, held[i++]);
    for (; i < n; i++)
        hold(h, held[i]);
}

static void boot(SoundHost *h)
{
    SoundDriver *d = &h->driver;
    Apu *a = apu_of(h);
    uint8_t old_delay = dsp_read(&a->dsp, DSP_EDL) & 0x0F;
    if (!h->booted)
        memset(d, 0, sizeof *d);
    d->echo_waits = true;
    /* the SPC700 stays where the original's program would wait */
    a->phase = APU_DRIVER;
    a->spc.pc = DRIVER_RUNNING;
    memset(a->cpu_in, 0, sizeof a->cpu_in);
    memset(a->cpu_out, 0, sizeof a->cpu_out);
    bool was = quiet();
    sound_boot(d, state_of(h->snes), &h->voices);
    vars_peek(was);
    h->booted = true;
    h->phase = WAITING;
    h->timer = 0;
    h->queued = 0;
    if (d->echo_draining)
        wait_for_echo(h, old_delay, false);
}

/* ---- The hooks -------------------------------------------------------------------------------- */

static void take_boot(void *ctx, Apu *apu)
{
    (void)apu;
    boot(ctx);
}

static void take_command(void *ctx, Apu *apu, const uint8_t bytes[4], uint8_t effects_asked)
{
    (void)apu;
    SoundHost *h = ctx;
    if (!h->booted)
        return;
    Waiting w = { { bytes[0], bytes[1], bytes[2], bytes[3] }, true, effects_asked };
    if (h->phase == WAITING)
        carry_out(h, w);
    else
        hold(h, w);
}

/* A command written to the ports: echoed, and carried out (or held while the
 * driver waits for its echo, but for a song's start, which waits on the port
 * for the wait to end). Whether there was one. */
static bool take_port_command(SoundHost *h)
{
    Apu *a = apu_of(h);
    uint8_t command = a->cpu_in[0];
    if (!command)
        return false;
    SoundCommand c = { command, a->cpu_in[1], a->cpu_in[2], a->cpu_in[3] };
    bool song = command >= SONG_COMMANDS && command < FIRST_AFTER_SONGS;
    if (h->phase != WAITING && song)
        return false;
    memset(a->cpu_in, 0, sizeof a->cpu_in);
    answer(h, command);
    Waiting w = { c, false, 0 };
    if (h->phase == WAITING)
        carry_out(h, w);
    else
        hold(h, w);
    return true;
}

static void echo_wait(SoundHost *h, unsigned cycles)
{
    take_port_command(h);
    if (h->wait_left > cycles) {
        h->wait_left -= cycles;
        return;
    }
    bool was = quiet();
    if (h->phase == ECHO_DRAINING) {
        sound_echo_drained(&h->driver);
        h->phase = ECHO_FILLING;
        h->wait_left = h->fill_cycles;
        vars_peek(was);
        return;
    }
    sound_echo_ready(&h->driver);
    vars_peek(was);
    h->phase = WAITING;
    h->took = COMMAND_CYCLES;
    run_held(h);
}

static unsigned run_host(void *ctx, Apu *apu, unsigned cycles)
{
    (void)apu;
    SoundHost *h = ctx;
    if (!h->booted)
        return cycles;
    h->timer += cycles;
    h->took = 0;
    if (h->phase == WAITING) {
        if (tick_due(h))
            tick(h);
        else if (take_port_command(h))
            h->took = COMMAND_CYCLES;
    } else {
        echo_wait(h, cycles);
    }
    if (h->took <= cycles)
        return cycles;
    h->timer += h->took - cycles;
    return h->took;
}

/* The machine's APU hands its time, boot and commands to this host. */
static void hook(SoundHost *h)
{
    Apu *a = apu_of(h);
    a->host = run_host;
    a->host_ctx = h;
    a->host_boot = take_boot;
    a->host_command = take_command;
}

SoundHost *sound_host_attach(Snes *s)
{
    SoundHost *h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    h->snes = s;
    h->voices = (AuVoices){ h, read_register, write_register, write_memory, read_memory };
    hook(h);
    return h;
}

unsigned long sound_host_samples_into_echo(const SoundHost *h) { return h->samples_into_echo; }

unsigned long sound_host_commands_dropped(const SoundHost *h) { return h->commands_dropped; }

bool sound_host_report(const SoundHost *h, FILE *out)
{
    fprintf(out, "sound host: %lu bytes of samples placed where the echo was writing, %lu commands dropped from a "
                 "full queue%s\n", h->samples_into_echo, h->commands_dropped,
            h->samples_into_echo || h->commands_dropped ? "" : ": none");
    return !h->samples_into_echo && !h->commands_dropped;
}

uint8_t sound_host_saved_song(const SoundHost *h) { return h->driver.saved_song_id; }

void sound_host_detach(SoundHost *h)
{
    if (!h)
        return;
    if (h->snes->apu.host_ctx == h) {   /* not a copy's (sound_host_copy) */
        h->snes->apu.host = NULL;
        h->snes->apu.host_ctx = NULL;
        h->snes->apu.host_boot = NULL;
        h->snes->apu.host_command = NULL;
    }
    free(h->loading);
    free(h);
}

/* ---- Saved states ------------------------------------------------------------------------------ */

/* Every field of the host and its driver, by name, beside the machine's
 * (snes/system.h, SnesStateExtra). A field added to the driver needs its line
 * here; sound_test checks that every byte of the driver is in one. */
typedef struct {
    SnesStateVisitor *v;
    char name[96];
    size_t prefix;
} Names;

static const char *named(Names *n, const char *field)
{
    snprintf(n->name + n->prefix, sizeof n->name - n->prefix, ".%s", field);
    return n->name;
}

/* Enter a part (`index` < 0: not one of several), and leave it. */
static size_t enter(Names *n, const char *part, int index)
{
    size_t was = n->prefix;
    int len = index < 0 ? snprintf(n->name + was, sizeof n->name - was, ".%s", part)
                        : snprintf(n->name + was, sizeof n->name - was, ".%s[%d]", part, index);
    n->prefix += (size_t)len;
    return was;
}
static void leave(Names *n, size_t was) { n->prefix = was; n->name[was] = 0; }

#define U(n, f, x) snes_state_uint((n)->v, named((n), (f)), &(x), sizeof(x), 1)
#define UA(n, f, a) snes_state_uint((n)->v, named((n), (f)), (a), sizeof((a)[0]), sizeof(a) / sizeof((a)[0]))
#define B(n, f, x) snes_state_bool((n)->v, named((n), (f)), &(x), 1)

static void visit_slide(Names *n, const char *part, int index, SoundSlide *s)
{
    size_t was = enter(n, part, index);
    U(n, "level", s->level);
    U(n, "step", s->step);
    U(n, "steps_left", s->steps_left);
    leave(n, was);
}

static void visit_wave(Names *n, const char *part, SoundWave *w)
{
    size_t was = enter(n, part, -1);
    U(n, "depth", w->depth);
    U(n, "rate", w->rate);
    U(n, "ticks_left", w->ticks_left);
    U(n, "delay", w->delay);
    U(n, "delay_left", w->delay_left);
    U(n, "ramp", w->ramp);
    U(n, "base_step", w->base_step);
    U(n, "step", w->step);
    U(n, "phase", w->phase);
    leave(n, was);
}

static void visit_track(Names *n, int index, SoundTrack *t)
{
    size_t was = enter(n, "tracks", index);
    U(n, "at", t->at);
    U(n, "ticks_left", t->ticks_left);
    U(n, "loop_level", t->loop_level);
    for (int i = 0; i < SOUND_LOOP_LEVELS; i++) {
        size_t loop = enter(n, "loops", i);
        U(n, "passes", t->loops[i].passes);
        U(n, "counts_left", t->loops[i].counts_left);
        U(n, "start", t->loops[i].start);
        leave(n, loop);
    }
    U(n, "instrument", t->instrument);
    U(n, "tuning", t->tuning);
    B(n, "envelope.adsr", t->envelope.adsr);
    U(n, "envelope.attack", t->envelope.attack);
    U(n, "envelope.decay", t->envelope.decay);
    U(n, "envelope.sustain_level", t->envelope.sustain_level);
    U(n, "envelope.sustain_rate", t->envelope.sustain_rate);
    U(n, "octave", t->octave);
    U(n, "transpose", t->transpose);
    U(n, "detune", t->detune);
    U(n, "note", t->note);
    U(n, "pitch", t->pitch);
    U(n, "volume_scale", t->volume_scale);
    visit_slide(n, "volume", -1, &t->volume);
    visit_slide(n, "pan", -1, &t->pan);
    U(n, "slide_semitones", t->slide_semitones);
    U(n, "slide_ticks", t->slide_ticks);
    U(n, "slide_step", t->slide_step);
    U(n, "pitch_fraction", t->pitch_fraction);
    visit_wave(n, "vibrato", &t->vibrato);
    visit_wave(n, "tremolo", &t->tremolo);
    U(n, "vibrato_scale", t->vibrato_scale);
    U(n, "vibrato_depth", t->vibrato_depth);
    U(n, "vibrato_offset", t->vibrato_offset);
    U(n, "pan_sweep", t->pan_sweep);
    U(n, "pan_sweep_period", t->pan_sweep_period);
    U(n, "pan_sweep_ticks_left", t->pan_sweep_ticks_left);
    U(n, "pan_sweep_step", t->pan_sweep_step);
    U(n, "pan_sweep_phase", t->pan_sweep_phase);
    leave(n, was);
}

static void visit_flags(Names *n, const char *part, SoundVoiceFlags *f)
{
    size_t was = enter(n, part, -1);
    U(n, "echo", f->echo);
    U(n, "noise", f->noise);
    U(n, "pitch_modulation", f->pitch_modulation);
    U(n, "sounding", f->sounding);
    U(n, "held", f->held);
    U(n, "slur", f->slur);
    U(n, "slurring", f->slurring);
    U(n, "roll", f->roll);
    leave(n, was);
}

static void visit_song(Names *n, const char *part, SoundSong *s)
{
    size_t was = enter(n, part, -1);
    for (int i = 0; i < SOUND_TRACKS; i++)
        visit_track(n, i, &s->tracks[i]);
    U(n, "relocation", s->relocation);
    U(n, "key_on", s->key_on);
    U(n, "key_off", s->key_off);
    visit_slide(n, "tempo", -1, &s->tempo);
    U(n, "music_beat", s->music_beat);
    U(n, "effect_beat", s->effect_beat);
    U(n, "volume", s->volume);
    visit_slide(n, "echo_volume", -1, &s->echo_volume);
    visit_slide(n, "echo_feedback", -1, &s->echo_feedback);
    for (int i = 0; i < 8; i++)
        visit_slide(n, "filter", i, &s->filter[i]);
    U(n, "filter_steps_left", s->filter_steps_left);
    U(n, "music_tracks", s->music_tracks);
    visit_flags(n, "music", &s->music);
    visit_flags(n, "effects", &s->effects);
    U(n, "music_noise_clock", s->music_noise_clock);
    U(n, "effect_noise_clock", s->effect_noise_clock);
    U(n, "marker", s->marker);
    U(n, "percussion", s->percussion);
    leave(n, was);
}

static void visit_samples(Names *n, SoundSamples *p)
{
    size_t was = enter(n, "samples", -1);
    UA(n, "block", p->block);
    UA(n, "length", p->length);
    UA(n, "start", p->start);
    UA(n, "song_start", p->song_start);
    UA(n, "song_loop", p->song_loop);
    U(n, "echo_delay", p->echo_delay);
    U(n, "echo_page", p->echo_page);
    U(n, "instrument_place", p->instrument_place);
    U(n, "effect_set", p->effect_set);
    U(n, "effects_overwritten", p->effects_overwritten);
    leave(n, was);
}

static void visit_driver(Names *n, SoundDriver *d)
{
    size_t was = enter(n, "driver", -1);
    UA(n, "instrument_tables", d->instrument_tables);
    UA(n, "sequences", d->sequences);
    UA(n, "percussion", d->percussion);
    UA(n, "semitone_pitch", d->semitone_pitch);
    UA(n, "note_length", d->note_length);
    UA(n, "vibrato_scale", d->vibrato_scale);
    snes_state_uint(n->v, named(n, "echo_filter"), &d->echo_filter[0][0], 1, sizeof d->echo_filter);
    UA(n, "own_sample", d->own_sample);
    U(n, "own_sample_nibble", d->own_sample_nibble);
    visit_song(n, "song", &d->song);
    visit_song(n, "saved", &d->saved);
    U(n, "song_id", d->song_id);
    U(n, "saved_song_id", d->saved_song_id);
    U(n, "last_song_started", d->last_song_started);
    B(n, "song_announced", d->song_announced);
    U(n, "announced_track_set", d->announced_track_set);
    B(n, "song_echo_changed", d->song_echo_changed);
    U(n, "echo_delay", d->echo_delay);
    U(n, "effect_voices", d->effect_voices);
    U(n, "echo_voices", d->echo_voices);
    U(n, "noise_voices", d->noise_voices);
    U(n, "pitch_modulation_voices", d->pitch_modulation_voices);
    U(n, "noise_clock", d->noise_clock);
    U(n, "echo_volume_heard", d->echo_volume_heard);
    U(n, "muted", d->muted);
    U(n, "volume_changed", d->volume_changed);
    U(n, "pitch_changed", d->pitch_changed);
    B(n, "mono", d->mono);
    B(n, "full_volume", d->full_volume);
    B(n, "paused", d->paused);
    B(n, "stepping_every_tick", d->stepping_every_tick);
    B(n, "fast", d->fast);
    B(n, "meter", d->meter);
    B(n, "meter_upper", d->meter_upper);
    B(n, "stopped", d->stopped);
    visit_slide(n, "tempo_modifier", -1, &d->tempo_modifier);
    visit_slide(n, "pitch_modifier", -1, &d->pitch_modifier);
    U(n, "effect_pitch_modifier", d->effect_pitch_modifier);
    U(n, "condition", d->condition);
    U(n, "highest_condition", d->highest_condition);
    for (int i = 0; i < SOUND_FADES; i++)
        visit_slide(n, "fades", i, &d->fades[i].volume);
    U(n, "fade_divider", d->fade_divider);
    UA(n, "meter_volume", d->meter_volume);
    snes_state_uint(n->v, named(n, "queue"), &d->queue[0], 1, sizeof d->queue);
    U(n, "queued", d->queued);
    B(n, "echo_waits", d->echo_waits);
    B(n, "song_voices_waiting", d->song_voices_waiting);
    U(n, "echo_silences_waiting", d->echo_silences_waiting);
    B(n, "echo_draining", d->echo_draining);
    UA(n, "status", d->status.port);
    visit_samples(n, &d->samples);
    U(n, "stray_reads", d->stray_reads);
    U(n, "first_stray_read", d->first_stray_read);
    leave(n, was);
}

static void visit_host(SnesStateVisitor *v, void *fields)
{
    SoundHost *h = fields;
    Names n = { v, "sound", strlen("sound") };
    B(&n, "booted", h->booted);
    /* an enum, whose size is the compiler's: a byte in a state */
    uint8_t phase = (uint8_t)h->phase;
    U(&n, "phase", phase);
    h->phase = phase <= ECHO_FILLING ? (Phase)phase : WAITING;
    U(&n, "timer", h->timer);
    U(&n, "wait_left", h->wait_left);
    U(&n, "fill_cycles", h->fill_cycles);
    for (int i = 0; i < SOUND_QUEUE; i++) {
        size_t was = enter(&n, "queue", i);
        snes_state_uint(v, named(&n, "command"), &h->queue[i].command, 1, sizeof h->queue[i].command);
        B(&n, "game", h->queue[i].game);
        U(&n, "effects_asked", h->queue[i].effects_asked);
        leave(&n, was);
    }
    U(&n, "queued", h->queued);
    visit_driver(&n, &h->driver);
}

/* The state read: the host takes the loaded fields, and keeps its machine,
 * its voices and what the driver points at. */
static void loaded(void *fields, void *loading)
{
    SoundHost *h = fields, *from = loading;
    Snes *snes = h->snes;
    AuVoices voices = h->voices;
    SoundHost *scratch = h->loading;
    SnesStateExtra extra = h->extra;
    unsigned long into_echo = h->samples_into_echo, dropped = h->commands_dropped;
    *h = *from;
    h->snes = snes;
    h->voices = voices;
    h->loading = scratch;
    h->extra = extra;
    h->samples_into_echo = into_echo;
    h->commands_dropped = dropped;
    h->driver.voices = &h->voices;
    h->driver.game = state_of(snes);
    if (h->queued > SOUND_QUEUE)
        h->queued = SOUND_QUEUE;
    if (h->driver.queued > SOUND_QUEUE)
        h->driver.queued = SOUND_QUEUE;
}

#define WHAT "the C sound driver"

const SnesStateExtra *sound_host_state(SoundHost *h)
{
    if (!h->loading)
        h->loading = malloc(sizeof *h->loading);
    if (!h->loading)
        return NULL;
    /* what a field the state lacks keeps: a host that hasn't booted */
    memset(h->loading, 0, sizeof *h->loading);
    h->extra = (SnesStateExtra){ SNES_STATE_HOST_C_SOUND_DRIVER, WHAT, visit_host, h, h->loading, loaded };
    return &h->extra;
}

SoundHost *sound_host_copy(const SoundHost *h)
{
    SoundHost *copy = malloc(sizeof *copy);
    if (!copy)
        return NULL;
    *copy = *h;
    copy->loading = NULL;
    return copy;
}

void sound_host_restore(SoundHost *h, const SoundHost *copy)
{
    loaded(h, (SoundHost *)copy);
    hook(h);
}

void sound_host_driver_fields(SoundHost *h, SnesStateField fn, void *ctx)
{
    SnesStateExtra extra = { SNES_STATE_HOST_C_SOUND_DRIVER, WHAT, visit_host, h, NULL, loaded };
    snes_state_extra_fields(&extra, fn, ctx);
}

size_t sound_host_driver_offset(void) { return offsetof(SoundHost, driver); }
