/* ct_sound_fuzz: the C sound driver (game/sound/) on command streams no movie
 * plays (docs/SOUND.md, "Validating the port").
 *
 *   ct_sound_fuzz --rom roms/ct-usa.sfc [--runs N] [--commands N] [--seed S]
 *                 [--made-up-songs 1] [--mixer dsp|native|check]
 *
 * The APU runs alone, with the C driver in the sound processor's place as
 * ct_play plays it (frontend/sound_host.h). It takes the game's boot and, at
 * random, half of the commands as the game's calls (songs at their own volume
 * and echo, the set of effects switched, instruments loaded, the driver
 * reloaded) and the rest through the ports as the scene sends them (songs at
 * made-up volumes among them). The commands are drawn at random, songs from
 * all of them, at random intervals from a tick to seconds; with
 * --made-up-songs, half the songs are random ones of the fuzzer's own (below).
 * A run checks that every command written to the ports is answered, that no
 * sample is placed where the echo is writing (sound_host_samples_into_echo)
 * and that no command is dropped from the host's full queue
 * (sound_host_commands_dropped). --mixer picks what makes its sound
 * (frontend/sound_host.h): check plays the S-DSP and the native mixer side by
 * side, and a run in which any frame of theirs differs fails
 * (dev/mixer_check.h).
 *
 * Every run's sound, every frame of it, is folded into a digest (64-bit
 * FNV-1a over the output's samples), printed at the end: the same seed, sizes
 * and options make the same digest, so a change to the driver that changes
 * what it plays shows as a new digest. tools/sound_golden.txt keeps the
 * digests of the gate's runs (tools/suite.py), a hash of our own output and
 * nothing of the cartridge's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/sound_data.h"
#include "port/vars_ram.h"
#include "snes/system.h"
#include "sound/sound.h"
#include "mixer_check.h"
#include "sound_host.h"

static Snes snes;
static uint64_t rng;
static SoundHost *host;
static MixerCheck *mixer_check;   /* --mixer check: the host's frames compared */
static SoundMixer mixer = SOUND_MIXER_DSP;

enum {
    FRAME_CLOCKS = 357368,    /* master clocks a frame */
    STEP_CLOCKS = 1000,
    AFTER_ECHO_CLOCKS = 3000,
    ECHO_LIMIT = 40 * FRAME_CLOCKS,
    ECHO_CHANGE_CLOCKS = 40 * FRAME_CLOCKS    /* two delays of 15 at 16 ms a unit, and some */
};

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}
static uint32_t below(uint32_t n) { return n ? next() % n : 0; }

/* The digest of what was heard: FNV-1a, 64 bits, over every sample. */
enum { DIGEST_BASIS = 0xCBF29CE484222325ull, DIGEST_PRIME = 0x100000001B3ull };
static uint64_t digest = DIGEST_BASIS;

static void hear(const int16_t *samples, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint16_t v = (uint16_t)samples[i];
        digest = (digest ^ (v & 0xFF)) * DIGEST_PRIME;
        digest = (digest ^ (v >> 8)) * DIGEST_PRIME;
    }
}

static void run(uint32_t clocks)
{
    while (clocks) {
        uint32_t chunk = clocks < FRAME_CLOCKS ? clocks : FRAME_CLOCKS;
        apu_advance(&snes.apu, chunk);
        int16_t sink[2 * 1024];
        size_t frames;
        while ((frames = apu_take_samples(&snes.apu, sink, 1024)) != 0)
            hear(sink, 2 * frames);
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

/* ---- What the fuzzer sends --------------------------------------------------------------- */

static const GameState *game(void) { return state_of(&snes); }
static uint8_t effect_set;
static uint16_t effect_count;
static bool set_overwritten;

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

/* A command through the ports, as the scene sends one: the last answered,
 * the parameters and the command written, and its echo waited for. */
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

/* The effects the set has: its table of track pairs ends where the first
 * track begins. */
static uint16_t count_effects(void)
{
    uint16_t length;
    const uint8_t *set = effect_set_bytes(effect_set, &length);
    uint16_t lowest = 0xFFFF;
    for (uint16_t at = 0; at + 1 < length && at < 0x400 && SOUND_EFFECTS + at < lowest; at += 2) {
        uint16_t p = (uint16_t)(set[at] | set[at + 1] << 8);
        if (p >> 8 && p >= SOUND_EFFECTS && p < lowest)
            lowest = p;
    }
    return (uint16_t)((lowest - SOUND_EFFECTS) / 4);
}

/* The driver started, as the game does at power-on and to reload it: the
 * boot, which the host takes as a call, and the time the original's upload
 * took, which it answers and the game lets pass before the driver starts. */
static void start_driver(void)
{
    Apu *a = &snes.apu;
    effect_set = 0;
    set_overwritten = false;
    uint32_t upload = a->host_boot(a->host_ctx, a);
    run(upload + 20 * FRAME_CLOCKS);
    effect_count = count_effects();
}

static void boot(void)
{
    apu_init(&snes.apu);
    sound_host_detach(host);
    mixer_check_free(mixer_check);
    host = sound_host_attach_mixer(&snes, mixer);
    mixer_check = mixer == SOUND_MIXER_CHECK && host ? mixer_check_attach(host) : NULL;
    start_driver();
}

/* The game's command, which the host takes as a call. */
static bool game_command(uint8_t command, uint8_t p1, uint8_t p2, uint8_t p3)
{
    const uint8_t bytes[4] = { command, p1, p2, p3 };
    snes.apu.host_command(snes.apu.host_ctx, &snes.apu, bytes, effect_set);
    run(AFTER_ECHO_CLOCKS);
    return true;
}

/* Command $FF $F0 stops the driver for the game to boot it again (the game
 * does after a game over). */
static bool reload(void)
{
    game_command(0xFF, 0xF0, 0, 0);
    start_driver();
    return true;
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
 * cartridge in the places of songs 0 and 1, so that the driver loads them as
 * it loads any song: they reach what no song in the game does (the events it
 * never uses, loops five deep, branches taken). A track is segments, each
 * ending in a note, a tie or a rest; its jumps and loops go to a segment's
 * start, so every way round the track plays something and the driver never
 * runs on without end. */
static uint8_t *image;
static bool made_up, made_up_playing;
enum { MADE_UP_SONGS = 2 };
enum { SEGMENT_ROOM = 64, MADE_UP_AT = 0x300000, MADE_UP_BASE = 0x1000, SONG_PTRS = 0x070D18 };
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
    uint8_t songs = sound_songs(game());
    if (r < 110) {
        static const uint8_t song_commands[] = { 0x10, 0x11, 0x14, 0x15 };
        uint8_t command = song_commands[below(4)];
        uint8_t song = (uint8_t)below(songs);
        bool made = made_up && below(2);
        if (made) {
            /* not the song the driver has saved: resuming it goes back into
             * the bytes it had */
            song = (uint8_t)below(MADE_UP_SONGS);
            if (song == sound_host_saved_song(host))
                song ^= 1;
            make_song(song);
        }
        uint8_t volume = below(4) ? sound_song_volume(game(), song) : (uint8_t)next();
        uint8_t echo = below(4) ? sound_song_echo_delay(game(), song) : (uint8_t)below(8);
        note("song", command, song, volume, echo);
        /* the game's song at its own volume through the game's calls, or at
         * another through the ports; at its own echo either way, which its
         * samples are placed to keep clear of (with another, they can lie
         * where the echo writes, in the original as here) */
        bool ok;
        uint8_t own_echo = sound_song_echo_delay(game(), song);
        if (volume == sound_song_volume(game(), song))
            ok = game_command(command, song, 0, 0);
        else
            ok = send_command(command, song, volume, own_echo);
        /* The original's CPU half waited for a song's start through the
         * echo's wait, which a new delay starts; the game's calls don't, and
         * the host holds what comes meanwhile. Songs piled up behind each
         * other's waits would outlast any wait here. */
        run(ECHO_CHANGE_CLOCKS);
        made_up_playing = made_up && song < MADE_UP_SONGS;
        return ok;
    }
    /* Not while a song has run over the effect table: an effect then plays
     * the song's bytes as its tracks' addresses, anywhere in sound memory,
     * which the C driver doesn't keep (docs/SOUND.md, "The port as built"). */
    if (r < 360 && !set_overwritten) {
        uint8_t command = below(2) ? 0x18 : 0x19, effect = (uint8_t)below(effect_count), pan = (uint8_t)next();
        note("effect", command, effect, pan, 0);
        if (below(2))
            return game_command(command, effect, pan, 0);
        return send_command(command, effect, pan, 0);
    }
    /* Nor switch the set then: its upload overwrites the song's end a message
     * at a time while the music goes on reading it. Nor while a made-up song
     * plays, whose tracks may have gone on into an effect's. */
    if (r < 375 && !set_overwritten && !made_up_playing) {
        uint8_t set = (uint8_t)below(SOUND_EFFECT_SETS);
        note("effect set", set, 0, 0, 0);
        effect_set = set;
        bool ok = game_command(0x70, set, 0, 0);
        effect_count = count_effects();
        return ok;
    }
    if (r < 385) {
        uint8_t block = (uint8_t)(1 + below(62));
        note("instrument", block, 0, 0, 0);
        return game_command(0x71, block, 0, 0);
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
        if (below(2))
            game_command(others[i].command, p1, p2, 0);
        else if (!send_command(others[i].command, p1, p2, 0))
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
    const char *rom_path = NULL;
    long runs = 10, commands = 400;
    uint64_t seed = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *a = argv[i], *v = argv[i + 1];
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--runs")) runs = strtol(v, NULL, 10);
        else if (!strcmp(a, "--commands")) commands = strtol(v, NULL, 10);
        else if (!strcmp(a, "--seed")) seed = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--made-up-songs")) made_up = strcmp(v, "0") != 0;
        else if (!strcmp(a, "--mixer")) {
            if (!sound_mixer_named(v, &mixer)) {
                fprintf(stderr, "--mixer must be dsp, native or check\n");
                return 2;
            }
        }
        else {
            fprintf(stderr, "unknown option %s\n", a);
            return 2;
        }
    }
    FILE *f = rom_path ? fopen(rom_path, "rb") : NULL;
    if (!f) {
        fprintf(stderr, "usage: ct_sound_fuzz --rom roms/ct-usa.sfc [--runs N] [--commands N] [--seed S] "
                        "[--made-up-songs 1] [--mixer dsp|native|check]\n");
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

    int failed = 0;
    for (long r = 0; r < runs; r++) {
        rng = (seed + (uint64_t)r) * 0x9E3779B97F4A7C15ull | 1;
        history_count = 0;
        boot();
        long sent = 0;
        bool stuck = false;
        for (; sent < commands; sent++) {
            snes.frame = (uint32_t)sent;
            wait_a_while();
            if (!one_command()) {
                stuck = true;
                break;
            }
        }
        run(10 * FRAME_CLOCKS);
        unsigned long into_echo = sound_host_samples_into_echo(host);
        unsigned long dropped = sound_host_commands_dropped(host);
        bool clean = into_echo == 0 && dropped == 0;
        fprintf(stderr, "run %ld (seed %llu): %ld commands%s; %s", r, (unsigned long long)(seed + (uint64_t)r), sent,
                stuck ? ", the driver stopped answering" : "", stuck ? "stuck" : "answered");
        if (into_echo)
            fprintf(stderr, ", but %lu bytes of samples were placed where the echo was writing", into_echo);
        if (dropped)
            fprintf(stderr, ", but %lu commands were dropped from the host's full queue", dropped);
        fprintf(stderr, "\n");
        if (mixer_check) {
            fprintf(stderr, "  ");
            clean = mixer_check_report(mixer_check, stderr) && clean;
        }
        if (!clean || stuck) {
            failed++;
            fprintf(stderr, "  the commands before (the first line is command %u of the run):\n",
                    history_count > HISTORY ? history_count - HISTORY : 0);
            for (unsigned i = history_count > HISTORY ? history_count - HISTORY : 0; i < history_count; i++)
                fprintf(stderr, "    %s\n", history[i % HISTORY]);
        }
    }
    sound_host_detach(host);
    mixer_check_free(mixer_check);
    fprintf(stderr, "%ld runs, %d failed; digest %016llx\n", runs, failed, (unsigned long long)digest);
    free(rom);
    return failed ? 1 : 0;
}
