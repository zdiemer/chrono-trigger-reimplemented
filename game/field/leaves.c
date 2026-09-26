/* What the field's routines leave the original's code and shadow mode, worked
 * out beside the wrappers that hand it on (field/scratch.h): the entries of
 * field/motion.c and field/animation.c, and event's moves, which run those
 * routines as C where the original called them and hand on the Y and V the
 * routines left (event/leaves.h). Nothing here is the game's: each function
 * derives a register, a flag or a cell from what the routine was given and
 * the state it left, and the logic files call none of them but through
 * ORIGINAL_EFFECT. */
#include "field/field.h"
#include "field/field_data.h"
#include "field/layers.h"
#include "field/parts.h"
#include "field/party.h"
#include "field/object.h"
#include "field/scratch.h"
#include "field/switches.h"
#include "vars.h"

/* ---- The direction routine ($C0:ABA2) ------------------------------------------------------ */

/* It works from two tiles its caller leaves in cells, and leaves the
 * direction in another (a word, its high byte cleared), beside its working
 * cells: the angle table index's low part and the two distances. */
void field_leave_aim(GameState *g, Aim aim)
{
    set_field_own_tile_x(g, aim.from.x);
    set_field_own_tile_y(g, aim.from.y);
    set_field_target_tile_x(g, aim.to.x);
    set_field_target_tile_y(g, aim.to.y);
}

void field_leave_direction(GameState *g, Aim aim, Direction d)
{
    Quarter q = field_quarter_between(aim.from, aim.to);
    unsigned scale = field_angle_scale(q);
    uint16_t across = (uint16_t)((q.across >> scale) & FIELD_DISTANCE_BITS);
    set_field_direction_work(g, (uint32_t)across | (uint32_t)q.across << 16 | (uint32_t)q.along << 24);
    set_field_direction(g, d);
}

/* Its overflow: that of the angle's sum with its quarter. */
bool field_direction_overflow(Aim aim, Direction d)
{
    Quarter q = field_quarter_between(aim.from, aim.to);
    return field_add_overflows8((uint8_t)(d - q.quarter), q.quarter);
}

/* ---- The check for objects in the way ($C0:5B95) --------------------------------------------- */

/* It keeps its caller's Y while it looks. */
void field_leave_in_the_way_check(GameState *g, FieldObject obj, uint16_t callers_y)
{
    if (field_looks_for_objects(g, obj))
        original_cell16(g, 0xC7, callers_y);
}

/* The object a move approaches, or the check found (or last looked at), as
 * the original keeps it: its offset, a word. */
void field_leave_target_object(GameState *g, FieldObject obj) { set_field_target_object(g, field_object_offset(obj)); }

/* ---- Starting a step ($C0:ACFD) ------------------------------------------------------------- */

/* It leaves Y the object where it looked ahead, and its caller's Y's low byte
 * where not; and the overflow of its last sum: the vertical position it last
 * looked ahead at, or where its last direction gave it no step, the quarter
 * turn to that direction's horizontal axis. */
uint16_t field_step_start_y(const GameState *g, StepStart r, uint16_t callers_y)
{
    return r.outcome != START_STILL ? field_current_object(g) : (uint16_t)(callers_y & 0x00FF);
}

bool field_step_start_overflow(const GameState *g, FieldObject obj, StepStart r)
{
    bool no_step = r.outcome == START_STILL || r.outcome == START_TURNED_STILL;
    if (no_step)
        return field_add_overflows8(r.direction, FIELD_QUARTER_TURN);
    /* The vertical step it last looked ahead with: the one it set, or where
     * it stayed blocked, its last direction's. */
    int8_t looked_ahead_y = r.outcome == START_BLOCKED ? field_velocity_for(g, obj, r.direction).y
                                                       : object_velocity(g, obj).y;
    return field_add_overflows16(object_position(g, obj).y, (uint16_t)(int16_t)looked_ahead_y);
}

/* ---- Jumps and animation ------------------------------------------------------------------- */

/* A jump ($C0:4D06, event's) leaves the overflow of lowering the vertical
 * velocity by its rise, its steps times its height halved: from the steps
 * and the height the jump left the object. */
bool field_jump_overflow(const GameState *g, FieldObject obj)
{
    uint16_t lift = (uint16_t)(0 - ((field_object_steps_left(g, obj) * field_object_acceleration(g, obj)) >> 1));
    uint16_t velocity = field_object_velocity_y(g, obj);
    return field_add_overflows16((uint16_t)(velocity - lift), lift);
}

/* Animating with its movement ($C0:5614) leaves the overflow of the speed's
 * sum, where it looked at the speed. */
static uint8_t magnitude(int8_t step) { return step < 0 ? (uint8_t)-step : (uint8_t)step; }

bool field_walk_overflow(const GameState *g, FieldObject obj)
{
    Velocity v = object_velocity(g, obj);
    return field_add_overflows8(magnitude(v.y), magnitude(v.x));
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

/* ---- The switches ($C0:0D45-$C0:16DB) ----------------------------------------------------- */

/* The overflow of the last sum the redraw's entries were worked out with:
 * the last entry's place in BG1's tile map plus the layer's base. */
bool field_switch_redraw_overflow(const GameState *g)
{
    uint16_t base = field_layer_map_base(g, 0);
    uint16_t last = field_switch_entries(g, field_switch_entry_count(g) - 1u);
    return field_add_overflows16((uint16_t)(last - base), base);
}

/* A's high byte at the switch's sound: the sound driver saves A on the stack
 * as it starts ($C7:0147's PHA) and takes it back, so the byte is left in the
 * stack below the caller's frame, which shadow mode compares. It is the
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

/* ---- The screen test (field/object.h, tile_well_on_screen) ------------------------------- */

/* Where the tile is well on screen, the overflow of its last subtraction:
 * the tile's row less the screen's top, in the objects' tiles. */
bool field_screen_test_overflow(const GameState *g, MapTile tile)
{
    return field_subtract_overflows8(tile.y, (uint8_t)(field_screen_top(g) >> 1));
}

/* ---- The party's steps (field/party.h: $C0:9E29, $C0:A26B, $C0:A2CE) --------------------- */

/* The gait's lookup adds 33 to the complement of each axis's speed that goes
 * backward, the horizontal's first: its overflow is the last of those sums,
 * where it made one. */
enum { BACKWARD_SPEED_BIAS = 0x21 };

static bool gait_lookup_overflow(Velocity walk, bool before)
{
    if (walk.y < 0)
        return field_add_overflows8((uint8_t)~walk.y, BACKWARD_SPEED_BIAS);
    if (walk.x < 0)
        return field_add_overflows8((uint8_t)~walk.x, BACKWARD_SPEED_BIAS);
    return before;
}

/* The leader's: the gait's lookup, but on a climb, which looks nothing up. */
bool field_leader_step_overflow(const GameState *g, bool before)
{
    if (ground_kind(leader_ground(g)) == GROUND_CLIMB)
        return before;
    return gait_lookup_overflow(field_pad_walk(g), before);
}

/* A member's: the distance to the one ahead of it, which it measures before
 * it moves on, and then the lookup of the gait its footprint shows. Waiting
 * on a climb it compares the leader's pace with the pad's where both went up
 * or down; on a moving floor, the last sum is the vertical walk against the
 * floor's pull, or the lookup's of that where it goes up. */
bool field_member_step_overflow(const GameState *g, PartyPlace place, bool moved_on)
{
    uint8_t at = field_trail_position(g, place), ahead = field_trail_position(g, place - 1);
    uint8_t measured_from = moved_on ? (uint8_t)(at - 1) % FIELD_TRAIL_GROUND_COUNT : at;
    bool v = field_subtract_overflows8(ahead, measured_from);
    Footprint f = field_footprint(g, at);
    GroundKind kind = ground_kind(f.ground);
    if (moved_on)
        return kind == GROUND_CLIMB ? v : gait_lookup_overflow(f.walk, v);
    if (kind == GROUND_PLAIN)
        return v;
    if (kind == GROUND_CLIMB)
        return f.walk.y != 0 && f.movement.y != 0 ? field_subtract_overflows8((uint8_t)f.movement.y, (uint8_t)f.walk.y)
                                                  : v;
    uint8_t against_pull = (uint8_t)-field_floor_pull(f.movement.y);
    uint8_t walk = (uint8_t)(against_pull + (uint8_t)f.walk.y);
    if ((int8_t)walk < 0)
        return field_add_overflows8((uint8_t)~walk, BACKWARD_SPEED_BIAS);
    return field_add_overflows8(against_pull, (uint8_t)f.walk.y);
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

/* A whole table's copy leaves its width less one (the block move's count),
 * the rows it counted down, the columns it started each row at offset by
 * where the table is, and where that is; a bit's copy the last bit it took
 * and the same, the columns within the row. */
void field_leave_table_copied(GameState *g, TileRectangle r, MapTable table)
{
    uint16_t base = map_table_address(table);
    original_cell16(g, 0xDB, (uint16_t)(r.width - 1));
    original_cell16(g, 0xDD, 0);
    original_cell16(g, 0xDF, (uint8_t)(r.from.column + base));
    original_cell16(g, 0xE1, (uint8_t)(r.to.column + base));
    set_field_own_tile_copy_table(g, base);
}

void field_leave_bit_copied(GameState *g, TileRectangle r, uint8_t bit)
{
    MapPlace last = { (uint8_t)(r.from.row + r.height - 1), (uint8_t)(r.from.column + r.width - 1) };
    original_cell8(g, 0xD9, (uint8_t)(field_tile_properties(g, last.row, last.column) & bit));
    original_cell16(g, 0xDB, (uint16_t)(r.width - 1));
    original_cell16(g, 0xDD, 0);
    original_cell16(g, 0xDF, r.from.column);
    original_cell16(g, 0xE1, r.to.column);
    original_cell16(g, 0xE3, 0);
}

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

/* ---- The party's start on the map (field/walk.c: $C0:9DC3) ----------------------------------- */

static TileShape start_shape(const GameState *g) { return tile_shape(map_tile_attributes(g, field_start_place(g))); }

/* Whether the tile it starts on is a slope, whose look counts its shift in
 * Y. */
bool field_start_on_a_slope(const GameState *g)
{
    return tile_shape_is_a_slope(start_shape(g));
}

/* On a slope, the overflow of taking the point's height from the line's
 * start and its place along. */
bool field_start_overflow(const GameState *g, bool before)
{
    if (!field_start_on_a_slope(g))
        return before;
    FieldSlopeSum s =
        field_slope_sum(g, tile_shape_slope(start_shape(g)), FIELD_START_FINE_X, FIELD_START_FINE_Y);
    return field_subtract_overflows8((uint8_t)(s.sum + s.height), s.height);
}
