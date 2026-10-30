/* Battle: drawing the combatants into the sprite buffer, once a frame.
 *
 * The sprites put in the buffer first show in front of the ones after them,
 * so the combatants are drawn nearest first: the pass lists the combatants on
 * the battlefield by how far down the screen they stand, and a combatant that
 * must show above everything (a flying enemy) a second time at the top of the
 * list. For each entry it draws, in this order:
 *
 * - what shows in front of the combatant: its damage numbers while numbers
 *   show; otherwise, when no effect plays, its animated marker; and while
 *   effects play, the effects its records put in front of it
 *   (battle/effects.h);
 * - the combatant itself: its frame's parts, sprites at offsets from where it
 *   stands (moved by its sprite's offset), in its palette, their tiles copied
 *   from its sprite entries;
 * - what shows behind it: the effects its records put behind it;
 * - its shadow, a sprite at its feet;
 * - while effects play, its afterimages: its frame drawn again at the four
 *   places its trail recorded (battle/object_motion.c), one frame in so many.
 *
 * A combatant with a second entry is drawn at whichever of its entries comes
 * first, and only its shadow and afterimages at the other. A sprite off the
 * left or right of the screen gets its X's ninth bit in the buffer's high
 * table; the battle keeps tables of which high-table byte and bit go with
 * each sprite.
 *
 * Each part of the drawing is a routine of the original's, which the
 * original's pass calls with the combatant and the next free sprite in cells
 * of the objects' page; the port's pass runs them as C, each in its routine's
 * own time (native_time_as), and they stay entries of their own. */
#include "battle/effects.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/sprite_data.h"
#include "charge.h"
#include "battle/program.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The sprite buffer ---------------------------------------------------------------- */

/* The buffer's first four sprites are the menu's cursors; the combatants'
 * start after them. A place in the buffer is a sprite's number; the battle
 * keeps the next free one as a byte offset, four a sprite. */
enum { SPRITE_BYTES = 4, FIRST_FREE_SPRITE = 4, SPRITES = 128 };

/* Push the sprite off the side of the screen, or onto its right-hand part:
 * the ninth bit of its X. */
static void set_x_ninth_bit(GameState *g, unsigned sprite)
{
    unsigned at = sprite * SPRITE_BYTES;
    unsigned byte = battle_oam_high_offsets(g, at);
    set_oam_high_table_byte(g, byte, (uint8_t)(oam_high_table_byte(g, byte) | battle_oam_x_high_bits(g, at)));
}

/* Whether a sprite at a point (screen coordinates, a word each) needs the
 * ninth bit: one past the left or right edge does; one above the top or
 * below the bottom gets it too, to take it off the screen where the hardware
 * would wrap it round, unless it is in the 16 lines just above the top, where
 * the hardware shows it coming in. */
enum { JUST_ABOVE_THE_TOP = 0xF0 };

static bool needs_x_ninth_bit(uint16_t x, uint16_t y)
{
    if (y >> 8)
        return (uint8_t)y < JUST_ABOVE_THE_TOP;
    return (x >> 8) != 0;
}

static void place_sprite(GameState *g, unsigned sprite, uint16_t x, uint16_t y)
{
    set_oam_buffer_x(g, sprite, (uint8_t)x);
    set_oam_buffer_y(g, sprite, (uint8_t)y);
    if (needs_x_ninth_bit(x, y))
        set_x_ninth_bit(g, sprite);
}

static void put_sprite(GameState *g, unsigned sprite, uint8_t x, uint8_t y, uint8_t tile, uint8_t attributes)
{
    set_oam_buffer_x(g, sprite, x);
    set_oam_buffer_y(g, sprite, y);
    set_oam_buffer_tile(g, sprite, tile);
    set_oam_buffer_attributes(g, sprite, attributes);
}

/* ---- What the original leaves in its cells -------------------------------------------- */

/* The original keeps its working values in cells of the objects' page
 * ($2E-$44) and of the battle's ($A1, $C6-$D1); the ports leave there what
 * it leaves, for shadow mode. `cell` is the original's direct-page address. */
static void leave_draw_cell16(GameState *g, uint8_t cell, uint16_t v)
{
    set_battle_draw_scratch(g, (unsigned)(cell - 0x2E), (uint8_t)v);
    set_battle_draw_scratch(g, (unsigned)(cell - 0x2E + 1), (uint8_t)(v >> 8));
}
static void leave_effect_cell16(GameState *g, uint8_t cell, uint16_t v)
{
    set_battle_effect_cells(g, (unsigned)(cell - 0xC6), (uint8_t)v);
    set_battle_effect_cells(g, (unsigned)(cell - 0xC6 + 1), (uint8_t)(v >> 8));
}

/* ---- A combatant's frame -------------------------------------------------------------- */

/* A combatant's frame is so many parts (by its graphics kind; a kind of 0
 * parts draws 256), each a sprite at an offset from where the frame is. The
 * part offsets of each combatant's frame start four entries apart and run on
 * over the next combatants' for a frame of more parts. */
enum { PART_ENTRIES_PER_COMBATANT = 4 };

/* A part keeps its frame's flips and its tile's ninth bit; the combatant's
 * palette and priority are the rest of its attributes. */
enum { PART_KEEPS = 0xC1 };

typedef struct {
    uint16_t x, y;
} FramePoint;

static unsigned frame_parts(const GameState *g, unsigned slot)
{
    uint8_t parts = battle_frame_parts(g, battle_object_kind(g, slot));
    return parts ? parts : 256;
}

static uint16_t first_part_offset(unsigned slot) { return (uint16_t)(slot * PART_ENTRIES_PER_COMBATANT * SPRITE_BYTES); }

/* The original leaves the frame's point, its last part's, the attributes and
 * the offset in its cells, and the part it came to. */
static void leave_parts_cells(GameState *g, unsigned slot, FramePoint at, FramePoint offset, uint8_t bits, unsigned parts)
{
    unsigned last_part = slot * PART_ENTRIES_PER_COMBATANT + parts - 1;
    FramePoint last = { (uint16_t)(battle_part_offsets_x(g, last_part) + at.x), (uint16_t)(battle_part_offsets_y(g, last_part) + at.y) };
    leave_draw_cell16(g, 0x30, at.y);
    leave_draw_cell16(g, 0x32, last.x);
    leave_draw_cell16(g, 0x34, 0);
    leave_draw_cell16(g, 0x36, (uint16_t)(bits << 8));
    leave_draw_cell16(g, 0x38, last.y);
    leave_draw_cell16(g, 0x3A, offset.x);
    leave_draw_cell16(g, 0x3C, offset.y);
    leave_draw_cell16(g, 0x42, (uint16_t)(first_part_offset(slot) + parts * SPRITE_BYTES));
}

// $CF:EAC5 (from $CF:EB1B), $CF:E99C (from $CF:EA11)
/* Draw the combatant's frame from `sprite` on, with its point at `at` moved
 * by `offset`. Returns the next free sprite. */
static unsigned draw_frame(GameState *g, unsigned slot, unsigned sprite, FramePoint at, FramePoint offset)
{
    at = (FramePoint){ (uint16_t)(at.x + offset.x), (uint16_t)(at.y + offset.y) };
    uint8_t bits = (uint8_t)(battle_object_palette(g, slot) | battle_object_attribute_bits(g, slot));
    unsigned parts = frame_parts(g, slot), part = slot * PART_ENTRIES_PER_COMBATANT;
    for (unsigned n = 0; n < parts; n++, sprite++, part++) {
        set_oam_buffer_attributes(g, sprite, (uint8_t)((oam_buffer_attributes(g, sprite) & PART_KEEPS) | bits));
        place_sprite(g, sprite, (uint16_t)(battle_part_offsets_x(g, part) + at.x), (uint16_t)(battle_part_offsets_y(g, part) + at.y));
    }
    /* With the party off the grid, its marking pass includes slots beyond
     * the combatants. Object 23's Y overlaps this frame's X, so that value
     * is also input to the grid (battlefield.c), including in native builds. */
    leave_draw_cell16(g, 0x2E, at.x);
    ORIGINAL_EFFECT(leave_parts_cells(g, slot, at, offset, bits, parts));
    return sprite;
}

static bool hidden(const GameState *g, unsigned slot) { return battle_object_hidden(g, slot) != 0; }

/* A signed byte as the word the frame's point is moved by. */
static uint16_t widened(int8_t v) { return (uint16_t)(int16_t)v; }

// $CF:EAC5
/* Draw the combatant where it stands, moved by its sprite's offset, unless
 * it is hidden. Returns the next free sprite. */
static unsigned draw_combatant(GameState *g, unsigned slot, unsigned sprite)
{
    ORIGINAL_EFFECT(leave_draw_cell16(g, 0x42, first_part_offset(slot)));
    if (hidden(g, slot))
        return sprite;
    return draw_frame(g, slot, sprite, (FramePoint){ combatant_x(g, slot), combatant_y(g, slot) },
                      (FramePoint){ widened(combatant_sprite_offset_x(g, slot)), widened(combatant_sprite_offset_y(g, slot)) });
}

/* ---- Tiles ------------------------------------------------------------------------------ */

/* The tiles of a combatant's frame come from its sprite entries, groups of
 * four sprites, copied into the sprites its parts are drawn in. Each
 * combatant's entries start four apart (the table at $CC:F84D says where each
 * object's are). A count of 0, or of 8 or more, copies eight groups. */
enum { ENTRIES_PER_COMBATANT = 4, SPRITES_PER_TILE_GROUP = 4, MOST_TILE_GROUPS = 8 };

static void leave_tile_groups(GameState *g, uint16_t count)
{
    set_battle_draw_tile_groups(g, count == 0 || count >= MOST_TILE_GROUPS ? (uint16_t)(count - (MOST_TILE_GROUPS - 1)) : 0);
}

// $CF:EB94
/* Copy `count` groups' tiles from the sprite entries from `from` on into the
 * sprites from `to` on. */
static void copy_tiles(GameState *g, unsigned from, unsigned to, uint16_t count)
{
    unsigned groups = count == 0 || count >= MOST_TILE_GROUPS ? MOST_TILE_GROUPS : count;
    for (unsigned n = 0; n < groups * SPRITES_PER_TILE_GROUP; n++) {
        set_oam_buffer_tile(g, to + n, battle_object_sprite_entries_tile(g, from + n));
        set_oam_buffer_attributes(g, to + n, battle_object_sprite_entries_attributes(g, from + n));
    }
    ORIGINAL_EFFECT(leave_tile_groups(g, count));
}

/* The groups a frame of so many parts takes: a group for sixteen parts,
 * rounded to the nearest (a frame of fewer than 15 parts counts 0, so its
 * tiles copy eight groups). */
static uint16_t tile_groups_for(uint16_t parts) { return (uint16_t)((parts + 1) >> 4); }

static void copy_combatant_tiles(GameState *g, unsigned slot, unsigned sprite, uint16_t parts)
{
    copy_tiles(g, slot * ENTRIES_PER_COMBATANT, sprite, tile_groups_for(parts));
}

/* ---- Effects in front of a combatant and behind it ------------------------------------ */

/* A combatant's records (battle/effects.h), a row an effect. */
enum { RECORD_ROWS = BATTLE_EFFECTS };

/* An effect's sprite set is so many sprites (its count's low six bits; none
 * draws 256), each an X and a Y from the effect's point, a tile and
 * attributes. The effect's own attribute bits are its sprites' priority. */
enum { SET_OF_EFFECT = 0x3F, EFFECT_SPRITE_KEEPS = 0xCF };

static void leave_effect_point_cells(GameState *g, uint8_t bits, uint16_t x, uint16_t y)
{
    set_battle_effect_sprite_attributes(g, bits);
    leave_effect_cell16(g, 0xCC, x);
    leave_effect_cell16(g, 0xCE, y);
}

/* The original leaves where in the set it came to, its count run out, and
 * the last sprite's point. */
static void leave_effect_sprite_cells(GameState *g, uint16_t from, unsigned sprites, uint16_t x, uint16_t y)
{
    uint16_t last = (uint16_t)(from + (sprites - 1) * SPRITE_BYTES);
    leave_effect_cell16(g, 0xC6, (uint16_t)(last + SPRITE_BYTES));
    leave_effect_cell16(g, 0xC8, 0);
    leave_effect_cell16(g, 0xCA, (uint16_t)(battle_effect_sprite_data(g, last) + x));
    leave_effect_cell16(g, 0xD0, (uint16_t)(battle_effect_sprite_data(g, last + 1u) + y));
}

// $CF:EEEB
/* Draw the effect the record names, at its anchor moved by its frame's
 * offset, while it has a frame showing. Returns the next free sprite. */
static unsigned draw_effect(GameState *g, unsigned record, unsigned sprite)
{
    unsigned effect = battle_object_effect_slots(g, record) & BATTLE_RECORD_EFFECT;
    uint8_t bits = battle_effect_attribute_bits(g, effect);
    uint16_t x = (uint16_t)(battle_anchor_x(g, effect) + battle_anchor_offset_x(g, effect));
    uint16_t y = (uint16_t)(battle_anchor_y(g, effect) + battle_anchor_offset_y(g, effect));
    ORIGINAL_EFFECT(leave_effect_point_cells(g, bits, x, y));
    if (battle_effect_frame_time(g, effect) == 0)
        return sprite;
    uint8_t set = battle_effect_sprite_set(g, effect) & SET_OF_EFFECT;
    unsigned counted = battle_bitmap_counts(g, set) & SET_OF_EFFECT;
    unsigned sprites = counted ? counted : 256;
    uint16_t first = battle_effect_sprite_set_start(g, set), from = first;
    for (unsigned n = 0; n < sprites; n++, from = (uint16_t)(from + SPRITE_BYTES)) {
        set_oam_buffer_tile(g, sprite + n, battle_effect_sprite_data(g, from + 2u));
        set_oam_buffer_attributes(g, sprite + n, (uint8_t)((battle_effect_sprite_data(g, from + 3u) & EFFECT_SPRITE_KEEPS) | bits));
        place_sprite(g, sprite + n, (uint16_t)(battle_effect_sprite_data(g, from) + x), (uint16_t)(battle_effect_sprite_data(g, from + 1u) + y));
    }
    ORIGINAL_EFFECT(leave_effect_sprite_cells(g, first, sprites, x, y));
    return sprite + counted;
}

/* Whether the record puts its effect on this side of the combatant, and the
 * effect shows: the record with its side bit cleared names it. */
static bool effect_on_side(const GameState *g, uint8_t record, bool in_front)
{
    if (record == 0 || ((record & BATTLE_RECORD_IN_FRONT) != 0) != in_front)
        return false;
    return battle_effect_shown(g, (uint8_t)(record & ~(in_front ? BATTLE_RECORD_IN_FRONT : BATTLE_RECORD_BEHIND))) != 0;
}

/* ---- Numbers, marker and shadow ------------------------------------------------------ */

enum { NUMBER_DIGITS = 4 };

// $CF:EE9F
/* The combatant's damage numbers, when it has them: four sprites prepared
 * with their high-table byte, which fills the byte of the four they go in.
 * Returns the next free sprite. */
static unsigned draw_numbers(GameState *g, unsigned slot, unsigned sprite)
{
    if (!battle_prepared_sprites_on(g, slot))
        return sprite;
    set_oam_high_table_byte(g, battle_oam_high_offsets(g, sprite * SPRITE_BYTES), battle_number_sprite_high(g, slot));
    for (unsigned d = 0; d < NUMBER_DIGITS; d++) {
        unsigned digit = slot * NUMBER_DIGITS + d;
        put_sprite(g, sprite + d, battle_number_sprites_x(g, digit), battle_number_sprites_y(g, digit),
                   battle_number_sprites_tile(g, digit), battle_number_sprites_attributes(g, digit));
    }
    ORIGINAL_EFFECT(set_battle_effect_cells(g, 0xC8 - 0xC6, 0));
    return sprite + NUMBER_DIGITS;
}

/* A marker is an animated sprite at the combatant, four frames, each shown
 * for the marker's period. */
enum { MARKER_FRAMES = 4 };

static void leave_marker_cells(GameState *g, uint8_t x, uint8_t y)
{
    set_battle_effect_cells(g, 0xCC - 0xC6, x);
    set_battle_effect_cells(g, 0xCD - 0xC6, y);
}

static uint8_t next_marker_frame(GameState *g, unsigned slot)
{
    uint8_t left = (uint8_t)(battle_marker_countdown(g, slot) - 1);
    set_battle_marker_countdown(g, slot, left ? left : battle_marker_period(g, slot));
    uint8_t frame = battle_marker_frame(g, slot);
    if (left == 0) {
        frame = (uint8_t)(frame + 1) == MARKER_FRAMES ? 0 : (uint8_t)(frame + 1);
        set_battle_marker_frame(g, slot, frame);
    }
    return frame;
}

// $CF:EE2C
/* The combatant's marker, when it has one. Returns the next free sprite. */
static unsigned draw_marker(GameState *g, unsigned slot, unsigned sprite)
{
    if (!battle_marker_on(g, slot))
        return sprite;
    uint8_t x = combatant_x(g, slot), y = combatant_y(g, slot);
    ORIGINAL_EFFECT(leave_marker_cells(g, x, y));
    uint8_t frame = next_marker_frame(g, slot);
    MarkerSprite m = battle_marker_sprite(g, (uint8_t)(battle_marker_kind(g, slot) + frame * SPRITE_BYTES));
    put_sprite(g, sprite, (uint8_t)(x + m.x), (uint8_t)(y + m.y), m.tile, (uint8_t)(m.attributes | battle_sprite_attributes(g)));
    return sprite + 1;
}

/* A shadow is the shadow tile 8 pixels left of and 4 above where the
 * combatant stands, in the shadows' priority, and takes four sprites' room. */
enum { SHADOW_TILE = 0x02, SHADOW_LEFT = 8, SHADOW_UP = 4, SHADOW_SPRITES = 4 };

// $CF:EA8A
/* The combatant's shadow, when it has one and isn't hidden. Returns the next
 * free sprite. */
static unsigned draw_shadow(GameState *g, unsigned slot, unsigned sprite)
{
    if (!battle_object_shadow(g, slot) || hidden(g, slot))
        return sprite;
    put_sprite(g, sprite, (uint8_t)(combatant_x(g, slot) - SHADOW_LEFT), (uint8_t)(combatant_y(g, slot) - SHADOW_UP),
               SHADOW_TILE, (uint8_t)(battle_sprite_attributes(g) | battle_cursor_priority(g)));
    return sprite + SHADOW_SPRITES;
}

/* ---- Afterimages ------------------------------------------------------------------------ */

/* A trail's point: its coordinates' low bytes are where the afterimage is
 * drawn; of the high bytes only the sign counts, so a point past the right
 * or bottom edge is drawn back on the screen. */
typedef struct {
    uint8_t x, y, x_high, y_high;
} TrailPoint;

enum { TRAIL_POINTS = 4 };

static TrailPoint trail_point(const GameState *g, unsigned point, unsigned slot)
{
    return (TrailPoint){ battle_trail_low_x_byte(g, point, slot), battle_trail_low_y_byte(g, point, slot),
                         battle_trail_high_x_byte(g, point, slot), battle_trail_high_y_byte(g, point, slot) };
}

static uint16_t sign_of(uint8_t high) { return (high & 0x80) ? 0xFF00 : 0; }

static void leave_trail_point_cells(GameState *g, TrailPoint p)
{
    leave_draw_cell16(g, 0x32, (uint16_t)(p.x | p.y << 8));
    leave_draw_cell16(g, 0x34, (uint16_t)(p.x_high | p.y_high << 8));
}

// $CF:E99C
/* One afterimage: the combatant's frame at a point of its trail, unless it
 * is hidden. Returns the next free sprite. */
static unsigned draw_afterimage_frame(GameState *g, unsigned slot, unsigned sprite, TrailPoint p)
{
    ORIGINAL_EFFECT(leave_trail_point_cells(g, p));
    ORIGINAL_EFFECT(leave_draw_cell16(g, 0x42, first_part_offset(slot)));
    if (hidden(g, slot))
        return sprite;
    return draw_frame(g, slot, sprite, (FramePoint){ p.x, p.y }, (FramePoint){ sign_of(p.x_high), sign_of(p.y_high) });
}

/* Whether the afterimages show this frame: one frame in so many. */
static bool afterimages_due(GameState *g, unsigned slot)
{
    uint8_t left = (uint8_t)(battle_afterimage_countdown(g, slot) - 1);
    set_battle_afterimage_countdown(g, slot, left ? left : battle_afterimage_period(g, slot));
    return left == 0;
}

/* ---- The draw list ---------------------------------------------------------------------- */

/* An entry of the draw list: the combatant, at its own entry or its second,
 * topmost one, sorted by its key: how far down the screen the combatant
 * stands, or the top key. The original keeps the list as the object, marked
 * at its own entry, ending with an end marker. */
enum { OWN_ENTRY = 0x40, END_OF_LIST = 0xFF, TOPMOST_KEY = 0xFE };

enum { MOST_ENTRIES = 2 * BATTLE_COMBATANTS };

typedef struct {
    uint8_t slot;
    bool own;
} DrawEntry;

typedef struct {
    uint8_t key[MOST_ENTRIES];
    DrawEntry entry[MOST_ENTRIES];
    unsigned count;
    bool second_entry[BATTLE_COMBATANTS];
} DrawList;

static void list_add(DrawList *list, uint8_t key, unsigned slot, bool own)
{
    list->key[list->count] = key;
    list->entry[list->count] = (DrawEntry){ (uint8_t)slot, own };
    list->count++;
}

/* Sort the list, largest key first: the original's exchange sort, which
 * the order of equal keys depends on. */
static void sort_draw_list(DrawList *list)
{
    for (unsigned i = 0; i + 1 < list->count; i++)
        for (unsigned j = i + 1; j < list->count; j++)
            if (list->key[i] < list->key[j]) {
                uint8_t key = list->key[i];
                DrawEntry entry = list->entry[i];
                list->key[i] = list->key[j];
                list->entry[i] = list->entry[j];
                list->key[j] = key;
                list->entry[j] = entry;
            }
}

static void leave_draw_list(GameState *g, const DrawList *list)
{
    leave_draw_cell16(g, 0x30, (uint16_t)(list->count - 1));
    for (unsigned i = 0; i < list->count; i++) {
        set_battle_draw_list_depth(g, i, list->key[i]);
        set_battle_draw_list(g, i, (uint8_t)(battle_object_of(list->entry[i].slot) | (list->entry[i].own ? OWN_ENTRY : 0)));
    }
    set_battle_draw_list(g, list->count, END_OF_LIST);
}

/* A combatant's second entry, as the original marks it: none, one to come,
 * or drawn at its first entry. */
enum { NO_SECOND_ENTRY = 0x00, SECOND_ENTRY = 0x01, DRAWN_AT_FIRST_ENTRY = 0x80 };

static void leave_second_entry_mark(GameState *g, unsigned slot, uint8_t mark) { set_battle_object_extra_draw(g, slot, mark); }

// $CF:E83A (to $CF:E8A6)
/* List the combatants on the battlefield, those with a second entry twice,
 * and sort the list. */
static DrawList build_draw_list(GameState *g)
{
    DrawList list = { { 0 }, { { 0, false } }, 0, { false } };
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!battle_slot_present(g, slot))
            continue;
        list_add(&list, combatant_y(g, slot), slot, true);
        list.second_entry[slot] = battle_object_extra_draw_on(g, slot) != 0;
        ORIGINAL_EFFECT(leave_second_entry_mark(g, slot, list.second_entry[slot] ? SECOND_ENTRY : NO_SECOND_ENTRY));
        if (list.second_entry[slot])
            list_add(&list, TOPMOST_KEY, slot, false);
    }
    sort_draw_list(&list);
    ORIGINAL_EFFECT(leave_draw_list(g, &list));
    return list;
}

/* ---- The pass, each part in the time of the original's routine for it ------------------ */

enum {
    DRAW_IN_FRONT = 0xCFEDB8, DRAW_BEHIND = 0xCFEDF7, DRAW_COMBATANT = 0xCFEAC5, COPY_TILES = 0xCFEB94,
    DRAW_SHADOW = 0xCFEA8A, DRAW_AFTERIMAGES = 0xCFE92C, DRAW_AFTERIMAGE = 0xCFE99C, DRAW_EFFECT = 0xCFEEEB,
    DRAW_NUMBERS = 0xCFEE9F, DRAW_MARKER = 0xCFEE2C,
};

/* The effects the combatant's records put on one side of it. */
static unsigned draw_effects(GameState *g, unsigned slot, unsigned sprite, bool in_front)
{
    for (unsigned row = 0; row < RECORD_ROWS; row++) {
        unsigned record = battle_record_of(slot, row);
        if (effect_on_side(g, battle_object_effect_slots(g, record), in_front))
            TIMED_AS(g, DRAW_EFFECT, sprite = draw_effect(g, record, sprite));
    }
    return sprite;
}

// $CF:EDB8
/* In front of the combatant: its numbers while numbers show; its marker
 * while no effect plays, unless markers are held off; otherwise its effects
 * in front. */
static unsigned draw_in_front(GameState *g, unsigned slot, unsigned sprite)
{
    if (battle_number_step(g))
        TIMED_AS(g, DRAW_NUMBERS, sprite = draw_numbers(g, slot, sprite));
    else if (!battle_effects_running(g) && !battle_markers_held(g))
        TIMED_AS(g, DRAW_MARKER, sprite = draw_marker(g, slot, sprite));
    else
        sprite = draw_effects(g, slot, sprite, true);
    return sprite;
}

// $CF:EDF7
/* Behind it: while effects play and no numbers show, its effects behind. */
static unsigned draw_behind(GameState *g, unsigned slot, unsigned sprite)
{
    if (battle_effects_running(g) && !battle_number_step(g))
        sprite = draw_effects(g, slot, sprite, false);
    return sprite;
}

/* An afterimage copies as many tiles as the last combatant whose frame was
 * drawn had parts. */
static unsigned draw_afterimage(GameState *g, unsigned slot, unsigned sprite, TrailPoint p, uint16_t last_parts)
{
    TIMED_AS(g, COPY_TILES, copy_combatant_tiles(g, slot, sprite, last_parts));
    return draw_afterimage_frame(g, slot, sprite, p);
}

// $CF:E92C
/* The combatant's afterimages, when they show this frame: its frame at each
 * of its trail's four points. */
static unsigned draw_afterimages(GameState *g, unsigned slot, unsigned sprite, uint16_t last_parts)
{
    ORIGINAL_EFFECT(set_battle_afterimage_object(g, (uint16_t)battle_object_of(slot)));
    if (!afterimages_due(g, slot))
        return sprite;
    for (unsigned point = 0; point < TRAIL_POINTS; point++)
        TIMED_AS(g, DRAW_AFTERIMAGE, sprite = draw_afterimage(g, slot, sprite, trail_point(g, point, slot), last_parts));
    return sprite;
}

/* The combatant, with what shows in front of it and behind it. */
static unsigned draw_with_effects(GameState *g, unsigned slot, unsigned sprite, uint8_t parts)
{
    TIMED_AS(g, DRAW_IN_FRONT, sprite = draw_in_front(g, slot, sprite));
    TIMED_AS(g, COPY_TILES, copy_combatant_tiles(g, slot, sprite, parts));
    TIMED_AS(g, DRAW_COMBATANT, sprite = draw_combatant(g, slot, sprite));
    TIMED_AS(g, DRAW_BEHIND, sprite = draw_behind(g, slot, sprite));
    return sprite;
}

/* What the pass leaves in its cells: the entry it stopped at (the list's
 * end), the last combatant it came to, the parts of the last whose frame it
 * drew, and the next free sprite. */
static void leave_pass_cells(GameState *g, const DrawList *list, uint8_t last_parts, unsigned sprite)
{
    set_battle_draw_list_position(g, (uint16_t)list->count);
    set_battle_draw_object(g, (uint16_t)battle_object_of(list->entry[list->count - 1].slot));
    set_battle_draw_part_count(g, last_parts);
    set_battle_oam_next(g, (uint16_t)(sprite * SPRITE_BYTES));
}

// $CF:E83A
/* Draw every combatant on the battlefield (the file's header).
 *
 * An accepted divergence: where the sprites run past the buffer's 128, the
 * original writes on over the high table and what follows it, and there the
 * order of its writes shows (it writes each part's tile back as it sets the
 * part's attributes, and sets high bits between); the port's order is its
 * own. No battle draws more sprites than the buffer holds; random states do
 * (tools/fuzz_routines.toml, the pass's loose variant). */
static void draw_objects(GameState *g)
{
    DrawList list = build_draw_list(g);
    bool drawn_early[BATTLE_COMBATANTS] = { false };
    unsigned sprite = FIRST_FREE_SPRITE;
    uint8_t last_parts = 0;
    for (unsigned i = 0; i < list.count; i++) {
        DrawEntry e = list.entry[i];
        unsigned slot = e.slot;
        uint8_t parts = battle_object_part_count(g, battle_object_of(slot));
        bool shadow = true;
        if (!drawn_early[slot] && parts != 0) {
            last_parts = parts;
            sprite = draw_with_effects(g, slot, sprite, parts);
            drawn_early[slot] = list.second_entry[slot];
            shadow = !drawn_early[slot];
            if (drawn_early[slot])
                ORIGINAL_EFFECT(leave_second_entry_mark(g, slot, DRAWN_AT_FIRST_ENTRY));
        }
        if (shadow)
            TIMED_AS(g, DRAW_SHADOW, sprite = draw_shadow(g, slot, sprite));
        if (battle_effects_running(g) && e.own && battle_trail_on(g, slot))
            TIMED_AS(g, DRAW_AFTERIMAGES, sprite = draw_afterimages(g, slot, sprite, last_parts));
    }
    ORIGINAL_EFFECT(leave_pass_cells(g, &list, last_parts, sprite));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The pass is guarded on someone being on the battlefield: with the list
 * empty, the original's sort would run through the whole bank. The drawing
 * routines on their own draw the combatant the pass is drawing (the object
 * in its cell, or in X), from the next free sprite in its cell, at a
 * sprite's place in the buffer. */
static bool someone_on_battlefield(const GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        if (battle_slot_present(g, slot))
            return true;
    return false;
}

static bool combatant_object(uint16_t object)
{
    return object >= BATTLE_FIRST_COMBATANT_OBJECT && object < BATTLE_FIRST_COMBATANT_OBJECT + BATTLE_COMBATANTS;
}
static unsigned slot_of_object(uint16_t object) { return object - BATTLE_FIRST_COMBATANT_OBJECT; }

static bool at_a_sprite(const GameState *g)
{
    uint16_t next = battle_oam_next(g);
    return next % SPRITE_BYTES == 0 && next < SPRITES * SPRITE_BYTES;
}

static bool drawing_a_combatant(const GameState *g) { return combatant_object(battle_draw_object(g)) && at_a_sprite(g); }

/* Each guard looks at the state without taking the machine's time. */
static bool peek(Snes *s, bool (*test)(const GameState *))
{
    vars_peek(true);
    bool holds = test(state_of(s));
    vars_peek(false);
    return holds;
}

bool battle_draw_pass_ready(Snes *s) { return battle_sprite_bank_ready(s) && peek(s, someone_on_battlefield); }

bool battle_drawing_combatant(Snes *s) { return battle_sprite_page_ready(s) && peek(s, drawing_a_combatant); }

/* The frame's own routines set A before they use it, whatever the caller
 * left there. */
bool battle_drawing_combatant_frame(Snes *s) { return battle_sprite_page(s) && peek(s, drawing_a_combatant); }

bool battle_drawing_combatant_in_x(Snes *s)
{
    return battle_sprite_page_ready(s) && combatant_object(s->cpu.x) && peek(s, at_a_sprite);
}

static uint16_t object_in_cell(Snes *s)
{
    vars_peek(true);
    uint16_t object = battle_draw_object(state_of(s));
    vars_peek(false);
    return object;
}

bool battle_drawing_combatant_x_and_cell(Snes *s) { return battle_drawing_combatant(s) && s->cpu.x == object_in_cell(s); }

/* An effect's record in X. */
bool battle_drawing_effect_record(Snes *s)
{
    return battle_sprite_bank_ready(s) && s->cpu.x < BATTLE_OBJECT_EFFECT_SLOTS_COUNT && peek(s, at_a_sprite);
}

/* What the original's pass hands its routines in the objects' page's cells. */
static unsigned slot_being_drawn(Snes *s) { return slot_of_object(battle_draw_object(state_of(s))); }
static uint16_t parts_last_drawn(Snes *s) { return battle_draw_part_count(state_of(s)); }
static unsigned next_free(Snes *s) { return battle_oam_next(state_of(s)) / SPRITE_BYTES; }

/* Each hands the next free sprite back in its cell, and leaves the direct
 * page on the objects' page, where the pass runs, and A's high byte 0, which
 * the callers read; the rest is dead. */
static void drawn_up_to(Snes *s, unsigned sprite)
{
    set_battle_oam_next(state_of(s), (uint16_t)(sprite * SPRITE_BYTES));
    s->cpu.d = BATTLE_SPRITE_PAGE;
    s->cpu.a = 0;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

void battle_draw_objects(Snes *s)
{
    draw_objects(state_of(s));
    s->cpu.d = BATTLE_PAGE;
    m_rts(s);
}

/* X the combatant's object. */
void battle_draw_shadow(Snes *s) { drawn_up_to(s, draw_shadow(state_of(s), slot_of_object(s->cpu.x), next_free(s))); }

void battle_draw_afterimages(Snes *s) { drawn_up_to(s, draw_afterimages(state_of(s), slot_being_drawn(s), next_free(s), parts_last_drawn(s))); }

/* Entered with 16-bit A; leaves 8-bit A. */
void battle_draw_combatant(Snes *s) { drawn_up_to(s, draw_combatant(state_of(s), slot_being_drawn(s), next_free(s))); }

/* The afterimage at the trail's point in the cells the original puts it in. */
void battle_draw_afterimage(Snes *s)
{
    GameState *g = state_of(s);
    TrailPoint p = { battle_draw_scratch(g, 0x32 - 0x2E), battle_draw_scratch(g, 0x33 - 0x2E), battle_draw_scratch(g, 0x34 - 0x2E),
                     battle_draw_scratch(g, 0x35 - 0x2E) };
    drawn_up_to(s, draw_afterimage(g, slot_being_drawn(s), next_free(s), p, parts_last_drawn(s)));
}

void battle_draw_in_front(Snes *s) { drawn_up_to(s, draw_in_front(state_of(s), slot_being_drawn(s), next_free(s))); }
void battle_draw_marker(Snes *s) { drawn_up_to(s, draw_marker(state_of(s), slot_being_drawn(s), next_free(s))); }
void battle_draw_numbers(Snes *s) { drawn_up_to(s, draw_numbers(state_of(s), slot_being_drawn(s), next_free(s))); }
void battle_draw_behind(Snes *s) { drawn_up_to(s, draw_behind(state_of(s), slot_being_drawn(s), next_free(s))); }

/* X the record. */
void battle_draw_effect(Snes *s) { drawn_up_to(s, draw_effect(state_of(s), s->cpu.x, next_free(s))); }

/* X and Y the sprite entry and the buffer's sprite to copy from and to, as
 * addresses in the data bank, and the count of groups in its cell. */
enum { SPRITE_ENTRIES_AT = 0x11B0, SPRITE_BUFFER_AT = 0x0700 };

static bool names_entry(uint16_t at)
{
    return at >= SPRITE_ENTRIES_AT && (at - SPRITE_ENTRIES_AT) % SPRITE_BYTES == 0
           && (at - SPRITE_ENTRIES_AT) / SPRITE_BYTES < BATTLE_OBJECT_SPRITE_ENTRIES_COUNT;
}
static bool names_sprite(uint16_t at)
{
    return at >= SPRITE_BUFFER_AT && (at - SPRITE_BUFFER_AT) % SPRITE_BYTES == 0 && (at - SPRITE_BUFFER_AT) / SPRITE_BYTES < SPRITES;
}

bool battle_tile_copy_in_buffers(Snes *s)
{
    return battle_sprite_page(s) && names_entry(s->cpu.x) && names_sprite(s->cpu.y);
}

void battle_copy_frame_tiles(Snes *s)
{
    GameState *g = state_of(s);
    copy_tiles(g, (unsigned)(s->cpu.x - SPRITE_ENTRIES_AT) / SPRITE_BYTES, (unsigned)(s->cpu.y - SPRITE_BUFFER_AT) / SPRITE_BYTES,
               battle_draw_tile_groups(g));
    m_rts(s);
}

/* ---- As C ------------------------------------------------------------------------------------ */

bool battle_objects_drawn(GameState *g)
{
    if (!someone_on_battlefield(g))
        return battle_needs_the_machine(g, 0xCFE83A, "a draw pass with nobody on the battlefield");
    draw_objects(g);
    return true;
}
