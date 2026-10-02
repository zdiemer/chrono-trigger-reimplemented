#include "sound_lockstep.h"

#include <stdlib.h>
#include <string.h>

#include "core/sound_data.h"
#include "platform/voices_dsp.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "sound/sound.h"
#include "sound_audio.h"
#include "sound_state.h"

/* The original's program, where it is watched (see the header). */
enum {
    BOOT = 0x0200,
    LOOP = 0x0299,
    LOOP_WAITED = 0x029C,
    TICK = 0x02A0,
    STATUS_SHOWN = 0x02E2,
    FIRST_QUEUE = 0x02F2,
    SECOND_QUEUE = 0x036E,
    AFTER_SECOND_QUEUE = 0x0371,
    LOADING_TICK = 0x0415,
    COMMAND = 0x0BAC,
    SONG_UPLOADED = 0x0C08,
    SONG_ECHO_READY = 0x0CE5,
    UPLOAD_HEADER = 0x1035,
    DELAY_ECHO_READY = 0x134B,
    ECHO_WRITES_AGAIN = 0x1397,
    METER_READS_LEVEL = 0x145C
};
enum { COMMAND_BYTES = 0x8D, UPLOAD_ADDRESS = 0x97, UPLOAD_ENDS = 0xE0, EFFECTS_PAGE = 0x2F, SONG_INSTRUMENTS = 0x1F40 };
enum { KON = 0x4C, KOFF = 0x5C, OUTX = 0x09, ESA = 0x6D };
enum { CHANGE_ECHO_DELAY = 0xFC, SONG_READY = 0x80 };
/* Where the boot's own samples start; the songs' lie after them. */
enum { SAMPLES_FROM = 0x4700, SONG_DIRECTORY = 0x1E80, SONG_DIRECTORY_BYTES = 4 * SOUND_SAMPLE_PLACES };
enum { REPORTED = 1000 };

/* How far through a tick the C driver has gone. */
typedef enum { NO_TICK, TICK_OPEN, BEGUN, VOICED, ENDED } TickPart;

struct SoundLockstep {
    Snes *snes;
    SoundCompareLevel level;
    SoundDriver driver;
    AuVoices voices;
    uint8_t regs[128];
    uint8_t memory[0x10000];
    uint8_t levels[8];         /* the voices' output levels the original's meter read this tick */
    unsigned levels_read, levels_given;
    SoundSide original, port;
    SoundAudio *audio;         /* the C driver heard beside the original, or NULL */

    bool booted, ever_booted;
    TickPart part;
    bool song_open;            /* a song's command came: it starts once its upload is in */

    unsigned long long ticks, loading_ticks, commands, loads, differing_ticks;
    bool diverged;
    uint32_t first_frame;
    unsigned long long first_tick;
    SoundDivergence first;
    char reported[REPORTED][160];
    unsigned reported_count;
    char state[1024];          /* where the two drivers' states differed at the first difference */
    unsigned state_differences;

    /* The C side's samples against the original CPU half's (sound_lockstep_check_places). */
    bool check_places, places_due;
    unsigned long places_checked, places_differing, instruments_refused;
    char places_first[200];
};

/* ---- The C driver's voices: a copy of the S-DSP's registers ------------------------------ */

static uint8_t read_register(void *ctx, uint8_t reg)
{
    SoundLockstep *l = ctx;
    reg &= 0x7F;
    if (sound_register_compared(reg))
        return l->regs[reg];
    /* what the S-DSP changes itself is read from the real one: an output
     * level (which changes every sample) as the original read it */
    if ((reg & 0x0F) == OUTX && l->levels_given < l->levels_read)
        return l->levels[l->levels_given++ % 8];
    return l->snes->apu.dsp.regs[reg];
}

static void write_register(void *ctx, uint8_t reg, uint8_t value)
{
    SoundLockstep *l = ctx;
    reg &= 0x7F;
    l->regs[reg] = value;
    if (l->audio)
        sound_audio_write(l->audio, &l->snes->apu, reg, value);
    if (reg == KON || reg == KOFF)
        sound_side_key_write(&l->port, reg, value);
}

static void write_memory(void *ctx, uint16_t at, uint8_t value)
{
    SoundLockstep *l = ctx;
    l->memory[at] = value;
}

static uint8_t read_memory(void *ctx, uint16_t at)
{
    SoundLockstep *l = ctx;
    return l->memory[at];
}

SoundLockstep *sound_lockstep_create(Snes *s, SoundCompareLevel level)
{
    SoundLockstep *l = calloc(1, sizeof *l);
    if (!l)
        return NULL;
    l->snes = s;
    l->level = level;
    l->voices = (AuVoices){ l, read_register, write_register, write_memory, read_memory };
    return l;
}

void sound_lockstep_destroy(SoundLockstep *l) { free(l); }

void sound_lockstep_listen(SoundLockstep *l, SoundAudio *audio) { l->audio = audio; }

/* ---- Running the C driver ---------------------------------------------------------------- */

/* The C driver reads the cartridge through the game's accessors, which must
 * leave the machine as it is: it runs between the SPC700's instructions. */
static bool quiet_begin(void)
{
    bool was = vars_peeking();
    vars_peek(true);
    return was;
}

static void quiet_end(bool was) { vars_peek(was); }

typedef enum { RUN_COMMAND, ANNOUNCE_SONG, LOAD_SONG, LOAD_EFFECTS, LOAD_EFFECTS_AGAIN, LOAD_INSTRUMENT, ECHO_DRAINED, ECHO_READY, TICK_BEGIN, TICK_VOICES, TICK_END, LOADING } Step;

static void drive(SoundLockstep *l, Step step, SoundCommand c, uint16_t value)
{
    bool was = quiet_begin();
    SoundDriver *d = &l->driver;
    switch (step) {
    case RUN_COMMAND:
        sound_run_command(d, c);
        /* the CPU half keeps the echo's new buffer free of samples once the
         * command is echoed */
        if (c.command == CHANGE_ECHO_DELAY)
            sound_keep_echo_free(d, c.p1);
        break;
    case ANNOUNCE_SONG:
        /* ... and a song's before it sends the command; the set of effects
         * the game asked for comes with a song that isn't refused */
        sound_keep_echo_free(d, c.p3);
        if (sound_announce_song(d, c))
            sound_ask_for_effects(d, (uint8_t)value);
        break;
    case LOAD_SONG:
        sound_load_announced_song(d);
        break;
    case LOAD_EFFECTS:
        sound_load_effects(d, (uint8_t)value);
        break;
    case LOAD_EFFECTS_AGAIN:
        if (d->samples.effects_overwritten)
            sound_load_effects(d, d->samples.effect_set);
        break;
    case LOAD_INSTRUMENT:
        /* the original's CPU half found that the sample fits; ct_sound_fuzz
         * plays a CPU half of its own that loads the instrument always */
        if (!sound_load_song_instrument(d, value)) {
            sound_load_instrument(d, SOUND_FIRST_SONG_INSTRUMENT, value);
            l->instruments_refused++;
        }
        break;
    case ECHO_DRAINED:
        sound_echo_drained(d);
        break;
    case ECHO_READY:
        sound_echo_ready(d);
        break;
    case TICK_BEGIN:
        sound_tick_begin(d);
        l->port.has_status = !d->stopped;
        memcpy(l->port.status, d->status.port, 4);
        break;
    case TICK_VOICES:
        sound_tick_voices(d);
        break;
    case TICK_END:
        sound_tick_end(d);
        break;
    case LOADING:
        sound_tick_while_loading(d);
        break;
    }
    quiet_end(was);
}

static void tick_to(SoundLockstep *l, TickPart part)
{
    static const Step steps[] = { [BEGUN] = TICK_BEGIN, [VOICED] = TICK_VOICES, [ENDED] = TICK_END };
    if (l->part == NO_TICK)
        return;
    while (l->part < part) {
        l->part++;
        drive(l, steps[l->part], (SoundCommand){ 0 }, 0);
    }
}

/* A tick the original cut short, paused before it took its commands, has
 * shown its status all the same. */
static void close_tick(SoundLockstep *l)
{
    tick_to(l, BEGUN);
    l->part = NO_TICK;
    if (l->audio)
        sound_audio_in_tick(l->audio, false);
}

/* ---- Comparing ---------------------------------------------------------------------------- */

static void fill_memory(uint8_t out[SOUND_COMPARED_MEMORY], const uint8_t *memory)
{
    memcpy(out, memory + VOICES_DSP_OWN_SAMPLE, VOICES_DSP_OWN_SAMPLE_BYTES);
    memcpy(out + VOICES_DSP_OWN_SAMPLE_BYTES, memory + VOICES_DSP_DIRECTORY + 4 * 0x10, 4);
}

static void compare(SoundLockstep *l)
{
    const Apu *apu = &l->snes->apu;
    memcpy(l->original.regs, apu->dsp.regs, sizeof l->original.regs);
    memcpy(l->port.regs, l->regs, sizeof l->port.regs);
    fill_memory(l->original.memory, apu->ram);
    fill_memory(l->port.memory, l->memory);
    SoundDivergence d;
    bool differs = sound_compare(&l->original, &l->port, l->level, &d);
    if (!differs && l->level == SOUND_COMPARE_STATE) {
        unsigned fields = sound_state_differences(&l->driver, apu->ram, NULL, 0);
        if (fields) {
            d = (SoundDivergence){ SOUND_STATE_DIFFERS, (uint16_t)fields, 0, 0, 0 };
            differs = true;
        }
    }
    if (differs) {
        l->differing_ticks++;
        if (!l->diverged) {
            l->diverged = true;
            l->state_differences = sound_state_differences(&l->driver, apu->ram, l->state, sizeof l->state);
            l->first = d;
            l->first_frame = l->snes->frame;
            l->first_tick = l->ticks + l->loading_ticks;
        }
        if (l->reported_count < REPORTED) {
            char what[128];
            sound_divergence_describe(&d, what, sizeof what);
            snprintf(l->reported[l->reported_count++], sizeof l->reported[0], "frame %u tick %llu: %s",
                     (unsigned)l->snes->frame, l->ticks + l->loading_ticks, what);
        }
    }
    sound_side_clear_writes(&l->original);
    sound_side_clear_writes(&l->port);
}

/* ---- The samples' places against the original CPU half's ---------------------------------- */

void sound_lockstep_check_places(SoundLockstep *l) { l->check_places = true; }

static uint16_t wram_word(const SoundLockstep *l, uint32_t at)
{
    return (uint16_t)(snes_bus_peek(&l->snes->bus, at) | snes_bus_peek(&l->snes->bus, at + 1) << 8);
}

static void place_differs(SoundLockstep *l, const char *what, unsigned index, unsigned want, unsigned got)
{
    if (!l->places_differing++)
        snprintf(l->places_first, sizeof l->places_first, "frame %u: %s[%u] is $%04X in the CPU half, $%04X here",
                 (unsigned)l->snes->frame, what, index, want, got);
}

/* Once the CPU half is done with a song, a change of the echo's delay or an
 * instrument (between commands, its upload over), its tables in work RAM
 * against the C side's places, and the samples and the directory's entries
 * in sound memory, below where the echo writes. */
static void compare_places(SoundLockstep *l)
{
    const SoundSamples *p = &l->driver.samples;
    if (snes_bus_peek(&l->snes->bus, RAM_SOUND_COMMAND) != 0 ||
        !(snes_bus_peek(&l->snes->bus, RAM_SOUND_DRIVER_SONG) & SONG_READY))
        return;
    l->places_due = false;
    l->places_checked++;
    unsigned before = (unsigned)l->places_differing;
    for (unsigned i = 0; i < SOUND_SAMPLE_PLACES; i++) {
        uint16_t block = wram_word(l, RAM_SOUND_DRIVER_SLOT_IDS + 2 * i);
        uint16_t length = wram_word(l, RAM_SOUND_DRIVER_SLOT_LENGTHS + 2 * i);
        if (block != p->block[i])
            place_differs(l, "block", i, block, p->block[i]);
        if (length != p->length[i])
            place_differs(l, "length", i, length, p->length[i]);
        uint16_t start = wram_word(l, RAM_SOUND_DRIVER_ADDRESS_TABLE + 4 * i);
        uint16_t loop = wram_word(l, RAM_SOUND_DRIVER_ADDRESS_TABLE + 4 * i + 2);
        if (start != p->song_start[i] || loop != p->song_loop[i])
            place_differs(l, "song_start", i, start, p->song_start[i]);
    }
    for (unsigned i = 0; i <= SOUND_SAMPLE_PLACES; i++) {
        uint16_t start = wram_word(l, RAM_SOUND_DRIVER_SLOT_ADDRESSES + 2 * i);
        if (start != p->start[i])
            place_differs(l, "start", i, start, p->start[i]);
    }
    uint8_t reserve = snes_bus_peek(&l->snes->bus, RAM_SOUND_DRIVER_RESERVE);
    uint8_t limit = snes_bus_peek(&l->snes->bus, RAM_SOUND_DRIVER_SLOT_LIMIT);
    uint16_t upload_slot = wram_word(l, RAM_SOUND_DRIVER_UPLOAD_SLOT);
    uint8_t pending = snes_bus_peek(&l->snes->bus, RAM_SOUND_DRIVER_2F00_PENDING);
    uint8_t set = snes_bus_peek(&l->snes->bus, RAM_SOUND_DRIVER_2F00_SET_SENT);
    if (reserve != p->echo_delay || limit != p->echo_page)
        place_differs(l, "echo_delay", 0, (unsigned)(reserve << 8 | limit), (unsigned)(p->echo_delay << 8 | p->echo_page));
    if (upload_slot != 2 * p->instrument_place)
        place_differs(l, "instrument_place", 0, upload_slot / 2u, p->instrument_place);
    if (pending != p->effects_overwritten || set != p->effect_set)
        place_differs(l, "effects", 0, (unsigned)(set << 8 | pending), (unsigned)(p->effect_set << 8 | p->effects_overwritten));

    const uint8_t *original = l->snes->apu.ram;
    for (unsigned at = SONG_DIRECTORY; at < SONG_DIRECTORY + SONG_DIRECTORY_BYTES; at++)
        if (original[at] != l->memory[at]) {
            place_differs(l, "directory byte", at, original[at], l->memory[at]);
            break;
        }
    uint32_t echo = (uint32_t)(l->snes->apu.dsp.regs[ESA] < l->snes->apu.dsp.echo_esa ? l->snes->apu.dsp.regs[ESA]
                                                                                       : l->snes->apu.dsp.echo_esa)
                    << 8;
    for (unsigned i = 0; i < SOUND_SAMPLE_PLACES && p->block[i]; i++) {
        for (uint32_t at = p->start[i]; at < (uint32_t)p->start[i] + p->length[i] && at < echo; at++)
            if (original[at] != l->memory[at]) {
                place_differs(l, "sample byte", at, original[at], l->memory[at]);
                break;
            }
    }
    if (l->places_differing != before && l->reported_count < REPORTED)
        snprintf(l->reported[l->reported_count++], sizeof l->reported[0], "frame %u: the samples' places differ",
                 (unsigned)l->snes->frame);
}

/* What a song moves and loads starts from what the original's memory holds,
 * which the echo may have run over where the C side's hasn't. */
static void sync_samples(SoundLockstep *l)
{
    if (l->check_places)
        memcpy(l->memory + SAMPLES_FROM, l->snes->apu.ram + SAMPLES_FROM, 0x10000 - SAMPLES_FROM);
}

/* ---- Watching the original ------------------------------------------------------------------ */

static void boot(SoundLockstep *l, const Apu *apu)
{
    if (!l->ever_booted) {
        memcpy(l->regs, apu->dsp.regs, sizeof l->regs);
        memcpy(l->memory, apu->ram, sizeof l->memory);
        l->ever_booted = true;
    }
    close_tick(l);
    if (l->audio)
        sound_audio_boot(l->audio, &l->snes->apu);
    bool was = quiet_begin();
    sound_boot(&l->driver, state_of(l->snes), &l->voices);
    quiet_end(was);
    l->driver.echo_waits = true;
    l->booted = true;
    l->song_open = false;
    sound_side_clear_writes(&l->original);
    sound_side_clear_writes(&l->port);
}

static void begin_tick(SoundLockstep *l, bool loading)
{
    close_tick(l);
    compare(l);
    if (l->places_due)
        compare_places(l);
    if (l->audio) {
        sound_audio_tick(l->audio, &l->snes->apu);
        sound_audio_in_tick(l->audio, true);
    }
    if (loading) {
        l->loading_ticks++;
        drive(l, LOADING, (SoundCommand){ 0 }, 0);
        if (l->audio)
            sound_audio_in_tick(l->audio, false);
        return;
    }
    l->ticks++;
    l->levels_read = l->levels_given = 0;
    l->part = TICK_OPEN;
}

static bool song_start(uint8_t command) { return command >= 0x10 && command < 0x18 && !(command & 2); }

static void command(SoundLockstep *l, const Apu *apu)
{
    SoundCommand c = { apu->ram[COMMAND_BYTES], apu->ram[COMMAND_BYTES + 1], apu->ram[COMMAND_BYTES + 2],
                       apu->ram[COMMAND_BYTES + 3] };
    l->commands++;
    if (song_start(c.command)) {
        l->song_open = true;
        sync_samples(l);
        drive(l, ANNOUNCE_SONG, c, snes_bus_peek(&l->snes->bus, RAM_SOUND_DRIVER_2F00_SET));
        return;
    }
    drive(l, RUN_COMMAND, c, 0);
    if (c.command == CHANGE_ECHO_DELAY)
        l->places_due = l->check_places;
}

/* A header of the CPU half's upload: what it loads, if it is something the C
 * driver takes a load for. The set of effects is the one the CPU half says it
 * sent; command $71's block is its parameter. A song's own tables the C
 * driver loads itself when the song starts. */
static void upload_header(SoundLockstep *l, const Apu *apu)
{
    if (apu->spc.a == UPLOAD_ENDS)
        return;
    uint16_t address = (uint16_t)(apu->ram[UPLOAD_ADDRESS] | apu->ram[UPLOAD_ADDRESS + 1] << 8);
    if (address >> 8 == EFFECTS_PAGE && !l->song_open) {
        uint8_t set = snes_bus_peek(&l->snes->bus, RAM_SOUND_DRIVER_2F00_SET_SENT) != 0;
        drive(l, LOAD_EFFECTS, (SoundCommand){ 0 }, set);
        l->loads++;
    } else if (address >> 8 == EFFECTS_PAGE) {
        /* with a song: loaded again because the last song ran over them,
         * which the C side decides for itself (sound_load_announced_song);
         * it does so here, before the song's start is compared */
        drive(l, LOAD_EFFECTS_AGAIN, (SoundCommand){ 0 }, 0);
        l->loads++;
    } else if (address == SONG_INSTRUMENTS && !l->song_open) {
        uint8_t block = snes_bus_peek(&l->snes->bus, RAM_SOUND_COMMAND_PARAMETERS);
        drive(l, LOAD_INSTRUMENT, (SoundCommand){ 0 }, block);
        l->loads++;
        l->places_due = l->check_places;
    }
}

void sound_lockstep_insn(SoundLockstep *l, const Apu *apu)
{
    uint16_t pc = apu->spc.pc;
    if (l->audio)
        sound_audio_watch(l->audio, &l->snes->apu);
    if (pc == BOOT) {
        boot(l, apu);
        return;
    }
    if (!l->booted)
        return;
    switch (pc) {
    case TICK:
        begin_tick(l, false);
        break;
    case LOADING_TICK:
        begin_tick(l, true);
        break;
    case STATUS_SHOWN:
        memcpy(l->original.status, apu->cpu_out, 4);
        l->original.has_status = true;
        break;
    case FIRST_QUEUE:
        tick_to(l, BEGUN);
        break;
    case SECOND_QUEUE:
        tick_to(l, VOICED);
        break;
    case AFTER_SECOND_QUEUE:
        tick_to(l, ENDED);
        break;
    case LOOP:
    case LOOP_WAITED:
        close_tick(l);
        l->song_open = false;
        break;
    case COMMAND:
        command(l, apu);
        break;
    case SONG_UPLOADED:
        /* what the song's command did before its data came: the music
         * stopped and the echo moved out of the samples' way */
        compare(l);
        if (l->song_open) {
            drive(l, LOAD_SONG, (SoundCommand){ 0 }, 0);
            l->places_due = l->check_places;
        }
        l->song_open = false;
        break;
    case ECHO_WRITES_AGAIN:
        drive(l, ECHO_DRAINED, (SoundCommand){ 0 }, 0);
        break;
    case SONG_ECHO_READY:
    case DELAY_ECHO_READY:
        drive(l, ECHO_READY, (SoundCommand){ 0 }, 0);
        break;
    case UPLOAD_HEADER:
        upload_header(l, apu);
        break;
    case METER_READS_LEVEL:
        l->levels[l->levels_read++ % 8] = apu->dsp.regs[apu->dsp_addr & 0x7F];
        break;
    default:
        break;
    }
}

void sound_lockstep_dsp_write(SoundLockstep *l, const Apu *apu, uint8_t reg, uint8_t value)
{
    reg &= 0x7F;
    if (l->audio)
        sound_audio_original_write(l->audio, apu, reg, value);
    if (l->booted && (reg == KON || reg == KOFF))
        sound_side_key_write(&l->original, reg, value);
}

bool sound_lockstep_diverged(const SoundLockstep *l) { return l->diverged; }

bool sound_lockstep_report(SoundLockstep *l, const char *path, FILE *summary)
{
    static const char *levels[] = { "status", "status and keys", "status, keys and S-DSP registers",
                                    "status, keys, S-DSP registers and state" };
    if (path) {
        FILE *f = fopen(path, "w");
        if (f) {
            for (unsigned i = 0; i < l->reported_count; i++)
                fprintf(f, "%s\n", l->reported[i]);
            fclose(f);
        }
    }
    if (!l->ever_booted) {
        fprintf(summary, "sound lockstep: the driver never booted (it needs a run from power-on)\n");
        return false;
    }
    fprintf(summary, "sound lockstep (%s): %llu ticks, %llu while loading, %llu commands, %llu loads", levels[l->level],
            l->ticks, l->loading_ticks, l->commands, l->loads);
    if (l->driver.stray_reads)
        fprintf(summary, ", %lu reads outside the C driver's sound memory (the first at $%04X)",
                (unsigned long)l->driver.stray_reads, l->driver.first_stray_read);
    bool places_same = true;
    if (l->check_places) {
        places_same = !l->places_differing && !l->instruments_refused;
        fprintf(summary, "%s; the samples' places checked %lu times against the CPU half's: %s",
                l->diverged ? "" : ": identical", l->places_checked,
                l->places_differing ? l->places_first : "identical");
        if (l->instruments_refused)
            fprintf(summary, ", but %lu of its instruments refused here", l->instruments_refused);
        if (!l->diverged) {
            fprintf(summary, "\n");
            return places_same;
        }
    }
    if (!l->diverged) {
        fprintf(summary, ": identical\n");
        return true;
    }
    char what[128];
    sound_divergence_describe(&l->first, what, sizeof what);
    fprintf(summary, ": %llu ticks differ, the first at frame %u tick %llu: %s\n", l->differing_ticks,
            (unsigned)l->first_frame, l->first_tick, what);
    if (l->state_differences)
        fprintf(summary, "  %u fields of the drivers' state differed there (C/original):%s\n", l->state_differences,
                l->state);
    return false;
}
