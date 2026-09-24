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
 * A combatant with a second entry is drawn at that one and only its shadow
 * and afterimages at the other. A sprite off the left or right of the screen
 * gets its X's ninth bit in the buffer's high table; the battle keeps tables
 * of which high-table byte and bit go with each sprite.
 *
 * The original's drawing routines run on the combatants' objects' page and
 * keep their working values in cells there; the numbers', the marker's and
 * the effects' switch to the battle's page. The pass runs each of them in its
 * own time (native_time_as), as the original calls them. */
#include "battle/effects.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/sprite_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The sprite buffer ---------------------------------------------------------------- */

/* The buffer's first four sprites are the menu's cursors; the combatants'
 * start after them. A place in the buffer is the sprite's number; the battle
 * counts the next free one in bytes, four a sprite. */
enum { SPRITE_BYTES = 4, FIRST_FREE_SPRITE = 4, SPRITES = 128 };

static unsigned next_sprite(const GameState *g) { return battle_oam_next(g) / SPRITE_BYTES; }
static void set_next_sprite(GameState *g, unsigned sprite) { set_battle_oam_next(g, (uint16_t)(sprite * SPRITE_BYTES)); }

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

/* ---- The drawing page's cells --------------------------------------------------------- */

/* The original keeps its working values in cells of the objects' page ($2E-$44)
 * and of the battle's ($A1, $C6-$D1); the ports leave there what it leaves,
 * for shadow mode. `cell` is the original's direct-page address. */
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

static void leave_parts_cells(GameState *g, unsigned slot, FramePoint at, FramePoint offset, uint8_t bits,
                              unsigned parts, FramePoint last)
{
    leave_draw_cell16(g, 0x2E, at.x);
    leave_draw_cell16(g, 0x30, at.y);
    leave_draw_cell16(g, 0x32, last.x);
    leave_draw_cell16(g, 0x34, 0);
    leave_draw_cell16(g, 0x36, (uint16_t)(bits << 8));
    leave_draw_cell16(g, 0x38, last.y);
    leave_draw_cell16(g, 0x3A, offset.x);
    leave_draw_cell16(g, 0x3C, offset.y);
    leave_draw_cell16(g, 0x42, (uint16_t)(slot * PART_ENTRIES_PER_COMBATANT * SPRITE_BYTES + parts * SPRITE_BYTES));
}

// $CF:EAC5 (from $CF:EB1B), $CF:E99C (from $CF:EA11)
/* Draw the combatant's frame with its first part's point at `at` plus the
 * `offset`, into the next free sprites. */
static void draw_frame(GameState *g, unsigned slot, FramePoint at, FramePoint offset)
{
    at = (FramePoint){ (uint16_t)(at.x + offset.x), (uint16_t)(at.y + offset.y) };
    uint8_t bits = (uint8_t)(battle_object_palette(g, slot) | battle_object_attribute_bits(g, slot));
    unsigned parts = frame_parts(g, slot), sprite = next_sprite(g), part = slot * PART_ENTRIES_PER_COMBATANT;
    FramePoint p = { 0, 0 };
    for (unsigned n = 0; n < parts; n++, sprite++, part++) {
        p = (FramePoint){ (uint16_t)(battle_part_offsets_x(g, part) + at.x), (uint16_t)(battle_part_offsets_y(g, part) + at.y) };
        set_oam_buffer_attributes(g, sprite, (uint8_t)((oam_buffer_attributes(g, sprite) & PART_KEEPS) | bits));
        place_sprite(g, sprite, p.x, p.y);
        set_next_sprite(g, sprite + 1);
    }
    ORIGINAL_EFFECT(leave_parts_cells(g, slot, at, offset, bits, parts, p));
}

static bool hidden(const GameState *g, unsigned slot) { return battle_object_hidden(g, slot) != 0; }

static void leave_first_part_cell(GameState *g, unsigned slot)
{
    leave_draw_cell16(g, 0x42, (uint16_t)(slot * PART_ENTRIES_PER_COMBATANT * SPRITE_BYTES));
}

/* A signed byte as the word the frame's point is moved by. */
static uint16_t widened(int8_t v) { return (uint16_t)(int16_t)v; }

// $CF:EAC5
/* Draw the combatant where it stands, moved by its sprite's offset, unless
 * it is hidden. */
static void draw_combatant(GameState *g, unsigned slot)
{
    ORIGINAL_EFFECT(leave_first_part_cell(g, slot));
    if (hidden(g, slot))
        return;
    draw_frame(g, slot, (FramePoint){ combatant_x(g, slot), combatant_y(g, slot) },
               (FramePoint){ widened(combatant_sprite_offset_x(g, slot)), widened(combatant_sprite_offset_y(g, slot)) });
}

/* ---- Tiles ------------------------------------------------------------------------------ */

/* The tiles of a combatant's frame come from its sprite entries, groups of
 * four sprites, copied into the sprites its parts are drawn in; the table at
 * $CC:F84D says where each object's entries are, which for the combatants is
 * four entries apart, as the map has them. A count of 0 or 8 or more copies
 * eight groups. */
enum { SPRITES_PER_TILE_GROUP = 4, MOST_TILE_GROUPS = 8 };

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

/* A combatant's tiles, into the next free sprites. */
static void copy_frame_tiles(GameState *g, unsigned slot, uint16_t count)
{
    copy_tiles(g, slot * SPRITES_PER_TILE_GROUP, next_sprite(g), count);
}

/* The groups a frame of so many parts takes: a group for sixteen parts, the
 * count rounded to the nearest (a frame of fewer than 15 parts counts 0, so
 * its tiles copy eight). */
static uint16_t tile_groups_for(uint16_t parts) { return (uint16_t)((parts + 1) >> 4); }

/* ---- What shows in front of a combatant and behind it -------------------------------- */

/* The records of a combatant: a row an effect, a byte a combatant; a record
 * names an effect (its low three bits) with bit 7 in front of the combatant
 * or bit 6 behind it. */
enum { RECORD_ROWS = BATTLE_EFFECTS, RECORD_EFFECT = 0x07, IN_FRONT = 0x80, BEHIND = 0x40 };

static unsigned record_of(unsigned slot, unsigned row) { return row * BATTLE_COMBATANTS + slot; }

/* An effect's sprite set: so many sprites (its count's low six bits; none
 * draws 256), each an X and a Y from the effect's point, a tile and
 * attributes. The effect's own attribute bits are its priority; the sprite
 * keeps the rest. */
enum { SET_OF_EFFECT = 0x3F, EFFECT_SPRITE_KEEPS = 0xCF };

static void leave_effect_cells(GameState *g, uint8_t bits, uint16_t x, uint16_t y)
{
    set_battle_effect_sprite_attributes(g, bits);
    leave_effect_cell16(g, 0xCC, x);
    leave_effect_cell16(g, 0xCE, y);
}

static void leave_effect_sprite_cells(GameState *g, uint16_t next, uint16_t x, uint16_t y)
{
    leave_effect_cell16(g, 0xC6, next);
    leave_effect_cell16(g, 0xC8, 0);
    leave_effect_cell16(g, 0xCA, x);
    leave_effect_cell16(g, 0xD0, y);
}

// $CF:EEEB
/* Draw the effect the record names, at its anchor moved by its frame's
 * offset, while it has a frame showing. */
static void draw_effect(GameState *g, unsigned record)
{
    unsigned effect = battle_object_effect_slots(g, record) & RECORD_EFFECT;
    uint8_t bits = battle_effect_attribute_bits(g, effect);
    uint16_t x = (uint16_t)(battle_anchor_x(g, effect) + battle_anchor_offset_x(g, effect));
    uint16_t y = (uint16_t)(battle_anchor_y(g, effect) + battle_anchor_offset_y(g, effect));
    ORIGINAL_EFFECT(leave_effect_cells(g, bits, x, y));
    if (battle_effect_frame_time(g, effect) == 0)
        return;
    uint8_t set = battle_effect_sprite_set(g, effect) & SET_OF_EFFECT;
    unsigned counted = battle_bitmap_counts(g, set) & SET_OF_EFFECT;
    unsigned sprites = counted ? counted : 256, sprite = next_sprite(g);
    uint16_t from = battle_effect_sprite_set_start(g, set);
    uint16_t sx = 0, sy = 0;
    for (unsigned n = 0; n < sprites; n++, from = (uint16_t)(from + SPRITE_BYTES)) {
        sx = (uint16_t)(battle_effect_sprite_data(g, from) + x);
        sy = (uint16_t)(battle_effect_sprite_data(g, from + 1u) + y);
        set_oam_buffer_tile(g, sprite + n, battle_effect_sprite_data(g, from + 2u));
        set_oam_buffer_attributes(g, sprite + n, (uint8_t)((battle_effect_sprite_data(g, from + 3u) & EFFECT_SPRITE_KEEPS) | bits));
        place_sprite(g, sprite + n, sx, sy);
    }
    set_next_sprite(g, sprite + counted);
    ORIGINAL_EFFECT(leave_effect_sprite_cells(g, from, sx, sy));
}

/* The effect a record names, with its side bit cleared, is shown while its
 * flag is set. */
static bool shown(const GameState *g, uint8_t effect) { return battle_effect_shown(g, effect) != 0; }

// $CF:EE9F
/* The combatant's damage numbers: four sprites, prepared with their
 * high-table byte, which fills the byte of the four they go in. */
enum { NUMBER_DIGITS = 4 };

static void draw_numbers(GameState *g, unsigned slot)
{
    if (!battle_prepared_sprites_on(g, slot))
        return;
    unsigned sprite = next_sprite(g);
    set_oam_high_table_byte(g, battle_oam_high_offsets(g, sprite * SPRITE_BYTES), battle_number_sprite_high(g, slot));
    for (unsigned d = 0; d < NUMBER_DIGITS; d++) {
        unsigned digit = slot * NUMBER_DIGITS + d;
        set_oam_buffer_x(g, sprite + d, battle_number_sprites_x(g, digit));
        set_oam_buffer_y(g, sprite + d, battle_number_sprites_y(g, digit));
        set_oam_buffer_tile(g, sprite + d, battle_number_sprites_tile(g, digit));
        set_oam_buffer_attributes(g, sprite + d, battle_number_sprites_attributes(g, digit));
    }
    set_next_sprite(g, sprite + NUMBER_DIGITS);
    ORIGINAL_EFFECT(set_battle_effect_cells(g, 0xC8 - 0xC6, 0));
}

// $CF:EE2C
/* The combatant's marker: an animated sprite at it, four frames, each shown
 * for the marker's period. */
enum { MARKER_FRAMES = 4 };

static void draw_marker(GameState *g, unsigned slot)
{
    if (!battle_marker_on(g, slot))
        return;
    uint8_t x = combatant_x(g, slot), y = combatant_y(g, slot);
    ORIGINAL_EFFECT(set_battle_effect_cells(g, 0xCC - 0xC6, x));
    ORIGINAL_EFFECT(set_battle_effect_cells(g, 0xCD - 0xC6, y));
    uint8_t left = (uint8_t)(battle_marker_countdown(g, slot) - 1);
    set_battle_marker_countdown(g, slot, left ? left : battle_marker_period(g, slot));
    if (left == 0) {
        uint8_t frame = (uint8_t)(battle_marker_frame(g, slot) + 1);
        set_battle_marker_frame(g, slot, frame == MARKER_FRAMES ? 0 : frame);
    }
    MarkerSprite m = battle_marker_sprite(g, (uint8_t)(battle_marker_frame(g, slot) * SPRITE_BYTES + battle_marker_kind(g, slot)));
    unsigned sprite = next_sprite(g);
    set_oam_buffer_x(g, sprite, (uint8_t)(x + m.x));
    set_oam_buffer_y(g, sprite, (uint8_t)(y + m.y));
    set_oam_buffer_tile(g, sprite, m.tile);
    set_oam_buffer_attributes(g, sprite, (uint8_t)(m.attributes | battle_sprite_attributes(g)));
    set_next_sprite(g, sprite + 1);
}

/* ---- The shadow ------------------------------------------------------------------------- */

/* A shadow is the shadow tile 8 pixels left of and 4 above the combatant's
 * point, in the shadows' priority, and takes four sprites' room. */
enum { SHADOW_TILE = 0x02, SHADOW_LEFT = 8, SHADOW_UP = 4, SHADOW_SPRITES = 4 };

// $CF:EA8A
static void draw_shadow(GameState *g, unsigned slot)
{
    if (!battle_object_shadow(g, slot) || hidden(g, slot))
        return;
    unsigned sprite = next_sprite(g);
    set_oam_buffer_x(g, sprite, (uint8_t)(combatant_x(g, slot) - SHADOW_LEFT));
    set_oam_buffer_y(g, sprite, (uint8_t)(combatant_y(g, slot) - SHADOW_UP));
    set_oam_buffer_tile(g, sprite, SHADOW_TILE);
    set_oam_buffer_attributes(g, sprite, (uint8_t)(battle_sprite_attributes(g) | battle_cursor_priority(g)));
    set_next_sprite(g, sprite + SHADOW_SPRITES);
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
/* One afterimage: the frame's parts at a point of the trail, unless the
 * combatant is hidden. */
static void draw_afterimage_parts(GameState *g, unsigned slot, TrailPoint p)
{
    ORIGINAL_EFFECT(leave_trail_point_cells(g, p));
    ORIGINAL_EFFECT(leave_first_part_cell(g, slot));
    if (hidden(g, slot))
        return;
    draw_frame(g, slot, (FramePoint){ p.x, p.y }, (FramePoint){ sign_of(p.x_high), sign_of(p.y_high) });
}

/* Whether the afterimages show this frame: one frame in so many. */
static bool afterimages_due(GameState *g, unsigned slot)
{
    uint8_t left = (uint8_t)(battle_afterimage_countdown(g, slot) - 1);
    set_battle_afterimage_countdown(g, slot, left ? left : battle_afterimage_period(g, slot));
    return left == 0;
}

/* ---- The draw list ---------------------------------------------------------------------- */

/* An entry of the draw list: the object, marked as the combatant's own entry
 * or as its second, topmost one, sorted by its key: how far down the screen
 * the combatant stands, or the top key. The list ends with an end marker,
 * which the pass knows by its top bit. */
enum { ENTRY_OBJECT = 0x1F, OWN_ENTRY = 0x40, END_OF_LIST = 0xFF, IS_END = 0x80, TOPMOST_KEY = 0xFE };

/* A combatant with a second entry is marked when that entry is drawn, and
 * its own entry then draws only its shadow and afterimages. */
enum { HAS_SECOND_ENTRY = 0x01, DRAWN_AT_SECOND_ENTRY = 0x80 };

enum { MOST_ENTRIES = 2 * BATTLE_COMBATANTS };

typedef struct {
    uint8_t key[MOST_ENTRIES], entry[MOST_ENTRIES];
    unsigned count;
} DrawList;

static void list_add(DrawList *list, uint8_t key, uint8_t entry)
{
    list->key[list->count] = key;
    list->entry[list->count] = entry;
    list->count++;
}

/* Sort the list, largest key first: the original's exchange sort, which
 * the order of equal keys depends on. */
static void sort_draw_list(DrawList *list)
{
    for (unsigned i = 0; i + 1 < list->count; i++)
        for (unsigned j = i + 1; j < list->count; j++)
            if (list->key[i] < list->key[j]) {
                uint8_t key = list->key[i], entry = list->entry[i];
                list->key[i] = list->key[j];
                list->entry[i] = list->entry[j];
                list->key[j] = key;
                list->entry[j] = entry;
            }
}

// $CF:E83A (to $CF:E8A6)
/* List the combatants on the battlefield, those with a second entry twice,
 * and sort the list. */
static void build_draw_list(GameState *g)
{
    DrawList list = { { 0 }, { 0 }, 0 };
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!battle_slot_present(g, slot))
            continue;
        unsigned object = battle_object_of(slot);
        list_add(&list, combatant_y(g, slot), (uint8_t)(object | OWN_ENTRY));
        uint8_t second = battle_object_extra_draw_on(g, slot);
        set_battle_object_extra_draw(g, slot, second ? HAS_SECOND_ENTRY : 0);
        if (second)
            list_add(&list, TOPMOST_KEY, (uint8_t)object);
    }
    ORIGINAL_EFFECT(leave_draw_cell16(g, 0x30, (uint16_t)(list.count - 1)));
    sort_draw_list(&list);
    for (unsigned i = 0; i < list.count; i++) {
        set_battle_draw_list_depth(g, i, list.key[i]);
        set_battle_draw_list(g, i, list.entry[i]);
    }
    set_battle_draw_list(g, list.count, END_OF_LIST);
}

static unsigned slot_of_object(unsigned object) { return object - BATTLE_FIRST_COMBATANT_OBJECT; }

static bool drawn_at_second_entry(const GameState *g, unsigned slot)
{
    return (battle_object_extra_draw(g, slot) & DRAWN_AT_SECOND_ENTRY) != 0;
}

/* ---- The pass, each part in the time of the original's routine for it ------------------ */

enum {
    DRAW_IN_FRONT = 0xCFEDB8, DRAW_BEHIND = 0xCFEDF7, DRAW_COMBATANT = 0xCFEAC5, COPY_TILES = 0xCFEB94,
    DRAW_SHADOW = 0xCFEA8A, DRAW_AFTERIMAGES = 0xCFE92C, DRAW_AFTERIMAGE = 0xCFE99C, DRAW_EFFECT = 0xCFEEEB,
    DRAW_NUMBERS = 0xCFEE9F, DRAW_MARKER = 0xCFEE2C,
};

/* The effects the combatant's records put on one side of it, while shown. */
static void draw_effects(Snes *s, unsigned slot, bool in_front)
{
    GameState *g = state_of(s);
    for (unsigned row = 0; row < RECORD_ROWS; row++) {
        unsigned record = record_of(slot, row);
        uint8_t v = battle_object_effect_slots(g, record);
        if (v == 0 || ((v & IN_FRONT) != 0) != in_front)
            continue;
        if (shown(g, (uint8_t)(v & ~(in_front ? IN_FRONT : BEHIND))))
            NATIVE_TIME_AS(s, DRAW_EFFECT, draw_effect(g, record));
    }
}

// $CF:EDB8
/* In front of the combatant: its numbers while numbers show; its marker
 * while no effect plays, unless markers are held off; otherwise its effects
 * in front. */
static void draw_in_front(Snes *s, unsigned slot)
{
    GameState *g = state_of(s);
    if (battle_number_step(g))
        NATIVE_TIME_AS(s, DRAW_NUMBERS, draw_numbers(g, slot));
    else if (!battle_effects_running(g) && !battle_markers_held(g))
        NATIVE_TIME_AS(s, DRAW_MARKER, draw_marker(g, slot));
    else
        draw_effects(s, slot, true);
}

// $CF:EDF7
/* Behind it: while effects play and no numbers show, its effects behind. */
static void draw_behind(Snes *s, unsigned slot)
{
    GameState *g = state_of(s);
    if (battle_effects_running(g) && !battle_number_step(g))
        draw_effects(s, slot, false);
}

/* The afterimages copy as many tiles as the last combatant drawn had parts. */
static void draw_afterimage(Snes *s, unsigned slot, TrailPoint p, uint16_t last_parts)
{
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, COPY_TILES, copy_frame_tiles(g, slot, tile_groups_for(last_parts)));
    draw_afterimage_parts(g, slot, p);
}

// $CF:E92C
/* The combatant's afterimages, when they show this frame: its frame at each
 * of its trail's four points. */
static void draw_afterimages(Snes *s, unsigned slot, uint16_t last_parts)
{
    GameState *g = state_of(s);
    ORIGINAL_EFFECT(set_battle_afterimage_object(g, (uint16_t)battle_object_of(slot)));
    if (!afterimages_due(g, slot))
        return;
    for (unsigned point = 0; point < TRAIL_POINTS; point++)
        NATIVE_TIME_AS(s, DRAW_AFTERIMAGE, draw_afterimage(s, slot, trail_point(g, point, slot), last_parts));
}

/* The combatant at a list entry: in front of it, its tiles and its frame,
 * behind it; then, unless it has a second entry, its shadow. Returns whether
 * it drew its frame (for how many tiles the afterimages copy). */
static bool draw_entry_combatant(Snes *s, unsigned slot, uint8_t parts)
{
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, DRAW_IN_FRONT, draw_in_front(s, slot));
    NATIVE_TIME_AS(s, COPY_TILES, copy_frame_tiles(g, slot, tile_groups_for(parts)));
    NATIVE_TIME_AS(s, DRAW_COMBATANT, draw_combatant(g, slot));
    NATIVE_TIME_AS(s, DRAW_BEHIND, draw_behind(s, slot));
    if (!battle_object_extra_draw(g, slot))
        return false;
    set_battle_object_extra_draw(g, slot, DRAWN_AT_SECOND_ENTRY);
    return true;
}

static void leave_pass_cells(GameState *g, unsigned position)
{
    set_battle_draw_list_position(g, (uint16_t)position);
}

// $CF:E83A
/* Draw every combatant on the battlefield (the file's header). */
static void draw_objects(Snes *s)
{
    GameState *g = state_of(s);
    build_draw_list(g);
    ORIGINAL_EFFECT(set_battle_draw_object(g, 0));
    ORIGINAL_EFFECT(set_battle_draw_part_count(g, 0));
    set_next_sprite(g, FIRST_FREE_SPRITE);
    uint16_t last_parts = 0;
    unsigned position = 0;
    for (;; position++) {
        uint8_t entry = battle_draw_list(g, position);
        if (entry & IS_END)
            break;
        unsigned object = entry & ENTRY_OBJECT, slot = slot_of_object(object);
        ORIGINAL_EFFECT(set_battle_draw_object(g, (uint16_t)object));
        uint8_t parts = battle_object_part_count(g, object);
        bool second_drawn = false;
        if (!drawn_at_second_entry(g, slot) && parts != 0) {
            last_parts = parts;
            ORIGINAL_EFFECT(set_battle_draw_part_count(g, parts));
            second_drawn = draw_entry_combatant(s, slot, parts);
        }
        if (!second_drawn)
            NATIVE_TIME_AS(s, DRAW_SHADOW, draw_shadow(g, slot));
        if (battle_effects_running(g) && (entry & OWN_ENTRY) && battle_trail_on(g, slot))
            NATIVE_TIME_AS(s, DRAW_AFTERIMAGES, draw_afterimages(s, slot, last_parts));
    }
    ORIGINAL_EFFECT(leave_pass_cells(g, position));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The pass is guarded on someone being on the battlefield: with the list
 * empty, the original's sort would run through the whole bank. The drawing
 * routines on their own draw the combatant the pass is drawing (the object
 * in its cell, or in X for the shadow), into the sprite buffer at a sprite's
 * place in it. */
static bool someone_on_battlefield(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool present = false;
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        present = present || battle_slot_present(g, slot);
    vars_peek(false);
    return present;
}

bool battle_draw_pass_ready(Snes *s) { return battle_sprite_bank_ready(s) && someone_on_battlefield(s); }

static bool combatant_object(uint16_t object)
{
    return object >= BATTLE_FIRST_COMBATANT_OBJECT && object < BATTLE_FIRST_COMBATANT_OBJECT + BATTLE_COMBATANTS;
}

static bool at_a_sprite(Snes *s)
{
    vars_peek(true);
    uint16_t next = battle_oam_next(state_of(s));
    vars_peek(false);
    return next % SPRITE_BYTES == 0 && next < SPRITES * SPRITE_BYTES;
}

static uint16_t object_being_drawn(Snes *s)
{
    vars_peek(true);
    uint16_t object = battle_draw_object(state_of(s));
    vars_peek(false);
    return object;
}

bool battle_drawing_combatant(Snes *s)
{
    return battle_sprite_page_ready(s) && combatant_object(object_being_drawn(s)) && at_a_sprite(s);
}

/* The frame's own routines set A before they use it, whatever the caller left. */
bool battle_drawing_combatant_frame(Snes *s)
{
    return battle_sprite_page(s) && combatant_object(object_being_drawn(s)) && at_a_sprite(s);
}

/* An effect's record in X. */
bool battle_drawing_effect_record(Snes *s)
{
    return battle_sprite_bank_ready(s) && s->cpu.x < BATTLE_OBJECT_EFFECT_SLOTS_COUNT && at_a_sprite(s);
}

bool battle_drawing_combatant_in_x(Snes *s)
{
    return battle_sprite_page_ready(s) && combatant_object(s->cpu.x) && at_a_sprite(s);
}

bool battle_drawing_combatant_x_and_cell(Snes *s)
{
    return battle_drawing_combatant(s) && s->cpu.x == object_being_drawn(s);
}

static unsigned slot_being_drawn(Snes *s) { return slot_of_object(object_being_drawn(s)); }

static uint16_t parts_being_drawn(Snes *s)
{
    vars_peek(true);
    uint16_t parts = battle_draw_part_count(state_of(s));
    vars_peek(false);
    return parts;
}

/* Each leaves the direct page on the objects' page, where the pass runs,
 * and A's high byte 0, which the callers read; the rest is dead. */
static void back_on_drawing_page(Snes *s)
{
    s->cpu.d = BATTLE_SPRITE_PAGE;
    s->cpu.a = 0;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

void battle_draw_objects(Snes *s)
{
    draw_objects(s);
    s->cpu.d = BATTLE_PAGE;
    m_rts(s);
}

void battle_draw_shadow(Snes *s)
{
    draw_shadow(state_of(s), slot_of_object(s->cpu.x));
    back_on_drawing_page(s);
}

void battle_draw_afterimages(Snes *s)
{
    draw_afterimages(s, slot_being_drawn(s), parts_being_drawn(s));
    back_on_drawing_page(s);
}

/* Entered with 16-bit A; leaves 8-bit A. */
void battle_draw_combatant(Snes *s)
{
    draw_combatant(state_of(s), slot_being_drawn(s));
    back_on_drawing_page(s);
}

/* The afterimage at the trail's point the cells hold. */
void battle_draw_afterimage(Snes *s)
{
    GameState *g = state_of(s);
    TrailPoint p = { battle_draw_scratch(g, 4), battle_draw_scratch(g, 5), battle_draw_scratch(g, 6), battle_draw_scratch(g, 7) };
    draw_afterimage(s, slot_being_drawn(s), p, parts_being_drawn(s));
    back_on_drawing_page(s);
}

void battle_draw_in_front(Snes *s)
{
    draw_in_front(s, slot_being_drawn(s));
    back_on_drawing_page(s);
}

void battle_draw_marker(Snes *s)
{
    draw_marker(state_of(s), slot_being_drawn(s));
    back_on_drawing_page(s);
}

void battle_draw_numbers(Snes *s)
{
    draw_numbers(state_of(s), slot_being_drawn(s));
    back_on_drawing_page(s);
}

void battle_draw_behind(Snes *s)
{
    draw_behind(s, slot_being_drawn(s));
    back_on_drawing_page(s);
}

/* X the record. */
void battle_draw_effect(Snes *s)
{
    draw_effect(state_of(s), s->cpu.x);
    back_on_drawing_page(s);
}

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
