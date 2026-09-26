/* ct_sound_fuzz: the C sound driver (game/sound/) against the original on
 * command streams no movie plays (docs/SOUND.md, "Validating the port").
 *
 *   ct_sound_fuzz --rom roms/ct-usa.sfc [--runs N] [--commands N] [--seed S]
 *                 [--made-up-songs 1] [--spc-coverage out.txt]
 *                 [--level status|keys|dsp|state] [--sound-driver c]
 *
 * The APU runs alone. The driver's boot blocks go into sound memory as the
 * CPU half's boot upload leaves them, and it starts at $0200; this program
 * then plays the CPU half's part through the ports (game/core/sound_driver.c
 * describes the protocol): commands and their parameters, echoes waited for;
 * a song's start with its upload (its instruments' tables, the effect set
 * again when the last song ran over it, its sequence and percussion); the
 * effect set switched (command $70's upload, which ticks while it loads); an
 * instrument loaded into the song's first place (command $71's); and the
 * driver sent back to the boot ROM and loaded again (command $FF $F0). The
 * commands are drawn at random, songs from all of them, at random intervals
 * from a tick to seconds; with --made-up-songs, half the songs are random
 * ones of the fuzzer's own (below). The lockstep harness (dev/sound_lockstep.h) runs
 * the C driver beside the original and compares every tick; a run stops at
 * its first difference and prints the commands that led to it.
 *
 * With --sound-driver c there is no original: the C driver plays in the
 * SPC700's place as ct_play's does (frontend/sound_host.h), and a run checks
 * that it answers the CPU half's every command and upload, and that nothing
 * is uploaded where the echo is writing (sound_host_uploads_into_echo).
 *
 * The CPU half's work RAM the harness reads (the effect set sent, command
 * $71's block) is written here as the CPU half would have it. The data the
 * CPU half uploads with a song that the driver itself doesn't read (samples,
 * the directory) isn't sent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/sound_data.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "snes/system.h"
#include "sound/sound.h"
#include "sound_host.h"
#include "sound_lockstep.h"

static Snes snes;
static uint64_t rng;
static uint32_t *coverage;
static SoundLockstep *lockstep;
static bool hosted;           /* the C driver in the SPC700's place */
static SoundHost *host;

enum {
    FRAME_CLOCKS = 357368,    /* master clocks a frame */
    STEP_CLOCKS = 1000,
    AFTER_ECHO_CLOCKS = 3000,
    ECHO_LIMIT = 40 * FRAME_CLOCKS,
    ECHO_CHANGE_CLOCKS = 40 * FRAME_CLOCKS,   /* two delays of 15 at 16 ms a unit, and some */
    UPLOAD_ENDS = 0xE0,
    KIND_TABLE = 2,
    KIND_DATA = 3,
    KIND_DATA_TICKING = 5
};

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}
static uint32_t below(uint32_t n) { return n ? next() % n : 0; }

static void on_insn(void *ctx, const Apu *apu)
{
    (void)ctx;
    if (coverage && coverage[apu->spc.pc] != UINT32_MAX)
        coverage[apu->spc.pc]++;
    sound_lockstep_insn(lockstep, apu);
}

static void on_dsp_write(void *ctx, const Apu *apu, uint8_t reg, uint8_t value)
{
    (void)ctx;
    sound_lockstep_dsp_write(lockstep, apu, reg, value);
}

static void run(uint32_t clocks)
{
    while (clocks) {
        uint32_t chunk = clocks < FRAME_CLOCKS ? clocks : FRAME_CLOCKS;
        apu_advance(&snes.apu, chunk);
        int16_t sink[2 * 1024];
        while (apu_take_samples(&snes.apu, sink, 1024))
            ;
        clocks -= chunk;
    }
}

static const Apu *apu(void) { return &snes.apu; }
static void port(uint8_t p, uint8_t v) { apu_write_port(&snes.apu, p, v); }

/* Run until the driver shows `value` on port 0 (and, if taken, has taken the
 * message off its port); false if it doesn't in time. The driver echoes a
 * message before it has read all of it, so the CPU half's own pace after an
 * echo is kept too. */
static bool await(uint8_t value, bool taken)
{
    for (uint32_t t = 0; t < ECHO_LIMIT; t += STEP_CLOCKS) {
        if (apu()->cpu_out[0] == value && (!taken || apu()->cpu_in[0] == 0)) {
            run(AFTER_ECHO_CLOCKS);
            return true;
        }
        run(STEP_CLOCKS);
    }
    fprintf(stderr, "  no answer %02X: out %02X %02X in %02X pc %04X sp %02X\n", value, apu()->cpu_out[0],
            apu()->cpu_out[1], apu()->cpu_in[0], apu()->spc.pc, apu()->spc.sp);
    return false;
}

/* ---- The CPU half's side of an upload -------------------------------------------------- */

typedef struct {
    uint8_t counter;
    bool first;
} Upload;

static bool header(Upload *u, uint16_t address, uint8_t kind)
{
    uint8_t c = u->first ? u->counter : UPLOAD_ENDS;
    port(2, (uint8_t)address);
    port(3, (uint8_t)(address >> 8));
    port(1, kind);
    port(0, c);
    if (!await(c, false))
        return false;
    u->counter = (uint8_t)((c + 1) & 0x7F);
    u->first = false;
    return true;
}

static bool message(Upload *u, uint8_t a, uint8_t b, uint8_t c)
{
    port(1, a);
    port(2, b);
    port(3, c);
    port(0, u->counter);
    if (!await(u->counter, false))
        return false;
    u->counter = (uint8_t)((u->counter + 1) & 0x7F);
    return true;
}

static bool send_bytes(Upload *u, uint16_t address, uint8_t kind, const uint8_t *bytes, uint16_t length)
{
    if (!header(u, address, kind))
        return false;
    for (uint32_t i = 0; i < length; i += 3)
        if (!message(u, bytes[i], i + 1 < length ? bytes[i + 1] : 0, i + 2 < length ? bytes[i + 2] : 0))
            return false;
    return true;
}

static bool send_words(Upload *u, uint16_t address, const uint16_t *words, unsigned n)
{
    if (!header(u, address, KIND_TABLE))
        return false;
    for (unsigned i = 0; i < n; i++)
        if (!message(u, (uint8_t)words[i], (uint8_t)(words[i] >> 8), 0))
            return false;
    return true;
}

static bool finish(Upload *u)
{
    uint8_t c = u->first ? u->counter : UPLOAD_ENDS;
    port(1, UPLOAD_ENDS);
    port(0, c);
    return await(c, false);
}

/* ---- What the fuzzer sends --------------------------------------------------------------- */

static const GameState *game(void) { return state_of(&snes); }
static uint8_t effect_set;
static uint16_t effect_count;
static bool set_overwritten, refused;

static uint8_t *effect_set_bytes(uint8_t set, uint16_t *length)
{
    static uint8_t bytes[0x1800];
    *length = sound_effect_set_length(game(), set);
    if (*length > sizeof bytes)
        *length = sizeof bytes;
    for (uint16_t i = 0; i < *length; i++)
        bytes[i] = sound_effect_set_byte(game(), set, i);
    return bytes;
}

static bool send_command(uint8_t command, uint8_t p1, uint8_t p2, uint8_t p3)
{
    if (!await(apu()->cpu_out[0], true))
        return false;
    port(3, p3);
    port(2, p2);
    port(1, p1);
    port(0, command);
    return await(command, true);
}

static bool play_song(uint8_t command, uint8_t song, uint8_t volume, uint8_t echo)
{
    if (!send_command(command, song, volume, echo))
        return false;
    uint8_t counter = (uint8_t)((command + 1) & 0x7F);
    refused = apu()->cpu_out[1] & 0x80;   /* the same song again */
    if (refused) {
        port(0, counter);
        run(FRAME_CLOCKS);
        return true;
    }
    Upload u = { counter, true };
    uint16_t tunings[16], envelopes[16];
    bool listed = true;
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t block = listed ? sound_song_instrument_block(game(), song, i) : 0;
        listed = block != 0;
        tunings[i] = sound_instrument_tuning(game(), block);
        envelopes[i] = sound_instrument_envelope(game(), block);
    }
    if (!send_words(&u, 0x1F40, tunings, 16) || !send_words(&u, 0x1FC0, envelopes, 16))
        return false;
    if (set_overwritten) {
        uint16_t length;
        const uint8_t *set = effect_set_bytes(effect_set, &length);
        if (!send_bytes(&u, SOUND_EFFECTS, KIND_DATA, set, length))
            return false;
    }
    static uint8_t sequence[0x2700];
    uint16_t length = sound_song_length(game(), song);
    uint16_t sent = (uint16_t)(3 * (length / 3 + 1));
    if (sent > sizeof sequence)
        sent = sizeof sequence;
    for (uint16_t i = 0; i < sent; i++)
        sequence[i] = sound_song_byte(game(), song, i);
    set_overwritten = length >= SOUND_EFFECTS - SOUND_SEQUENCES;
    uint8_t percussion[36];
    for (uint8_t i = 0; i < sizeof percussion; i++)
        percussion[i] = sound_song_percussion_byte(game(), song, i);
    if (!send_bytes(&u, SOUND_SEQUENCES, KIND_DATA, sequence, sent) ||
        !send_bytes(&u, SOUND_PERCUSSION, KIND_DATA, percussion, sizeof percussion))
        return false;
    return finish(&u);
}

enum { LOAD = 0xFE };

static bool switch_effect_set(uint8_t set)
{
    effect_set = set;
    snes.bus.wram[RAM_ABS(RAM_SOUND_DRIVER_2F00_SET_SENT)] = set;
    if (!send_command(LOAD, 0, 0, 0))
        return false;
    Upload u = { (LOAD + 1) & 0x7F, true };
    uint16_t length;
    const uint8_t *bytes = effect_set_bytes(set, &length);
    return send_bytes(&u, SOUND_EFFECTS, KIND_DATA_TICKING, bytes, length) && finish(&u);
}

static bool load_instrument(uint8_t block)
{
    snes.bus.wram[RAM_ABS(RAM_SOUND_COMMAND_PARAMETERS)] = block;
    if (!send_command(LOAD, 0, 0, 0))
        return false;
    Upload u = { (LOAD + 1) & 0x7F, true };
    /* a block of silence where the CPU half puts the song's first sample,
     * below any echo buffer (the longest reaches down to $7900) */
    static const uint8_t sample[9] = { 0x03 };
    uint16_t tuning = sound_instrument_tuning(game(), block), envelope = sound_instrument_envelope(game(), block);
    return send_bytes(&u, 0x4901, KIND_DATA_TICKING, sample, sizeof sample) && send_words(&u, 0x1F40, &tuning, 1) &&
           send_words(&u, 0x1FC0, &envelope, 1) && finish(&u);
}

/* The effects the set has: its table of track pairs ends where the first
 * track begins. */
static uint16_t count_effects(void)
{
    uint16_t lowest = 0xFFFF;
    for (uint16_t at = 0; at < 0x400 && SOUND_EFFECTS + at < lowest; at += 2) {
        uint16_t p = (uint16_t)(apu()->ram[SOUND_EFFECTS + at] | apu()->ram[SOUND_EFFECTS + at + 1] << 8);
        if (p >> 8 && p >= SOUND_EFFECTS && p < lowest)
            lowest = p;
    }
    return (uint16_t)((lowest - SOUND_EFFECTS) / 4);
}

/* The driver's boot blocks where the boot upload leaves them, and the
 * driver started, as the CPU half does at power-on and to reload it. */
static void start_driver(void)
{
    Apu *a = &snes.apu;
    for (uint8_t k = 0; k < SOUND_BOOT_BLOCKS; k++) {
        uint32_t at = sound_boot_block(&snes, (uint16_t)(2 * k));
        uint16_t length = sound_data_length(&snes, at), to = sound_boot_block_destination(&snes, (uint16_t)(2 * k));
        for (uint16_t i = 0; i < length; i++)
            a->ram[(uint16_t)(to + i)] = sound_data_byte(&snes, at + 2 + i);
    }
    a->phase = APU_DRIVER;
    a->spc.pc = 0x0200;
    a->spc.sp = 0xEF;
    a->spc.psw = SPC_FLAG_Z;
    effect_set = 0;
    set_overwritten = false;
    snes.bus.wram[RAM_ABS(RAM_SOUND_DRIVER_2F00_SET_SENT)] = 0;
    run(20 * FRAME_CLOCKS);
    effect_count = count_effects();
}

static void boot(void)
{
    Apu *a = &snes.apu;
    apu_init(a);
    a->on_insn = on_insn;
    a->on_dsp_write = on_dsp_write;
    if (hosted) {
        sound_host_detach(host);
        host = sound_host_attach(&snes);
    }
    start_driver();
}

/* Command $FF $F0 sends the driver back to the boot ROM, which announces
 * itself on ports 0-1, for the CPU half to load the driver again (the game
 * does after a game over). Its echo is gone as soon as the boot ROM runs, so
 * the announcement is what is waited for. */
enum { RELOAD_WAIT_CLOCKS = 40 * FRAME_CLOCKS };   /* past a wait for the echo, which queues it */

static bool reload(void)
{
    if (!await(apu()->cpu_out[0], true))
        return false;
    port(1, 0xF0);
    port(0, 0xFF);
    for (uint32_t t = 0; t < RELOAD_WAIT_CLOCKS; t += STEP_CLOCKS) {
        if (apu()->phase != APU_DRIVER && apu()->cpu_out[0] == 0xAA && apu()->cpu_out[1] == 0xBB) {
            port(0, 0);
            port(1, 0);
            start_driver();
            return true;
        }
        run(STEP_CLOCKS);
    }
    fprintf(stderr, "  not reloaded: phase %d out %02X %02X in %02X %02X pc %04X\n", apu()->phase, apu()->cpu_out[0],
            apu()->cpu_out[1], apu()->cpu_in[0], apu()->cpu_in[1], apu()->spc.pc);
    return false;
}

/* ---- A run ----------------------------------------------------------------------------- */

enum { HISTORY = 24 };
static char history[HISTORY][64];
static unsigned history_count;

static void note(const char *what, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    snprintf(history[history_count++ % HISTORY], sizeof history[0], "%s %02X %02X %02X %02X", what, a, b, c, d);
}

/* ---- Made-up songs --------------------------------------------------------------------- */

/* Songs of random notes and events, written into the fuzzer's own copy of the
 * cartridge in the places of songs 0 and 1, so that both drivers load them as
 * they load any song: they reach what no song in the game does (the events it
 * never uses, loops five deep, branches taken). A track is segments, each
 * ending in a note, a tie or a rest; its jumps and loops go to a segment's
 * start, so every way round the track plays something and neither driver
 * runs on without end. */
static uint8_t *image;
static bool made_up, made_up_playing;
enum { MADE_UP_SONGS = 2 };
enum { SAVED_SONG = 0xBB, SEGMENT_ROOM = 64, MADE_UP_AT = 0x300000, MADE_UP_BASE = 0x1000, SONG_PTRS = 0x070D18 };
enum { FIRST_EVENT = 0xC4, INSTRUMENT = 0xDC, LOOP_START = 0xE2, LOOP_END = 0xE3, JUMP = 0xF6, LOOP_EXIT = 0xF5, CONDITION = 0xFA };

static uint8_t event_operands(uint8_t event)
{
    return sound_program_byte(game(), (uint16_t)(0x15CC + event - FIRST_EVENT));
}

typedef struct {
    uint8_t *bytes;
    uint16_t length, limit;
    uint16_t segments[64];
    uint8_t segment_depth[64];  /* the loops open where each segment starts */
    unsigned segment_count;
    unsigned open_loops;
    unsigned loop_segment[8];   /* the segment each open loop started in */
} Stream;

static void emit(Stream *s, uint8_t b)
{
    if (s->length < s->limit)
        s->bytes[s->length++] = b;
}

/* An address in the song as the song gives them: the base it was built for,
 * for the byte after the header. */
static void emit_address(Stream *s, uint16_t offset)
{
    uint16_t address = (uint16_t)(MADE_UP_BASE + offset - 0x24);
    emit(s, (uint8_t)address);
    emit(s, (uint8_t)(address >> 8));
}

/* A segment outside every loop: jumps go only between those, as in the
 * game's songs, so a loop's end is never met without its start. */
static uint16_t some_segment(const Stream *s)
{
    for (;;) {
        unsigned n = below(s->segment_count);
        if (!s->segment_depth[n])
            return s->segments[n];
    }
}

static bool jumps(uint8_t event) { return event == JUMP || event == LOOP_EXIT || event == CONDITION; }

static void emit_jump(Stream *s, uint8_t event)
{
    if (s->open_loops)
        return;
    emit(s, event);
    if (event != JUMP)
        emit(s, (uint8_t)below(event == CONDITION ? 0x20 : 5));
    emit_address(s, some_segment(s));
}

static void emit_event(Stream *s)
{
    /* loops a good part of the time, so that they nest past the four levels */
    uint32_t pick = below(8);
    uint8_t event = pick == 0 ? LOOP_START : pick == 1 ? LOOP_END : (uint8_t)(FIRST_EVENT + below(0x100 - FIRST_EVENT));
    if ((event >= 0xEB && event <= 0xEF) || event >= 0xFE || jumps(event))
        return;   /* the ends go at the track's end, the jumps after a segment's note */
    /* a loop closes in a later segment than it opened, so it holds a note */
    if (event == LOOP_END && (!s->open_loops || s->loop_segment[s->open_loops - 1] + 1 >= s->segment_count))
        return;
    if (event == LOOP_START) {
        if (s->open_loops >= 6)
            return;
        s->loop_segment[s->open_loops++] = s->segment_count - 1;
        emit(s, event);
        emit(s, (uint8_t)below(4));
        return;
    }
    if (event == LOOP_END) {
        s->open_loops--;
        emit(s, event);
        return;
    }
    emit(s, event);
    if (event == 0xE9 || event == 0xEA) {
        emit(s, (uint8_t)below(effect_count));
    } else if (event == INSTRUMENT) {
        /* the look-ahead reads an instrument's number as the next note, so it
         * stays below the events, as in the game's songs */
        emit(s, (uint8_t)below(FIRST_EVENT));
    } else if (event == 0xF0 || event == 0xF1) {
        if (event == 0xF1)
            emit(s, (uint8_t)below(16));
        emit(s, (uint8_t)(0x40 + below(0xA0)));
    } else {
        for (uint8_t i = event_operands(event); i; i--)
            emit(s, (uint8_t)next());
    }
}

static void make_track(Stream *s)
{
    static const uint8_t jump_events[3] = { JUMP, LOOP_EXIT, CONDITION };
    s->segment_count = 0;
    s->open_loops = 0;
    unsigned segments = 3 + below(10);
    /* room for a segment's events, note and jump, and the loops' ends and
     * the track's end after it */
    for (unsigned n = 0; n < segments && s->length + SEGMENT_ROOM < s->limit; n++) {
        s->segment_depth[s->segment_count] = (uint8_t)s->open_loops;
        s->segments[s->segment_count++] = s->length;
        for (unsigned events = below(5); events; events--)
            emit_event(s);
        uint32_t kind = below(10);
        emit(s, (uint8_t)(kind < 7 ? below(0xA8) : kind < 8 ? 0xA8 + below(14) : 0xB6 + below(14)));
        if (below(5) == 0)
            emit_jump(s, jump_events[below(3)]);
    }
    while (s->open_loops) {
        emit(s, LOOP_END);
        s->open_loops--;
        emit(s, (uint8_t)below(0xA8));
    }
    if (below(3))
        emit(s, 0xEB);
    else
        emit_jump(s, JUMP);
}

/* A song of up to eight tracks in both sets, and its place in song_ptrs. */
static void make_song(uint8_t song)
{
    static uint8_t bytes[0xE00];
    Stream s = { bytes, 0x24, sizeof bytes, { 0 }, { 0 }, 0, 0, { 0 } };
    memset(bytes, 0, sizeof bytes);
    bytes[0] = (uint8_t)MADE_UP_BASE;
    bytes[1] = MADE_UP_BASE >> 8;
    bytes[2] = bytes[3] = 0xFF;
    for (unsigned set = 0; set < 2; set++) {
        for (unsigned track = 0; track < 8; track++) {
            unsigned at = 4 + 16 * set + 2 * track;
            if (below(3) == 0 || s.length + 2 * SEGMENT_ROOM > s.limit) {
                bytes[at] = bytes[at + 1] = 0xFF;
                continue;
            }
            uint16_t address = (uint16_t)(MADE_UP_BASE + s.length - 0x24);
            bytes[at] = (uint8_t)address;
            bytes[at + 1] = (uint8_t)(address >> 8);
            make_track(&s);
        }
    }
    uint32_t at = MADE_UP_AT + 0x1000u * song;
    image[at] = (uint8_t)s.length;
    image[at + 1] = (uint8_t)(s.length >> 8);
    memcpy(image + at + 2, bytes, s.length + 3);
    uint32_t bus = 0xC00000u + at;
    image[SONG_PTRS + 3 * song] = (uint8_t)bus;
    image[SONG_PTRS + 3 * song + 1] = (uint8_t)(bus >> 8);
    image[SONG_PTRS + 3 * song + 2] = (uint8_t)(bus >> 16);
}

static bool one_command(void)
{
    uint32_t r = below(1000);
    uint8_t songs = sound_song_count(&snes);
    if (r < 110) {
        static const uint8_t song_commands[] = { 0x10, 0x11, 0x14, 0x15 };
        uint8_t command = song_commands[below(4)];
        uint8_t song = (uint8_t)below(songs);
        bool made = made_up && below(2);
        if (made) {
            /* not the song the driver has saved: resuming it goes back into
             * the bytes it had */
            song = (uint8_t)below(MADE_UP_SONGS);
            if (song == apu()->ram[SAVED_SONG])
                song ^= 1;
            make_song(song);
        }
        uint8_t volume = below(4) ? sound_song_parameter(&snes, song) : (uint8_t)next();
        uint8_t echo = below(4) ? (uint8_t)(sound_song_reserve(&snes, song) & 0x0F) : (uint8_t)below(8);
        note("song", command, song, volume, echo);
        bool ok = play_song(command, song, volume, echo);
        if (!refused)
            made_up_playing = made_up && song < MADE_UP_SONGS;
        return ok;
    }
    /* Not while a song has run over the effect table: an effect then plays
     * the song's bytes as its tracks' addresses, anywhere in sound memory,
     * which the C driver doesn't keep (docs/SOUND.md, "The port as built"). */
    if (r < 360 && !set_overwritten) {
        uint8_t command = below(2) ? 0x18 : 0x19, effect = (uint8_t)below(effect_count), pan = (uint8_t)next();
        note("effect", command, effect, pan, 0);
        return send_command(command, effect, pan, 0);
    }
    /* Nor switch the set then: its upload overwrites the song's end a message
     * at a time while the music goes on reading it. Nor while a made-up song
     * plays, whose tracks may have gone on into an effect's. */
    if (r < 375 && !set_overwritten && !made_up_playing) {
        uint8_t set = (uint8_t)below(SOUND_EFFECT_SETS);
        note("effect set", set, 0, 0, 0);
        bool ok = switch_effect_set(set);
        effect_count = count_effects();
        return ok;
    }
    if (r < 385) {
        uint8_t block = (uint8_t)(1 + below(62));
        note("instrument", block, 0, 0, 0);
        return load_instrument(block);
    }
    if (r < 388) {
        note("reload", 0xFF, 0xF0, 0, 0);
        return reload();
    }
    static const struct {
        uint16_t weight;
        uint8_t command, p1_below, p2_below;
    } others[] = {
        { 80, 0x80, 0, 0 }, { 30, 0x81, 0, 0 }, { 20, 0x82, 0, 0 }, { 20, 0x83, 0, 0 },
        { 20, 0x85, 0, 0 }, { 20, 0x86, 0, 0 }, { 20, 0x87, 0, 0 }, { 40, 0x88, 0x20, 0 },
        { 25, 0x89, 0, 0 }, { 15, 0x8E, 2, 0 }, { 10, 0xF0, 0, 0 }, { 25, 0xF1, 0, 0 },
        { 30, 0xF2, 2, 0 }, { 15, 0xF3, 2, 0 }, { 15, 0xF4, 0, 0 }, { 15, 0xF8, 0, 0 },
        { 15, 0xF9, 0, 0 }, { 30, 0xF5, 2, 0 }, { 10, 0xF6, 2, 0 }, { 25, 0xFC, 16, 0 },
        { 5, 0xFD, 0x80, 0 }, { 15, 0xFF, 1, 2 },
    };
    unsigned total = 0;
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++)
        total += others[i].weight;
    uint32_t pick = below(total);
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) {
        if (pick >= others[i].weight) {
            pick -= others[i].weight;
            continue;
        }
        uint8_t p1 = others[i].p1_below ? (uint8_t)below(others[i].p1_below) : (uint8_t)next();
        uint8_t p2 = others[i].p2_below ? (uint8_t)below(others[i].p2_below) : (uint8_t)next();
        /* A register written as it is: a voice's own, not the echo's buffer,
         * the directory or the reset, which would have the S-DSP write over
         * the driver itself. */
        if (others[i].command == 0xFD)
            p1 = (uint8_t)(below(8) << 4 | below(8));
        if (others[i].command == 0xFF)
            p1 = 1;   /* the meter; $F0 reloads the driver (reload, above) */
        if ((others[i].command & 0xF0) == 0x80 && others[i].command <= 0x87 && below(2))
            p1 = (uint8_t)below(8);   /* short fades and slides too */
        note("command", others[i].command, p1, p2, 0);
        if (!send_command(others[i].command, p1, p2, 0))
            return false;
        /* A change of the echo's delay waits for the buffer with the queue
         * open, and runs the queue again from its start when the wait ends:
         * a second change queued behind it runs the first again, which runs
         * the second, for ever (the original's; the game never sends two so
         * close). So the next command waits for the change to be over. */
        if (others[i].command == 0xFC)
            run(ECHO_CHANGE_CLOCKS);
        return true;
    }
    return true;
}

static void wait_a_while(void)
{
    uint32_t r = below(100);
    uint32_t clocks = r < 55 ? below(FRAME_CLOCKS / 2) : r < 90 ? below(8 * FRAME_CLOCKS) : below(180 * FRAME_CLOCKS);
    run(clocks);
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *coverage_path = NULL;
    long runs = 10, commands = 400;
    uint64_t seed = 1;
    SoundCompareLevel level = SOUND_COMPARE_DSP;
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *a = argv[i], *v = argv[i + 1];
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--runs")) runs = strtol(v, NULL, 10);
        else if (!strcmp(a, "--commands")) commands = strtol(v, NULL, 10);
        else if (!strcmp(a, "--seed")) seed = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--spc-coverage")) coverage_path = v;
        else if (!strcmp(a, "--made-up-songs")) made_up = strcmp(v, "0") != 0;
        else if (!strcmp(a, "--sound-driver")) hosted = strcmp(v, "c") == 0;
        else if (!strcmp(a, "--level")) level = !strcmp(v, "status") ? SOUND_COMPARE_STATUS
                                               : !strcmp(v, "keys") ? SOUND_COMPARE_KEYS
                                               : !strcmp(v, "state") ? SOUND_COMPARE_STATE : SOUND_COMPARE_DSP;
        else {
            fprintf(stderr, "unknown option %s\n", a);
            return 2;
        }
    }
    FILE *f = rom_path ? fopen(rom_path, "rb") : NULL;
    if (!f) {
        fprintf(stderr, "usage: ct_sound_fuzz --rom roms/ct-usa.sfc [--runs N] [--commands N] [--seed S]\n");
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc((size_t)n);
    if (!rom || fread(rom, 1, (size_t)n, f) != (size_t)n)
        return 1;
    fclose(f);
    size_t header = (size_t)n % 0x8000 == 0x200 ? 0x200 : 0;
    image = rom + header;
    snes_init(&snes, image, (size_t)n - header);
    if (coverage_path)
        coverage = calloc(0x10000, sizeof *coverage);

    int failed = 0;
    for (long r = 0; r < runs; r++) {
        rng = (seed + (uint64_t)r) * 0x9E3779B97F4A7C15ull | 1;
        lockstep = sound_lockstep_create(&snes, level);
        history_count = 0;
        boot();
        long sent = 0;
        bool stuck = false;
        for (; sent < commands && !sound_lockstep_diverged(lockstep); sent++) {
            snes.frame = (uint32_t)sent;
            wait_a_while();
            if (!one_command()) {
                stuck = true;
                break;
            }
        }
        run(10 * FRAME_CLOCKS);
        fprintf(stderr, "run %ld (seed %llu): %ld commands%s; ", r, (unsigned long long)(seed + (uint64_t)r), sent,
                stuck ? ", the driver stopped answering" : "");
        bool same = true;
        if (hosted) {
            unsigned long into_echo = sound_host_uploads_into_echo(host);
            same = into_echo == 0;
            fprintf(stderr, "%s", stuck ? "stuck" : "answered");
            if (!same)
                fprintf(stderr, ", but %lu bytes were uploaded where the echo was writing", into_echo);
            fprintf(stderr, "\n");
        }
        else
            same = sound_lockstep_report(lockstep, NULL, stderr);
        if (!same || stuck) {
            failed++;
            fprintf(stderr, "  the commands before (the first line is command %u of the run):\n",
                    history_count > HISTORY ? history_count - HISTORY : 0);
            for (unsigned i = history_count > HISTORY ? history_count - HISTORY : 0; i < history_count; i++)
                fprintf(stderr, "    %s\n", history[i % HISTORY]);
        }
        sound_lockstep_destroy(lockstep);
    }
    if (coverage_path) {
        FILE *out = fopen(coverage_path, "w");
        if (out) {
            fprintf(out, "# SPC700 addresses executed: AAAA count\n");
            for (uint32_t pc = 0; pc < 0x10000; pc++)
                if (coverage[pc])
                    fprintf(out, "%04X %u\n", (unsigned)pc, (unsigned)coverage[pc]);
            fclose(out);
        }
    }
    fprintf(stderr, "%ld runs, %d differed or stopped\n", runs, failed);
    free(rom);
    return failed ? 1 : 0;
}
