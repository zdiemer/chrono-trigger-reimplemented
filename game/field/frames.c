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
 * table (plans).
 *
 * When the field comes back from a battle it puts each of the party and the
 * enemies back as they were: their tiles in VRAM still show the frame they
 * had, so that frame's sprites go straight onto their records, through the
 * same plans, with nothing drawn (object_put_drawn_frame_back).
 *
 * An object of the 24-sprite layout (a large figure a script loads) draws no
 * frames through the queue: its one frame is drawn whole when it is loaded,
 * and again when the field is set up after a menu, straight into its own
 * place in VRAM, with its sprites' records laid out once
 * (object_draw_whole_figure). */
#include "field/frames.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/graphics.h"
#include "field/object.h"
#include "field/parts.h"
#include "field/sprites.h"
#include "platform/timing.h"
#include "platform/video.h"

#include "charge.h"
#include "original_effects.h"
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

/* A frame's description for `sprites` sprites: their tiles' names, then their
 * offsets. */
static uint8_t description_bytes_for(unsigned sprites)
{
    return (uint8_t)(sprites * (TILES_A_SPRITE * WORDS_A_TILE_NAME + OFFSET_BYTES));
}
static uint8_t description_bytes(const FramePlan *plan) { return description_bytes_for(plan->sprites); }

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

static int8_t sprite_offset(const GameState *g, unsigned sprites, uint32_t description, unsigned n, Axis axis)
{
    unsigned offsets = sprites * TILES_A_SPRITE * WORDS_A_TILE_NAME;
    return (int8_t)field_frame_offset_byte(g, description + offsets + n * OFFSET_BYTES + axis);
}

/* ---- For the original's code and shadow mode (field/scratch.h) -------------------------------- */

/* Drawing starts from cells the original's copies read: the graphics set,
 * the build place's bank, and the frame tables' bank the frame read reads. */
static void leave_drawing_from(GameState *g, ObjectGraphics set)
{
    set_field_sprite_graphics_bank(g, set.bank);
    set_field_sprite_graphics_address(g, set.address);
    set_field_sprite_tile_bank(g, FIELD_BUILD_BANK);
}

static void leave_start(GameState *g, ObjectGraphics set)
{
    leave_drawing_from(g, set);
    set_field_frame_entry_bank(g, FIELD_FRAME_TABLES_BANK);
}

/* A finished frame's sprites' attributes before their priority: the palette
 * and the tiles' page. */
static void leave_attributes(GameState *g, FieldObject obj)
{
    original_cell8(g, FIELD_LAST_WORKED_CELL, (uint8_t)(field_object_palette(g, obj) | object_sprite_tiles(g, obj) >> 8));
}

/* The new frame's number, which the claim is made with ($EE's low byte). */
static void leave_new_frame(GameState *g, uint8_t frame)
{
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | frame));
}

/* The description's place, found with the multiplier. */
static void leave_description_place(GameState *g, uint32_t description)
{
    set_field_frame_description_address(g, (uint16_t)description);
    set_field_frame_description_bank(g, (uint8_t)(description >> 16));
}

static void leave_description(GameState *g, const FramePlan *plan, uint8_t frame, uint32_t description)
{
    hw_set_multiplicand(g, frame);
    (void)hw_multiply_by(g, description_bytes(plan));
    leave_description_place(g, description);
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

/* The frame's sprites, into the frame's data the records are rebuilt from,
 * or straight onto the records. */
typedef enum { INTO_THE_FRAME_DATA, ONTO_THE_RECORDS } SpritesGo;

static void frame_sprites(GameState *g, FieldObject obj, const FramePlan *plan, uint32_t description, SpritesGo where)
{
    uint8_t tile = (uint8_t)object_sprite_tiles(g, obj);
    for (unsigned n = 0; n < plan->sprites; n++) {
        if (n == plan->row_break)
            tile = (uint8_t)(tile + NEXT_TILE_ROW);
        int8_t y = sprite_offset(g, plan->sprites, description, n, AXIS_Y);
        bool upper = plan->lower_sprites == BY_OFFSET ? (y >= UPPER_OFFSET && y < 0) : n >= plan->lower_sprites;
        FrameSprite sprite = { sprite_offset(g, plan->sprites, description, n, AXIS_X), y, tile,
                               object_frame_attributes(g, obj, upper) };
        if (where == INTO_THE_FRAME_DATA)
            sprites_set_frame_sprite(g, obj, n, sprite);
        else
            sprites_set_record_sprite(g, obj, n, sprite);
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

/* Whether the object is on the screen as sprites: shown and in view. The
 * 24-sprite layout's object is shown too, but its frames are its records, so
 * nothing of its frame is drawn or put back. */
static bool object_on_screen(const GameState *g, FieldObject obj)
{
    return object_can_be_seen(g, obj) && object_view(g, obj) != OUT_OF_VIEW;
}

// $C0:CAD9 (and its layouts' $C0:CBDC, $C0:CEF5, $C0:D4F7)
/* Draw the object's next frame, or the next part of it. An object holding a
 * frame draws the 8-sprite layout's whole at once; one holding a 12-sprite
 * frame never draws it, and stays at the queue's head. */
static FrameBuild draw_next_frame(GameState *g, FieldObject obj)
{
    if (!object_on_screen(g, obj))
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
    frame_sprites(g, obj, plan, description, INTO_THE_FRAME_DATA);
    ORIGINAL_EFFECT(leave_attributes(g, obj));
    return FRAME_DRAWN;
}

/* ---- The 24-sprite figure ----------------------------------------------------------------- */

/* The figure's 96 tiles, 24 sprites of four, drawn in bank $7F where the
 * frame builder draws (field_figure_tiles, its areas borrowed and the bytes
 * after them) and sent to VRAM at once from tile $40, where its sprites show
 * them: eight sprites a row of tiles, the rows two tile rows apart. The
 * figure shows palette 1 at priority 2 whatever its object's palette is
 * (field_object_palette, which the frames the queue draws take). */
enum { FIGURE_SPRITES = 24, FIGURE_TILES = FIGURE_SPRITES * TILES_A_SPRITE, FIGURE_FIRST_TILE = 0x40,
       FIGURE_SPRITES_A_ROW = 8, FIGURE_PALETTE = 1, FIGURE_PRIORITY = 2, FIGURE_FRAME = 0 };
enum { FIGURE_VRAM = FIGURE_FIRST_TILE * VRAM_WORDS_A_TILE };
enum { ATTRIBUTE_PALETTE_SHIFT = 1, ATTRIBUTE_PRIORITY_SHIFT = 4 };

static uint16_t figure_place(void) { return RAM_ABS(RAM_FIELD_FIGURE_TILES); }

static FrameSprite figure_sprite(const GameState *g, uint32_t description, unsigned n)
{
    return (FrameSprite){ sprite_offset(g, FIGURE_SPRITES, description, n, AXIS_X),
                          sprite_offset(g, FIGURE_SPRITES, description, n, AXIS_Y),
                          (uint8_t)(FIGURE_FIRST_TILE + (n / FIGURE_SPRITES_A_ROW) * 2 * NEXT_TILE_ROW +
                                    (n % FIGURE_SPRITES_A_ROW) * NEXT_SPRITE_TILE),
                          (uint8_t)(FIGURE_PRIORITY << ATTRIBUTE_PRIORITY_SHIFT | FIGURE_PALETTE << ATTRIBUTE_PALETTE_SHIFT) };
}

/* For the original's code and shadow mode: the set and the description it
 * drew from, and the run of all its tiles, as a frame's drawing leaves them. */
static void leave_figure(GameState *g, ObjectGraphics set, uint32_t description)
{
    leave_drawing_from(g, set);
    leave_description_place(g, description);
    leave_run(g, (TileRun){ 0, FIGURE_TILES, 0 }, (uint16_t)(figure_place() + (FIGURE_TILES - 1) * TILE_BYTES));
}

// $C0:E12A
/* Draw a 24-sprite figure's frame whole: its tiles from its graphics set as
 * its description names them, sent at once to its place in VRAM, and each
 * sprite's record laid out from the description's offsets. The figure shows
 * frame 0, from the place it is drawn in. */
void object_draw_whole_figure(GameState *g, Platform *pf, FieldObject obj)
{
    ObjectGraphics set = object_graphics(g, obj), d = object_frame_descriptions(g, obj);
    uint32_t description = (uint32_t)d.bank << 16 | d.address;
    set_field_object_frame_staging(g, obj, figure_place());
    object_set_frame_drawn(g, obj, FIGURE_FRAME);
    for (unsigned t = 0; t < FIGURE_TILES; t++)
        field_draw_frame_tile(g, set, tile_name(g, description, t), (uint16_t)(figure_place() + t * TILE_BYTES));
    ORIGINAL_EFFECT(leave_figure(g, set, description));
    vid_send_vram(pf, PF_AT(0xC0E19A), FIGURE_VRAM, PF_GAME(RAM_FIELD_FIGURE_TILES), FIELD_FIGURE_TILES_BYTES,
                  VID_VRAM_WORD_PORT);
    for (unsigned n = 0; n < FIGURE_SPRITES; n++)
        sprites_set_record_sprite(g, obj, n, figure_sprite(g, description, n));
}

// $C0:CB0A (and its layouts' $C0:CDC8, $C0:D124, $C0:DD28)
/* Put the frame the object's tiles show straight onto its records, built,
 * with nothing drawn: where the field brings the party and the enemies back
 * after a battle ($C0:07EC, $C0:08A0), whose tiles in VRAM are as they were.
 * An object off the screen gets nothing, and one of the 24-sprite layout
 * keeps its records as they are. */
void object_put_drawn_frame_back(GameState *g, FieldObject obj)
{
    if (!object_on_screen(g, obj))
        return;
    SpriteLayout layout = object_sprite_layout(g, obj);
    if (layout == LAYOUT_24)
        return;
    const FramePlan *plan = plan_for(g, obj, layout);
    uint8_t frame = object_frame_drawn(g, obj);
    uint32_t description = frame_description(g, obj, plan, frame);
    ORIGINAL_EFFECT(leave_description(g, plan, frame, description));
    frame_sprites(g, obj, plan, description, ONTO_THE_RECORDS);
    ORIGINAL_EFFECT(leave_attributes(g, obj));
    object_mark_records_built(g, obj);
}

/* ---- The queue ------------------------------------------------------------------------------ */

/* How a frame's drawing ended: the queue drawn through, or the frame's time
 * up with objects left in it; or an object's drawing didn't come back to the
 * queue (the original's code went on elsewhere), and it is left as it was. */
typedef enum { QUEUE_DRAWN, QUEUE_OUT_OF_TIME, DRAWING_WENT_ELSEWHERE } QueueDrawn;

/* An object's frame drawn, a part at a time for the larger layouts; or, as
 * C, the object first in the queue holding its frame until the next one
 * (FRAME_WAITS), which no further call this frame changes. */
typedef enum { FRAME_WHOLE, FRAME_HAS_MORE_PARTS, FRAME_HELD_TO_THE_NEXT, FRAME_DRAWING_WENT_ELSEWHERE } FrameDrawing;
typedef FrameDrawing (*DrawFrame)(void *ctx, FieldObject obj, bool alone);

// $C0:CA76
/* Every frame, once the objects' animations have moved on: the queued
 * objects' new frames are drawn, the first in the queue first, while the
 * frame has time for them, an object staying first until its frame is whole;
 * what is left waits for the next frame. While what the last frame drew is
 * still waiting to go to VRAM, nothing is drawn. */
static QueueDrawn draw_queued_frames(GameState *g, Platform *pf, DrawFrame draw, void *ctx)
{
    if (field_vram_queue_used(g, 0) != 0)
        return QUEUE_DRAWN;
    set_field_vram_queue_end(g, 0);
    while (tm_frame_time_left(pf, PF_AT(0xC0CA85), field_animation_line_limit(g))) {
        FrameQueueFront front = object_frame_queue_front(g);
        if (!front.queued)
            return QUEUE_DRAWN;
        object_make_current(g, front.first);
        FrameDrawing drawn = draw(ctx, front.first, front.alone);
        if (drawn == FRAME_DRAWING_WENT_ELSEWHERE)
            return DRAWING_WENT_ELSEWHERE;
        /* the original asks again until the beam ends the frame's time; a
         * host's time doesn't run out, so the frame's drawing ends here */
        if (drawn == FRAME_HELD_TO_THE_NEXT)
            return QUEUE_OUT_OF_TIME;
        if (drawn == FRAME_WHOLE)
            object_frame_queue_take_first(g, front.alone);
    }
    return QUEUE_OUT_OF_TIME;
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

/* A part of the object's next frame drawn, as C: true while more of its
 * frame is to draw. An object holding a 12-sprite frame never draws it, and
 * the original's screen setup, which asks again until the frame is whole,
 * would ask for ever (nothing moves the object on while the screen is set
 * up, so no movie reaches it); as C the drawing ends there, as the frame
 * queue's does on a host (FRAME_HELD_TO_THE_NEXT). */
bool field_frame_part_drawn(GameState *g, FieldObject obj)
{
    return draw_next_frame(g, obj) == FRAME_PART_DRAWN;
}

// $C0:CAD9
/* The current object (8-bit X). The carry says the object stays at the
 * queue's head, with more of its frame to draw. The overflow is worked out
 * from what drawing came to (field/leaves.c): the sprite table's building
 * after the frame queue leaves it as it is, and it goes round the main loop
 * to the next frame's pause and menu opening ($C0:18D9), which reach the
 * sound driver's saved flags ($C7:0A39's PHP, taken back other than by PLP)
 * and the menus' BVC at $C2:F7D9; tools/v_readers.py --after C0:CAD9 has it
 * read. No decision of the field's reads it (banks $C0 and $FD have no
 * branch on it). */
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

/* Each object's frame is the frame builder's entry ($C0:CAD9), called on the
 * machine from the original's call of it: one call for the only object in
 * the queue, another for the first of several. As for the scripts' turns
 * (field/script_turns.c), the call is for shadow mode's comparison, a frame's
 * drawing at a time, not for the beam, which a C call would read right too. A
 * build with no machine calls the builder as C. It reads the current object
 * and the overflow, and says in the carry whether the frame has more parts to
 * draw. */
enum { DRAW_A_FRAME = 0xC0CAD9, THE_FIRST_OF_SEVERAL = 0xC0CAAE, THE_ONLY_ONE = 0xC0CAC2 };
enum { THE_BUILDER_READS_THE_OBJECT = DC_A | DC_X | DC_Y | DC_N | DC_Z };

/* For shadow mode: the carry the original's choice between its two calls
 * leaves, the queue's head compared with its tail, which the builder then
 * replaces with its answer. */
static void leave_the_choice(Snes *s)
{
    const GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, field_animation_queue_head(g) >= field_animation_queue_tail(g));
}

static FrameDrawing draw_on_the_machine(void *ctx, FieldObject obj, bool alone)
{
    (void)obj;
    Snes *s = ctx;
    ORIGINAL_EFFECT(leave_the_choice(s));
    if (!bridge_call(s, alone ? THE_ONLY_ONE : THE_FIRST_OF_SEVERAL, DRAW_A_FRAME, THE_BUILDER_READS_THE_OBJECT,
                     on_the_field_page(s, true)))
        return FRAME_DRAWING_WENT_ELSEWHERE;
    return m_flag(s, CPU_FLAG_C) ? FRAME_HAS_MORE_PARTS : FRAME_WHOLE;
}

static FrameDrawing draw_the_frame(void *ctx, FieldObject obj, bool alone)
{
    (void)alone;
    GameState *g = ctx;
    FrameBuild outcome = FRAME_NO_NEXT;
    TIMED_AS(g, DRAW_A_FRAME, outcome = draw_next_frame(g, obj));
    if (outcome == FRAME_WAITS)
        return FRAME_HELD_TO_THE_NEXT;
    return outcome == FRAME_PART_DRAWN ? FRAME_HAS_MORE_PARTS : FRAME_WHOLE;
}

void field_queued_frames_drawn(GameState *g, Platform *pf) { (void)draw_queued_frames(g, pf, draw_the_frame, g); }

/* With 8-bit A and index registers, as the original returns. */
void field_draw_queued_frames(Snes *s)
{
    if (draw_queued_frames(state_of(s), m_platform(s), draw_on_the_machine, s) == DRAWING_WENT_ELSEWHERE)
        return;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_rts(s);
}

/* Its guard: on the field's page, its cells through a data bank that
 * reaches low RAM, the queue in the tables. */
bool field_queued_frames_can_be_drawn(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    bool ok = object_frame_queue_in_the_tables(state_of(s));
    vars_peek(false);
    return ok;
}

// $C0:E12A
/* The current object, in X too. It returns with 16-bit index registers
 * whatever it came with, as the original does. Of what it leaves, two are
 * read after it: the overflow, which the last sum of the drawing (a tile's
 * place in the build bank, well below $8000) leaves clear, and which goes on
 * round the main loop to the menus' entry, as the frame builder's does; and Y,
 * at the description's last byte (the last sprite's Y offset), which the event
 * engine's dispatch has live after the command that loads the figure
 * ($C0:47E5) returns. */
enum { FIGURE_Y_AFTER = FIGURE_TILES * WORDS_A_TILE_NAME + FIGURE_SPRITES * OFFSET_BYTES - 1 };

uint16_t field_whole_figure_y(void) { return FIGURE_Y_AFTER; }

void field_draw_whole_figure(Snes *s)
{
    object_draw_whole_figure(state_of(s), m_platform(s), (FieldObject)field_current_object_slot(s));
    m_rep(s, CPU_FLAG_X);
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.y = FIGURE_Y_AFTER;
    m_rts(s);
}

// $C0:CB0A
/* The current object (8-bit X). Nothing the original leaves in the
 * registers is read after it (its callers' own callers read none). */
void field_put_drawn_frame(Snes *s)
{
    GameState *g = state_of(s);
    object_put_drawn_frame_back(g, (FieldObject)field_current_object_slot(s));
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
        uint32_t end = field_set_tile_start(graphics, tile_name(g, description, t)) + TILE_BYTES;
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

/* Its guard: the object's descriptions in the ROM and its records inside
 * their tables. */
bool object_drawn_frame_can_go_back(const GameState *g, FieldObject obj)
{
    return object_frame_descriptions(g, obj).bank >= FIRST_ROM_BANK &&
           sprites_records_in_the_tables(g, obj, object_sprite_layout(g, obj));
}

/* The figure's guard: the current object on the field's page with the data
 * bank $00, the same object in X, its description in the ROM, its records
 * inside their tables, and its graphics set in the ROM or built in bank $7F
 * (as the figures the game loads are), each tile there inside the bank and
 * clear of the build place the figure is drawn into. */
static bool figure_tiles_clear(const GameState *g, ObjectGraphics set, ObjectGraphics d)
{
    uint32_t description = (uint32_t)d.bank << 16 | d.address;
    uint32_t place = figure_place(), place_end = place + FIELD_FIGURE_TILES_BYTES;
    for (unsigned t = 0; t < FIGURE_TILES; t++) {
        uint32_t start = field_set_tile_start(set, tile_name(g, description, t)), end = start + TILE_BYTES;
        if (end > BANK_BYTES || !(end <= place || start >= place_end))
            return false;
    }
    return true;
}

bool field_figure_drawable(const GameState *g, ObjectGraphics set, ObjectGraphics descriptions, uint16_t records)
{
    return descriptions.bank >= FIRST_ROM_BANK && sprites_records_fit(records, LAYOUT_24) &&
           ((set.bank >= FIRST_ROM_BANK && set.bank != LAST_BANK) ||
            (set.bank == FIELD_BUILD_BANK && figure_tiles_clear(g, set, descriptions)));
}

bool field_figure_can_be_drawn(Snes *s)
{
    if (!field_current_object_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    bool ok = (s->cpu.x >> 1) == obj && !(s->cpu.x & 1) &&
              field_figure_drawable(g, object_graphics(g, obj), object_frame_descriptions(g, obj),
                                    object_sprite_records(g, obj));
    vars_peek(false);
    return ok;
}

bool field_frames_can_put(Snes *s)
{
    if (!field_current_object_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = object_making_current_recognises(g) &&
              object_drawn_frame_can_go_back(g, (FieldObject)field_current_object_slot(s));
    vars_peek(false);
    return ok;
}
