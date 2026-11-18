/* ct_gba: the game on the GBA with its picture and its sound
 * (docs/PLATFORM.md, "The GBA build, as built").
 *
 * The driver runs one frame of the game per turn of its loop, as ct_native
 * does, with the pads from the keypad (system/keypad.h) or, where a runner
 * drives it from outside (system/drive_box.h), from the runner. The game's
 * save RAM is the cartridge's SRAM, which the game reads and writes where it
 * is (services/cartridge_save.h). Asked by a runner, it reports over
 * mGBA's debug log: the frames and lag frames run, a CRC-32 of work RAM and
 * save RAM (as ct_native --end-state writes them) and of save RAM alone, each
 * part's work a frame, and the memory it used. A frame of the game takes as
 * long as its work, which is what the run measures, and begins at once
 * where the game is behind the GBA's time (Clock, below); its picture is made
 * as it ends and shown from the next vertical blank, made in that blank
 * where it can't be made ahead, or left out (Pictures, below). */
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

#include "services/cartridge_save.h"
#include "services/direct_sound.h"
#include "services/mixer.h"
#include "services/video.h"
#include "system/debug_log.h"
#include "system/drive.h"
#include "system/fiber.h"
#include "system/frames.h"
#include "system/interrupts.h"
#include "system/keypad.h"

/* The pack after the program (pack.py): the catalogue's resources in its
 * order, the sound's samples, the colours' table where it was packed with a
 * profile that changes them, the scripts read a command at a time where it
 * was packed with them, and the packed streams unpacked
 * (host/streams.py). */
typedef struct {
    char magic[4];
    uint32_t catalogue, count;
    uint32_t samples_at;           /* services/mixer.h, MixerSamples */
    uint32_t colours_at;           /* services/video.h, gba_video_use_colours; 0: none */
    uint32_t records_at;           /* the script records (frontend/script_records.h); 0: none */
    uint32_t unpacked_at;          /* a word a resource: where its AssetUnpacked is from there, or 0 */
} PackHead;
typedef struct { uint32_t at, length; } PackEntry;
/* The script records: "CTSR", the catalogue's version and its count, then
 * each resource's records' place (from "CTSR") and length, 0 where none. */
typedef struct {
    char magic[4];
    uint32_t catalogue, count;
} RecordsHead;

extern const uint8_t __rom_end[];
extern const uint8_t __iwram_bss_end[], __stack_top[];   /* the driver's stack, zeroed at the start (start.s) */

/* The driver's stack ends in a canary, which a frame that ran the stack
 * down past its room writes over: the game's frame and the picture run on
 * it, and fast RAM leaves it little to spare. */
enum { CANARY = 0x5AFEC0DEu };
static volatile uint32_t *canary(void) { return (volatile uint32_t *)(uintptr_t)__iwram_bss_end; }

static void check_stacks(void)
{
    if (*canary() != CANARY) {
        fprintf(stderr, "ct_gba: the driver's stack ran past its %u bytes\n", (unsigned)(__stack_top - __iwram_bss_end));
        abort();
    }
    if (!gba_interrupt_stacks_whole()) {
        fprintf(stderr, "ct_gba: the interrupts' or the mixing's stack ran past its room\n");
        abort();
    }
}

/* The driver's stack's deepest use: from its lowest byte that isn't zero,
 * above the canary. */
static size_t driver_stack_used(void)
{
    const uint8_t *at = __iwram_bss_end + sizeof(uint32_t);
    while (at < __stack_top && !*at)
        at++;
    return (size_t)(__stack_top - at);
}

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

static const uint8_t *pack_unpacked(const PackHead *head, AssetId id, uint32_t *length)
{
    const uint8_t *table = (const uint8_t *)head + head->unpacked_at;
    uint32_t at = id < head->count ? ((const uint32_t *)table)[id] : 0;
    if (!at)
        return NULL;
    const AssetUnpacked *u = (const AssetUnpacked *)(table + at);
    *length = sizeof *u + u->length;
    return table + at;
}

static const uint8_t *pack_form(void *ctx, uint8_t form, AssetId id, uint32_t *length)
{
    const PackHead *head = ctx;
    if (form == ASSET_FORM_UNPACKED)
        return pack_unpacked(head, id, length);
    if (form != ASSET_FORM_SCRIPT_RECORDS || !head->records_at)
        return NULL;
    const RecordsHead *records = (const RecordsHead *)((const uint8_t *)head + head->records_at);
    if (id >= records->count)
        return NULL;
    const PackEntry *e = (const PackEntry *)(records + 1) + id;
    *length = e->length;
    return e->at ? (const uint8_t *)records + e->at : NULL;
}

static void read_game(void *ctx, uint32_t addr, uint8_t *out, uint32_t len) { state_host_read_span(ctx, addr, out, len); }
static const uint8_t *game_in_place(void *ctx, uint32_t addr, uint32_t len) { return state_host_span_in_place(ctx, addr, len); }
static void run_main_thread(Platform *pf, void *ctx) { program_main_thread(ctx, pf); }
static void run_vblank(Platform *pf, void *ctx) { program_vblank(ctx, pf); }
static void run_line(Platform *pf, void *ctx) { program_line(ctx, pf); }

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
 * histogram in quarters of a GBA frame (280,896 cycles) for its 99th
 * percentile, which needs no memory a frame. The histogram goes to 16 GBA
 * frames, the heaviest frames in its last quarter: no part's 99th
 * percentile has come near it (the largest, the jet bike race's picture,
 * is under 2.5), and work RAM has the room for Mode 7's runs of lines
 * (services/video.c) for it. */
enum { GBA_FRAME_CYCLES = 280896, GBA_LINE_CYCLES = 1232, BUCKET = GBA_FRAME_CYCLES / 4, BUCKETS = 64 };
typedef struct {
    const char *name;
    uint64_t total;
    uint32_t most;
    uint16_t count[BUCKETS];
} Cycles;

static void count_cycles(Cycles *c, uint32_t cycles)
{
    c->total += cycles;
    if (cycles > c->most)
        c->most = cycles;
    uint16_t *count = &c->count[cycles / BUCKET < BUCKETS ? cycles / BUCKET : BUCKETS - 1];
    if (*count < UINT16_MAX)
        (*count)++;
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

/* The mixing's cycles a frame, the total and the heaviest: made in the
 * playback's interrupt, inside whichever part it came in, so no histogram
 * of its own (work RAM has no room for one). */
typedef struct {
    uint64_t total;
    uint32_t most;
} Mixing;

static void count_mixing(Mixing *m, uint32_t cycles)
{
    m->total += cycles;
    if (cycles > m->most)
        m->most = cycles;
}

/* The game's frames against the GBA's time: a game frame begins no sooner
 * than a GBA frame after the last began, so the game never runs ahead of the
 * GBA, and at once where it is behind, as after a frame that ran long, until
 * it has caught up; it gives up catching up on more than MOST_BEHIND, a
 * second. The
 * frames' beginnings stay on the GBA's vertical blanks, a GBA frame apart
 * from the first: a frame that has its picture made ahead (Pictures, below)
 * then has it shown at the blank its next frame begins in. How long the
 * driver held a frame back, for the report. */
enum { MOST_BEHIND = 60 * GBA_FRAME_CYCLES };
typedef struct {
    uint32_t next;   /* cycles_now() where the next game frame may begin */
    uint64_t held;
} Clock;

static bool behind_the_clock(const Clock *c) { return (int32_t)(cycles_now() - c->next) >= 0; }

static void hold_to_the_clock(Clock *c)
{
    uint32_t now = cycles_now();
    if ((int32_t)(c->next - now) > 0) {
        while ((int32_t)(c->next - cycles_now()) > 0)
            ;
        c->held += c->next - now;
    }
    while ((int32_t)(now - c->next) > MOST_BEHIND)
        c->next += GBA_FRAME_CYCLES;
    c->next += GBA_FRAME_CYCLES;
}

/* A frame's picture is made as the frame ends, and shown from the next
 * vertical blank (services/video.h, gba_video_make_ahead); a picture that
 * can only be made in a blank waits for it (gba_video_present). Where the
 * game is behind the GBA's time, one is left out, and the next frame begins
 * at once, where the frame ends with the last one made ahead still waiting
 * for its blank, or where the last picture's making and this frame's game
 * and sound take more than a GBA frame, so that the game catches up; never
 * two in a row, so the picture is never more than a frame behind the game.
 * Where the game is ahead the picture is made in time the driver would hold
 * the game back for. What the game does
 * doesn't depend on its picture: the picture is made from the video memory
 * the game's frames leave, and a picture left out leaves its changes to the
 * next. How long the driver waited for blanks, for the report. */
typedef struct {
    volatile uint32_t due;     /* cycles_now() at the vertical blank a GBA frame after the last picture shown */
    volatile bool waiting;     /* a picture made ahead, not shown yet */
    bool left_out;             /* the last frame's picture */
    uint32_t made;             /* cycles: the last frame's picture's making, 0 where it was left out */
    uint32_t pictures_left_out, left_out_waiting;   /* all, and where the last made ahead still waited */
    uint32_t given_way;                             /* made ahead and never shown, the next made in its place */
    volatile uint32_t pictures_shown;
    volatile uint32_t shown_blank, shown_frame;   /* the last shown: where its blank began, its game frame */
    volatile bool unpaced;                        /* not counted in the pacing yet (pictures_paced) */
    uint64_t awaited;
} Pictures;

static bool picture_left_out(Pictures *p, uint32_t game_and_sound, bool behind)
{
    p->left_out = !p->left_out && behind && (p->waiting || p->made + game_and_sound > GBA_FRAME_CYCLES);
    if (p->left_out) {
        p->pictures_left_out++;
        p->left_out_waiting += p->waiting;
        p->made = 0;
    }
    return p->left_out;
}

/* A picture made in the blank waits for the one it is due at, never the
 * one the last picture was shown in: a short frame that ended inside that
 * blank would otherwise show its picture in it too, so that the GBA never
 * showed the last one, and make two pictures for one shown. Half a GBA
 * frame before the picture is due is past the end of the last one's blank,
 * which is under a third of a frame long. */
static void picture_await_due(const Pictures *p)
{
    while ((int32_t)(cycles_now() - (p->due - GBA_FRAME_CYCLES / 2)) < 0)
        ;
    gba_video_await_blank();
}

/* The picture of the game's frame `frame` made, and the cycles it took
 * here: ahead, or in the blank it is due at. */
static uint32_t picture_made_for(Pictures *p, Platform *pf, uint32_t frame)
{
    uint32_t making = cycles_now();
    bool was_waiting = p->waiting;
    p->waiting = true;   /* until shown (picture_on_screen), where made ahead */
    if (gba_video_make_ahead(pf, frame)) {
        p->given_way += was_waiting;
        return cycles_now() - making;
    }
    p->waiting = false;
    picture_await_due(p);
    uint32_t blank = cycles_now();
    p->awaited += blank - making;
    gba_video_present(pf, frame);
    return cycles_now() - blank;
}

/* The pictures' pacing as a player sees it: how many GBA frames apart each
 * picture shown is from the last, a histogram, and the longest gaps, with
 * the game's frame that ended them and whose work they were: the last
 * picture's making, after its blank, and the heaviest game and sound of the
 * frames between. */
enum { PACING_GAPS = 16, LONGEST_GAPS = 8 };
typedef struct {
    uint32_t gap;                  /* GBA frames, in hundredths */
    uint32_t frame;                /* the game's frame whose picture ended it */
    uint32_t picture, game, sound; /* cycles */
} Gap;
typedef struct {
    uint32_t last_blank, last_picture, game_most, sound_most;
    uint16_t count[PACING_GAPS + 1];
    Gap longest[LONGEST_GAPS];
} Pacing;

static void frame_paced(Pacing *p, uint32_t game, uint32_t sound)
{
    if (game > p->game_most)
        p->game_most = game;
    if (sound > p->sound_most)
        p->sound_most = sound;
}

static void picture_paced(Pacing *p, uint32_t blank, uint32_t frame)
{
    uint32_t cycles = blank - p->last_blank;
    uint32_t gap = (cycles + GBA_FRAME_CYCLES / 2) / GBA_FRAME_CYCLES;
    uint16_t *count = &p->count[gap < PACING_GAPS ? gap : PACING_GAPS];
    if (*count < UINT16_MAX)
        (*count)++;
    unsigned least = 0;
    for (unsigned i = 1; i < LONGEST_GAPS; i++)
        if (p->longest[i].gap < p->longest[least].gap)
            least = i;
    uint32_t hundredths = (uint32_t)((uint64_t)cycles * 100 / GBA_FRAME_CYCLES);
    if (hundredths > p->longest[least].gap)
        p->longest[least] = (Gap){ hundredths, frame, p->last_picture, p->game_most, p->sound_most };
    p->last_blank = blank;
    p->game_most = p->sound_most = 0;
}

static void picture_made(Pacing *p, uint32_t cycles) { p->last_picture = cycles; }

/* The steady state: how many GBA frames each game frame's work fills, its
 * game, its sound and its picture's making: one where it fits, two where it
 * runs into the next, and so on. */
enum { FILLED = 4 };
typedef struct {
    uint32_t count[FILLED];   /* 1, 2, 3, more */
} Filled;

static void frame_filled(Filled *f, uint32_t work)
{
    uint32_t frames = work / GBA_FRAME_CYCLES;
    f->count[frames < FILLED ? frames : FILLED - 1]++;
}

static void report_filled(const Filled *f)
{
    fprintf(stderr, "ct_gba: GBA frames a game frame's work fills (1, 2, 3, more): %u %u %u %u\n",
            (unsigned)f->count[0], (unsigned)f->count[1], (unsigned)f->count[2], (unsigned)f->count[3]);
}

static void report_pacing(const Pacing *p)
{
    fprintf(stderr, "ct_gba: pictures apart, in GBA frames (0-%u, more):", PACING_GAPS - 1);
    for (unsigned i = 0; i <= PACING_GAPS; i++)
        fprintf(stderr, " %u", (unsigned)p->count[i]);
    fprintf(stderr, "\n");
    for (unsigned i = 0; i < LONGEST_GAPS; i++)
        if (p->longest[i].gap) {
            char picture[16], game[16], sound[16];
            gba_frames(picture, sizeof picture, p->longest[i].picture);
            gba_frames(game, sizeof game, p->longest[i].game);
            gba_frames(sound, sizeof sound, p->longest[i].sound);
            fprintf(stderr, "ct_gba: a gap of %u.%02u GBA frames to frame %u: the picture before it %s, the game's "
                            "heaviest frame %s, the sound's %s\n",
                    (unsigned)(p->longest[i].gap / 100), (unsigned)(p->longest[i].gap % 100),
                    (unsigned)p->longest[i].frame, picture, game, sound);
        }
}

/* What the report covers: the frames run and each part's cycles in them. */
typedef struct {
    GameState *g;
    Platform *pf;
    uint32_t frames;
    Clock clock;
    Pictures pictures;
    Pacing pacing;
    Filled filled;
    Cycles game, sounds, picture;
    Mixing mixing;
} Run;

static Run run = { .game = { .name = "game" }, .sounds = { .name = "sound" }, .picture = { .name = "picture" } };

/* A picture goes on the screen, from the vertical blank's interrupt where
 * it was made ahead: the runner told, and where the blank began noted for
 * the pacing, which the driver counts outside the interrupt (pictures_paced):
 * the interrupts' stack has no room for it. */
static void picture_on_screen(uint32_t frame)
{
    drive_showing(frame);
    uint32_t blank = cycles_now() - gba_video_lines_into_blank() * GBA_LINE_CYCLES;
    run.pictures.due = blank + GBA_FRAME_CYCLES;
    run.pictures.waiting = false;
    run.pictures.pictures_shown++;
    run.pictures.shown_blank = blank;
    run.pictures.shown_frame = frame;
    run.pictures.unpaced = true;
}

/* The picture shown since the driver last looked, counted in the pacing. */
static void pictures_paced(void)
{
    if (!run.pictures.unpaced)
        return;
    run.pictures.unpaced = false;
    picture_paced(&run.pacing, run.pictures.shown_blank, run.pictures.shown_frame);
}

static void report(void *ctx)
{
    const Run *run = ctx;
    fprintf(stderr, "ct_gba: %u frame(s); %u lag frame(s)\n", (unsigned)run->frames,
            (unsigned)tm_host_lag_frames(run->pf));
    if (frames_replayed() && frames_decisions_taken() == frames_decisions_given())
        fprintf(stderr, "ct_gba: %u of the runner's decisions replayed\n", (unsigned)frames_decisions_taken());
    else if (frames_replayed())
        fprintf(stderr, "ct_gba: the runner's decisions unread: %u of %u replayed\n", (unsigned)frames_decisions_taken(),
                (unsigned)frames_decisions_given());
    if (run->frames) {
        report_cycles(&run->game, run->frames);
        report_cycles(&run->sounds, run->frames);
        report_cycles(&run->picture, run->frames);
        fprintf(stderr, "ct_gba: %u picture(s) shown, %u left out, the game's frame running late (%u with the "
                        "last still waiting for its blank), %u made ahead and giving way to the next\n",
                (unsigned)run->pictures.pictures_shown, (unsigned)run->pictures.pictures_left_out,
                (unsigned)run->pictures.left_out_waiting, (unsigned)run->pictures.given_way);
        char held[16], awaited[16];
        gba_frames(held, sizeof held, run->clock.held);
        gba_frames(awaited, sizeof awaited, run->pictures.awaited);
        fprintf(stderr, "ct_gba: GBA frames waited: %s holding the game to the GBA's time, %s for blanks to make "
                        "pictures in\n",
                held, awaited);
        report_pacing(&run->pacing);
        report_filled(&run->filled);
        char mean[16], most[16];
        gba_frames(mean, sizeof mean, run->mixing.total / run->frames);
        gba_frames(most, sizeof most, run->mixing.most);
        fprintf(stderr, "ct_gba: mixing a frame, in GBA frames: mean %s, heaviest %s\n", mean, most);
    }
    fprintf(stderr, "ct_gba: sound %u sample(s) queued, %u played, %u of silence where the ring wasn't filled\n",
            (unsigned)ct_gba_sound.queued, (unsigned)ct_gba_sound.played, (unsigned)ct_gba_sound.silent);
    fprintf(stderr, "ct_gba: end state crc32 %08X, save RAM crc32 %08X\n", (unsigned)end_state_crc(run->g),
            (unsigned)ct_crc32(state_host_sram(run->g), STATE_HOST_SRAM_BYTES));
    fprintf(stderr, "ct_gba: arena %u bytes, the SNES's video memory %u\n", (unsigned)arena_used(),
            (unsigned)gba_video_bytes());
    for (unsigned s = 0; s < fiber_slots(); s++)
        if (fiber_stack_high_water(s))
            fprintf(stderr, "ct_gba: stack %u %u of %u bytes\n", s, (unsigned)fiber_stack_high_water(s),
                    (unsigned)gba_fiber_slot_size(s));
    fprintf(stderr, "ct_gba: the driver's stack %u of %u bytes\n", (unsigned)driver_stack_used(),
            (unsigned)(__stack_top - __iwram_bss_end));
    fprintf(stderr, "ct_gba: the interrupts' stack %u of %u bytes, the mixing's %u of %u\n",
            (unsigned)gba_interrupts_stack_used(), (unsigned)gba_interrupts_stack_size(),
            (unsigned)gba_nested_stack_used(), (unsigned)gba_nested_stack_size());
    fprintf(stderr, "ct_gba: reported\n");
}

int main(void)
{
    debug_log_open();
    const PackHead *head = pack();
    if (memcmp(head->magic, "CTGB", 4) || head->catalogue != asset_catalogue_version ||
        head->count != asset_catalogue_count) {
        fprintf(stderr, "ct_gba: no pack of this catalogue after the program: pack it again (targets/gba/host/pack.py)\n");
        abort();
    }
    if (head->records_at) {
        const RecordsHead *records = (const RecordsHead *)((const uint8_t *)head + head->records_at);
        if (memcmp(records->magic, "CTSR", 4) || records->catalogue != asset_catalogue_version ||
            records->count != asset_catalogue_count) {
            fprintf(stderr, "ct_gba: the pack's script records are another catalogue's: pack it again\n");
            abort();
        }
    }
    static AssetSource source;
    source = (AssetSource){ pack_resource, (void *)head, pack_form };
    assets_use(&source);

    GameState *g = state_host_create_lasting_over(cartridge_save_open());
    static Platform pf;
    if (!g) {
        fprintf(stderr, "ct_gba: no memory for the game's state\n");
        abort();
    }
    *canary() = CANARY;
    pf.save.bytes = state_host_sram(g);
    vid_host_start(&pf, read_game, g);
    gba_video_read_in_place(game_in_place);
    gba_video_use_colours(head->colours_at ? (const uint16_t *)((const uint8_t *)head + head->colours_at) : NULL);
    mixer_use_samples((const MixerSamples *)((const uint8_t *)head + head->samples_at));
    SoundNative *sound = sound_native_create(g, &pf);
    if (!sound) {
        fprintf(stderr, "ct_gba: no memory for the sound: %u bytes, with %u of the arena taken\n",
                (unsigned)sound_native_bytes(), (unsigned)arena_used());
        abort();
    }
    /* no time of the original's kept: the frames decided by the runner's
     * decisions or the GBA's own time (system/frames.h) */
    tm_host_start(&pf, &(TmHostProgram){ run_main_thread, run_vblank, run_line, g, NULL, NULL, NULL, frames_decide, NULL,
                                         frames_pass },
                  1);
    direct_sound_start(mixer_fill);
    bool driven = drive_open();
    gba_video_tell_shown(picture_on_screen);
    fprintf(stderr, "ct_gba: playing, the pads from %s\n", driven ? "the runner" : "the keypad");

    /* Each frame's cycles: the game's, the sound driver's and the picture's,
     * and the mixing's, in whichever of them the playback's interrupt came. */
    run.g = g, run.pf = &pf;
    static Keypad keypad;
    cycles_start();
    frames_start(driven);
    fprintf(stderr, "ct_gba: the game's frames decided by %s\n",
            frames_replayed() ? "the runner's decisions" : "the GBA's own time");
    gba_video_await_blank();
    run.clock.next = cycles_now();
    run.pictures.due = run.clock.next + GBA_FRAME_CYCLES;
    run.pacing.last_blank = run.clock.next;
    for (;;) {
        hold_to_the_clock(&run.clock);
        uint32_t pads = driven ? drive_next_pads(g, report, &run) : keypad_buttons(&keypad, keypad_held());
        in_host_set(&pf, 0, (uint16_t)pads);
        in_host_set(&pf, 1, (uint16_t)(pads >> 16));
        uint32_t began = cycles_now();
        frames_game_begins();
        tm_host_frame(&pf);
        uint32_t game_done = cycles_now();
        frames_game_took(game_done - began);
        sound_native_frame(sound);
        uint32_t sound_done = cycles_now();
        frame_paced(&run.pacing, game_done - began, sound_done - game_done);
        pictures_paced();
        uint32_t picture = 0;
        if (!picture_left_out(&run.pictures, sound_done - began, behind_the_clock(&run.clock))) {
            picture = run.pictures.made = picture_made_for(&run.pictures, &pf, run.frames + 1);
            pictures_paced();
            picture_made(&run.pacing, picture);
        }
        count_cycles(&run.game, game_done - began);
        count_cycles(&run.sounds, sound_done - game_done);
        count_cycles(&run.picture, picture);
        frame_filled(&run.filled, sound_done - began + picture);
        count_mixing(&run.mixing, mixer_take_cycles());
        check_stacks();
        run.frames++;
    }
}
