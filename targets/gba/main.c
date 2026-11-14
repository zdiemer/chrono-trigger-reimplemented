/* ct_gba: the game on the GBA with no picture and no sound heard
 * (docs/PLATFORM.md, "The GBA build, as built").
 *
 * The driver plays the movie the packing step put in the cartridge
 * (targets/gba/pack.py), one frame of the game per call of its loop as
 * ct_native does, then reports the end over mGBA's debug log: the frames and
 * lag frames run, a CRC-32 of work RAM and save RAM at the end (as
 * ct_native --end-state writes them) and of save RAM alone, and the memory
 * it used. The loop doesn't wait for the GBA's own frames: a frame of the
 * game takes as long as its work, which is what the run measures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checksum.h"
#include "core/program.h"
#include "data/assets.h"
#include "platform/native/host.h"
#include "port/arena.h"
#include "port/fiber.h"
#include "port/state_host.h"
#include "sound_native.h"

#include "debug_log.h"
#include "gba_video.h"

/* The pack after the program (pack.py): the catalogue's resources in its
 * order, the movie's pads and the save RAM it starts from. */
typedef struct {
    char magic[4];
    uint32_t catalogue, count;
    uint32_t frames, pads_at;      /* the movie: its frames, and a pad word each (pack.py) */
    uint32_t sram_at;              /* 0: none, save RAM erased */
} PackHead;
typedef struct { uint32_t at, length; } PackEntry;

extern const uint8_t __rom_end[];

static const PackHead *pack(void)
{
    uintptr_t at = ((uintptr_t)__rom_end + 255u) & ~(uintptr_t)255u;   /* pack.py's alignment */
    return (const PackHead *)at;
}

static const uint8_t *pack_resource(void *ctx, AssetId id, uint32_t *length)
{
    const PackHead *head = ctx;
    if (id >= head->count)
        return NULL;
    const PackEntry *e = (const PackEntry *)(head + 1) + id;
    *length = e->length;
    return (const uint8_t *)head + e->at;
}

static uint8_t read_game(void *ctx, uint32_t addr) { return state_host_read8(ctx, addr); }
static void run_main_thread(Platform *pf, void *ctx) { program_main_thread(ctx, pf); }
static void run_vblank(Platform *pf, void *ctx) { program_vblank(ctx, pf); }
static void run_line(Platform *pf, void *ctx) { program_line(ctx, pf); }
static uint32_t take_owed(void *ctx) { return state_host_take_owed(ctx); }
static void game_accessed(void *ctx, uint32_t clocks) { state_host_game_access(ctx, clocks); }

enum { WORK_RAM = 0x7E0000, WORK_RAM_BYTES = 0x20000 };

/* CRC-32 as frontend/checksum.c's, a byte at a time over what --end-state writes. */
static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int k = 0; k < 8; k++)
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    return crc;
}

static uint32_t end_state_crc(GameState *g)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t at = 0; at < WORK_RAM_BYTES; at++)
        crc = crc_byte(crc, state_host_read8(g, WORK_RAM + at));
    const uint8_t *sram = state_host_sram(g);
    for (uint32_t at = 0; at < STATE_HOST_SRAM_BYTES; at++)
        crc = crc_byte(crc, sram[at]);
    return ~crc;
}

/* A part's cycles a frame over the run: the total, the heaviest, and a
 * histogram in eighths of a GBA frame (280,896 cycles) for its 99th
 * percentile, which needs no memory a frame. */
enum { GBA_FRAME_CYCLES = 280896, BUCKET = GBA_FRAME_CYCLES / 8, BUCKETS = 512 };
typedef struct {
    const char *name;
    uint64_t total;
    uint32_t most;
    uint32_t count[BUCKETS];
} Cycles;

static void count_cycles(Cycles *c, uint32_t cycles)
{
    c->total += cycles;
    if (cycles > c->most)
        c->most = cycles;
    c->count[cycles / BUCKET < BUCKETS ? cycles / BUCKET : BUCKETS - 1]++;
}

/* As GBA frames, to hundredths. */
static void gba_frames(char *out, size_t room, uint64_t cycles)
{
    snprintf(out, room, "%lu.%02lu", (unsigned long)(cycles / GBA_FRAME_CYCLES),
             (unsigned long)(cycles % GBA_FRAME_CYCLES * 100 / GBA_FRAME_CYCLES));
}

static void report_cycles(const Cycles *c, uint32_t frames)
{
    uint32_t seen = 0, bucket = 0;
    while (bucket < BUCKETS - 1 && (seen += c->count[bucket]) < frames - frames / 100)
        bucket++;
    char mean[16], p99[16], most[16];
    gba_frames(mean, sizeof mean, c->total / frames);
    gba_frames(p99, sizeof p99, (uint64_t)(bucket + 1) * BUCKET);
    gba_frames(most, sizeof most, c->most);
    fprintf(stderr, "ct_gba: %s a frame, in GBA frames: mean %s, 99th percentile under %s, heaviest %s\n", c->name,
            mean, p99, most);
}

/* The game's frame the driver is in, for a runner that takes pictures at
 * chosen frames (targets/gba/mgba_run.c). */
volatile uint32_t ct_gba_frame;

int main(void)
{
    debug_log_open();
    const PackHead *head = pack();
    if (memcmp(head->magic, "CTGB", 4) || head->catalogue != asset_catalogue_version ||
        head->count != asset_catalogue_count) {
        fprintf(stderr, "ct_gba: no pack of this catalogue after the program: pack it again (targets/gba/pack.py)\n");
        abort();
    }
    static AssetSource source;
    source = (AssetSource){ pack_resource, (void *)head };
    assets_use(&source);

    uint32_t frames = head->frames;
    const uint32_t *pads = (const uint32_t *)((const uint8_t *)head + head->pads_at);

    GameState *g = state_host_create_lasting(head->sram_at ? (const uint8_t *)head + head->sram_at : NULL);
    static Platform pf;
    if (!g) {
        fprintf(stderr, "ct_gba: no memory for the game's state\n");
        abort();
    }
    pf.save.bytes = state_host_sram(g);
    vid_host_start(&pf, read_game, g);
    SoundNative *sound = sound_native_create(g, &pf);
    if (!sound) {
        fprintf(stderr, "ct_gba: no memory for the sound\n");
        abort();
    }
    tm_host_start(&pf, &(TmHostProgram){ run_main_thread, run_vblank, run_line, g, take_owed, game_accessed, NULL }, 1);
    fprintf(stderr, "ct_gba: playing %u frame(s)\n", (unsigned)frames);

    /* Each frame's cycles, the game's, the sound's and the picture's. */
    static Cycles game = { .name = "game" }, sounds = { .name = "sound" }, picture = { .name = "picture" };
    cycles_start();
    for (uint32_t frame = 0; frame < frames; frame++) {
        ct_gba_frame = frame;
        in_host_set(&pf, 0, (uint16_t)pads[frame]);
        in_host_set(&pf, 1, (uint16_t)(pads[frame] >> 16));
        uint32_t began = cycles_now();
        tm_host_frame(&pf);
        uint32_t game_done = cycles_now();
        sound_native_frame(sound);
        sound_native_discard_audio(sound);
        uint32_t sound_done = cycles_now();
        gba_video_present(&pf);
        count_cycles(&game, game_done - began);
        count_cycles(&sounds, sound_done - game_done);
        count_cycles(&picture, cycles_now() - sound_done);
        if (frame % 600 == 599)
            fprintf(stderr, "ct_gba: frame %u\n", (unsigned)frame + 1);
    }
    ct_gba_frame = frames;

    fprintf(stderr, "ct_gba: %u frame(s); %u lag frame(s)\n", (unsigned)frames, (unsigned)tm_host_lag_frames(&pf));
    if (frames) {
        report_cycles(&game, frames);
        report_cycles(&sounds, frames);
        report_cycles(&picture, frames);
    }
    fprintf(stderr, "ct_gba: end state crc32 %08X, save RAM crc32 %08X\n", (unsigned)end_state_crc(g),
            (unsigned)ct_crc32(state_host_sram(g), STATE_HOST_SRAM_BYTES));
    fprintf(stderr, "ct_gba: arena %u bytes, the SNES's video memory %u\n", (unsigned)arena_used(),
            (unsigned)gba_video_bytes());
    for (unsigned s = 0; s < fiber_slots(); s++)
        if (fiber_stack_high_water(s))
            fprintf(stderr, "ct_gba: stack %u %u of %u bytes\n", s, (unsigned)fiber_stack_high_water(s),
                    (unsigned)fiber_stack_size());
    fprintf(stderr, "ct_gba: done\n");
    for (;;)
        ;
}
