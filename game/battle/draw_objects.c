/* Battle: drawing the battle objects into the OAM buffer (bank CF).
 *
 * Each frame the objects of the eleven battle slots (battle objects $0C-$16)
 * are listed with their Y position as the sort key, those with a topmost part
 * listed a second time with the key $FE, and the list is sorted, largest key
 * first. Then each entry in order draws its object: what goes under it (the
 * prepared number sprites, the animated marker, or the effect sprites marked
 * to go under), its parts' tiles copied into place, its parts at its position
 * plus its sprite offset and each part's offset, what goes over it, and its
 * shadow unless it has a topmost entry; and, while effects run, its trail of
 * four afterimages. Sprites go to the OAM buffer from battle_oam_next on, 4
 * bytes each, with their X high bit in the high table through
 * battle_oam_high_offsets and battle_oam_x_high_bits.
 *
 * The routines run with the direct page on the battle_sprites page ($1D00),
 * where the objects' positions and the draw list live, and data bank $7E. The
 * marker and the effect sprites use cells on the battle page ($0000) instead,
 * which the original switches the direct page to for them. A's high byte is 0
 * all through a frame's drawing (the entries that take it along are guarded
 * on that), and what each leaves in A is returned where it is live. */
#include "battle/battle.h"

#include "battle/sprite_data.h"
#include "port/machine.h"
#include "ram.h"

enum {
    FIRST_OBJECT = 0x0C,   /* battle object of battle slot 0 */
    OBJECTS_END = 0x17,
    SPRITE_BYTES = 4,
    FIRST_FREE_OFFSET = 0x10,   /* after the four cursor sprites */
    SHADOW_TILE = 0x02,
    SHADOW_BYTES = 0x10,
    SHADOW_LEFT = 8,
    SHADOW_UP = 4,
    LIST_OBJECT = 0x1F,
    LIST_MAIN = 0x40,   /* clear in a topmost entry */
    LIST_END = 0x80,
    TOPMOST_KEY = 0xFE,
    EXTRA_DRAWN = 0x80,
    PARTS_PER_OBJECT = 16,   /* bytes of battle_part_offsets per object */
    TILE_GROUPS = 8,
    OFFSCREEN_Y = 0xF0,
    TRAIL_RECORDS = 4,
    TRAIL_RECORD_STRIDE = 0x16,
    EFFECT_RECORD_STRIDE = 11,
    EFFECT_RECORDS_END = 0x58,
    EFFECT_UNDER = 0x80,
    MARKER_FRAMES = 4,
    PREPARED_SPRITES = 4,
    SPRITE_SETS = 0x3F,
    EFFECT_ANCHORS = 0x07,
    EFFECT_ATTRIBUTES_KEPT = 0xCF,
    TILE_ATTRIBUTES_KEPT = 0xC1FF,
};

/* The drawing scratch (battle_draw_scratch, $2E up on the page) by what it
 * holds: the base position (16-bit X, Y), the part's X, the part count, the
 * attribute word, the part's Y, the sprite offset sign-extended, the part
 * index. An afterimage's recorded position passes in the part X and count
 * cells. */
enum { BASE_X = 0x00, BASE_Y = 0x02, PART_X = 0x04, PART_COUNT = 0x06, ATTRIBUTES = 0x08, PART_Y = 0x0A,
       OFFSET_X = 0x0C, OFFSET_Y = 0x0E, PART_INDEX = 0x14 };

/* The battle page's cells the marker and the effect sprites use (from
 * battle_effect_cells, $C6). */
enum { CELL_FROM = 0x00, CELL_COUNT = 0x02, CELL_X = 0x04, CELL_ANCHOR_X = 0x06, CELL_ANCHOR_Y = 0x08,
       CELL_Y = 0x0A, CELL_MARKER_X = 0x06, CELL_MARKER_Y = 0x07 };

static uint8_t scratch8(Snes *s, uint8_t c) { return m_read8(s, RAM_BATTLE_DRAW_SCRATCH + c); }
static void set_scratch8(Snes *s, uint8_t c, uint8_t v) { m_write8(s, RAM_BATTLE_DRAW_SCRATCH + c, v); }
static uint16_t scratch16(Snes *s, uint8_t c) { return m_read16(s, RAM_BATTLE_DRAW_SCRATCH + c); }
static void set_scratch16(Snes *s, uint8_t c, uint16_t v) { m_write16(s, RAM_BATTLE_DRAW_SCRATCH + c, v); }
static uint8_t cell8(Snes *s, uint8_t c) { return m_read8(s, RAM_BATTLE_EFFECT_CELLS + c); }
static void set_cell8(Snes *s, uint8_t c, uint8_t v) { m_write8(s, RAM_BATTLE_EFFECT_CELLS + c, v); }
static uint16_t cell16(Snes *s, uint8_t c) { return m_read16(s, RAM_BATTLE_EFFECT_CELLS + c); }
static void set_cell16(Snes *s, uint8_t c, uint16_t v) { m_write16(s, RAM_BATTLE_EFFECT_CELLS + c, v); }

static uint16_t oam_next(Snes *s) { return m_read16(s, RAM_BATTLE_OAM_NEXT); }
static void set_oam_next(Snes *s, uint16_t v) { m_write16(s, RAM_BATTLE_OAM_NEXT, v); }
/* The object being drawn (16-bit, its high byte kept 0). */
static uint16_t draw_object(Snes *s) { return m_read16(s, RAM_BATTLE_DRAW_OBJECT); }
/* Its battle slot, as the original's 8-bit subtraction leaves it. */
static uint16_t draw_slot(Snes *s) { return (uint8_t)(m_read8(s, RAM_BATTLE_DRAW_OBJECT) - FIRST_OBJECT); }

/* A cell of the drawing page indexed the way the original's direct-page
 * indexing does it: 16-bit, in bank $00, where the page's cells are the work
 * RAM's. An index read back from a cell a sprite landed on can take it past
 * the RAM. */
static uint32_t page_at(uint32_t cell, uint16_t x) { return (uint16_t)((uint16_t)cell + x); }

static uint16_t sign_extend(uint8_t v) { return (uint16_t)((v & 0x80) ? 0xFF00 | v : v); }

/* Mark the sprite at OAM buffer offset `at` with its X high bit (the bit as
 * for offset `bit_at`, which is `at` unless a sprite landed on
 * battle_oam_next); returns the high-table byte. */
static uint8_t set_x_high_bit(Snes *s, uint16_t at, uint16_t bit_at)
{
    uint8_t byte = m_read8(s, RAM_BATTLE_OAM_HIGH_OFFSETS + at);
    uint8_t v = (uint8_t)(m_read8(s, RAM_OAM_HIGH_TABLE + byte) | m_read8(s, RAM_BATTLE_OAM_X_HIGH_BITS + bit_at));
    m_write8(s, RAM_OAM_HIGH_TABLE + byte, v);
    return v;
}

/* Whether a sprite at the 16-bit position gets its X high bit: one above the
 * top (Y's high byte set) unless it is at $F0 or below; otherwise one whose
 * X's high byte is set. `tested` is the byte the original tests. */
static bool needs_x_high(uint16_t x, uint16_t y, uint8_t *tested)
{
    if (y >> 8) {
        *tested = (uint8_t)y;
        return (uint8_t)y < OFFSCREEN_Y;
    }
    *tested = (uint8_t)(x >> 8);
    return (x >> 8) != 0;
}

// $CF:EB94
/* Copy the tile and attribute words (bytes 2-3 of each sprite) of groups of
 * four sprites from `from` to `to` in `bank`, as many groups as
 * battle_draw_tile_groups counts (16-bit, counted down after each of the
 * first seven; at most eight, and eight when it starts at 0 or above 7). */
static void copy_sprite_tiles(Snes *s, uint32_t bank, uint16_t from, uint16_t to)
{
    for (uint16_t group = 0; group < TILE_GROUPS; group++) {
        for (uint16_t k = 0; k < 4; k++) {
            uint16_t at = (uint16_t)(2 + group * PARTS_PER_OBJECT + k * SPRITE_BYTES);
            m_write16(s, bank + to + at, m_read16(s, bank + from + at));
        }
        if (group == TILE_GROUPS - 1)
            break;   /* the last group isn't counted */
        uint16_t left = (uint16_t)(m_read16(s, RAM_BATTLE_DRAW_TILE_GROUPS) - 1);
        m_write16(s, RAM_BATTLE_DRAW_TILE_GROUPS, left);
        if (left == 0)
            break;
    }
}

/* The return address a JSR leaves on the stack at `sp`, which a copy reading
 * the stack page sees. */
static void push_return(Snes *s, uint16_t sp, uint16_t ret)
{
    m_write8(s, sp, (uint8_t)(ret >> 8));
    m_write8(s, (uint16_t)(sp - 1), (uint8_t)ret);
}

/* The tiles of the drawn object's parts into place for its next sprites: from
 * its tile buffer, as many groups of four as its parts need. `sp` and `ret`:
 * where the original's JSR to the copy leaves its return address. */
static void copy_object_tiles(Snes *s, uint16_t sp, uint16_t ret)
{
    push_return(s, sp, ret);
    uint16_t to = (uint16_t)(oam_next(s) + (uint16_t)RAM_OAM_BUFFER);
    uint16_t from = battle_object_tile_source(s, (uint16_t)(draw_object(s) << 1));
    m_write16(s, RAM_BATTLE_DRAW_TILE_GROUPS, (uint16_t)((uint16_t)(m_read16(s, RAM_BATTLE_DRAW_PART_COUNT) + 1) >> 4));
    copy_sprite_tiles(s, RAM_OAM_BUFFER & 0xFF0000u, from, to);
}

/* The object's parts at the base position in the scratch: each part at the
 * base plus the part's offset, its attributes the object's palette and bits,
 * as many as its frame has. Returns what the original leaves in A. */
static uint8_t draw_parts(Snes *s, uint16_t object)
{
    set_scratch8(s, ATTRIBUTES + 1,
                 (uint8_t)(m_read8(s, RAM_BATTLE_OBJECT_PALETTE - FIRST_OBJECT + object)
                           | m_read8(s, RAM_BATTLE_OBJECT_ATTRIBUTE_BITS - FIRST_OBJECT + object)));
    set_scratch8(s, ATTRIBUTES, 0);
    uint8_t kind = m_read8(s, RAM_BATTLE_OBJECT_KIND - FIRST_OBJECT + object);
    set_scratch16(s, PART_COUNT, battle_frame_part_count(s, (uint8_t)(kind << 1)));
    /* The cells are read back where the original reads them: a sprite written
     * far enough on lands on them. */
    uint8_t a;
    do {
        uint16_t part = scratch16(s, PART_INDEX), at = oam_next(s);
        set_scratch16(s, PART_X, (uint16_t)(m_read16(s, RAM_BATTLE_PART_OFFSETS_X + part) + scratch16(s, BASE_X)));
        set_scratch16(s, PART_Y, (uint16_t)(m_read16(s, RAM_BATTLE_PART_OFFSETS_Y + part) + scratch16(s, BASE_Y)));
        uint32_t tile = RAM_OAM_BUFFER_TILE + at;
        m_write16(s, tile, (uint16_t)((m_read16(s, tile) & TILE_ATTRIBUTES_KEPT) | scratch16(s, ATTRIBUTES)));
        m_write8(s, RAM_OAM_BUFFER_X + at, scratch8(s, PART_X));
        m_write8(s, RAM_OAM_BUFFER_Y + at, scratch8(s, PART_Y));
        if (needs_x_high(scratch16(s, PART_X), scratch16(s, PART_Y), &a))
            a = set_x_high_bit(s, at, oam_next(s));
        set_oam_next(s, (uint16_t)(oam_next(s) + SPRITE_BYTES));
        set_scratch16(s, PART_INDEX, (uint16_t)(scratch16(s, PART_INDEX) + SPRITE_BYTES));
        set_scratch8(s, PART_COUNT, (uint8_t)(scratch8(s, PART_COUNT) - 1));
    } while (scratch8(s, PART_COUNT) != 0);
    return a;
}

/* The drawn object's first part index (16 bytes an object, from object $0C,
 * 8-bit), and whether it shows. */
static bool start_parts(Snes *s, uint16_t object, uint8_t *hidden)
{
    set_scratch16(s, PART_INDEX, (uint8_t)((uint8_t)(object - FIRST_OBJECT) << 4));
    *hidden = m_read8(s, RAM_BATTLE_OBJECT_HIDDEN - FIRST_OBJECT + object);
    return *hidden == 0;
}

/* The base position: the position plus the signed offset in the scratch. */
static void add_base_offsets(Snes *s)
{
    set_scratch16(s, BASE_X, (uint16_t)(scratch16(s, BASE_X) + scratch16(s, OFFSET_X)));
    set_scratch16(s, BASE_Y, (uint16_t)(scratch16(s, BASE_Y) + scratch16(s, OFFSET_Y)));
}

// $CF:EAC5
/* Draw the drawn object's parts at its position plus its sprite offset,
 * unless it is hidden. Returns what the original leaves in A. */
static uint8_t draw_sprite_parts(Snes *s)
{
    uint16_t object = (uint8_t)draw_object(s);
    uint8_t hidden;
    if (!start_parts(s, object, &hidden))
        return hidden;
    /* one after the other: an object past the eleven reads its Y from the
     * scratch the X was just written to */
    set_scratch16(s, BASE_X, m_read8(s, RAM_BATTLE_OBJECT_X + object));
    set_scratch16(s, OFFSET_X, sign_extend(m_read8(s, RAM_BATTLE_SPRITE_OFFSET_X - FIRST_OBJECT + object)));
    set_scratch16(s, BASE_Y, m_read8(s, RAM_BATTLE_OBJECT_Y + object));
    set_scratch16(s, OFFSET_Y, sign_extend(m_read8(s, RAM_BATTLE_SPRITE_OFFSET_Y - FIRST_OBJECT + object)));
    add_base_offsets(s);
    return draw_parts(s, object);
}

// $CF:E99C
/* One afterimage of the drawn object's trail: its tiles, and its parts at the
 * recorded position (the low bytes in the part X cells, the high bytes in the
 * part count cells, of which only the sign counts), unless it is hidden. */
static uint8_t draw_afterimage(Snes *s, uint16_t sp)
{
    copy_object_tiles(s, sp, 0xE9B8);
    uint16_t object = (uint8_t)draw_object(s);
    uint8_t hidden;
    if (!start_parts(s, object, &hidden))
        return hidden;
    set_scratch16(s, BASE_X, scratch8(s, PART_X));
    set_scratch16(s, OFFSET_X, (scratch8(s, PART_COUNT) & 0x80) ? 0xFF00 : 0);
    set_scratch16(s, BASE_Y, scratch8(s, PART_X + 1));
    set_scratch16(s, OFFSET_Y, (scratch8(s, PART_COUNT + 1) & 0x80) ? 0xFF00 : 0);
    add_base_offsets(s);
    return draw_parts(s, object);
}

// $CF:E92C
/* The object's trail: when its countdown runs out, draw the four afterimages
 * at the four positions it recorded. Returns what the original leaves in A
 * (`a` when it doesn't draw). */
static uint16_t step_trail(Snes *s, uint16_t object, uint16_t a, uint16_t sp)
{
    static const uint16_t returns[TRAIL_RECORDS] = { 0xE94F, 0xE968, 0xE981, 0xE99A };
    m_write16(s, RAM_BATTLE_EFFECT_OBJECT, object);
    uint32_t countdown = RAM_BATTLE_EFFECT_COUNTDOWN - FIRST_OBJECT + object;
    uint8_t left = (uint8_t)(m_read8(s, countdown) - 1);
    m_write8(s, countdown, left);
    if (left != 0)
        return a;
    m_write8(s, countdown, m_read8(s, RAM_BATTLE_EFFECT_RELOAD - FIRST_OBJECT + object));
    for (uint16_t record = 0; record < TRAIL_RECORDS; record++) {
        uint32_t o = (uint32_t)m_read16(s, RAM_BATTLE_EFFECT_OBJECT) - FIRST_OBJECT + record * TRAIL_RECORD_STRIDE;
        set_scratch8(s, PART_X, m_read8(s, RAM_BATTLE_TRAIL_LOW_X + o));
        set_scratch8(s, PART_X + 1, m_read8(s, RAM_BATTLE_TRAIL_LOW_Y + o));
        set_scratch8(s, PART_COUNT, m_read8(s, RAM_BATTLE_TRAIL_HIGH_X + o));
        set_scratch8(s, PART_COUNT + 1, m_read8(s, RAM_BATTLE_TRAIL_HIGH_Y + o));
        push_return(s, sp, returns[record]);
        a = draw_afterimage(s, (uint16_t)(sp - 2));
    }
    return a;
}

// $CF:EA8A
/* The object's shadow, when it has one and isn't hidden: the shadow tile, 8
 * left of and 4 above its position, with the shadows' attributes; it takes
 * four sprites' room. Returns the byte that stopped it, or -1 once drawn. */
static int draw_shadow(Snes *s, uint16_t object)
{
    uint8_t has = m_read8(s, RAM_BATTLE_OBJECT_SHADOW - FIRST_OBJECT + object);
    if (!has)
        return has;
    uint8_t hidden = m_read8(s, RAM_BATTLE_OBJECT_HIDDEN - FIRST_OBJECT + object);
    if (hidden)
        return hidden;
    uint16_t at = oam_next(s);
    m_write8(s, RAM_OAM_BUFFER_X + at, (uint8_t)(m_read8(s, RAM_BATTLE_OBJECT_X + object) - SHADOW_LEFT));
    m_write8(s, RAM_OAM_BUFFER_Y + at, (uint8_t)(m_read8(s, RAM_BATTLE_OBJECT_Y + object) - SHADOW_UP));
    m_write8(s, RAM_OAM_BUFFER_TILE + at, SHADOW_TILE);
    m_write8(s, RAM_OAM_BUFFER_ATTRIBUTES + at,
             (uint8_t)(m_read8(s, RAM_BATTLE_SPRITE_ATTRIBUTES) | m_read8(s, RAM_BATTLE_CURSOR_PRIORITY)));
    set_oam_next(s, (uint16_t)(at + SHADOW_BYTES));
    return -1;
}

// $CF:EEEB
/* The sprites of the effect in effect record `record` (its low three bits the
 * effect, which is also its anchor): the effect's sprite set, at the anchor
 * plus its offset, with the effect's attribute bits, while the effect shows. */
static void draw_effect_sprites(Snes *s, uint16_t record)
{
    uint8_t effect = m_read8(s, RAM_BATTLE_OBJECT_EFFECT_SLOTS + record) & EFFECT_ANCHORS;
    uint16_t w = (uint16_t)(effect << 1);
    m_write8(s, RAM_BATTLE_EFFECT_SPRITE_ATTRIBUTES, m_read8(s, RAM_BATTLE_EFFECT_ATTRIBUTE_BITS + effect));
    set_cell16(s, CELL_ANCHOR_X,
               (uint16_t)(m_read16(s, RAM_BATTLE_ANCHOR_X + w) + m_read16(s, RAM_BATTLE_ANCHOR_OFFSET_X + w)));
    set_cell16(s, CELL_ANCHOR_Y,
               (uint16_t)(m_read16(s, RAM_BATTLE_ANCHOR_Y + w) + m_read16(s, RAM_BATTLE_ANCHOR_OFFSET_Y + w)));
    if (!m_read8(s, RAM_BATTLE_SLOT_DELAYS + effect))
        return;
    uint8_t set = m_read8(s, RAM_BATTLE_EFFECT_SPRITE_SET + effect) & SPRITE_SETS;
    uint8_t bytes = (uint8_t)(m_read8(s, RAM_BATTLE_BITMAP_COUNTS + set) << 2);
    uint16_t from = battle_effect_sprite_set_start(s, (uint16_t)(set << 1));
    set_cell16(s, CELL_FROM, from);
    set_cell16(s, CELL_COUNT, (uint16_t)(bytes >> 2));
    uint16_t at = oam_next(s);
    do {
        uint16_t x = (uint16_t)(m_read8(s, RAM_BATTLE_EFFECT_SPRITE_DATA + from) + cell16(s, CELL_ANCHOR_X));
        set_cell16(s, CELL_X, x);
        m_write8(s, RAM_OAM_BUFFER_X + at, (uint8_t)x);
        uint16_t y = (uint16_t)(m_read8(s, RAM_BATTLE_EFFECT_SPRITE_DATA + 1 + from) + cell16(s, CELL_ANCHOR_Y));
        set_cell16(s, CELL_Y, y);
        m_write8(s, RAM_OAM_BUFFER_Y + at, (uint8_t)y);
        m_write8(s, RAM_OAM_BUFFER_TILE + at, m_read8(s, RAM_BATTLE_EFFECT_SPRITE_DATA + 2 + from));
        m_write8(s, RAM_OAM_BUFFER_ATTRIBUTES + at,
                 (uint8_t)((m_read8(s, RAM_BATTLE_EFFECT_SPRITE_DATA + 3 + from) & EFFECT_ATTRIBUTES_KEPT)
                           | m_read8(s, RAM_BATTLE_EFFECT_SPRITE_ATTRIBUTES)));
        uint8_t tested;
        if (needs_x_high(cell16(s, CELL_X), cell16(s, CELL_Y), &tested))
            set_x_high_bit(s, at, at);
        from = (uint16_t)(cell16(s, CELL_FROM) + SPRITE_BYTES);
        set_cell16(s, CELL_FROM, from);
        at = (uint16_t)(at + SPRITE_BYTES);
        set_cell8(s, CELL_COUNT, (uint8_t)(cell8(s, CELL_COUNT) - 1));
    } while (cell8(s, CELL_COUNT) != 0);
    set_oam_next(s, (uint16_t)(oam_next(s) + bytes));
}

/* The drawn object's effect records (one for each of its eight effects, 11
 * bytes apart), those marked to go under it (bit 7 set) or over it, whose
 * effect shows. The first record is looked at whatever the offset. Returns
 * the offset the loop ends at, which the original leaves in A. */
static uint8_t draw_effects(Snes *s, bool under)
{
    uint8_t record = (uint8_t)draw_slot(s);
    do {
        uint8_t v = m_read8(s, RAM_BATTLE_OBJECT_EFFECT_SLOTS + record);
        if (v != 0 && ((v & EFFECT_UNDER) != 0) == under) {
            uint8_t effect = under ? (uint8_t)(v & 0x7F) : (uint8_t)(v & 0xBF);
            if (m_read8(s, RAM_BATTLE_EFFECT_SHOWN + effect))
                draw_effect_sprites(s, record);
        }
        record = (uint8_t)(record + EFFECT_RECORD_STRIDE);
    } while (record < EFFECT_RECORDS_END);
    return record;
}

/* $CF:EE2C: the object's animated marker, when it has one: its frame steps
 * every period frames through four, and the frame's sprite goes at the
 * object's position. */
static void draw_marker(Snes *s)
{
    uint16_t slot = draw_slot(s);
    if (!m_read8(s, RAM_BATTLE_MARKER_ON + slot))
        return;
    set_cell8(s, CELL_MARKER_X, m_read8(s, RAM_BATTLE_OBJECT_X + FIRST_OBJECT + slot));
    set_cell8(s, CELL_MARKER_Y, m_read8(s, RAM_BATTLE_OBJECT_Y + FIRST_OBJECT + slot));
    uint32_t countdown = RAM_BATTLE_MARKER_COUNTDOWN + slot, frame = RAM_BATTLE_MARKER_FRAME + slot;
    uint8_t left = (uint8_t)(m_read8(s, countdown) - 1);
    m_write8(s, countdown, left);
    if (left == 0) {
        m_write8(s, countdown, m_read8(s, RAM_BATTLE_MARKER_PERIOD + slot));
        uint8_t f = (uint8_t)(m_read8(s, frame) + 1);
        m_write8(s, frame, f);
        if (f == MARKER_FRAMES)
            m_write8(s, frame, 0);
    }
    uint8_t k = (uint8_t)((uint8_t)(m_read8(s, frame) << 2) + m_read8(s, RAM_BATTLE_MARKER_KIND + slot));
    uint16_t at = oam_next(s);
    m_write8(s, RAM_OAM_BUFFER_X + at, (uint8_t)(battle_marker_sprite(s, k, 0) + cell8(s, CELL_MARKER_X)));
    m_write8(s, RAM_OAM_BUFFER_Y + at, (uint8_t)(battle_marker_sprite(s, k, 1) + cell8(s, CELL_MARKER_Y)));
    m_write8(s, RAM_OAM_BUFFER_TILE + at, battle_marker_sprite(s, k, 2));
    m_write8(s, RAM_OAM_BUFFER_ATTRIBUTES + at,
             (uint8_t)(battle_marker_sprite(s, k, 3) | m_read8(s, RAM_BATTLE_SPRITE_ATTRIBUTES)));
    set_oam_next(s, (uint16_t)(at + SPRITE_BYTES));
}

/* $CF:EE9F: the object's four prepared number sprites, when it has them, with
 * their high-table byte. */
static void copy_prepared_sprites(Snes *s)
{
    uint16_t slot = draw_slot(s);
    if (!m_read8(s, RAM_BATTLE_PREPARED_SPRITES_ON + slot))
        return;
    uint16_t at = oam_next(s);
    uint8_t byte = m_read8(s, RAM_BATTLE_OAM_HIGH_OFFSETS + at);
    m_write8(s, RAM_OAM_HIGH_TABLE + byte, m_read8(s, RAM_BATTLE_NUMBER_SPRITE_HIGH + slot));
    uint16_t from = (uint8_t)(slot << 4);
    set_cell8(s, CELL_COUNT, PREPARED_SPRITES);
    do {
        for (uint16_t k = 0; k < SPRITE_BYTES; k++)
            m_write8(s, RAM_OAM_BUFFER + at + k, m_read8(s, RAM_BATTLE_NUMBER_SPRITES + from + k));
        at = (uint16_t)(at + SPRITE_BYTES);
        from = (uint16_t)(from + SPRITE_BYTES);
        set_cell8(s, CELL_COUNT, (uint8_t)(cell8(s, CELL_COUNT) - 1));
    } while (cell8(s, CELL_COUNT) != 0);
    set_oam_next(s, at);
}

// $CF:EDB8
/* What goes under the object: while numbers show, its prepared sprites;
 * otherwise, unless effects run or markers are held off, its marker; or else
 * its effects marked to go under. */
static void draw_underlays(Snes *s)
{
    if (m_read8(s, RAM_BATTLE_NUMBER_STEP)) {
        copy_prepared_sprites(s);
        return;
    }
    if (!m_read8(s, RAM_BATTLE_EFFECTS_RUNNING) && !m_read8(s, RAM_BATTLE_MARKERS_HELD)) {
        draw_marker(s);
        return;
    }
    draw_effects(s, true);
}

// $CF:EDF7
/* What goes over it: while effects run and no numbers show, its effects
 * marked to go over. Returns what the original leaves in A. */
static uint8_t draw_overlays(Snes *s)
{
    uint8_t running = m_read8(s, RAM_BATTLE_EFFECTS_RUNNING);
    if (!running)
        return running;
    uint8_t step = m_read8(s, RAM_BATTLE_NUMBER_STEP);
    if (step)
        return step;
    return draw_effects(s, false);
}

/* Build the draw list: each present object keyed by its Y, and a topmost
 * entry keyed $FE for those with one. Returns the last entry's index (16-bit;
 * $FFFF for an empty list). */
static uint16_t build_draw_list(Snes *s)
{
    uint16_t n = 0;
    for (uint16_t object = FIRST_OBJECT; object < OBJECTS_END; object++) {
        if (!m_read8(s, RAM_BATTLE_SLOT_PRESENT - FIRST_OBJECT + object))
            continue;
        m_write8(s, page_at(RAM_BATTLE_DRAW_LIST_DEPTH, n), m_read8(s, RAM_BATTLE_OBJECT_Y + object));
        m_write8(s, page_at(RAM_BATTLE_DRAW_LIST, n), (uint8_t)(object | LIST_MAIN));
        n++;
        uint8_t extra = m_read8(s, RAM_BATTLE_OBJECT_EXTRA_DRAW_ON - FIRST_OBJECT + object);
        m_write8(s, RAM_BATTLE_OBJECT_EXTRA_DRAW - FIRST_OBJECT + object, extra);
        if (extra) {
            m_write8(s, RAM_BATTLE_OBJECT_EXTRA_DRAW - FIRST_OBJECT + object, 1);
            m_write8(s, page_at(RAM_BATTLE_DRAW_LIST_DEPTH, n), TOPMOST_KEY);
            m_write8(s, page_at(RAM_BATTLE_DRAW_LIST, n), (uint8_t)object);
            n++;
        }
    }
    return (uint16_t)(n - 1);
}

/* Sort the list by key, largest first: the original's exchange sort, which
 * runs only when the last index's low byte isn't 0, with 16-bit bounds.
 * Returns where its outer index ends. */
static uint16_t sort_draw_list(Snes *s, uint16_t last)
{
    uint16_t i = 0;
    if ((uint8_t)last == 0)
        return i;
    do {
        uint16_t j = i;
        do {
            uint32_t ki = RAM_BATTLE_DRAW_LIST_DEPTH + i, kj = page_at(RAM_BATTLE_DRAW_LIST_DEPTH + 1u, j);
            uint8_t a = m_read8(s, ki), b = m_read8(s, kj);
            if (a < b) {
                m_write8(s, ki, b);
                m_write8(s, kj, a);
                uint32_t ei = RAM_BATTLE_DRAW_LIST + i, ej = page_at(RAM_BATTLE_DRAW_LIST + 1u, j);
                uint8_t e = m_read8(s, ei);
                m_write8(s, ei, m_read8(s, ej));
                m_write8(s, ej, e);
            }
            j++;
        } while (j != last);
        i++;
    } while (i != last);
    return i;
}

// $CF:E83A
/* Draw all the battle objects (see the header). `sp`: the stack pointer, where
 * the original's calls leave their return addresses. */
static void draw_objects(Snes *s, uint16_t sp)
{
    uint16_t last = build_draw_list(s);
    set_scratch16(s, BASE_Y, last);
    uint16_t end = sort_draw_list(s, last);
    m_write8(s, RAM_BATTLE_DRAW_LIST + 1u + end, 0xFF);
    m_write16(s, RAM_BATTLE_DRAW_OBJECT, 0);
    m_write16(s, RAM_BATTLE_DRAW_PART_COUNT, 0);
    m_write16(s, RAM_BATTLE_DRAW_LIST_POSITION, 0);
    set_oam_next(s, FIRST_FREE_OFFSET);
    for (;;) {
        uint8_t entry = m_read8(s, page_at(RAM_BATTLE_DRAW_LIST, m_read16(s, RAM_BATTLE_DRAW_LIST_POSITION)));
        if (entry & LIST_END)
            break;
        uint16_t object = entry & LIST_OBJECT;
        m_write8(s, RAM_BATTLE_DRAW_OBJECT, (uint8_t)object);
        bool shadow = true;
        if (!(m_read8(s, page_at(RAM_BATTLE_OBJECT_EXTRA_DRAW - FIRST_OBJECT, object)) & EXTRA_DRAWN)) {
            /* from here the object is read back (16-bit) where the original
             * reloads it: enough sprites reach it */
            object = draw_object(s);
            uint8_t parts = m_read8(s, page_at(RAM_BATTLE_OBJECT_PART_COUNT, object));
            if (parts != 0) {
                m_write8(s, RAM_BATTLE_DRAW_PART_COUNT, parts);
                draw_underlays(s);
                copy_object_tiles(s, sp, 0xE8F3);
                draw_sprite_parts(s);
                draw_overlays(s);
                object = draw_object(s);
                uint32_t extra = page_at(RAM_BATTLE_OBJECT_EXTRA_DRAW - FIRST_OBJECT, object);
                if (m_read8(s, extra)) {
                    m_write8(s, extra, EXTRA_DRAWN);
                    shadow = false;
                }
            }
        }
        if (shadow)
            draw_shadow(s, object);
        if (m_read8(s, RAM_BATTLE_EFFECTS_RUNNING)) {
            uint8_t e = m_read8(s, page_at(RAM_BATTLE_DRAW_LIST, m_read16(s, RAM_BATTLE_DRAW_LIST_POSITION)));
            if ((e & LIST_MAIN) && m_read8(s, RAM_BATTLE_TRAIL_ON - FIRST_OBJECT + (e & LIST_OBJECT))) {
                push_return(s, sp, 0xE920);
                step_trail(s, e & LIST_OBJECT, 0, (uint16_t)(sp - 2));
            }
        }
        m_write8(s, RAM_BATTLE_DRAW_LIST_POSITION, (uint8_t)(m_read8(s, RAM_BATTLE_DRAW_LIST_POSITION) + 1));
    }
}

/* ---- entries ------------------------------------------------------------------------- */

/* The drawing routines' page and bank: the direct page on battle_sprites and
 * data bank $7E; with A's high byte 0 for those that take it into an index. */
bool battle_sprite_page(Snes *s) { return s->cpu.d == (uint16_t)RAM_BATTLE_OBJECT_X && s->cpu.db == 0x7E; }
bool battle_sprite_page_ready(Snes *s) { return battle_sprite_page(s) && (s->cpu.a & 0xFF00) == 0; }
/* The whole drawing sets its own direct page. */
bool battle_sprite_bank_ready(Snes *s) { return s->cpu.db == 0x7E && (s->cpu.a & 0xFF00) == 0; }

static void return_a8(Snes *s, uint16_t a)
{
    s->cpu.a = a;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* Leaves the direct page at $0000 and X 0. */
void battle_draw_objects(Snes *s)
{
    draw_objects(s, s->cpu.s);
    s->cpu.d = 0x0000;
    s->cpu.x = 0x0000;
    m_rts(s);
}

/* X the object; A kept on the paths that don't draw, 0 on the one that does. */
void battle_draw_shadow(Snes *s)
{
    int r = draw_shadow(s, s->cpu.x);
    return_a8(s, r < 0 ? 0 : (uint16_t)((s->cpu.a & 0xFF00) | r));
}

/* X the object. */
void battle_step_object_effect(Snes *s) { return_a8(s, step_trail(s, s->cpu.x, s->cpu.a, s->cpu.s)); }
/* Entered with 16-bit A; leaves 8-bit A with its high byte 0. */
void battle_draw_sprite_parts(Snes *s) { return_a8(s, draw_sprite_parts(s)); }
void battle_draw_afterimage(Snes *s) { return_a8(s, draw_afterimage(s, s->cpu.s)); }

/* X and Y the source and destination in the data bank. */
void battle_copy_sprite_tiles(Snes *s)
{
    copy_sprite_tiles(s, (uint32_t)s->cpu.db << 16, s->cpu.x, s->cpu.y);
    m_rts(s);
}

/* The marker path leaves the direct page back on the drawing page. */
void battle_draw_object_underlays(Snes *s)
{
    draw_underlays(s);
    m_rts(s);
}

void battle_draw_object_overlays(Snes *s) { return_a8(s, (uint16_t)((s->cpu.a & 0xFF00) | draw_overlays(s))); }

/* X the effect record; leaves A 0 and the direct page, and X, on the drawing
 * page. */
void battle_draw_effect_sprites(Snes *s)
{
    draw_effect_sprites(s, s->cpu.x);
    s->cpu.d = (uint16_t)RAM_BATTLE_OBJECT_X;
    s->cpu.x = (uint16_t)RAM_BATTLE_OBJECT_X;
    return_a8(s, 0);
}
