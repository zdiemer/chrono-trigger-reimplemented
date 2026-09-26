/* Field objects' frames: drawing an object's new frame.
 *
 * An object's sprites show tiles the field keeps for it in VRAM
 * (object_sprite_tiles). When its animation moves on to a frame it doesn't
 * show yet, the frame queue (the objects queued by field/animation.c, worked
 * through while the frame has lines to spare) has the frame builder draw the
 * new frame: it claims tile areas in bank $7F for it (field/sprites.c),
 * draws the frame's tiles there from the object's graphics set as the frame's
 * description names them (field/graphics.c copies each, straight or
 * mirrored), queues the areas to be sent to the object's tiles in VRAM, and
 * puts the frame's sprites -- their offsets from the object, tiles and
 * attributes -- in the frame's data, from which the object's sprite records
 * are rebuilt when it is next drawn, freeing the areas.
 *
 * A 4-sprite frame is drawn at once. The larger layouts are drawn a part at a
 * time, one part each time the queue comes to the object (a 12-sprite frame
 * in three), and the object stays at the queue's head until its frame is
 * whole; the parts drawn are counted in the records' state, which is also
 * what tells the records to wait for the whole frame. Each layout's plan --
 * which of the description's tiles each part draws where, what is sent to
 * VRAM and how the sprites' tiles follow each other there -- is a row of one
 * table (plans). */
#include "field/frames.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/graphics.h"
#include "field/object.h"
#include "field/parts.h"
#include "field/sprites.h"

#include "charge.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The plans ------------------------------------------------------------------------------ */

/* A frame's description names its tiles, four a sprite, as words, and then
 * each sprite's X and Y offset from the object, a byte each. A run of its
 * tiles is drawn one after another into the areas, TILE_BYTES apart, from a
 * place in them. */
enum { TILE_BYTES = FIELD_TILE_BYTES, WORDS_A_TILE_NAME = 2, TILES_A_SPRITE = 4, OFFSET_BYTES = 2 };

typedef struct {
    uint8_t first, count;   /* the description's tiles */
    uint16_t to;            /* bytes into the object's areas */
} TileRun;

/* A part of the frame: the runs drawn together, the second (if any) where
 * the first doesn't continue. */
enum { MOST_RUNS = 2, MOST_PARTS = 3 };
typedef struct {
    uint8_t runs;
    TileRun run[MOST_RUNS];
} FramePart;

/* What is sent to VRAM once the frame is whole: two stretches of the areas,
 * each to the object's tiles plus a number of VRAM words. */
typedef struct {
    uint16_t from, bytes, to_words;
} Upload;

/* Where each sprite's tiles are in VRAM: two tiles on from the last's (a
 * 16-pixel sprite's top row), and past a row of VRAM's tiles once, after
 * `row_break` sprites, where the sprites run on to the next pair of rows
 * (NO_ROW_BREAK: none). */
enum { NEXT_SPRITE_TILE = 2, NEXT_TILE_ROW = 0x10, NO_ROW_BREAK = 0xFF };

/* Which half of the object a sprite's priority is taken from, which decides
 * whether the sprite shows in front of the map's layers or behind them as
 * that half does (object_set_sprite_priorities): the first `lower_sprites`
 * the lower half's, the rest the upper half's. The 4-sprite layout's frames
 * place their sprites freely, so there it goes by where each starts
 * (BY_OFFSET): the original tests the Y offset as an unsigned byte for $E8
 * and above, which is the offsets -24 to -1, so a sprite starting less than
 * 24 lines above the object's point takes the upper half's priority, and
 * one starting further up, or below the point, the lower half's. */
enum { BY_OFFSET = 0xFF, UPPER_OFFSET = -24 };

typedef struct {
    uint8_t sprites;
    uint8_t parts;
    FramePart part[MOST_PARTS];
    Upload upload[2];
    uint8_t row_break;
    uint8_t lower_sprites;
} FramePlan;

/* 16 tiles, the two rows of four 16-pixel sprites. */
static const FramePlan FOUR = {
    4, 1, { { 1, { { 0, 16, 0x000 } } } },
    { { 0x000, 0x100, 0x000 }, { 0x100, 0x100, 0x100 } }, NO_ROW_BREAK, BY_OFFSET,
};
/* 32 tiles in two parts of 16. */
static const FramePlan EIGHT = {
    8, 2, { { 1, { { 0, 16, 0x000 } } }, { 1, { { 16, 16, 0x200 } } } },
    { { 0x000, 0x200, 0x000 }, { 0x200, 0x200, 0x100 } }, NO_ROW_BREAK, 4,
};
/* 48 tiles in three parts: the top rows of the first eight sprites across
 * the first two parts' first runs, their bottom rows across their second,
 * and the last four sprites' rows last, below the first eight's in VRAM. */
static const FramePlan TWELVE = {
    12, 3,
    { { 2, { { 0, 8, 0x000 }, { 8, 8, 0x200 } } },
      { 2, { { 16, 8, 0x100 }, { 24, 8, 0x300 } } },
      { 1, { { 32, 16, 0x400 } } } },
    { { 0x000, 0x500, 0x000 }, { 0x500, 0x100, 0x300 } }, 8, 4,
};
/* The same where the object's tiles start at tile $68, half way along a row
 * of VRAM's: the first four sprites fill the row's end, and the other eight
 * start on the next pair of rows. The original tests for that tile alone
 * (another start half way along a row would take the plan above, and its
 * sprites would run off the row); the game gives no 12-sprite object any
 * other. */
enum { TWELVE_SPLIT_TILES = 0x68 };
static const FramePlan TWELVE_SPLIT = {
    12, 3,
    { { 1, { { 0, 16, 0x000 } } },
      { 2, { { 16, 8, 0x200 }, { 24, 8, 0x400 } } },
      { 2, { { 32, 8, 0x300 }, { 40, 8, 0x500 } } } },
    { { 0x000, 0x100, 0x000 }, { 0x100, 0x500, 0x100 } }, 4, 4,
};

static const FramePlan *plan_for(const GameState *g, FieldObject obj, SpriteLayout layout)
{
    switch (layout) {
    case LAYOUT_4: return &FOUR;
    case LAYOUT_8: return &EIGHT;
    default: return (uint8_t)object_sprite_tiles(g, obj) == TWELVE_SPLIT_TILES ? &TWELVE_SPLIT : &TWELVE;
    }
}

static uint8_t description_bytes(const FramePlan *plan)
{
    return (uint8_t)(plan->sprites * (TILES_A_SPRITE * WORDS_A_TILE_NAME + OFFSET_BYTES));
}

/* ---- A frame's description ------------------------------------------------------------------- */

/* Where a frame's description is: the object's descriptions' start plus the
 * frame's number times a description's size, summed in 16 bits, in their
 * bank. */
static uint32_t frame_description(const GameState *g, FieldObject obj, const FramePlan *plan, uint8_t frame)
{
    ObjectGraphics d = object_frame_descriptions(g, obj);
    return (uint32_t)d.bank << 16 | (uint16_t)(d.address + frame * description_bytes(plan));
}

static uint16_t tile_name(const GameState *g, uint32_t description, unsigned tile)
{
    return field_frame_tile_word(g, description + tile * WORDS_A_TILE_NAME);
}

static int8_t sprite_offset(const GameState *g, const FramePlan *plan, uint32_t description, unsigned n, Axis axis)
{
    unsigned offsets = (unsigned)plan->sprites * TILES_A_SPRITE * WORDS_A_TILE_NAME;
    return (int8_t)field_frame_offset_byte(g, description + offsets + n * OFFSET_BYTES + axis);
}

/* ---- For the original's code and shadow mode (field/scratch.h) -------------------------------- */

/* Drawing starts from cells the original's copies read: the graphics set,
 * the build place's bank, and the frame tables' bank the frame read reads. */
static void leave_start(GameState *g, ObjectGraphics set)
{
    set_field_sprite_graphics_bank(g, set.bank);
    set_field_sprite_graphics_address(g, set.address);
    set_field_sprite_tile_bank(g, FIELD_BUILD_BANK);
    set_field_frame_entry_bank(g, FIELD_FRAME_TABLES_BANK);
}

/* A finished frame's sprites' attributes before their priority: the palette
 * and the tiles' page ($D9). */
enum { ATTRIBUTES_CELL = 0xD9 };
static void leave_attributes(GameState *g, FieldObject obj)
{
    original_cell8(g, ATTRIBUTES_CELL, (uint8_t)(field_object_palette(g, obj) | object_sprite_tiles(g, obj) >> 8));
}

/* The new frame's number, which the claim is made with ($EE's low byte). */
static void leave_new_frame(GameState *g, uint8_t frame)
{
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | frame));
}

/* The description's place, found with the multiplier. */
static void leave_description(GameState *g, const FramePlan *plan, uint8_t frame, uint32_t description)
{
    hw_set_multiplicand(g, frame);
    (void)hw_multiply_by(g, description_bytes(plan));
    set_field_frame_description_address(g, (uint16_t)description);
    set_field_frame_description_bank(g, (uint8_t)(description >> 16));
}

/* After a run: its count run down to 0, the Y the last copy saved (the last
 * tile's place in the description), the place of the last tile, and the work
 * RAM port's cursor past it, which the copies leave there. */
static void leave_run(GameState *g, TileRun run, uint16_t last_at)
{
    set_field_frame_tiles_left(g, 0);
    original_cell16(g, FIELD_SAVED_Y_CELL, (uint16_t)((run.first + run.count - 1) * WORDS_A_TILE_NAME));
    set_field_sprite_tile_offset(g, last_at);
    hw_port_seek(g, (uint16_t)(last_at + TILE_BYTES), FIELD_BUILD_BANK & 1);
}

/* ---- Drawing ------------------------------------------------------------------------------- */

/* A run of the description's tiles, one after another from its place in the
 * object's areas. */
static void draw_run(GameState *g, FieldObject obj, ObjectGraphics set, uint32_t description, TileRun run)
{
    uint16_t at = (uint16_t)(field_object_frame_staging(g, obj) + run.to);
    for (unsigned t = 0; t < run.count; t++)
        field_draw_frame_tile(g, set, tile_name(g, description, run.first + t), (uint16_t)(at + t * TILE_BYTES));
    ORIGINAL_EFFECT(leave_run(g, run, (uint16_t)(at + (run.count - 1) * TILE_BYTES)));
}

static void draw_part(GameState *g, FieldObject obj, ObjectGraphics set, uint32_t description, const FramePart *part)
{
    for (unsigned r = 0; r < part->runs; r++)
        draw_run(g, obj, set, description, part->run[r]);
    object_note_frame_part_drawn(g, obj);
}

/* ---- The whole frame ---------------------------------------------------------------------- */

/* The VRAM queue (field_vram_queue_*, sent in the next vblank by
 * field/display.c): an entry takes the frame's two stretches, and the entry
 * after it marks the queue's end. The builder marks the entries as words,
 * which reaches the byte after each flag. */
enum { QUEUE_ENTRY = 2, VRAM_WORDS_A_TILE = 16, TILE_NUMBER_BITS = 0x1FF };

static void mark_entry(GameState *g, unsigned e, uint16_t word)
{
    set_field_vram_queue_used(g, e, (uint8_t)word);
    set_field_vram_queue_used_high(g, e, (uint8_t)(word >> 8));
}

static void queue_upload(GameState *g, FieldObject obj, const FramePlan *plan)
{
    unsigned e = field_vram_queue_end(g) / QUEUE_ENTRY;
    uint16_t areas = field_object_frame_staging(g, obj);
    uint16_t tiles = (uint16_t)((object_sprite_tiles(g, obj) & TILE_NUMBER_BITS) * VRAM_WORDS_A_TILE);
    set_field_vram_queue_source(g, e, (uint16_t)(areas + plan->upload[0].from));
    set_field_vram_queue_vram(g, e, (uint16_t)(tiles + plan->upload[0].to_words));
    set_field_vram_queue_size(g, e, plan->upload[0].bytes);
    set_field_vram_queue_source2(g, e, (uint16_t)(areas + plan->upload[1].from));
    set_field_vram_queue_vram2(g, e, (uint16_t)(tiles + plan->upload[1].to_words));
    set_field_vram_queue_size2(g, e, plan->upload[1].bytes);
    mark_entry(g, e, (uint16_t)((field_vram_queue_used_high(g, e) << 8 | field_vram_queue_used(g, e)) + 1));
    mark_entry(g, e + 1, 0);
    set_field_vram_queue_end(g, (uint8_t)(field_vram_queue_end(g) + QUEUE_ENTRY));
}

/* The frame's sprites into the frame's data. */
static void frame_sprites(GameState *g, FieldObject obj, const FramePlan *plan, uint32_t description)
{
    uint8_t tile = (uint8_t)object_sprite_tiles(g, obj);
    for (unsigned n = 0; n < plan->sprites; n++) {
        if (n == plan->row_break)
            tile = (uint8_t)(tile + NEXT_TILE_ROW);
        int8_t y = sprite_offset(g, plan, description, n, AXIS_Y);
        bool upper = plan->lower_sprites == BY_OFFSET ? (y >= UPPER_OFFSET && y < 0) : n >= plan->lower_sprites;
        FrameSprite sprite = { sprite_offset(g, plan, description, n, AXIS_X), y, tile,
                               object_frame_attributes(g, obj, upper) };
        sprites_set_frame_sprite(g, obj, n, sprite);
        tile = (uint8_t)(tile + NEXT_SPRITE_TILE);
    }
}

/* ---- The builder --------------------------------------------------------------------------- */

/* The part of the frame the object is due to draw: the 4-sprite layout's one,
 * or the larger layouts' next (any beyond the last is the last). */
static unsigned part_due(const GameState *g, FieldObject obj, const FramePlan *plan)
{
    if (plan->parts == 1)
        return 0;
    unsigned drawn = object_records_state(g, obj).changed;
    return drawn < plan->parts ? drawn : (unsigned)plan->parts - 1;
}

/* The object's next frame, where it starts one: the frame a script holds it
 * on, or the one its animation moves on to (field/animation.c), timed as the
 * read that takes it. */
static ObjectFrame next_frame(GameState *g, FieldObject obj)
{
    if (object_holds_frame(g, obj))
        return (ObjectFrame){ true, object_held_frame(g, obj) };
    ObjectFrame f;
    TIMED_AS(g, 0xC0CB3A, f = object_take_next_frame(g, obj, FIELD_FRAME_TABLES_BANK));
    return f;
}

// $C0:CAD9 (and its layouts' $C0:CBDC, $C0:CEF5, $C0:D4F7)
/* Draw the object's next frame, or the next part of it. An object holding a
 * frame draws the 8-sprite layout's whole at once; one holding a 12-sprite
 * frame never draws it, and stays at the queue's head. */
static FrameBuild draw_next_frame(GameState *g, FieldObject obj)
{
    if (!object_can_be_seen(g, obj) || object_view(g, obj) == OUT_OF_VIEW)
        return FRAME_NOT_DUE;
    SpriteLayout layout = object_sprite_layout(g, obj);
    if (layout == LAYOUT_24)
        return FRAME_NOT_DUE;
    ObjectGraphics set = object_graphics(g, obj);
    ORIGINAL_EFFECT(leave_start(g, set));
    bool holding = object_holds_frame(g, obj);
    if (holding && layout == LAYOUT_12)
        return FRAME_WAITS;

    const FramePlan *plan = plan_for(g, obj, layout);
    unsigned first = holding ? 0 : part_due(g, obj, plan);
    unsigned last = holding ? (unsigned)plan->parts - 1 : first;
    if (first == 0) {
        ObjectFrame f = next_frame(g, obj);
        if (!f.has_frame)
            return FRAME_NO_NEXT;
        if (f.frame == object_frame_drawn(g, obj))
            return FRAME_ALREADY_DRAWN;
        ORIGINAL_EFFECT(leave_new_frame(g, f.frame));
        if (!sprites_claim_frame_areas(g, obj, layout))
            return FRAME_NO_ROOM;
        object_set_frame_drawn(g, obj, f.frame);
    }
    uint8_t frame = object_frame_drawn(g, obj);
    uint32_t description = frame_description(g, obj, plan, frame);
    ORIGINAL_EFFECT(leave_description(g, plan, frame, description));
    for (unsigned p = first; p <= last; p++)
        draw_part(g, obj, set, description, &plan->part[p]);
    if (last + 1 < plan->parts)
        return FRAME_PART_DRAWN;
    queue_upload(g, obj, plan);
    frame_sprites(g, obj, plan, description);
    ORIGINAL_EFFECT(leave_attributes(g, obj));
    return FRAME_DRAWN;
}

/* ---- For the derivations (field/parts.h, field/leaves.c) ------------------------------------ */

FieldDrawnTile field_frame_part_last_tile(const GameState *g, FieldObject obj, unsigned part)
{
    const FramePlan *plan = plan_for(g, obj, object_sprite_layout(g, obj));
    const FramePart *p = &plan->part[part];
    TileRun run = p->run[p->runs - 1];
    uint32_t description = frame_description(g, obj, plan, object_frame_drawn(g, obj));
    uint16_t at = (uint16_t)(field_object_frame_staging(g, obj) + run.to + (run.count - 1) * TILE_BYTES);
    return (FieldDrawnTile){ at, (tile_name(g, description, run.first + run.count - 1u) & FIELD_TILE_MIRRORED) != 0 };
}

FieldFrameSent field_frame_sent(const GameState *g, FieldObject obj)
{
    const FramePlan *plan = plan_for(g, obj, object_sprite_layout(g, obj));
    uint8_t before_the_skip = (uint8_t)((uint8_t)object_sprite_tiles(g, obj) + plan->row_break * NEXT_SPRITE_TILE);
    return (FieldFrameSent){ plan->upload[1].from, plan->row_break != NO_ROW_BREAK, before_the_skip, NEXT_TILE_ROW };
}

/* ---- The entry ------------------------------------------------------------------------------ */

// $C0:CAD9
/* The current object (8-bit X). The carry says the object stays at the
 * queue's head, with more of its frame to draw. The overflow is worked out
 * from what drawing came to (field/leaves.c): liveness keeps it past the
 * frame queue, round the main loop to the menus, whose entry saves the flags
 * ($C2:8013) and puts them back on leaving; no decision of the field's reads
 * it (banks $C0 and $FD have no branch on it). */
void field_draw_next_frame(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    FrameBuildBefore before = { m_flag(s, CPU_FLAG_V), field_object_animation_frame(g, obj) };
    FrameBuild outcome = draw_next_frame(g, obj);
    m_set_flag(s, CPU_FLAG_V, field_frame_build_overflow(g, obj, outcome, before));
    m_set_flag(s, CPU_FLAG_C, outcome == FRAME_PART_DRAWN || outcome == FRAME_WAITS);
    m_rts(s);
}

/* The guard: the current object's pictures where the port reads them as the
 * original does -- its frame tables in their bank for the frame read, its
 * descriptions in the ROM, its graphics in the ROM below its last bank
 * (whose tiles would carry into bank $00, where nothing answers) or built in
 * bank $7F (elsewhere the mirrored copy would read the I/O registers), and
 * built graphics whose tiles the frame names inside the bank (past it the
 * copies would read low RAM, the builder's own cells among it), its records
 * inside their tables for the frame's
 * data -- the areas it goes on drawing into clear of the bank's end (a
 * frame's first part claims its own), and room in the VRAM queue for its
 * entry and the end after it. */
enum { FIRST_ROM_BANK = 0xC0, LAST_BANK = 0xFF, MOST_AREA_BYTES = 0x600, QUEUE_ENTRIES = 8 };
#define BANK_BYTES 0x10000u

/* The frame the builder would draw from, where it draws one: the one drawn,
 * for a part after the first; else the held or the next. */
static ObjectFrame frame_ahead(const GameState *g, FieldObject obj, const FramePlan *plan, bool holding)
{
    if (!holding && part_due(g, obj, plan) > 0)
        return (ObjectFrame){ true, object_frame_drawn(g, obj) };
    if (holding)
        return (ObjectFrame){ true, object_held_frame(g, obj) };
    return object_next_frame(g, obj, FIELD_FRAME_TABLES_BANK);
}

static bool built_tiles_in_bank(const GameState *g, FieldObject obj, SpriteLayout layout)
{
    ObjectGraphics graphics = object_graphics(g, obj);
    if (graphics.bank != FIELD_BUILD_BANK || layout == LAYOUT_24)
        return true;
    const FramePlan *plan = plan_for(g, obj, layout);
    ObjectFrame f = frame_ahead(g, obj, plan, object_holds_frame(g, obj));
    if (!f.has_frame)
        return true;
    uint32_t description = frame_description(g, obj, plan, f.frame);
    for (unsigned t = 0; t < (unsigned)plan->sprites * TILES_A_SPRITE; t++) {
        uint32_t end = graphics.address + (uint32_t)(tile_name(g, description, t) & FIELD_TILE_NUMBER) * TILE_BYTES + TILE_BYTES;
        if (end > BANK_BYTES)
            return false;
    }
    return true;
}

bool field_frames_can_draw(Snes *s)
{
    if (!field_current_object_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    SpriteLayout layout = object_sprite_layout(g, obj);
    uint8_t end = field_vram_queue_end(g);
    bool goes_on = layout != LAYOUT_24 && !object_holds_frame(g, obj) &&
                   part_due(g, obj, plan_for(g, obj, layout)) > 0;
    uint8_t graphics = object_graphics(g, obj).bank;
    bool ok = object_frame_tables_in_bank(g, obj) && object_frame_descriptions(g, obj).bank >= FIRST_ROM_BANK &&
              ((graphics >= FIRST_ROM_BANK && graphics != LAST_BANK) || graphics == FIELD_BUILD_BANK) && built_tiles_in_bank(g, obj, layout) &&
              sprites_records_in_the_tables(g, obj, layout) &&
              (!goes_on || (uint32_t)field_object_frame_staging(g, obj) + MOST_AREA_BYTES <= BANK_BYTES) &&
              !(end & 1) && end / QUEUE_ENTRY + 1 < QUEUE_ENTRIES;
    vars_peek(false);
    return ok;
}
