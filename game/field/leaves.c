/* Register results still carried across the field's machine compatibility
 * path and into the gated battle/menu callers. The field's own byte-exact
 * comparison and scratch reconstruction have retired. */
#include "event/variables.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/layers.h"
#include "field/parts.h"
#include "field/party.h"
#include "field/object.h"
#include "field/scratch.h"
#include "field/switches.h"
#include "vars.h"

/* ---- Casting an object (field/cast.c) ------------------------------------------------------- */

uint16_t field_cast_y(FieldSpriteRecord r, GraphicsSource source)
{
    if (source == GRAPHICS_SHARED)
        return (uint16_t)(r.animations * FIELD_WORD_ENTRY_BYTES);
    return (uint16_t)(r.graphics * FIELD_LONG_ENTRY_BYTES + 2);
}

/* ---- The frame read ($C0:CB3A) --------------------------------------------------------------- */

/* Its overflow is that of the entry's last sum: the frame taken back off,
 * where the animation started over; the table and the place in it
 * otherwise. It started over where it took a frame and left the animation a
 * frame before its first; where it was there already, the entry tells:
 * starting over reads from the animation's start, and a frame found there
 * from a place past it. */
bool field_frame_read_overflow(const GameState *g, FieldObject obj, bool took_a_frame, uint8_t frame_before)
{
    uint16_t entry = field_frame_entry_address(g);
    bool started_over = took_a_frame && field_animation_rewound(g, obj) &&
                        (!field_frame_before_the_first(frame_before) || entry == field_first_frame_place(g, obj));
    if (started_over)
        return field_subtract_overflows16((uint16_t)(entry + frame_before), frame_before);
    uint16_t table = field_facing_frame_table(g, obj);
    return field_add_overflows16((uint16_t)(entry - table), table);
}

/* ---- The frame builder ($C0:CAD9) --------------------------------------------------------- */

/* Its last sum's: where the frame read ended the build, the read's; where a
 * part's copies did, their last step along the areas (a straight copy's own
 * sum, or the step to a mirrored tile's place); where the frame was
 * finished, the second stretch's place in the areas, or where the tiles skip
 * a row, that skip. Its plain paths leave the overflow as it came. */
enum { TILE_STEP = 32 };

static bool copies_overflow(const GameState *g, FieldObject obj, unsigned part)
{
    FieldDrawnTile last = field_frame_part_last_tile(g, obj, part);
    return field_add_overflows16(last.mirrored ? (uint16_t)(last.at - TILE_STEP) : last.at, TILE_STEP);
}

static bool finished_overflow(const GameState *g, FieldObject obj)
{
    FieldFrameSent sent = field_frame_sent(g, obj);
    if (!sent.skips_a_row)
        return field_add_overflows16(field_object_frame_staging(g, obj), sent.second_stretch);
    return field_add_overflows8(sent.tile_before_the_skip, sent.row);
}

bool field_frame_build_overflow(const GameState *g, FieldObject obj, FrameBuild outcome, FrameBuildBefore before)
{
    switch (outcome) {
    case FRAME_NO_NEXT:
        return field_frame_read_overflow(g, obj, false, before.animation_frame);
    case FRAME_ALREADY_DRAWN:
    case FRAME_NO_ROOM:
        return object_holds_frame(g, obj) ? before.overflow
                                          : field_frame_read_overflow(g, obj, true, before.animation_frame);
    case FRAME_PART_DRAWN:
        return copies_overflow(g, obj, object_records_state(g, obj).changed - 1u);
    case FRAME_DRAWN:
        return finished_overflow(g, obj);
    default:
        return before.overflow;
    }
}

/* ---- The frame wait ($C0:EC60) ------------------------------------------------------------ */

/* What the frame wait leaves in A: the idle count's high byte, as it counted
 * the frame, and 0, the vblank's flag it waited on. The battle takes A as
 * the wait before it leaves it (liveness has it read, $C1:0000). The held
 * continuation hands that value to the still-gated battle caller. */
uint16_t field_frame_wait_leaves_a(const GameState *g) { return (uint16_t)(field_idle_frames(g) & 0xFF00); }

/* ---- The layers' edges ($C0:7F9A) --------------------------------------------------------- */

FieldEdgesLeave field_edges_leave(const GameState *g)
{
    enum { COLUMNS = FIELD_TILE_MAP_COLUMNS - 1, ROWS = FIELD_TILE_MAP_ROWS - 1 };
    unsigned last = FIELD_LAYERS - 1;
    uint8_t x = field_layer_position_x(g, last), y = field_layer_position_y(g, last);
    uint16_t row = (uint16_t)((y - 1) & ROWS);
    uint16_t start = field_tile_map_index((uint16_t)((x + FIELD_TILE_MAP_HALF_COLUMNS) & COLUMNS), row);
    uint16_t base = field_layer_map_base(g, last);
    return (FieldEdgesLeave){ start, row, (uint16_t)(start + base) < start, field_add_overflows16(start, base) };
}

/* ---- The searchlight ($C0:2409) ------------------------------------------------------------- */

/* The overflow of its last corner's move: the last corner's y and its speed,
 * added in 16 bits. */
bool field_searchlight_overflow(const GameState *g)
{
    enum { LAST_CORNER = FIELD_SEARCHLIGHT_CORNERS_COUNT - 1 };
    uint16_t speed = field_searchlight_speeds_y(g, LAST_CORNER);
    return field_add_overflows16((uint16_t)(field_searchlight_corners_y(g, LAST_CORNER) - speed), speed);
}

/* ---- The step event, before a chest ($C0:0D45-$C0:16DB) --------------------------------------- */

/* A's high byte as the step event's code leaves it: a pressed switch's, the
 * high byte of its last redraw entry, as the entries are worked out in 16
 * bits; otherwise the remembering's, the list's entry where it stopped. */
uint8_t field_step_event_high_a(const GameState *g, uint16_t step, bool pressed)
{
    if (pressed)
        return (uint8_t)(field_switch_entries(g, field_switch_entry_count(g) - 1u) >> 8);
    return (uint8_t)(field_step_kept_at(g, step) >> 8);
}

/* ---- The chest ($C0:16EE) --------------------------------------------------------------------- */

/* The overflow of the last sum an opened chest's entries were worked out
 * with: the last entry's place in BG1's tile map plus the layer's base. */
bool field_chest_redraw_overflow(const GameState *g)
{
    uint16_t base = field_layer_map_base(g, 0);
    uint16_t last = field_chest_entries(g, FIELD_CHEST_ENTRIES_COUNT - 1);
    return field_add_overflows16((uint16_t)(last - base), base);
}

/* ---- The switches ($C0:0D45-$C0:16DB) ----------------------------------------------------- */

/* The overflow of the last sum the redraw's entries were worked out with:
 * the last entry's place in BG1's tile map plus the layer's base. */
bool field_switch_redraw_overflow(const GameState *g)
{
    uint16_t base = field_layer_map_base(g, 0);
    uint16_t last = field_switch_entries(g, field_switch_entry_count(g) - 1u);
    return field_add_overflows16((uint16_t)(last - base), base);
}

/* A's high byte at the switch's sound: the sound driver saves the caller's
 * registers on the stack as it starts ($C7:0147's PHA), and keeps that frame
 * there while it waits on the sound CPU, so shadow mode compares it as the
 * stack it is at those waits; liveness counts the PHA as a read. It is the
 * original's last word-wide value on its way round the switch's tiles: for
 * the one-by-two switch the upper tile moved on as a word, whose high byte
 * is the tile to its right; for the others, the index of the tile a row up
 * from where the original's walk round the tiles ended its last row (the
 * two-by-two's other column, the two-by-three's own). */
enum { MAP_ROW_STEP = 0x100 };
uint8_t field_switch_sound_high_a(const GameState *g, SwitchPress p, uint16_t step)
{
    if (p.shape == SWITCH_ONE_BY_TWO)
        return field_map_byte(g, (uint16_t)(field_switch_upper_tile(step) + 1));
    int along = p.shape == SWITCH_TWO_BY_TWO ? (p.step_column == 0 ? 1 : -1) : 0;
    uint16_t last = (uint16_t)(step + along - (field_switch_rows(p.shape) - 1u) * MAP_ROW_STEP);
    return (uint8_t)(last >> 8);
}

/* ---- A tile copy (field/layers.h: $C0:AF4E) ------------------------------------------------ */

/* The copy takes the tables in the order of the bits its description names
 * them by, whole tables first and the properties' bits last: the last part
 * it made is its highest bit. */
enum { NO_PART = -1, TILE_COPY_PARTS = 8 };

static int last_part(uint8_t maps)
{
    for (int part = TILE_COPY_PARTS - 1; part >= 0; part--)
        if (maps & 1u << part)
            return part;
    return NO_PART;
}

/* Where the original keeps each whole table: their rows interleaved, a row
 * of each every 256 bytes. */
static uint16_t map_table_address(MapTable table)
{
    static const uint16_t address[MAP_TABLES] = { 0x3000, 0x3040, 0x3080, 0x30C0, 0x7000, 0x7040 };
    return address[table];
}

static uint16_t last_row_start(TileRectangle r) { return (uint16_t)((r.to.row + r.height - 1) << 8 | r.to.column); }

/* Y: a whole table's copy leaves it past the last row's last tile, where the
 * original keeps the table; a bit's copy on that tile, counted from the
 * map's first. */
uint16_t field_tile_copy_y(const GameState *g, uint16_t before)
{
    int part = last_part(field_tile_copy_maps(g));
    if (part == NO_PART)
        return before;
    TileRectangle r = field_described_tile_copy(g);
    if (part >= MAP_TABLES)
        return (uint16_t)(last_row_start(r) + r.width - 1);
    return (uint16_t)(map_table_address((MapTable)part) + last_row_start(r) + r.width);
}

/* The overflow: a whole table's copy steps to its last row with a sum that
 * leaves the signed range where the row starts from $8000 on (the attribute
 * tables' lower rows), or with one row, found where it goes; a bit's copy
 * stays in the map's first 16 KiB, and leaves it clear. */
bool field_tile_copy_overflow(const GameState *g, bool before)
{
    int part = last_part(field_tile_copy_maps(g));
    if (part == NO_PART)
        return before;
    if (part >= MAP_TABLES)
        return false;
    TileRectangle r = field_described_tile_copy(g);
    uint16_t base = map_table_address((MapTable)part);
    if (r.height == 1)
        return field_add_overflows16((uint16_t)(r.to.row << 8 | r.to.column), base);
    enum { ROW = 0x100 };
    return field_add_overflows16((uint16_t)(base + last_row_start(r) - ROW), ROW);
}

/* ---- The Start assignment ($C0:1985) ------------------------------------------------------- */

/* The story's progress less the progress its menu waits for ($C0:1993). Its
 * readers: the menus' entry keeps the flags it is entered with ($C2:8013's
 * PHP), and the main loop's next frame reaches the sound driver's saved flags
 * (tools/v_readers.py --after C0:1985). */
bool field_story_test_overflow(const GameState *g)
{
    return field_subtract_overflows8(story_progress(g), FIELD_START_MENU_FROM_STORY);
}
