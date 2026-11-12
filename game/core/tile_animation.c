/* Animated background tiles: a waterfall falling, a light flickering, in every
 * game mode, sent from its interrupt handler.
 *
 * Twelve slots (tile_animations) each animate 128 bytes of tiles in VRAM
 * through eight frames. A slot keeps an accumulator whose bits 9-11 say which
 * frame it is on; each time the slot is sent it steps on by that frame's step,
 * so a frame lasts as long as its step makes it, and the frame's tiles go to
 * the slot's place in VRAM from where the slot's table says they are
 * (tile_animation_tables, a step and a source for each frame). The slots take
 * turns: 0-5 are sent on even frames of the count (tile_animation_frame), 6-11
 * on odd ones. A slot whose VRAM address has its top bit set is unused.
 *
 * A location's tile set names the set of animations its slots start with,
 * loaded as the location is (tile_animations_load). */
#include "core/tile_animation.h"
#include "core/tile_animation_data.h"

#include "platform/video.h"
#include "port/vars_ram.h"
#include "vars.h"

/* What a slot sends: 128 bytes of tiles. */
enum { TILE_BYTES = 0x80, UNUSED_SLOT = 0x8000 };

static bool in_use(const GameState *g, unsigned slot) { return !(tile_animations_vram(g, slot) & UNUSED_SLOT); }

/* The frame a slot is on, 0-7: its accumulator's bits 9-11. */
enum { FRAME_SHIFT = 9, FRAMES = 8 };
static uint8_t current_frame(const GameState *g, unsigned slot)
{
    return (uint8_t)((tile_animations_accumulator(g, slot) >> FRAME_SHIFT) % FRAMES);
}

/* A frame's step and its tiles' address, a word each in the slot's table. */
static uint16_t table_word(uint8_t (*byte)(const GameState *, unsigned, unsigned), const GameState *g, unsigned slot,
                           uint8_t frame)
{
    return (uint16_t)(byte(g, slot, frame * 2u) | byte(g, slot, frame * 2u + 1) << 8);
}
static uint16_t frame_step(const GameState *g, unsigned slot, uint8_t frame)
{
    return table_word(tile_animation_tables_steps_byte, g, slot, frame);
}
static uint32_t frame_tiles(const GameState *g, unsigned slot, uint8_t frame)
{
    return (uint32_t)tile_animations_bank(g, slot) << 16 | table_word(tile_animation_tables_sources_byte, g, slot, frame);
}

/* A slot in use sends the frame it is on and steps on. `at` names the
 * original's send for this slot: it has one for each. */
static void send_slot(GameState *g, Platform *pf, unsigned slot, PfOrigin at)
{
    if (!in_use(g, slot))
        return;
    uint8_t frame = current_frame(g, slot);
    set_tile_animations_accumulator(g, slot, (uint16_t)(tile_animations_accumulator(g, slot) + frame_step(g, slot, frame)));
    vid_send_vram(pf, at, tile_animations_vram(g, slot), PF_GAME(frame_tiles(g, slot, frame)), TILE_BYTES, NULL);
}

static bool odd_frame(uint8_t count) { return count & 1; }

// $FD:E022
/* Count the frame on, and send this frame's half of the slots. */
void tile_animations_send(GameState *g, Platform *pf)
{
    vid_start_vram_uploads(pf, PF_AT(0xFDE02B), VID_VRAM_WORD_PORT);
    uint8_t count = (uint8_t)(tile_animation_frame(g) + 1);
    set_tile_animation_frame(g, count);
    if (!odd_frame(count)) {
        send_slot(g, pf, 0, PF_AT(0xFDE051));
        send_slot(g, pf, 1, PF_AT(0xFDE07D));
        send_slot(g, pf, 2, PF_AT(0xFDE0A9));
        send_slot(g, pf, 3, PF_AT(0xFDE0D5));
        send_slot(g, pf, 4, PF_AT(0xFDE101));
        send_slot(g, pf, 5, PF_AT(0xFDE12D));
    } else {
        send_slot(g, pf, 6, PF_AT(0xFDE166));
        send_slot(g, pf, 7, PF_AT(0xFDE192));
        send_slot(g, pf, 8, PF_AT(0xFDE1BE));
        send_slot(g, pf, 9, PF_AT(0xFDE1EA));
        send_slot(g, pf, 10, PF_AT(0xFDE216));
        send_slot(g, pf, 11, PF_AT(0xFDE242));
    }
}

/* ---- A location's animations ------------------------------------------------------------------ */

/* The ROM's sets of animations, a stream of twelve slots. A slot the set
 * leaves unused is one byte, UNUSED_IN_SET. Any other starts with how many
 * distinct frames the animation has, then the slot's place in VRAM, then the
 * frames' steps, a byte each, and their tiles' addresses, a word each, in bank
 * $7F. An animation of two or four frames is played round twice or four times
 * over the slot's eight; any other number in the stream is a full eight. A
 * step is kept four times as long as the stream gives it. */
enum { UNUSED_IN_SET = 0x80, SLOT_UNUSED_HIGH = 0x80, TILES_BANK = 0x7F, STEP_SCALE = 4 };

typedef struct {
    const GameState *g;
    uint16_t at;
} SetStream;

static uint8_t next_byte(SetStream *in) { return tile_animation_set_byte(in->g, in->at++); }
static uint16_t next_word(SetStream *in)
{
    uint16_t lo = next_byte(in);
    return (uint16_t)(lo | next_byte(in) << 8);
}

static uint8_t distinct_frames(uint8_t kind) { return kind == 2 || kind == 4 ? kind : FRAMES; }

/* The tables are written in the order of the slots in use: the n-th slot in
 * use gets the n-th table, whatever its number. Every set the game has puts its
 * unused slots last, so the n-th table is the n-th slot's, which is where the
 * uploads read it. */
static void load_slot(GameState *g, unsigned slot, unsigned table, SetStream *in)
{
    uint8_t kind = next_byte(in);
    uint16_t vram = next_word(in);
    set_tile_animations_accumulator(g, slot, 0);
    set_tile_animations_vram(g, slot, vram);
    set_tile_animations_bank(g, slot, TILES_BANK);
    uint8_t distinct = distinct_frames(kind);
    uint16_t steps[FRAMES], sources[FRAMES];
    for (unsigned f = 0; f < distinct; f++)
        steps[f] = (uint16_t)(next_byte(in) * STEP_SCALE);
    for (unsigned f = 0; f < distinct; f++)
        sources[f] = next_word(in);
    for (unsigned f = 0; f < FRAMES; f++) {
        uint16_t step = steps[f % distinct], source = sources[f % distinct];
        set_tile_animation_tables_steps_byte(g, table, 2 * f, (uint8_t)step);
        set_tile_animation_tables_steps_byte(g, table, 2 * f + 1, (uint8_t)(step >> 8));
        set_tile_animation_tables_sources_byte(g, table, 2 * f, (uint8_t)source);
        set_tile_animation_tables_sources_byte(g, table, 2 * f + 1, (uint8_t)(source >> 8));
    }
}

/* A slot the set leaves unused keeps what it had but its place, marked. */
static void leave_unused(GameState *g, unsigned slot)
{
    set_tile_animations_vram(g, slot, (uint16_t)(SLOT_UNUSED_HIGH << 8 | (tile_animations_vram(g, slot) & 0x00FF)));
}

// $FD:DE98
/* Start the animated tiles of the current location's tile set, all twelve
 * slots, from the set its record names. */
void tile_animations_load(GameState *g)
{
    SetStream in = { g, tile_animation_set_start(g, location_tile_animation_set(g, location_record_offset(g))) };
    unsigned tables = 0;
    for (unsigned slot = 0; slot < TILE_ANIMATIONS_COUNT; slot++) {
        if (tile_animation_set_byte(g, in.at) == UNUSED_IN_SET) {
            in.at++;
            leave_unused(g, slot);
            continue;
        }
        load_slot(g, slot, tables++, &in);
    }
}
