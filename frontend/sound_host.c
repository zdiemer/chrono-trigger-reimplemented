#include "sound_host.h"

#include <stdlib.h>
#include <string.h>

#include "apu/dsp.h"
#include "core/sound_data.h"
#include "platform/voices_dsp.h"
#include "port/vars_ram.h"
#include "ram.h"
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

/* The host answers at once, but the original takes a while over a command,
 * the end of an upload or the commands queued in a wait, and its answer stays
 * on port 0 until the next tick shows the status there: the CPU half would
 * miss it after an upload or a wait, with a tick due, or when ticking fast
 * (every 128 cycles). The host takes about the original's time over those
 * before it ticks again. */
enum { COMMAND_CYCLES = 256 };

/* The protocol (docs/SOUND.md, "Commands from the CPU" and "Uploads";
 * game/core/sound_driver.c is the CPU half's side). */
enum {
    SONG_COMMANDS = 0x10,       /* $10-$1F: songs and effects; $10, $11, $14 and $15 start a song */
    FIRST_QUEUED_AFTER_SONGS = 0x18,
    EFFECT_67 = 0x18,
    EFFECT_45 = 0x19,
    LAST_SONG_COMMAND = 0x1F,
    RUN_AT_ONCE = 0x80,         /* $80 and up run as they come; below, $18-$1F and the ignored ones */
    UPLOAD = 0xFE,
    METER_OR_RELOAD = 0xFF,
    RELOAD = 0xF0,              /* $FF's parameter that sends the driver back to the boot ROM */
    SAME_SONG = 0xFF,           /* port 1's answer to a song that is playing already */
    HEADER_ENDS_UPLOAD = 0xE0,  /* on port 1 in a header; on port 0, a header follows */
    RECEIVING = 0xEE,           /* port 1 while an upload's data comes */
    COUNTER_MASK = 0x7F,
    MESSAGE_IS_DATA = 0x80      /* clear in a data message's counter */
};

/* Where uploads go: below the directory is the driver's own, which a
 * receiver takes and drops; the effect set's page and the song's first
 * instrument's tables are loads the C driver takes itself. */
enum { OWN_PAGES_END = 0x1E, EFFECT_SET_PAGE = 0x2F, SONG_INSTRUMENT_TUNINGS = 0x1F40 };

/* The boot ROM's entry, which the driver jumps to with the ROM mapped to be
 * loaded again; and the driver's entry, where the boot upload starts it. */
enum { BOOT_ROM = 0xFFC0, BOOT_ROM_MAPPED = 0x80, DRIVER_ENTRY = 0x0200, DRIVER_RUNNING = 0x0299 };

typedef enum {
    WAITING,          /* for a command, between ticks */
    REFUSING,         /* a song that plays already: until the CPU half moves on */
    FIRST_HEADER,     /* an upload's first header, under the counter after its command */
    FIRST_MESSAGE,    /* the first message after a header, which is always data */
    MESSAGES,         /* data, or a header, or the end */
    ECHO_DRAINING,    /* after a change of the echo's delay: the old buffer's time */
    ECHO_FILLING      /* ... and the new one's */
} Phase;

/* How a header's data is received: bytes a message, into sound memory (none
 * below the driver's own pages); ticking while it comes; or a move within
 * sound memory: a destination, a length and a source, over and over. */
typedef enum { DATA, DATA_TICKING, MOVE, DROPPED } Receiver;
typedef enum { MOVE_TO, MOVE_LENGTH, MOVE_FROM } MoveStep;

struct SoundHost {
    Snes *snes;
    SoundDriver driver;
    AuVoices voices;
    bool booted;
    Phase phase;
    uint32_t timer;             /* SPC700 cycles since the driver's last tick was due */
    unsigned took;              /* cycles the host takes over what it did in this call */

    uint8_t last;               /* the port 0 value the host last answered */
    uint8_t first_counter;
    Receiver receiver;
    uint8_t bytes;              /* a data message's bytes */
    uint16_t at;
    MoveStep move_step;
    uint16_t move_from, move_to;
    bool song_upload;           /* a song's data is coming: the song starts at its end */
    bool song_echo;             /* ... and its command changed the echo's delay */
    uint8_t song_old_delay;
    uint32_t song_echo_cycles;  /* the time since the command, which counts toward the echo's wait */

    unsigned long uploads_into_echo;

    uint32_t wait_left;
    uint32_t fill_cycles;
    SoundCommand queue[SOUND_QUEUE];
    uint8_t queued;
};

/* ---- The voices, into the emulated S-DSP ------------------------------------------------ */

static uint8_t read_register(void *ctx, uint8_t reg)
{
    SoundHost *h = ctx;
    return dsp_read(&h->snes->apu.dsp, reg);
}

static void write_register(void *ctx, uint8_t reg, uint8_t value)
{
    SoundHost *h = ctx;
    dsp_write(&h->snes->apu.dsp, reg, value);
}

static void write_memory(void *ctx, uint16_t at, uint8_t value)
{
    SoundHost *h = ctx;
    h->snes->apu.ram[at] = value;
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

static Apu *apu_of(SoundHost *h) { return &h->snes->apu; }

static void clear_input_ports(SoundHost *h)
{
    memset(apu_of(h)->cpu_in, 0, sizeof apu_of(h)->cpu_in);
}

static void answer(SoundHost *h, uint8_t port0)
{
    apu_of(h)->cpu_out[0] = port0;
    h->last = port0;
}

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

static void tick_while_loading(SoundHost *h)
{
    bool was = quiet();
    sound_tick_while_loading(&h->driver);
    vars_peek(was);
}

static void boot(SoundHost *h)
{
    SoundDriver *d = &h->driver;
    uint8_t old_delay = dsp_read(&apu_of(h)->dsp, DSP_EDL) & 0x0F;
    if (!h->booted)
        memset(d, 0, sizeof *d);
    d->echo_waits = true;
    bool was = quiet();
    sound_boot(d, state_of(h->snes), &h->voices);
    vars_peek(was);
    h->booted = true;
    h->phase = WAITING;
    h->timer = 0;
    h->queued = 0;
    h->song_upload = false;
    apu_of(h)->spc.pc = DRIVER_RUNNING;
    if (d->echo_draining) {
        h->phase = ECHO_DRAINING;
        h->wait_left = old_delay * ECHO_STEP_CYCLES;
        h->fill_cycles = d->echo_delay > old_delay ? (d->echo_delay - old_delay) * ECHO_STEP_CYCLES : 0;
    }
}

/* Back to the boot ROM, for the CPU half to load the driver again. */
static void reload(SoundHost *h)
{
    bool was = quiet();
    sound_run_command(&h->driver, (SoundCommand){ METER_OR_RELOAD, RELOAD, 0, 0 });
    vars_peek(was);
    apu_of(h)->control = BOOT_ROM_MAPPED;
    apu_of(h)->spc.pc = BOOT_ROM;
    h->phase = WAITING;
    h->song_upload = false;
}

/* A command carried out. A change of the echo's delay (command $FC) makes
 * the driver wait, the old delay's time and then up to the new one's. */
static void carry_out(SoundHost *h, SoundCommand c)
{
    if (c.command == METER_OR_RELOAD && c.p1 == RELOAD) {
        reload(h);
        return;
    }
    SoundDriver *d = &h->driver;
    uint8_t old_delay = dsp_read(&apu_of(h)->dsp, DSP_EDL) & 0x0F;
    bool was_draining = d->echo_draining;
    bool was = quiet();
    sound_run_command(d, c);
    vars_peek(was);
    if (!d->echo_draining || was_draining)
        return;
    uint8_t new_delay = d->echo_delay;
    h->phase = ECHO_DRAINING;
    h->wait_left = old_delay * ECHO_STEP_CYCLES;
    h->fill_cycles = new_delay > old_delay ? (new_delay - old_delay) * ECHO_STEP_CYCLES : 0;
}

/* ---- Songs ---------------------------------------------------------------------------------- */

/* A song's command, before its data comes: the driver stops the music and,
 * with a new echo delay, stops the echo writing and moves its buffer at once,
 * out of the way of the samples about to arrive, as the original does (the
 * old buffer can reach down over where they go). The echo's wait runs
 * meanwhile, since the original counts the upload's time toward it; with no
 * echo before, the echo writes again at once. */
static void announce_song(SoundHost *h, SoundCommand c)
{
    SoundDriver *d = &h->driver;
    uint8_t old_delay = dsp_read(&apu_of(h)->dsp, DSP_EDL) & 0x0F;
    bool was = quiet();
    sound_announce_song(d, c);
    h->song_upload = true;
    h->song_echo = d->echo_draining;
    h->song_old_delay = old_delay;
    h->song_echo_cycles = 0;
    if (h->song_echo && !old_delay)
        sound_echo_drained(d);
    vars_peek(was);
}

/* The song, once its data is in; then what is left of the echo's wait: the
 * old delay's time from the command, and the new one's from the end of that
 * (from the command, if there was no echo before). */
static void start_song(SoundHost *h)
{
    SoundDriver *d = &h->driver;
    h->song_upload = false;
    bool was = quiet();
    sound_start_announced_song(d);
    vars_peek(was);
    if (!h->song_echo)
        return;
    uint32_t elapsed = h->song_echo_cycles, fill = d->echo_delay * ECHO_STEP_CYCLES;
    if (d->echo_draining) {
        uint32_t drain = h->song_old_delay * ECHO_STEP_CYCLES;
        h->phase = ECHO_DRAINING;
        h->wait_left = drain > elapsed ? drain - elapsed : 0;
        h->fill_cycles = fill;
    } else {
        h->phase = ECHO_FILLING;
        h->wait_left = fill > elapsed ? fill - elapsed : 0;
    }
}

/* ---- Uploads ------------------------------------------------------------------------------ */

static void begin_upload(SoundHost *h, uint8_t command)
{
    h->first_counter = (uint8_t)((command + 1) & COUNTER_MASK);
    h->phase = FIRST_HEADER;
}

static void end_upload(SoundHost *h)
{
    h->phase = WAITING;
    h->took = COMMAND_CYCLES;
    if (h->song_upload)
        start_song(h);
}

/* A header: where the data goes and how it comes, or the upload's end. The
 * set of effects is the one the CPU half's work RAM says it sent, and command
 * $71's instrument the block its parameter names, as ct_dev's lockstep
 * harness takes them. */
static void header(SoundHost *h, uint8_t counter)
{
    Apu *a = apu_of(h);
    uint8_t kind = a->cpu_in[1];
    uint16_t at = (uint16_t)(a->cpu_in[2] | a->cpu_in[3] << 8);
    if (kind == HEADER_ENDS_UPLOAD) {
        a->cpu_out[1] = 0;
        answer(h, counter);
        clear_input_ports(h);
        end_upload(h);
        return;
    }
    a->cpu_out[1] = kind;
    answer(h, counter);
    static const Receiver receivers[8] = { DROPPED, DATA, DATA, DATA, DROPPED, DATA_TICKING, DROPPED, MOVE };
    static const uint8_t bytes[8] = { 0, 1, 2, 3, 0, 3, 0, 0 };
    uint8_t which = at >> 8 < OWN_PAGES_END ? 0 : kind & 7;
    h->receiver = receivers[which];
    h->bytes = bytes[which];
    h->at = at;
    h->move_step = MOVE_TO;
    h->move_from = at;
    h->phase = FIRST_MESSAGE;

    bool was = quiet();
    if (at >> 8 == EFFECT_SET_PAGE) {
        uint8_t set = snes_bus_peek(&h->snes->bus, RAM_SOUND_DRIVER_2F00_SET_SENT) != 0;
        sound_load_effects(&h->driver, set);
    } else if (at == SONG_INSTRUMENT_TUNINGS && !h->song_upload) {
        uint8_t block = snes_bus_peek(&h->snes->bus, RAM_SOUND_COMMAND_PARAMETERS);
        sound_load_instrument(&h->driver, SOUND_FIRST_SONG_INSTRUMENT, block);
    }
    vars_peek(was);
}

/* A move within sound memory: whole pages up from the first, then what is
 * left down from its last byte, as the original copies them. */
static void move(SoundHost *h, uint16_t length)
{
    uint8_t *ram = apu_of(h)->ram;
    uint16_t from = h->move_from, to = h->move_to;
    for (uint8_t pages = (uint8_t)(length >> 8); pages; pages--) {
        for (uint16_t i = 0; i < 0x100; i++)
            ram[(uint16_t)(to + i)] = ram[(uint16_t)(from + i)];
        from = (uint16_t)(from + 0x100);
        to = (uint16_t)(to + 0x100);
    }
    from--;
    to--;
    for (uint8_t i = (uint8_t)length; i; i--)
        ram[(uint16_t)(to + i)] = ram[(uint16_t)(from + i)];
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

static void upload_byte(SoundHost *h, uint8_t byte)
{
    Apu *a = apu_of(h);
    if (in_echo(&a->dsp, h->at))
        h->uploads_into_echo++;
    a->ram[h->at++] = byte;
}

static void data(SoundHost *h, uint8_t counter)
{
    Apu *a = apu_of(h);
    uint16_t word = (uint16_t)(a->cpu_in[2] | a->cpu_in[3] << 8);
    switch (h->receiver) {
    case DATA:
    case DATA_TICKING:
        for (uint8_t i = 0; i < h->bytes; i++)
            upload_byte(h, a->cpu_in[1 + i]);
        break;
    case MOVE:
        if (h->move_step == MOVE_TO) {
            h->move_to = word;
            h->move_step = MOVE_LENGTH;
        } else if (h->move_step == MOVE_LENGTH) {
            move(h, word);
            h->move_step = MOVE_FROM;
        } else {
            h->move_from = word;
            h->move_step = MOVE_TO;
        }
        break;
    case DROPPED:
        break;
    }
    answer(h, counter);
}

/* ---- The echo's wait ----------------------------------------------------------------------- */

/* While the driver waits, commands are taken and echoed, and carried out when
 * each part of the wait is over; a song's start and an upload wait on the
 * port for the driver to be back. */
static void take_while_waiting(SoundHost *h)
{
    Apu *a = apu_of(h);
    uint8_t command = a->cpu_in[0];
    if (!command || command == UPLOAD || (command >= SONG_COMMANDS && command < FIRST_QUEUED_AFTER_SONGS))
        return;
    SoundCommand c = { command, a->cpu_in[1], a->cpu_in[2], a->cpu_in[3] };
    h->queue[h->queued < SOUND_QUEUE ? h->queued++ : SOUND_QUEUE - 1] = c;
    clear_input_ports(h);
    answer(h, command);
}

/* The commands taken meanwhile, until one starts another wait or sends the
 * driver back to the boot ROM. */
static void run_queue(SoundHost *h)
{
    Phase part = h->phase;
    uint8_t n = h->queued;
    h->queued = 0;
    for (uint8_t i = 0; i < n && h->phase == part; i++)
        carry_out(h, h->queue[i]);
}

static void wait_for_echo(SoundHost *h, unsigned cycles)
{
    take_while_waiting(h);
    if (h->wait_left > cycles) {
        h->wait_left -= cycles;
        return;
    }
    Phase part = h->phase;
    h->took = COMMAND_CYCLES;
    run_queue(h);
    if (h->phase != part)
        return;
    bool was = quiet();
    if (part == ECHO_DRAINING) {
        sound_echo_drained(&h->driver);
        h->phase = ECHO_FILLING;
        h->wait_left = h->fill_cycles;
    } else {
        sound_echo_ready(&h->driver);
        h->phase = WAITING;
    }
    vars_peek(was);
}

/* ---- Answering the ports --------------------------------------------------------------------- */

static bool starts_song(uint8_t command) { return command >= SONG_COMMANDS && command < EFFECT_67 && !(command & 2); }

/* Whether there was a command. */
static bool take_command(SoundHost *h)
{
    Apu *a = apu_of(h);
    uint8_t command = a->cpu_in[0];
    if (!command)
        return false;
    SoundCommand c = { command, a->cpu_in[1], a->cpu_in[2], a->cpu_in[3] };
    clear_input_ports(h);
    if (command >= RUN_AT_ONCE) {
        answer(h, command);
        if (command == UPLOAD)
            begin_upload(h, command);
        else
            carry_out(h, c);
        return true;
    }
    if (starts_song(command)) {
        if (c.p1 == h->driver.last_song_started) {
            a->cpu_out[1] = SAME_SONG;
            answer(h, command);
            h->phase = REFUSING;
            return true;
        }
        a->cpu_out[1] = c.p1;
        answer(h, command);
        announce_song(h, c);
        begin_upload(h, command);
        return true;
    }
    if (command == EFFECT_67 || command == EFFECT_45) {
        answer(h, command);
        carry_out(h, c);
        return true;
    }
    if (command >= SONG_COMMANDS && command <= LAST_SONG_COMMAND)
        return true;   /* the original's table has no answer for the rest of $10-$1F; the game never sends them */
    answer(h, command);   /* the rest are echoed and ignored */
    return true;
}

/* A message during an upload: data (its counter's top bit clear), a header
 * (under $E0), the driver sent back to the boot ROM, or nothing new. */
static void take_message(SoundHost *h)
{
    Apu *a = apu_of(h);
    uint8_t counter = a->cpu_in[0];
    if (counter == h->last)
        return;
    if (!(counter & MESSAGE_IS_DATA))
        data(h, counter);
    else if (counter == HEADER_ENDS_UPLOAD)
        header(h, counter);
    else if (counter == METER_OR_RELOAD && a->cpu_in[1] == RELOAD)
        reload(h);
    else
        h->last = counter;
}

static unsigned run_host(void *ctx, Apu *apu, unsigned cycles)
{
    SoundHost *h = ctx;
    if (apu->spc.pc == DRIVER_ENTRY)
        boot(h);
    h->timer += cycles;
    h->took = 0;
    if (h->song_upload && h->song_echo_cycles < UINT32_MAX - cycles)
        h->song_echo_cycles += cycles;
    switch (h->phase) {
    case WAITING:
        if (tick_due(h))
            tick(h);
        else if (take_command(h))
            h->took = COMMAND_CYCLES;
        break;
    case REFUSING:
        if (apu->cpu_in[0]) {
            clear_input_ports(h);
            h->phase = WAITING;
        }
        break;
    case FIRST_HEADER: {
        uint8_t counter = apu->cpu_in[0];
        if (counter == h->first_counter)
            header(h, counter);
        else if (counter && counter == h->last)
            answer(h, counter);   /* the command again: answered again */
        break;
    }
    case FIRST_MESSAGE:
        if (apu->cpu_in[0] != h->last) {
            apu->cpu_out[1] = RECEIVING;
            h->phase = MESSAGES;
            data(h, apu->cpu_in[0]);
        }
        break;
    case MESSAGES:
        if (h->receiver == DATA_TICKING && tick_due(h))
            tick_while_loading(h);
        else
            take_message(h);
        break;
    case ECHO_DRAINING:
    case ECHO_FILLING:
        wait_for_echo(h, cycles);
        break;
    }
    if (h->took <= cycles)
        return cycles;
    h->timer += h->took - cycles;
    return h->took;
}

SoundHost *sound_host_attach(Snes *s)
{
    SoundHost *h = calloc(1, sizeof *h);
    if (!h)
        return NULL;
    h->snes = s;
    h->voices = (AuVoices){ h, read_register, write_register, write_memory };
    s->apu.host = run_host;
    s->apu.host_ctx = h;
    return h;
}

unsigned long sound_host_uploads_into_echo(const SoundHost *h) { return h->uploads_into_echo; }

void sound_host_detach(SoundHost *h)
{
    if (!h)
        return;
    h->snes->apu.host = NULL;
    h->snes->apu.host_ctx = NULL;
    free(h);
}
