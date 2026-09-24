/* Field objects' sprites: the frame's sprite table.
 *
 * Every frame the field builds the OAM image (field_sprite_table, which the
 * vblank uploads) from the objects in view, walking the rows from the bottom
 * of the screen up (field/object_update.c) so that nearer objects come first
 * and are drawn over the ones behind them. The table is kept in three ranges
 * by an object's priority: the objects drawn in front of everything, the
 * ordinary ones, and those drawn behind. Each range fills from its start, and
 * the entries a range used last frame and not this one are hidden.
 *
 * An object's sprites are its sprite records (field_sprite_records, from the
 * object's offset): 4, 8, 12 or 24 of them by the object's layout, each a
 * sprite's place on the screen, its offset from the object's, and its tile and
 * attributes. Before it is drawn an object brings its records up to date:
 * only their screen positions where only the object moved, or rebuilt from
 * its frame's data after the frame builder drew a new frame. The 4-sprite
 * layout's placing is this file's (place_sprites); the rebuilding, and the
 * larger layouts' placing, are still the original's routines.
 *
 * An object's sprites are read into a list of Sprite, whose X is a signed
 * screen coordinate, and one function writes a list into the table
 * (write_sprites): it alone knows the table's form, a byte of X and a ninth
 * bit apart, and streams the entries through the work RAM port. The lists are
 * an object's, not a range's: a range's would be up to 128 sprites of locals,
 * more than a build for the 65816 can keep. */
#include "field/sprites.h"
#include "field/field.h"
#include "field/object.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* A sprite as the table shows it. Its Y is already in the table's form (the
 * records hold it that way, a line at $E0 or past hiding it); its X is a
 * screen coordinate. */
typedef struct {
    int16_t x;
    uint8_t y, tile, attributes;
    bool large;
} Sprite;

enum { MOST_SPRITES = 24 };

/* The layouts: how many sprites, and high table bytes (four sprites a byte). */
typedef struct {
    uint8_t sprites, high_bytes;
} Layout;
static const Layout layouts[4] = { [LAYOUT_4] = { 4, 1 }, [LAYOUT_8] = { 8, 2 }, [LAYOUT_12] = { 12, 3 },
                                   [LAYOUT_24] = { 24, 6 } };

static Layout layout_of(const GameState *g, FieldObject obj) { return layouts[object_sprite_layout(g, obj)]; }

/* ---- The table's storage: the only code that knows its form ------------------------------ */

/* The table is in low RAM at FIELD_SPRITE_TABLE_ADDRESS: 128 entries of four bytes (X's low
 * byte, Y, tile, attributes), then the high table, two bits a sprite (X's
 * ninth bit, and large). The ranges' cursors are kept as the table's
 * addresses; here they are offsets into it. */
enum { OAM_ENTRY = 4, ENTRY_Y = 1, HIDDEN_Y = 0xE0 };
enum { X_NINTH_BIT = 0x01, LARGE_BIT = 0x02, BITS_A_SPRITE = 2, SPRITES_A_HIGH_BYTE = 4 };

enum { RANGES = 3 };   /* by SpriteRange, as field_sprite_range_* index them */

/* Each range's first entry and the entry after its last, and its first high
 * table byte, as offsets into the table: the front range entries 4-27, the
 * middle 28-103, the back 104-127. */
static const uint16_t range_first_entry[RANGES] = { 0x070, 0x1A0, 0x010 };
static const uint16_t range_limit[RANGES] = { 0x1A0, 0x200, 0x070 };
static const uint16_t range_first_high[RANGES] = { 0x207, 0x21A, 0x201 };

static uint16_t range_next(const GameState *g, unsigned r) { return (uint16_t)(field_sprite_range_next(g, r) - FIELD_SPRITE_TABLE_ADDRESS); }
static void set_range_next(GameState *g, unsigned r, uint16_t at) { set_field_sprite_range_next(g, r, (uint16_t)(at + FIELD_SPRITE_TABLE_ADDRESS)); }
static uint16_t range_high(const GameState *g, unsigned r) { return (uint16_t)(field_sprite_range_high(g, r) - FIELD_SPRITE_TABLE_ADDRESS); }
static void set_range_high(GameState *g, unsigned r, uint16_t at) { set_field_sprite_range_high(g, r, (uint16_t)(at + FIELD_SPRITE_TABLE_ADDRESS)); }

/* For the original's code and shadow mode (field/scratch.h): the object's
 * sprite count, which it counts down in field_sprites_left as it sends them. */
static void leave_sprites_left(GameState *g) { set_field_sprites_left(g, 0); }

/* The table's entries are streamed through the work RAM port, in bank $7E. */
static void open_table_stream(GameState *g) { hw_port_seek_bank(g, 0); }

/* For the original's memory and shadow mode: a range that more sprites fill
 * than it has room for runs its high table cursor on past the table's end,
 * and the original writes the high bits there too, into whatever low RAM
 * follows the table (the table's entries run on the same way, through the
 * port). The port writes the same bytes, by address, since they are no
 * variable's. A build without the original's memory has nothing past the
 * table, and a range that full loses its last sprites' high bits. */
static void leave_stray_high_bits(GameState *g, uint16_t at, uint8_t bits)
{
    wram_write8(g, 0x7E0000u | (uint16_t)(FIELD_SPRITE_TABLE_ADDRESS + at), bits);
}

static void set_high_bits(GameState *g, uint16_t at, uint8_t bits)
{
    if (at < FIELD_SPRITE_TABLE_BYTES)
        set_field_sprite_table_byte(g, at, bits);
    else
        ORIGINAL_EFFECT(leave_stray_high_bits(g, at, bits));
}

/* Write an object's sprites into its range: their high table bits at the
 * range's high table cursor, and their entries from its next entry; and move
 * the range on past them. */
static void write_sprites(GameState *g, unsigned range, const Sprite *sprites, unsigned count)
{
    uint16_t high = range_high(g, range);
    for (unsigned b = 0; b < count / SPRITES_A_HIGH_BYTE; b++) {
        uint8_t bits = 0;
        for (unsigned i = 0; i < SPRITES_A_HIGH_BYTE; i++) {
            const Sprite *s = &sprites[b * SPRITES_A_HIGH_BYTE + i];
            uint8_t pair = (uint8_t)((((uint16_t)s->x >> 8) & X_NINTH_BIT) | (s->large ? LARGE_BIT : 0));
            bits = (uint8_t)(bits | pair << (BITS_A_SPRITE * i));
        }
        set_high_bits(g, (uint16_t)(high + b), bits);
    }
    set_range_high(g, range, (uint16_t)(high + count / SPRITES_A_HIGH_BYTE));

    uint16_t entry = range_next(g, range);
    hw_port_seek_address(g, (uint16_t)(entry + FIELD_SPRITE_TABLE_ADDRESS));
    set_range_next(g, range, (uint16_t)(entry + count * OAM_ENTRY));
    for (unsigned n = 0; n < count; n++) {
        hw_port_write(g, (uint8_t)sprites[n].x);
        hw_port_write(g, sprites[n].y);
        hw_port_write(g, sprites[n].tile);
        hw_port_write(g, sprites[n].attributes);
    }
    ORIGINAL_EFFECT(leave_sprites_left(g));
}

/* Hide the entries each range used last frame beyond this frame's last, and
 * remember where this frame's ended. (The cursors are compared as the
 * addresses they are kept as.) */
static void hide_leftovers(GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++) {
        uint16_t next = field_sprite_range_next(g, r);
        uint16_t limit = (uint16_t)(range_limit[r] + FIELD_SPRITE_TABLE_ADDRESS);
        for (uint16_t entry = next; entry < field_sprite_range_end(g, r); entry = (uint16_t)(entry + OAM_ENTRY)) {
            set_field_sprite_table_byte(g, (uint16_t)(entry - FIELD_SPRITE_TABLE_ADDRESS + ENTRY_Y), HIDDEN_Y);
            if ((uint16_t)(entry + OAM_ENTRY) >= limit)
                break;
        }
        set_field_sprite_range_end(g, r, next);
    }
}

/* ---- The records' storage ----------------------------------------------------------------- */

/* One of an object's sprites as its records keep it (field_sprite_records, 8
 * bytes a sprite from the object's offset): its screen X's low byte, its
 * screen Y in the table's form, its X offset from the object's screen
 * position (a word), its Y offset, and its tile and attributes (its look).
 * The object's sprites' high table bits are kept apart, in three words for up
 * to 24 sprites. The frame's data a rebuild takes the records from
 * (field_sprite_frame_data) keeps the same sprite at the same place. These
 * functions are the only code that knows the layout. */
typedef struct {
    uint16_t at;   /* where the record is among the records */
} SpriteRecord;

enum { RECORD_BYTES = 8, RECORD_X = 0, RECORD_Y = 1, RECORD_X_OFFSET = 2, RECORD_Y_OFFSET = 4, RECORD_TILE = 6,
       RECORD_ATTRIBUTES = 7 };

static SpriteRecord sprite_record(const GameState *g, FieldObject obj, unsigned n)
{
    return (SpriteRecord){ (uint16_t)(object_sprite_records(g, obj) + n * RECORD_BYTES) };
}

static uint8_t record_byte(const GameState *g, SpriteRecord r, unsigned field)
{
    return field_sprite_records_byte(g, (unsigned)r.at + field);
}
static void set_record_byte(GameState *g, SpriteRecord r, unsigned field, uint8_t v)
{
    set_field_sprite_records_byte(g, (unsigned)r.at + field, v);
}
static uint16_t record_word(const GameState *g, SpriteRecord r, unsigned field)
{
    uint8_t low = record_byte(g, r, field);
    return (uint16_t)(record_byte(g, r, field + 1) << 8 | low);
}
static void set_record_word(GameState *g, SpriteRecord r, unsigned field, uint16_t v)
{
    set_record_byte(g, r, field, (uint8_t)v);
    set_record_byte(g, r, field + 1, (uint8_t)(v >> 8));
}

static uint8_t record_x(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_X); }
static void set_record_x(GameState *g, SpriteRecord r, uint8_t x) { set_record_byte(g, r, RECORD_X, x); }
static uint8_t record_y(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_Y); }
static void set_record_y(GameState *g, SpriteRecord r, uint8_t y) { set_record_byte(g, r, RECORD_Y, y); }
static uint16_t record_x_offset(const GameState *g, SpriteRecord r) { return record_word(g, r, RECORD_X_OFFSET); }
static void set_record_x_offset(GameState *g, SpriteRecord r, uint16_t v) { set_record_word(g, r, RECORD_X_OFFSET, v); }
static uint8_t record_y_offset(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_Y_OFFSET); }
static void set_record_y_offset(GameState *g, SpriteRecord r, uint8_t v) { set_record_byte(g, r, RECORD_Y_OFFSET, v); }
static uint8_t record_tile(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_TILE); }
static uint8_t record_attributes(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_ATTRIBUTES); }
static void set_record_look(GameState *g, SpriteRecord r, uint16_t tile_and_attributes)
{
    set_record_word(g, r, RECORD_TILE, tile_and_attributes);
}

/* The same sprite in the frame's data. */
static uint16_t frame_word(const GameState *g, SpriteRecord r, unsigned field)
{
    uint8_t low = field_sprite_frame_data_byte(g, (unsigned)r.at + field);
    return (uint16_t)(field_sprite_frame_data_byte(g, (unsigned)r.at + field + 1) << 8 | low);
}
static uint16_t frame_x_offset(const GameState *g, SpriteRecord r) { return frame_word(g, r, RECORD_X_OFFSET); }
static uint8_t frame_y_offset(const GameState *g, SpriteRecord r)
{
    return field_sprite_frame_data_byte(g, (unsigned)r.at + RECORD_Y_OFFSET);
}
static uint16_t frame_look(const GameState *g, SpriteRecord r) { return frame_word(g, r, RECORD_TILE); }


/* The object's sprites as a list, from its records and its high bits. */
static unsigned object_sprites(const GameState *g, FieldObject obj, Sprite *out)
{
    Layout layout = layout_of(g, obj);
    for (unsigned n = 0; n < layout.sprites; n++) {
        SpriteRecord r = sprite_record(g, obj, n);
        uint8_t pair = (uint8_t)(object_sprite_high_bits(g, obj, n) >> (BITS_A_SPRITE * (n % SPRITES_A_HIGH_BYTE)));
        uint16_t nine = (uint16_t)(record_x(g, r) | (pair & X_NINTH_BIT) << 8);
        out[n] = (Sprite){ screen_coordinate_of(nine), record_y(g, r), record_tile(g, r), record_attributes(g, r),
                           (pair & LARGE_BIT) != 0 };
    }
    return layout.sprites;
}

/* ---- Drawing an object --------------------------------------------------------------------- */


// $C0:B309
/* Draw the object's sprites into its range. */
static void draw_object(GameState *g, FieldObject obj)
{
    Sprite sprites[MOST_SPRITES];
    unsigned count = object_sprites(g, obj, sprites);
    write_sprites(g, object_sprite_range(g, obj), sprites, count);
}

/* ---- Placing the 4-sprite layout ------------------------------------------------------------ */

/* Every field sprite is large. */
enum { ALL_LARGE = 0xAA, SPRITES_PLACED = 4 };

/* Whether a sprite whose Y comes round the screen shows, by where the object
 * is: an object above the screen's top edge shows only the sprites whose Y
 * came round past 0 and to $E0 or more; one in the screen's lower half hides
 * the sprites that came round; one in its upper half hides those whose Y went
 * past $7F to below $E0. */
typedef enum { SCREEN_OFF_TOP, SCREEN_LOWER, SCREEN_UPPER } ScreenHalf;

enum { LOWER_HALF = 0x80 };

static ScreenHalf screen_half(int16_t screen_y)
{
    if (screen_y < 0)
        return SCREEN_OFF_TOP;
    return (screen_y & LOWER_HALF) ? SCREEN_LOWER : SCREEN_UPPER;
}

static uint8_t sprite_y(ScreenHalf half, uint8_t offset, uint8_t screen_y)
{
    uint16_t sum = (uint16_t)(offset + screen_y);
    uint8_t y = (uint8_t)sum;
    bool came_round = sum > 0xFF;
    switch (half) {
    case SCREEN_OFF_TOP:
        return came_round && y >= HIDDEN_Y ? y : HIDDEN_Y;
    case SCREEN_LOWER:
        return came_round && y >= HIDDEN_Y ? HIDDEN_Y : y;
    case SCREEN_UPPER:
        return y >= LOWER_HALF && y < HIDDEN_Y ? HIDDEN_Y : y;
    }
    return y;
}

/* For the original's code and shadow mode (field/scratch.h): placing leaves
 * the object's screen position ($C3, $C5, as the object's table keeps it),
 * its records' offset ($D9), and the ninth bits it gathered: all but the
 * first sprite's ($E5) and the first's ($E6). */
static void leave_place(GameState *g, FieldObject obj, uint8_t ninth_bits)
{
    original_cell16(g, 0xC3, field_object_screen_x(g, obj));
    original_cell16(g, 0xC5, (uint16_t)(field_object_screen_y(g, obj) & SCREEN_NINE_BITS));
    original_cell16(g, 0xD9, object_sprite_records(g, obj));
    original_cell8(g, 0xE5, (uint8_t)(ninth_bits >> BITS_A_SPRITE));
    original_cell8(g, 0xE6, (uint8_t)(ninth_bits & X_NINTH_BIT));
}

/* For the original's memory and shadow mode: for an object above the screen's
 * top the original places a fifth sprite's Y, in the record after the
 * object's four, which is the next block's first record (another object's,
 * placed again when that object is drawn) or past the records. The port writes
 * the same byte; the object's own sprites don't read it. */
static void leave_stray_fifth_sprite_y(GameState *g, FieldObject obj, int16_t screen_y)
{
    SpriteRecord fifth = sprite_record(g, obj, SPRITES_PLACED);
    set_record_y(g, fifth, sprite_y(SCREEN_OFF_TOP, record_y_offset(g, fifth), (uint8_t)screen_y));
}

// $C0:B788, $C0:B8CA
/* Place the object's four sprites at its screen position: each sprite's X
 * and Y are its offset from the object plus the object's screen position,
 * and the X's ninth bits go into the object's high bits. `reload` first takes
 * the offsets, tiles and attributes from the object's new frame. (Each pass
 * over the sprites is the original's, in its order: a frame's data past its
 * table can run on into the records.) */
static void place_sprites(GameState *g, FieldObject obj, bool reload)
{
    ScreenPoint at = object_screen_position(g, obj);
    uint8_t ninth_bits = 0;
    for (int n = SPRITES_PLACED - 1; n >= 0; n--) {
        SpriteRecord r = sprite_record(g, obj, (unsigned)n);
        if (reload)
            set_record_x_offset(g, r, frame_x_offset(g, r));
        uint16_t x = (uint16_t)(record_x_offset(g, r) + (uint16_t)at.x);
        set_record_x(g, r, (uint8_t)x);
        ninth_bits = (uint8_t)(ninth_bits << BITS_A_SPRITE | ((x >> 8) & X_NINTH_BIT));
    }
    object_set_first_sprites_high_bits(g, obj, (uint8_t)(ninth_bits | ALL_LARGE));

    ScreenHalf half = screen_half(at.y);
    for (unsigned n = 0; n < SPRITES_PLACED; n++) {
        SpriteRecord r = sprite_record(g, obj, n);
        if (reload)
            set_record_y_offset(g, r, frame_y_offset(g, r));
        set_record_y(g, r, sprite_y(half, record_y_offset(g, r), (uint8_t)at.y));
    }
    if (half == SCREEN_OFF_TOP)
        ORIGINAL_EFFECT(leave_stray_fifth_sprite_y(g, obj, at.y));
    if (reload)
        for (unsigned n = 0; n < SPRITES_PLACED; n++) {
            SpriteRecord r = sprite_record(g, obj, n);
            set_record_look(g, r, frame_look(g, r));
        }
    ORIGINAL_EFFECT(leave_place(g, obj, ninth_bits));
}

/* ---- What an object's records need ------------------------------------------------------- */


typedef enum { RECORDS_NOTHING, RECORDS_PLACE, RECORDS_REBUILD } RecordsWork;

/* What an object's records need: nothing to draw, only placing, or a
 * rebuild. The 4-sprite layout places when nothing changed, rebuilds
 * otherwise; the 8- and 12-sprite ones rebuild once at least 2 or 3 of their
 * sprites changed, and place with fewer (or where not built yet, draw
 * nothing). */
static uint8_t rebuild_after(unsigned layout) { return layout == LAYOUT_4 ? 1 : layout == LAYOUT_8 ? 2 : 3; }

static RecordsWork records_work(const GameState *g, FieldObject obj)
{
    RecordsState st = object_records_state(g, obj);
    uint8_t at_least = rebuild_after(object_sprite_layout(g, obj));
    if (!st.built && st.changed == 0)
        return RECORDS_NOTHING;
    if (st.built)
        return st.changed < at_least ? RECORDS_PLACE : RECORDS_REBUILD;
    return st.changed < at_least ? RECORDS_NOTHING : RECORDS_REBUILD;
}

/* Whether bringing the records up to date calls nothing of the original's:
 * they need nothing, or only placing, which is this file's own for the
 * 4-sprite layout. */
static bool records_work_is_own(const GameState *g, FieldObject obj)
{
    RecordsWork work = records_work(g, obj);
    switch (object_sprite_layout(g, obj)) {
    case LAYOUT_4: return work != RECORDS_REBUILD;
    case LAYOUT_8:
    case LAYOUT_12: return work == RECORDS_NOTHING;
    default: return false;
    }
}

/* ---- Where it meets the original ---------------------------------------------------------- */

/* The records' routines, and the sprite table's steps that are entries of
 * their own: called as the original calls them, going on where each came
 * back with 8-bit A and 16-bit X on the field's page and the bank it had. */
static bool call_out(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    return bridge_call(s, site, routine, dont_care, on_the_field_page(s, false));
}

/* The rebuilding and placing routines read the object in X. */
enum { RECORDS_DONT_CARE = DC_A | DC_N | DC_Z | DC_C };

/* The steps of bringing an object's records up to date, and each layout's
 * routine for each and where the update calls it from: the placing, the
 * rebuilding, and the release of the old frame's sprites after a rebuild.
 * The 4-sprite layout's placing and rebuilding are entries of this file. */
typedef enum { PLACE, REBUILD, RELEASE } RecordsStep;
static const struct {
    uint32_t place_site, place, rebuild_site, rebuild, release_site, release;
} records_calls[3] = {
    [LAYOUT_4] = { 0xC0B732, 0xC0B788, 0xC0B71F, 0xC0B8CA, 0xC0B722, 0xC0E9E2 },
    [LAYOUT_8] = { 0xC0B759, 0xC0BA65, 0xC0B744, 0xC0BCDC, 0xC0B747, 0xC0E9FF },
    [LAYOUT_12] = { 0xC0B780, 0xC0BFF2, 0xC0B76B, 0xC0C2BF, 0xC0B76E, 0xC0EA1F },
};

/* Placing, and the overflow of the last sprite's Y, which the callers read. */
static void place_and_leave_overflow(Snes *s, FieldObject obj, bool reload);

static bool records_in_bank(const GameState *g, FieldObject obj);

/* A step: the 4-sprite layout's placing as C, timed as the entry it is, where
 * the entry's guard would take the object; the rest are the original's. The
 * placing and the rebuilding are called with the object in X; the release
 * reads the X the original's rebuild leaves (which is why the 4-sprite
 * rebuild, an entry too, is called rather than run as C). */
static bool records_step(Snes *s, FieldObject obj, unsigned layout, RecordsStep step)
{
    if (step == PLACE && layout == LAYOUT_4 && records_in_bank(state_of(s), obj)) {
        NATIVE_TIME_AS(s, records_calls[LAYOUT_4].place, place_and_leave_overflow(s, obj, false));
        return true;
    }
    switch (step) {
    case PLACE:
        s->cpu.x = field_object_offset(obj);
        return call_out(s, records_calls[layout].place_site, records_calls[layout].place, RECORDS_DONT_CARE);
    case REBUILD:
        s->cpu.x = field_object_offset(obj);
        return call_out(s, records_calls[layout].rebuild_site, records_calls[layout].rebuild, RECORDS_DONT_CARE);
    default:
        return call_out(s, records_calls[layout].release_site, records_calls[layout].release, RECORDS_DONT_CARE);
    }
}

/* What bringing the records up to date came to. */
typedef enum { RECORDS_STOPPED, RECORDS_NONE_TO_DRAW, RECORDS_READY } RecordsOutcome;

// $C0:B701
/* Bring an object's records up to date for drawing, for the 4-, 8- and
 * 12-sprite layouts (the 24-sprite one is still the original's, from
 * $C0:C73A). The object is the current one, which the original's routines
 * read. */
static RecordsOutcome bring_records_up_to_date(Snes *s, FieldObject obj)
{
    GameState *g = state_of(s);
    SpriteLayout layout = object_sprite_layout(g, obj);
    RecordsWork work = records_work(g, obj);
    if (work == RECORDS_PLACE && !records_step(s, obj, layout, PLACE))
        return RECORDS_STOPPED;
    if (work == RECORDS_REBUILD) {
        if (!records_step(s, obj, layout, REBUILD) || !records_step(s, obj, layout, RELEASE))
            return RECORDS_STOPPED;
        object_mark_records_built(g, obj);
    }
    return work == RECORDS_NOTHING ? RECORDS_NONE_TO_DRAW : RECORDS_READY;
}

/* The current object's (16-bit X). The carry says there was nothing to
 * draw. */
void field_update_object_sprites(Snes *s)
{
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    if (object_sprite_layout(state_of(s), obj) == LAYOUT_24) {
        s->cpu.x = field_current_object(state_of(s));
        m_stop_at(s, 0xC0C73A, DC_A | DC_V | DC_C);
        return;
    }
    RecordsOutcome r = bring_records_up_to_date(s, obj);
    if (r == RECORDS_STOPPED)
        return;
    m_set_flag(s, CPU_FLAG_C, r == RECORDS_NONE_TO_DRAW);
    m_rts(s);
}

/* The overflow of the last sprite's step to the next record, which the
 * callers read. */
static bool records_end_overflow(const GameState *g, FieldObject obj)
{
    uint16_t last = sprite_record(g, obj, (unsigned)(layout_of(g, obj).sprites - 1)).at;
    return field_add_overflows16(last, RECORD_BYTES);
}

/* The records, brought up to date as the original's draw does: as C where
 * that calls nothing, and by calling the records' entry otherwise. */
enum { UPDATE_RECORDS = 0xC0B701, DRAW = 0xC0B309 };

static RecordsOutcome records_for_draw(Snes *s, FieldObject obj)
{
    if (records_work_is_own(state_of(s), obj))
        return bring_records_up_to_date(s, obj);
    if (!call_out(s, DRAW, UPDATE_RECORDS, DC_A | DC_X | DC_N | DC_Z))
        return RECORDS_STOPPED;
    return m_flag(s, CPU_FLAG_C) ? RECORDS_NONE_TO_DRAW : RECORDS_READY;
}

/* A draw's flags: the carry says there was nothing to draw, and after a
 * draw the overflow is that of the records' end. */
static void leave_draw_flags(Snes *s, RecordsOutcome r, FieldObject obj)
{
    if (r == RECORDS_READY)
        m_set_flag(s, CPU_FLAG_V, records_end_overflow(state_of(s), obj));
    m_set_flag(s, CPU_FLAG_C, r == RECORDS_NONE_TO_DRAW);
}

// $C0:B309
/* The current object's (16-bit X); Y is what the records' step left. */
void field_draw_object_sprites(Snes *s)
{
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    RecordsOutcome r = records_for_draw(s, obj);
    if (r == RECORDS_STOPPED)
        return;
    if (r == RECORDS_READY)
        draw_object(state_of(s), obj);
    leave_draw_flags(s, r, obj);
    m_rts(s);
}

/* The table's building draws its objects as C, each timed as the draw it is,
 * where no object's draw calls anything of the original's; otherwise it
 * calls the draw's entry for every object, as the original does. It is all
 * or none because shadow mode matches a bridge's calls to the original's in
 * order: drawing one object as C and calling the next would pair that call
 * with the original's draw of the first. So the rows are walked once first,
 * to see whether every draw is one of this file's. The ranges start at
 * their first entries every frame, and no layout this file draws can run a
 * range's cursors out of low RAM: 64 objects of 12 sprites from the back
 * range's start end at $14A0. Rows that name more draws than there are
 * objects (a link round in a circle) are left to the original. */
enum { MOST_DRAWS = FIELD_OBJECTS };

static bool draw_is_own(const GameState *g, FieldLink link)
{
    FieldObject obj = field_linked_object(link);
    return field_names_object(field_link_byte(link)) && records_in_bank(g, obj) && records_work_is_own(g, obj);
}

static bool every_draw_is_own(const GameState *g)
{
    unsigned draws = 0;
    for (int row = FIELD_ROWS - 1; row >= 0; row--)
        for (FieldLink link = row_first_object(g, (unsigned)row); field_link_is_object(link);
             link = object_next_in_row(g, field_linked_object(link)))
            if (++draws > MOST_DRAWS || !draw_is_own(g, link))
                return false;
    return true;
}

static void draw_as_c(Snes *s, FieldObject obj)
{
    RecordsOutcome r = bring_records_up_to_date(s, obj);
    if (r == RECORDS_READY)
        NATIVE_TIME_AS(s, DRAW, draw_object(state_of(s), obj));
    leave_draw_flags(s, r, obj);
}

/* The draw's call sites: a row's first object, and the rest. The draw reads
 * Y (the original's row counter, the row doubled) and the carry through;
 * nothing else it is entered with. */
enum { DRAW_DONT_CARE = DC_A | DC_X | DC_N | DC_Z };
static const uint32_t draw_sites[2] = { 0xC0B2AF, 0xC0B2BB };

/* Draw an object on a row. The original keeps its row count in Y, which the
 * draw it calls is handed and could move on; where it comes back other than
 * it went, the port stops at the call's return and the original goes on from
 * the row the draw left. */
static bool draw(Snes *s, FieldObject obj, unsigned row, unsigned site, bool as_c)
{
    object_make_current(state_of(s), obj);
    if (as_c) {
        draw_as_c(s, obj);
        return true;
    }
    uint16_t row_count = (uint16_t)(row * 2);
    s->cpu.y = row_count;
    if (!call_out(s, draw_sites[site], DRAW, DRAW_DONT_CARE))
        return false;
    if (s->cpu.y == row_count)
        return true;
    m_stop_at_own(s, draw_sites[site] + JSR_SIZE, 0);
    return false;
}

/* For the original's code and shadow mode (field/scratch.h): the building
 * keeps the current object as a byte in its word, whose high byte it clears. */
static void leave_current_object_as_byte(GameState *g)
{
    set_field_current_object(g, (uint16_t)(field_current_object(g) & 0x00FF));
}

/* The ranges start over each frame at their first entries. */
static void start_ranges(GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++) {
        set_range_high(g, r, range_first_high[r]);
        set_field_sprite_range_spare(g, r, 0);
        set_range_next(g, r, range_first_entry[r]);
    }
}

// $C0:B271
/* Build the frame's sprite table: every object in view, the rows from the
 * bottom of the screen up, then the ranges' leftovers hidden. */
void field_build_sprite_table(Snes *s)
{
    GameState *g = state_of(s);
    start_ranges(g);
    open_table_stream(g);
    ORIGINAL_EFFECT(leave_current_object_as_byte(g));
    bool as_c = every_draw_is_own(g);
    for (int row = FIELD_ROWS - 1; row >= 0; row--) {
        FieldLink link = row_first_object(g, (unsigned)row);
        for (unsigned site = 0; field_link_is_object(link); site = 1) {
            FieldObject obj = field_linked_object(link);
            if (!draw(s, obj, (unsigned)row, site, as_c))
                return;
            link = object_next_in_row(g, obj);
        }
    }
    hide_leftovers(g);
    m_rts(s);
}

static void place_and_leave_overflow(Snes *s, FieldObject obj, bool reload)
{
    GameState *g = state_of(s);
    place_sprites(g, obj, reload);
    ScreenPoint at = object_screen_position(g, obj);
    unsigned last = screen_half(at.y) == SCREEN_OFF_TOP ? SPRITES_PLACED : SPRITES_PLACED - 1;
    m_set_flag(s, CPU_FLAG_V, field_add_overflows8(record_y_offset(g, sprite_record(g, obj, last)), (uint8_t)at.y));
}

/* The object in X (16-bit), which the guard has made sure is the current
 * one, whose high table bits the original's placing writes. */
static void place_entry(Snes *s, bool reload)
{
    place_and_leave_overflow(s, (FieldObject)field_object_in_x(s), reload);
    m_rts(s);
}

void field_position_sprites(Snes *s) { place_entry(s, false); }
void field_load_and_position_sprites(Snes *s) { place_entry(s, true); }

/* ---- Guards ------------------------------------------------------------------------------- */

/* The table's cursors past its start and in low RAM, where the table is
 * reached through a data bank of 0 the same as in bank $7E (a busy frame
 * runs a range's cursors past the table's end, which the port writes as the
 * original does). Below it they would land on the building's own cells, and
 * past low RAM on the I/O registers; no frame has them there. A draw writes
 * at most LARGEST_DRAW bytes. */
enum { LOW_RAM_END = 0x2000, LARGEST_DRAW = MOST_SPRITES * OAM_ENTRY };

static bool in_table(uint16_t cursor)
{
    return cursor >= FIELD_SPRITE_TABLE_ADDRESS && cursor <= LOW_RAM_END - LARGEST_DRAW;
}

static bool cursors_in_table(const GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++)
        if (!in_table(field_sprite_range_high(g, r)) || !in_table(field_sprite_range_next(g, r)))
            return false;
    return true;
}

/* An object's records inside bank $7F, where the records are: past its end
 * the original reads bank $80, whose low half is low RAM and the I/O
 * registers, and reading those takes what the bus last held. The game puts
 * every object's records well inside it. */
enum { RECORDS_ROOM = 0x10000 - 0x4BC0, LARGEST_LAYOUT = MOST_SPRITES * RECORD_BYTES };

static bool records_in_bank(const GameState *g, FieldObject obj)
{
    return (uint32_t)object_sprite_records(g, obj) + LARGEST_LAYOUT + RECORD_BYTES <= RECORDS_ROOM;
}

bool field_sprites_draw_in_table(Snes *s)
{
    if (!field_current_object_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = cursors_in_table(g) && records_in_bank(g, (FieldObject)field_current_object_slot(s));
    vars_peek(false);
    return ok;
}

/* Placing: the object in X is the current object, whose high bits the
 * original writes (the game always places the current object). */
bool field_sprites_place_object(Snes *s)
{
    if (!field_current_object_low_bank(s) || !field_names_object(s->cpu.x))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = field_object_in_x(s) == field_current_object_slot(s) && records_in_bank(g, (FieldObject)field_object_in_x(s));
    vars_peek(false);
    return ok;
}
