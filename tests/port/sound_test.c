/* Checks the C sound driver's pieces that need no ROM (docs/SOUND.md):
 *
 * - the voices' S-DSP implementation (platform/voices_dsp.h): an envelope, a
 *   gain, an echo delay and a held voice as the registers that make them;
 * - the driver (game/sound/) on a made-up cartridge: a song of one note on
 *   one track keys voice 0 on with the note's pitch and volume, shows the
 *   music playing, and lets the voice go when its track ends; a sound effect
 *   takes voice 7 and gives it back; a fade to silence stops the music; the
 *   same song started again is refused; a song announced with a new echo
 *   delay stops the echo writing and moves it before its data comes;
 * - the game's side (game/sound/loads.c) on the same cartridge: a song's
 *   samples placed after the boot's own, with the directory pointed at them;
 *   the next song keeping the sample it shares, moved down over the one it
 *   doesn't, and loading the one it lacks after it; command $71's sample in
 *   the place of the song's first; a longer echo forgetting the places it
 *   runs over; and a song's volume and echo delay taken from its entry;
 * - the host's saved state (frontend/sound_host.h): every byte of the driver
 *   is in a field of it; all three mixers restore a playing host, including
 *   queued commands and echo waits, and continue with identical sound;
 * - the host as the player plays it, on the same cartridge: the boot and a
 *   run of the game's commands as calls, heard through the S-DSP and the
 *   native mixer side by side, every frame of the two the same, and what was
 *   heard folded into a digest that must be the one kept here: a change to
 *   the driver, the host or the mixer that changes the sound fails until the
 *   digest is taken again on purpose. (The driver isn't compared with the
 *   original's any more: docs/REIMPLEMENTATION.md, "The sound driver's
 *   teardown".)
 *
 * Exits non-zero on failure.
 *
 *   sound_test */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/vars_ram.h"
#include "platform/voices_dsp.h"
#include "sound_host.h"
#include "snes/system.h"
#include "sound/sound.h"
#include "mixer_check.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ---- The voices as S-DSP registers ------------------------------------------------------- */

static uint8_t regs[128];
static uint8_t memory[0x10000];
static unsigned kon_writes;

static uint8_t read_reg(void *ctx, uint8_t reg)
{
    (void)ctx;
    return regs[reg & 0x7F];
}

static void write_reg(void *ctx, uint8_t reg, uint8_t value)
{
    (void)ctx;
    regs[reg & 0x7F] = value;
    if ((reg & 0x7F) == 0x4C && value)
        kon_writes++;
}

static void write_memory(void *ctx, uint16_t at, uint8_t value)
{
    (void)ctx;
    memory[at] = value;
}

static uint8_t read_memory(void *ctx, uint16_t at)
{
    (void)ctx;
    return memory[at];
}

static VoicesDsp voices_dsp;

static void test_voices(void)
{
    au_voice_envelope(&voices_dsp.voices, 2, (AuEnvelope){ .adsr = true, .attack = 0xF, .decay = 7, .sustain_level = 5, .sustain_rate = 0x1F });
    CHECK(regs[0x25] == 0xFF && regs[0x26] == 0xBF, "envelope %02X %02X", regs[0x25], regs[0x26]);
    au_voice_gain(&voices_dsp.voices, 3, (AuGain){ AU_GAIN_DECREASE_EXPONENTIAL, 0 });
    CHECK(regs[0x37] == 0xA0, "gain %02X", regs[0x37]);
    au_voice_gain(&voices_dsp.voices, 3, (AuGain){ AU_GAIN_DIRECT, 0x40 });
    CHECK(regs[0x37] == 0x40, "direct gain %02X", regs[0x37]);
    au_echo_delay(&voices_dsp.voices, 5);
    CHECK(regs[0x7D] == 5 && regs[0x6D] == 0xC9, "echo delay %02X at %02X", regs[0x7D], regs[0x6D]);
    regs[0x6C] = 0x13;
    au_echo_writes(&voices_dsp.voices, false);
    CHECK(regs[0x6C] == 0x33, "echo writes off %02X", regs[0x6C]);
    au_echo_writes(&voices_dsp.voices, true);
    CHECK(regs[0x6C] == 0x13, "echo writes on %02X", regs[0x6C]);
    au_voices_hold(&voices_dsp.voices, true);
    CHECK(regs[0x25] == 0x7F, "held %02X", regs[0x25]);
    au_voices_hold(&voices_dsp.voices, false);
    CHECK(regs[0x25] == 0xFF, "released %02X", regs[0x25]);
    au_voice_pitch(&voices_dsp.voices, 1, 0x1234);
    CHECK(regs[0x12] == 0x34 && regs[0x13] == 0x12, "pitch");
    static const uint8_t brr[9] = { 0x02, 1, 2, 3 };
    au_sample_define(&voices_dsp.voices, 0x10, brr, sizeof brr, 0);
    CHECK(memory[VOICES_DIRECTORY + 0x40] == (uint8_t)VOICES_OWN_SAMPLE &&
              memory[VOICES_OWN_SAMPLE + 2] == 2, "the driver's own sample");

    /* A move down: whole pages first, then the rest from its end, as the
     * original's receiver copies (over itself where the two overlap). */
    for (int i = 0; i < 0x300; i++)
        memory[0x5000 + i] = (uint8_t)i;
    au_sample_memory_move(&voices_dsp.voices, 0x4F80, 0x5000, 0x180);
    CHECK(memory[0x4F80] == 0x00 && memory[0x507F] == 0xFF && memory[0x5080] == 0x00 && memory[0x50FF] == 0x7F,
          "the move: %02X %02X %02X %02X", memory[0x4F80], memory[0x507F], memory[0x5080], memory[0x50FF]);
    au_sample_place(&voices_dsp.voices, 0x21, 0x1234, 0x5678);
    CHECK(memory[VOICES_DIRECTORY + 0x84] == 0x34 && memory[VOICES_DIRECTORY + 0x87] == 0x56,
          "a sample's directory entry");
}

/* ---- The driver on a made-up cartridge --------------------------------------------------- */

enum { ROM_SIZE = 0x400000, BANK_C7 = 0x070000 };
static uint8_t *rom;
static Snes snes;

static void put8(uint32_t at, uint8_t v) { rom[BANK_C7 + (at & 0xFFFF)] = v; }
static void put16(uint32_t at, uint16_t v)
{
    put8(at, (uint8_t)v);
    put8(at + 1, (uint8_t)(v >> 8));
}
static void put_bytes(uint32_t at, const uint8_t *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        put8(at + i, b[i]);
}

/* A cartridge with the driver's tables where its boot block 0 holds them,
 * three instruments, a set of effects with one effect, and song 1: one track
 * that plays one note at full volume in octave 4, at a tempo of about a step
 * a tick, and ends. */
static void make_cartridge(void)
{
    rom = calloc(ROM_SIZE, 1);
    static const uint16_t blocks[6] = { 0x1000, 0x4309, 0x3000, 0x3100, 0x3200, 0x3300 };
    static const uint16_t to[6] = { 0x0200, 0x2F00, 0x4700, 0x1E00, 0x1F80, 0x1F00 };
    for (int k = 0; k < 6; k++) {
        put16(0x0010 + 2 * k, blocks[k]);
        put16(0x001C + 2 * k, to[k]);
    }
    put16(0x1000, 0x1C00);   /* block 0: $0200-$1DFF */
#define PROGRAM(spc) (0x1002 + (spc) - 0x0200)
    for (int i = 0; i < 12; i++)
        put16(PROGRAM(0x1D6D + 2 * i), (uint16_t)(0x1000 + 0x100 * i));
    for (int i = 0; i < 14; i++)
        put8(PROGRAM(0x1DB3 + i), (uint8_t)(0x30 - i));
    put16(0x3300, 6);   /* three tunings of 0 */
    put16(0x3200, 6);   /* three envelopes */
    put16(0x3202, 0xE0FF);
    put16(0x3204, 0xE0FF);
    put16(0x3206, 0xE0FF);

    /* the effects: effect 0 has one track, at $2F10, one note */
    static const uint8_t effects[] = { 0x10, 0x2F, 0x00, 0x00, [0x10] = 0x00, 0xEB };
    put16(0x4309, sizeof effects);
    put_bytes(0x430B, effects, sizeof effects);

    /* song 1 at $C7:8000, built for $0024 */
    put8(0x0D18 + 3, 0x00);
    put8(0x0D18 + 4, 0x80);
    put8(0x0D18 + 5, 0xC7);
    uint8_t song[0x24 + 10] = { 0x24, 0x00, 0xFF, 0xFF };
    for (int i = 4; i < 0x24; i += 2)
        song[i] = song[i + 1] = 0xFF;
    song[4] = 0x24;
    song[5] = 0x00;
    static const uint8_t track[] = { 0xC4, 0x7F, 0xD6, 0x04, 0xF0, 0xE0, 0x00, 0xEB };
    memcpy(song + 0x24, track, sizeof track);
    put16(0x8000, sizeof song);
    put_bytes(0x8002, song, sizeof song);

    /* Songs 2 and 3 are song 1 again, with other samples: song 1 has blocks 1
     * and 2, song 2 blocks 2 and 3, song 3 block 3 (lengths 270, 27 and 9,
     * each byte its block's number times 16 plus its place in it, looping
     * from its second BRR block). The boot's own samples are 18 bytes at
     * $4700, and its directory two entries. Song 2 plays with an echo of
     * delay 3, the others 5. */
    put8(0x0AE9, 4);
    put8(0x0AD8, 0xFF);
    for (int n = 2; n <= 3; n++) {
        put8(0x0D18 + 3 * n, 0x00);
        put8(0x0D18 + 3 * n + 1, 0x80);
        put8(0x0D18 + 3 * n + 2, 0xC7);
    }
    static const uint16_t song_blocks[4][2] = { { 0, 0 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
    for (int n = 1; n <= 3; n++) {
        put16(0x0E11 + 32 * n, song_blocks[n][0]);
        put16(0x0E11 + 32 * n + 2, song_blocks[n][1]);
        put8(0x241D + 2 * n, n == 2 ? 3 : 5);
        put8(0x241E + 2 * n, (uint8_t)(0x40 + n));
    }
    static const uint16_t lengths[3] = { 270, 27, 9 };
    for (int id = 1; id <= 3; id++) {
        uint16_t at = (uint16_t)(0x9000 + 0x200 * id);
        put16(0x0AEA + 3 * (id - 1), at);
        put8(0x0AEA + 3 * (id - 1) + 2, 0xC7);
        put16(at, lengths[id - 1]);
        for (int i = 0; i < lengths[id - 1]; i++)
            put8(at + 2 + i, (uint8_t)(16 * id + i));
        put16(0x0BA4 + 2 * (id - 1), 9);
        put16(0x0C20 + 2 * (id - 1), (uint16_t)(0x100 * id));
        put16(0x0C9C + 2 * (id - 1), 0xE0FF);
    }
    put16(0x3000, 18);
    for (int i = 0; i < 18; i++)
        put8(0x3002 + i, 0xA0);
    static const uint8_t boot_directory[8] = { 0x00, 0x47, 0x00, 0x47, 0x09, 0x47, 0x09, 0x47 };
    put16(0x3100, sizeof boot_directory);
    put_bytes(0x3102, boot_directory, sizeof boot_directory);
    snes_init(&snes, rom, ROM_SIZE);
}

static SoundDriver driver;

static void tick(int n)
{
    for (int i = 0; i < n; i++)
        sound_tick(&driver);
}

static void test_driver(void)
{
    make_cartridge();
    memset(regs, 0, sizeof regs);
    sound_boot(&driver, state_of(&snes), &voices_dsp.voices);
    CHECK(regs[0x5D] == VOICES_DIRECTORY >> 8 && regs[0x0C] == 0x40 && regs[0x7D] == 5,
          "the boot: directory %02X, master volume %02X, echo delay %02X", regs[0x5D], regs[0x0C], regs[0x7D]);
    tick(2);
    CHECK(regs[0x5C] == 0, "the boot's key off is flushed and cleared (%02X)", regs[0x5C]);

    sound_run_command(&driver, (SoundCommand){ 0x10, 1, 0xFF, 5 });
    kon_writes = 0;
    tick(2);
    CHECK(regs[0x4C] == 0x01 || kon_writes, "voice 0 keyed on");
    /* note 0 in octave 4 is semitone 2 of octave 3, less 10: pitch $1200 >> 1 */
    CHECK(regs[0x02] == 0x00 && regs[0x03] == 0x09, "pitch %02X%02X", regs[0x03], regs[0x02]);
    CHECK(regs[0x00] == 0x7B && regs[0x01] == 0x00, "volume %02X %02X", regs[0x00], regs[0x01]);
    CHECK(sound_playing(&driver) == 0x01, "the music shows as playing: %02X", sound_playing(&driver));

    sound_run_command(&driver, (SoundCommand){ 0x10, 1, 0xFF, 5 });
    CHECK(driver.song.music_tracks == 0x01, "the same song again is refused");

    sound_run_command(&driver, (SoundCommand){ 0x18, 0, 0x80, 0 });
    tick(8);   /* the effects step every other tick or so, and an effect's track waits a step */
    CHECK(driver.effect_voices == 0x80 && (sound_playing(&driver) & 0x80), "the effect takes voice 7");
    CHECK(regs[0x74] == 2, "the effect's instrument: %02X", regs[0x74]);
    tick(300);
    CHECK(driver.effect_voices == 0 && !(sound_playing(&driver) & 0x80), "the effect gives voice 7 back");
    CHECK(driver.song.music_tracks == 0 && sound_playing(&driver) == 0, "the track ends: %02X",
          sound_playing(&driver));

    sound_run_command(&driver, (SoundCommand){ 0x10, 1, 0xFF, 5 });
    tick(4);
    CHECK(driver.song.music_tracks == 0x01, "the song starts again once it has ended");
    sound_run_command(&driver, (SoundCommand){ 0x81, 2, 0, 0 });
    tick(7);
    CHECK(driver.fades[0].volume.steps_left == 1, "one step of the fade left: %u", driver.fades[0].volume.steps_left);
    tick(7);
    CHECK(driver.song.music_tracks == 0 && driver.last_song_started == 0xFF, "the fade to silence stops the music");

    /* A song's command with a new echo delay, its data uploaded after: the
     * echo stops writing and moves at the command, before the samples come,
     * and the song plays only once started. */
    driver.echo_waits = true;
    CHECK(sound_announce_song(&driver, (SoundCommand){ 0x10, 1, 0xFF, 2 }), "the song is announced");
    CHECK((regs[0x6C] & 0x20) && regs[0x7D] == 2 && regs[0x6D] == 0xE1,
          "the echo stops writing and moves at the command: FLG %02X, delay %02X at %02X", regs[0x6C], regs[0x7D],
          regs[0x6D]);
    CHECK(driver.song.music_tracks == 0, "the song waits for its data");
    sound_start_announced_song(&driver);
    CHECK(driver.song.music_tracks == 0x01, "the song starts once its data is in");
    sound_echo_ready(&driver);
    CHECK(!(regs[0x6C] & 0x20), "the echo writes again once its buffer is ready: FLG %02X", regs[0x6C]);
    free(rom);
}

/* ---- The game's side: the samples ---------------------------------------------------------- */

enum { BASE = 0x4712 };

static uint16_t directory_word(uint8_t sample, uint8_t half)
{
    uint16_t at = (uint16_t)(VOICES_DIRECTORY + 4 * sample + 2 * half);
    return (uint16_t)(memory[at] | memory[at + 1] << 8);
}

static void test_samples(void)
{
    make_cartridge();
    memset(memory, 0, sizeof memory);
    memset(&driver, 0, sizeof driver);
    sound_boot(&driver, state_of(&snes), &voices_dsp.voices);
    const SoundSamples *p = &driver.samples;
    CHECK(memory[0x4700] == 0xA0 && memory[0x4711] == 0xA0 && directory_word(1, 0) == 0x4709,
          "the boot's own samples and their directory");
    CHECK(p->start[0] == BASE && !p->block[0] && p->echo_delay == 5 && p->echo_page == 0xC9,
          "the places start after them: %04X", p->start[0]);

    sound_game_command(&driver, (SoundCommand){ 0x10, 1, 0, 0 }, 0);
    CHECK(driver.song.volume == 0x41 && driver.song_id == 1, "song 1's volume from its entry: %02X",
          driver.song.volume);
    CHECK(p->block[0] == 1 && p->block[1] == 2 && p->start[1] == BASE + 270 && p->start[2] == BASE + 297,
          "song 1's samples: %u %u at %04X %04X", p->block[0], p->block[1], p->start[1], p->start[2]);
    CHECK(memory[BASE] == 0x10 && memory[BASE + 270] == 0x20 && memory[BASE + 296] == 0x20 + 26,
          "song 1's sample bytes");
    CHECK(directory_word(0x20, 0) == BASE && directory_word(0x20, 1) == BASE + 9 &&
              directory_word(0x21, 0) == BASE + 270,
          "the directory points at them: %04X %04X %04X", directory_word(0x20, 0), directory_word(0x20, 1),
          directory_word(0x21, 0));
    CHECK(driver.instrument_tables[0x40] == 0x00 && driver.instrument_tables[0x41] == 0x01,
          "the song's first instrument is block 1's: %02X%02X", driver.instrument_tables[0x41],
          driver.instrument_tables[0x40]);

    /* Song 2 shares block 2: it moves down over block 1, and block 3 loads
     * after it. */
    sound_game_command(&driver, (SoundCommand){ 0x10, 2, 0, 0 }, 0);
    CHECK(p->block[0] == 2 && p->block[1] == 3 && !p->block[2] && p->start[1] == BASE + 27 &&
              p->start[2] == BASE + 36 && !p->start[3],
          "song 2's samples: %u %u %u, %04X %04X", p->block[0], p->block[1], p->block[2], p->start[1], p->start[2]);
    CHECK(memory[BASE] == 0x20 && memory[BASE + 26] == 0x20 + 26 && memory[BASE + 27] == 0x30,
          "block 2 moved down and block 3 after it: %02X %02X %02X", memory[BASE], memory[BASE + 26],
          memory[BASE + 27]);
    CHECK(p->echo_delay == 3 && p->echo_page == 0xD9 && driver.echo_delay == 3, "song 2's echo from its entry");
    CHECK(p->instrument_place == 0, "command $71's place is the song's first sample's");

    /* $71: block 3 (9 bytes) fits where block 2 (27) is. */
    CHECK(sound_load_song_instrument(&driver, 3) && memory[BASE] == 0x30 && p->block[0] == 3 && p->length[0] == 9,
          "block 3 in the song's first place");

    /* Song 3, with one sample, keeps the directory's second entry song 2
     * left. */
    sound_game_command(&driver, (SoundCommand){ 0x10, 3, 0, 0 }, 0);
    CHECK(p->block[0] == 3 && directory_word(0x20, 0) == BASE && directory_word(0x21, 0) == BASE + 27,
          "song 3's sample, and song 2's second entry kept: %u %04X %04X", p->block[0], directory_word(0x20, 0),
          directory_word(0x21, 0));

    /* A much longer echo leaves room for no sample: the places go. */
    driver.samples.start[1] = 0xC000;
    sound_game_command(&driver, (SoundCommand){ 0xFC, 15, 0, 0 }, 0);
    CHECK(p->echo_delay == 15 && p->echo_page == 0x79 && !p->block[0] && !p->start[1],
          "an echo that runs over the places forgets them: %u %04X", p->block[0], p->start[1]);
    CHECK(!sound_load_song_instrument(&driver, 3), "no instrument without a song's samples");
    free(rom);
}

/* ---- The host's saved state ------------------------------------------------------------------- */

typedef struct {
    uint8_t *covered;
    const uint8_t *from, *to;
} Coverage;

static void cover(void *ctx, const char *name, void *field, unsigned size, uint32_t count)
{
    (void)name;
    Coverage *c = ctx;
    const uint8_t *at = field;
    if (at >= c->from && at < c->to)
        memset(c->covered + (at - c->from), 1, (size_t)size * count);
}

/* Every byte of the driver is in a field the host's state holds, but for
 * the two pointers it is given and its padding (taken as in state_test: a
 * gap shorter than the alignment of what follows). */
static void test_host_state(void)
{
    static Snes machine;
    SoundHost *h = sound_host_attach(&machine);
    uint8_t *driver_at = (uint8_t *)h + sound_host_driver_offset();
    Coverage c = { calloc(1, sizeof(SoundDriver)), driver_at, driver_at + sizeof(SoundDriver) };
    sound_host_driver_fields(h, cover, &c);
    memset(c.covered + offsetof(SoundDriver, game), 1, sizeof(void *));
    memset(c.covered + offsetof(SoundDriver, voices), 1, sizeof(void *));
    for (size_t at = 0; at < sizeof(SoundDriver);) {
        if (c.covered[at]) {
            at++;
            continue;
        }
        size_t end = at;
        while (end < sizeof(SoundDriver) && !c.covered[end])
            end++;
        size_t align = 8;
        while (end % align)
            align /= 2;
        CHECK(end - at < align, "SoundDriver bytes %zu-%zu are in no field of the host's state", at, end - 1);
        at = end;
    }
    free(c.covered);
    sound_host_detach(h);

    /* A state saved with the mixer alone has an S-DSP left where it was,
     * which the check would compare the mixer with: each mixer's states are
     * a host program of their own, and load only with it. */
    SoundHost *native = sound_host_attach_mixer(&machine, SOUND_MIXER_NATIVE);
    uint8_t native_id = sound_host_state(native)->id;
    sound_host_detach(native);
    SoundHost *checked = sound_host_attach_mixer(&machine, SOUND_MIXER_CHECK);
    uint8_t checked_id = sound_host_state(checked)->id;
    sound_host_detach(checked);
    CHECK(native_id != checked_id, "a state saved with the native mixer would load with the mixer check");
}

/* ---- The host, as the player plays it ---------------------------------------------------------- */

/* The digest of the sound the run below makes: 64-bit FNV-1a over every
 * sample of every frame. Taken again (from the FAIL line) only when a change
 * means to change the sound. */
#define HOST_DIGEST 0x0f41cd925b0e252dull
enum { FRAME_CLOCKS = 357368 };

static uint64_t heard = 0xCBF29CE484222325ull;

static void play_frames(unsigned frames)
{
    for (unsigned f = 0; f < frames; f++) {
        apu_advance(&snes.apu, FRAME_CLOCKS);
        int16_t out[2 * 1024];
        size_t n;
        while ((n = apu_take_samples(&snes.apu, out, 1024)) != 0) {
            for (size_t i = 0; i < 2 * n; i++) {
                uint16_t v = (uint16_t)out[i];
                heard = (heard ^ (v & 0xFF)) * 0x100000001B3ull;
                heard = (heard ^ (v >> 8)) * 0x100000001B3ull;
            }
        }
    }
}

static void game_command(uint8_t command, uint8_t p1, uint8_t p2, uint8_t effects_asked, unsigned then_frames)
{
    const uint8_t bytes[4] = { command, p1, p2, 0 };
    snes.apu.host_command(snes.apu.host_ctx, &snes.apu, bytes, effects_asked);
    play_frames(then_frames);
}

static void test_host(void)
{
    make_cartridge();
    /* the songs' track takes instrument 0 first, the boot's first sample with
     * its envelope, which loops from its start where its second block ends:
     * its note sounds until it is let go */
    static const uint8_t track[] = { 0xDC, 0x00, 0xC4, 0x7F, 0xD6, 0x04, 0xF0, 0xE0, 0x00, 0xEB };
    put_bytes(0x8002 + 0x24, track, sizeof track);
    put8(0x3002 + 9, 0xA3);
    apu_init(&snes.apu);
    SoundHost *h = sound_host_attach_mixer(&snes, SOUND_MIXER_CHECK);
    MixerCheck *check = h ? mixer_check_attach(h) : NULL;
    CHECK(h && check, "out of memory");
    if (!h || !check)
        return;
    Apu *a = &snes.apu;
    uint32_t upload = a->host_boot(a->host_ctx, a);
    play_frames(upload / FRAME_CLOCKS + 20);
    game_command(0x10, 1, 0, 0, 90);     /* song 1 */
    game_command(0x18, 0, 0x80, 0, 60);  /* the effect, in the middle */
    game_command(0x10, 2, 0, 0, 150);    /* song 2, its echo's wait first */
    game_command(0x71, 3, 0, 0, 30);     /* block 3 in the song's first place */
    game_command(0x10, 3, 0, 0, 120);    /* song 3 */
    game_command(0x81, 8, 0, 0, 60);     /* the music faded out */
    printf("  ");
    CHECK(mixer_check_report(check, stdout), "the native mixer's frames differ from the S-DSP's");
    CHECK(sound_host_samples_into_echo(h) == 0 && sound_host_commands_dropped(h) == 0,
          "a sample placed where the echo writes, or a command dropped");
    CHECK(heard == HOST_DIGEST, "the host's sound on the made-up cartridge: digest 0x%016llxull, kept 0x%016llxull",
          (unsigned long long)heard, (unsigned long long)HOST_DIGEST);
    mixer_check_free(check);
    sound_host_detach(h);
    free(rom);
}

static void test_host_round_trip(void)
{
    for (SoundMixer mixer = SOUND_MIXER_DSP; mixer <= SOUND_MIXER_CHECK; mixer++) {
        make_cartridge();
        snes_power_on(&snes);
        SoundHost *h = sound_host_attach_mixer(&snes, mixer);
        CHECK(h != NULL, "round trip: attach host");
        if (!h)
            return;
        uint32_t upload = snes.apu.host_boot(snes.apu.host_ctx, &snes.apu);
        play_frames(upload / FRAME_CLOCKS + 20);
        game_command(0x10, 1, 0, 0, 8);
        game_command(0x10, 2, 0, 0, 0);   /* echo delay changes; commands wait */
        game_command(0x18, 0, 0x80, 0, 0);
        size_t len, expected_len, actual_len;
        uint8_t *state = snes_state_write_with(&snes, sound_host_state(h), &len);
        CHECK(state != NULL, "round trip: save host");
        heard = 0xCBF29CE484222325ull;
        play_frames(60);
        game_command(0x10, 3, 0, 0, 30);
        uint64_t expected_sound = heard;
        uint8_t *expected = snes_state_write_with(&snes, sound_host_state(h), &expected_len);
        sound_host_detach(h);

        /* A fresh host must restore both its fields and the bindings that
         * let later commands and ticks reach its own driver and voices. */
        snes_init(&snes, rom, ROM_SIZE);
        snes_power_on(&snes);
        char why[256];
        CHECK(snes_state_read(&snes, state, len, why, sizeof why) != 0 && strstr(why, "host program"),
              "round trip: host state accepted without host: %s", why);
        h = sound_host_attach_mixer(&snes, mixer);
        CHECK(h != NULL, "round trip: attach restored host");
        if (!h)
            return;
        CHECK(snes_state_read_with(&snes, sound_host_state(h), state, len, why, sizeof why) == 0,
              "round trip: restore mixer %d: %s", mixer, why);
        heard = 0xCBF29CE484222325ull;
        play_frames(60);
        game_command(0x10, 3, 0, 0, 30);
        CHECK(heard == expected_sound, "round trip: mixer %d sound differs", mixer);
        uint8_t *actual = snes_state_write_with(&snes, sound_host_state(h), &actual_len);
        CHECK(expected && actual && expected_len == actual_len && !memcmp(expected, actual, actual_len),
              "round trip: mixer %d machine or host fields differ after playing on", mixer);
        sound_host_detach(h);
        free(actual);
        free(expected);
        free(state);
        free(rom);
    }
}

int main(void)
{
    voices_dsp_init(&voices_dsp, NULL, read_reg, write_reg, write_memory, read_memory);
    test_voices();
    test_driver();
    test_samples();
    test_host_state();
    test_host();
    test_host_round_trip();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("sound_test: ok\n");
    return 0;
}
