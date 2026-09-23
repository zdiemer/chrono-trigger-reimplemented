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
#include "field/object.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

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
enum { LAYOUT_4, LAYOUT_8, LAYOUT_12, LAYOUT_24 };
static const Layout layouts[4] = { { 4, 1 }, { 8, 2 }, { 12, 3 }, { 24, 6 } };

static unsigned layout_kind(const GameState *g, FieldObject obj) { return field_object_sprite_layout(g, obj) & 3; }
static Layout layout_of(const GameState *g, FieldObject obj) { return layouts[layout_kind(g, obj)]; }

/* ---- The table's storage: the only code that knows its form ------------------------------ */

/* The table is in low RAM at SPRITE_TABLE: 128 entries of four bytes (X's low
 * byte, Y, tile, attributes), then the high table, two bits a sprite (X's
 * ninth bit, and large). The ranges' cursors are kept as the table's
 * addresses; here they are offsets into it. */
enum { SPRITE_TABLE = 0x0700, OAM_ENTRY = 4, ENTRY_Y = 1, HIDDEN_Y = 0xE0 };
enum { X_NINTH_BIT = 0x01, LARGE_BIT = 0x02, BITS_A_SPRITE = 2, SPRITES_A_HIGH_BYTE = 4 };

enum { MIDDLE, BACK, FRONT, RANGES };   /* the ranges, as field_sprite_range_* index them */

/* Each range's first entry and the entry after its last, and its first high
 * table byte, as offsets into the table: the front range entries 4-27, the
 * middle 28-103, the back 104-127. */
static const uint16_t range_first_entry[RANGES] = { 0x070, 0x1A0, 0x010 };
static const uint16_t range_limit[RANGES] = { 0x1A0, 0x200, 0x070 };
static const uint16_t range_first_high[RANGES] = { 0x207, 0x21A, 0x201 };

static uint16_t range_next(const GameState *g, unsigned r) { return (uint16_t)(field_sprite_range_next(g, r) - SPRITE_TABLE); }
static void set_range_next(GameState *g, unsigned r, uint16_t at) { set_field_sprite_range_next(g, r, (uint16_t)(at + SPRITE_TABLE)); }
static uint16_t range_high(const GameState *g, unsigned r) { return (uint16_t)(field_sprite_range_high(g, r) - SPRITE_TABLE); }
static void set_range_high(GameState *g, unsigned r, uint16_t at) { set_field_sprite_range_high(g, r, (uint16_t)(at + SPRITE_TABLE)); }

/* For the original's code and shadow mode (field/scratch.h): the object's
 * sprite count, which it counts down in field_sprites_left as it sends them. */
static void leave_sprites_left(GameState *g) { set_field_sprites_left(g, 0); }

/* The table's entries are streamed through the work RAM port, in bank $7E. */
static void open_table_stream(GameState *g) { hw_port_seek_bank(g, 0); }

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
        set_field_sprite_table_byte(g, (uint16_t)(high + b), bits);
    }
    set_range_high(g, range, (uint16_t)(high + count / SPRITES_A_HIGH_BYTE));

    uint16_t entry = range_next(g, range);
    hw_port_seek_address(g, (uint16_t)(entry + SPRITE_TABLE));
    set_range_next(g, range, (uint16_t)(entry + count * OAM_ENTRY));
    for (unsigned n = 0; n < count; n++) {
        hw_port_write(g, (uint8_t)sprites[n].x);
        hw_port_write(g, sprites[n].y);
        hw_port_write(g, sprites[n].tile);
        hw_port_write(g, sprites[n].attributes);
    }
    leave_sprites_left(g);
}

/* Hide the entries each range used last frame beyond this frame's last, and
 * remember where this frame's ended. (The cursors are compared as the
 * addresses they are kept as.) */
static void hide_leftovers(GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++) {
        uint16_t next = field_sprite_range_next(g, r);
        uint16_t limit = (uint16_t)(range_limit[r] + SPRITE_TABLE);
        for (uint16_t entry = next; entry < field_sprite_range_end(g, r); entry = (uint16_t)(entry + OAM_ENTRY)) {
            set_field_sprite_table_byte(g, (uint16_t)(entry - SPRITE_TABLE + ENTRY_Y), HIDDEN_Y);
            if ((uint16_t)(entry + OAM_ENTRY) >= limit)
                break;
        }
        set_field_sprite_range_end(g, r, next);
    }
}

/* ---- The records' storage ----------------------------------------------------------------- */

/* A record is 8 bytes: the sprite's screen X (its low byte), its screen Y in
 * the table's form, its X offset from the object's screen position (a word),
 * its Y offset, and its tile and attributes. The object's sprites' high table
 * bits are kept apart, in three words for up to 24 sprites. The frame data
 * the records are rebuilt from lies at the same offsets. */
enum { RECORD = 8, RECORD_X = 0, RECORD_Y = 1, RECORD_X_OFFSET = 2, RECORD_Y_OFFSET = 4, RECORD_TILE = 6,
       RECORD_ATTRIBUTES = 7 };

static unsigned record_of(const GameState *g, FieldObject obj, unsigned n)
{
    return (uint16_t)(field_object_sprite_records(g, obj) + n * RECORD);
}

static uint16_t record_word(const GameState *g, unsigned at)
{
    uint8_t low = field_sprite_records_byte(g, at);
    return (uint16_t)(field_sprite_records_byte(g, at + 1) << 8 | low);
}

static void set_record_word(GameState *g, unsigned at, uint16_t v)
{
    set_field_sprite_records_byte(g, at, (uint8_t)v);
    set_field_sprite_records_byte(g, at + 1, (uint8_t)(v >> 8));
}

static uint16_t frame_word(const GameState *g, unsigned at)
{
    uint8_t low = field_sprite_frame_data_byte(g, at);
    return (uint16_t)(field_sprite_frame_data_byte(g, at + 1) << 8 | low);
}

static uint16_t object_high_word(const GameState *g, FieldObject obj, unsigned w)
{
    return w == 0 ? field_object_sprite_high(g, obj) : w == 1 ? field_object_sprite_high_more(g, obj)
                                                             : field_object_sprite_high_last(g, obj);
}

/* The object's sprites as a list, from its records and its high bits. */
static unsigned object_sprites(const GameState *g, FieldObject obj, Sprite *out)
{
    Layout layout = layout_of(g, obj);
    for (unsigned n = 0; n < layout.sprites; n++) {
        unsigned record = record_of(g, obj, n);
        uint8_t high = (uint8_t)(object_high_word(g, obj, n / 8) >> (8 * ((n / 4) & 1)));
        uint8_t pair = (uint8_t)(high >> (BITS_A_SPRITE * (n % SPRITES_A_HIGH_BYTE)));
        uint16_t nine = (uint16_t)(field_sprite_records_byte(g, record + RECORD_X) | (pair & X_NINTH_BIT) << 8);
        out[n] = (Sprite){ screen_coordinate_of(nine), field_sprite_records_byte(g, record + RECORD_Y),
                           field_sprite_records_byte(g, record + RECORD_TILE),
                           field_sprite_records_byte(g, record + RECORD_ATTRIBUTES), (pair & LARGE_BIT) != 0 };
    }
    return layout.sprites;
}

/* ---- Drawing an object --------------------------------------------------------------------- */

enum { PRIORITY_RANGE = 0x0C, PRIORITY_BACK = 0x04 };   /* field_object_priority_mode */

/* The range an object's sprites go in, by its priority. */
static unsigned range_of(const GameState *g, FieldObject obj)
{
    uint8_t priority = field_object_priority_mode(g, obj) & PRIORITY_RANGE;
    return priority == 0 ? MIDDLE : (priority & PRIORITY_BACK) ? BACK : FRONT;
}

// $C0:B309
/* Draw the object's sprites into its range. */
static void draw_object(GameState *g, FieldObject obj)
{
    Sprite sprites[MOST_SPRITES];
    unsigned count = object_sprites(g, obj, sprites);
    write_sprites(g, range_of(g, obj), sprites, count);
}

/* ---- Placing the 4-sprite layout ------------------------------------------------------------ */

/* Every field sprite is large. */
enum { ALL_LARGE = 0xAA, SPRITES_PLACED = 4 };

/* Whether a sprite whose Y comes round the screen shows, by where the object
 * is: an object above the screen's top edge shows only the sprites whose Y
 * came round past 0 and to $E0 or more; one in the screen's lower half hides
 * the sprites that came round; one in its upper half hides those whose Y went
 * past $7F to below $E0. The original writes a fifth sprite's Y for an object
 * above the top, which the port writes too. */
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
    original_cell16(g, 0xD9, field_object_sprite_records(g, obj));
    original_cell8(g, 0xE5, (uint8_t)(ninth_bits >> BITS_A_SPRITE));
    original_cell8(g, 0xE6, (uint8_t)(ninth_bits & X_NINTH_BIT));
}

// $C0:B788, $C0:B8CA
/* Place the object's four sprites at its screen position: each sprite's X
 * and Y are its offset from the object plus the object's screen position,
 * and the X's ninth bits go into the high bits `high_of`'s table keeps.
 * `reload` first takes the offsets, tiles and attributes from the object's
 * new frame. */
static void place_sprites(GameState *g, FieldObject obj, FieldObject high_of, bool reload)
{
    ScreenPoint at = object_screen_position(g, obj);
    uint8_t ninth_bits = 0;
    for (int n = SPRITES_PLACED - 1; n >= 0; n--) {
        unsigned record = record_of(g, obj, (unsigned)n);
        if (reload)
            set_record_word(g, record + RECORD_X_OFFSET, frame_word(g, record + RECORD_X_OFFSET));
        uint16_t x = (uint16_t)(record_word(g, record + RECORD_X_OFFSET) + (uint16_t)at.x);
        set_field_sprite_records_byte(g, record + RECORD_X, (uint8_t)x);
        ninth_bits = (uint8_t)(ninth_bits << BITS_A_SPRITE | ((x >> 8) & X_NINTH_BIT));
    }
    set_field_object_sprite_high(g, high_of,
                                 (uint16_t)((field_object_sprite_high(g, high_of) & 0xFF00) | (ninth_bits | ALL_LARGE)));

    ScreenHalf half = screen_half(at.y);
    unsigned count = half == SCREEN_OFF_TOP ? SPRITES_PLACED + 1 : SPRITES_PLACED;
    for (unsigned n = 0; n < count; n++) {
        unsigned record = record_of(g, obj, n);
        if (reload && n < SPRITES_PLACED)
            set_field_sprite_records_byte(g, record + RECORD_Y_OFFSET,
                                          field_sprite_frame_data_byte(g, record + RECORD_Y_OFFSET));
        uint8_t offset = field_sprite_records_byte(g, record + RECORD_Y_OFFSET);
        set_field_sprite_records_byte(g, record + RECORD_Y, sprite_y(half, offset, (uint8_t)at.y));
    }
    if (reload)
        for (unsigned n = 0; n < SPRITES_PLACED; n++) {
            unsigned record = record_of(g, obj, n);
            set_record_word(g, record + RECORD_TILE, frame_word(g, record + RECORD_TILE));
        }
    leave_place(g, obj, ninth_bits);
}

/* ---- What an object's records need ------------------------------------------------------- */

/* The records' state, as field_object_sprite_state keeps it: whether they
 * have been built since the object came into view, and a count of the
 * frame's sprites the frame builder has changed since (the original keeps
 * the first as the byte's top bit). */
typedef struct {
    bool built;
    uint8_t changed;
} RecordsState;

enum { RECORDS_BUILT_BIT = 0x80 };

static RecordsState records_state(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_sprite_state(g, obj);
    return (RecordsState){ (stored & RECORDS_BUILT_BIT) != 0, (uint8_t)(stored & ~RECORDS_BUILT_BIT) };
}

static void mark_records_built(GameState *g, FieldObject obj) { set_field_object_sprite_state(g, obj, RECORDS_BUILT_BIT); }

typedef enum { RECORDS_NOTHING, RECORDS_PLACE, RECORDS_REBUILD } RecordsWork;

/* What an object's records need: nothing to draw, only placing, or a
 * rebuild. The 4-sprite layout places when nothing changed, rebuilds
 * otherwise; the 8- and 12-sprite ones rebuild once at least 2 or 3 of their
 * sprites changed, and place with fewer (or where not built yet, draw
 * nothing). */
static uint8_t rebuild_after(unsigned layout) { return layout == LAYOUT_4 ? 1 : layout == LAYOUT_8 ? 2 : 3; }

static RecordsWork records_work(const GameState *g, FieldObject obj)
{
    RecordsState st = records_state(g, obj);
    uint8_t at_least = rebuild_after(layout_kind(g, obj));
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
    switch (layout_kind(g, obj)) {
    case LAYOUT_4: return work != RECORDS_REBUILD;
    case LAYOUT_8:
    case LAYOUT_12: return work == RECORDS_NOTHING;
    default: return false;
    }
}

/* ---- Where it meets the original ---------------------------------------------------------- */

/* The records' routines, and the sprite table's steps that are entries of
 * their own: called as the original calls them, going on where each came
 * back with 8-bit A and 16-bit X on the field's page and the bank it had,
 * and stopping at the call's return otherwise. */
enum { JSR_SIZE = 3 };   /* a call's return is the instruction after it */

static bool call_out(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    uint8_t bank = s->cpu.db;
    m_jsr_dc(s, site, routine, dont_care);
    if (field_machine_back(s, false, FIELD_PAGE, bank))
        return true;
    m_stop_at_own(s, site + JSR_SIZE, 0);
    return false;
}

/* The rebuilding and placing routines read the object in X. */
enum { RECORDS_DONT_CARE = DC_A | DC_N | DC_Z | DC_C };

/* Each layout's routines, and where the records' update calls them from: the
 * placing, the rebuilding, and the release of the old frame's sprites after
 * a rebuild (at the rebuild's call site plus a call). The 4-sprite layout's
 * placing and rebuilding are entries of this file. */
static const uint32_t place_sites[3] = { 0xC0B732, 0xC0B759, 0xC0B780 };
static const uint32_t place_routines[3] = { 0xC0B788, 0xC0BA65, 0xC0BFF2 };
static const uint32_t rebuild_sites[3] = { 0xC0B71F, 0xC0B744, 0xC0B76B };
static const uint32_t rebuild_routines[3] = { 0xC0B8CA, 0xC0BCDC, 0xC0C2BF };
static const uint32_t release_sites[3] = { 0xC0B722, 0xC0B747, 0xC0B76E };
static const uint32_t release_routines[3] = { 0xC0E9E2, 0xC0E9FF, 0xC0EA1F };
enum { POSITION_SPRITES = 0xC0B788 };

/* The object in X (16-bit); its high table bits are the current object's.
 * The overflow is that of the last sprite's Y, which the callers read. */
static void place_and_leave_overflow(Snes *s, FieldObject obj, FieldObject high_of, bool reload);

static bool records_in_bank(const GameState *g, FieldObject obj);

/* Placing, as C and timed as the entry it is where the entry's guard would
 * take the object, and called otherwise; the rest are the original's (the
 * 4-sprite rebuild is an entry too, but the release after it reads the X the
 * original's rebuild leaves, which the port doesn't make). */
static bool records_step(Snes *s, uint32_t site, uint32_t routine)
{
    GameState *g = state_of(s);
    uint16_t current = field_current_object(g);
    FieldObject obj = (FieldObject)(current >> 1);
    if (routine != POSITION_SPRITES || !field_names_object(current) || !records_in_bank(g, obj))
        return call_out(s, site, routine, RECORDS_DONT_CARE);
    NATIVE_TIME_AS(s, POSITION_SPRITES, place_and_leave_overflow(s, obj, obj, false));
    return true;
}

/* What bringing the records up to date came to. */
typedef enum { RECORDS_STOPPED, RECORDS_NONE_TO_DRAW, RECORDS_READY } RecordsOutcome;

// $C0:B701
/* Bring the current object's records up to date for drawing, for the 4-, 8-
 * and 12-sprite layouts (the 24-sprite one is still the original's, from
 * $C0:C73A). */
static RecordsOutcome bring_records_up_to_date(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    unsigned layout = layout_kind(g, obj);
    RecordsWork work = records_work(g, obj);
    s->cpu.x = field_current_object(g);
    if (work == RECORDS_PLACE && !records_step(s, place_sites[layout], place_routines[layout]))
        return RECORDS_STOPPED;
    if (work == RECORDS_REBUILD) {
        if (!records_step(s, rebuild_sites[layout], rebuild_routines[layout]) ||
            !call_out(s, release_sites[layout], release_routines[layout], RECORDS_DONT_CARE))
            return RECORDS_STOPPED;
        mark_records_built(g, (FieldObject)field_current_object_slot(s));
    }
    return work == RECORDS_NOTHING ? RECORDS_NONE_TO_DRAW : RECORDS_READY;
}

/* The carry says there was nothing to draw. */
void field_update_object_sprites(Snes *s)
{
    GameState *g = state_of(s);
    if (layout_kind(g, (FieldObject)field_current_object_slot(s)) == LAYOUT_24) {
        s->cpu.x = field_current_object(g);
        m_stop_at(s, 0xC0C73A, DC_A | DC_V | DC_C);
        return;
    }
    RecordsOutcome r = bring_records_up_to_date(s);
    if (r == RECORDS_STOPPED)
        return;
    m_set_flag(s, CPU_FLAG_C, r == RECORDS_NONE_TO_DRAW);
    m_rts(s);
}

/* The overflow of the last sprite's step to the next record, which the
 * callers read. */
static bool records_end_overflow(const GameState *g, FieldObject obj)
{
    uint16_t last = (uint16_t)(field_object_sprite_records(g, obj) + (layout_of(g, obj).sprites - 1) * RECORD);
    return field_add_overflows16(last, RECORD);
}

/* The records, brought up to date as the original's draw does: as C where
 * that calls nothing, and by calling the records' entry otherwise. */
enum { UPDATE_RECORDS = 0xC0B701, DRAW = 0xC0B309 };

static RecordsOutcome records_for_draw(Snes *s)
{
    GameState *g = state_of(s);
    if (records_work_is_own(g, (FieldObject)field_current_object_slot(s)))
        return bring_records_up_to_date(s);
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
    RecordsOutcome r = records_for_draw(s);
    if (r == RECORDS_STOPPED)
        return;
    FieldObject obj = (FieldObject)field_current_object_slot(s);
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
    FieldObject obj = field_object_of_link(link);
    return field_names_object(link) && records_in_bank(g, obj) && records_work_is_own(g, obj);
}

static bool every_draw_is_own(const GameState *g)
{
    unsigned draws = 0;
    for (int row = FIELD_ROWS - 1; row >= 0; row--)
        for (FieldLink link = row_first_object(g, (unsigned)row); field_link_is_object(link);
             link = object_next_in_row(g, field_object_of_link(link)))
            if (++draws > MOST_DRAWS || !draw_is_own(g, link))
                return false;
    return true;
}

static void draw_as_c(Snes *s, FieldObject obj)
{
    RecordsOutcome r = bring_records_up_to_date(s);
    if (r == RECORDS_READY)
        NATIVE_TIME_AS(s, DRAW, draw_object(state_of(s), obj));
    leave_draw_flags(s, r, obj);
}

/* The draw's call sites: a row's first object, and the rest. The draw reads
 * Y and the carry through; nothing else it is entered with. */
enum { DRAW_DONT_CARE = DC_A | DC_X | DC_N | DC_Z };
static const uint32_t draw_sites[2] = { 0xC0B2AF, 0xC0B2BB };

/* The original counts the rows in Y, a row's number doubled, and a draw it
 * calls can move Y on; the building goes on from what the draw left. */
static bool draw(Snes *s, FieldObject obj, uint16_t *row_y, unsigned site, bool as_c)
{
    object_make_current(state_of(s), obj);
    if (as_c) {
        draw_as_c(s, obj);
        return true;
    }
    s->cpu.y = *row_y;
    if (!call_out(s, draw_sites[site], DRAW, DRAW_DONT_CARE))
        return false;
    *row_y = s->cpu.y;
    return true;
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
void field_build_sprite_table(Snes *s)
{
    GameState *g = state_of(s);
    start_ranges(g);
    open_table_stream(g);
    leave_current_object_as_byte(g);
    bool as_c = every_draw_is_own(g);
    uint16_t row_y = 2 * (FIELD_ROWS - 1);
    do {
        FieldLink link = row_first_object(g, row_y / 2);
        for (unsigned site = 0; field_link_is_object(link); site = 1) {
            FieldObject obj = field_object_of_link(link);
            if (!draw(s, obj, &row_y, site, as_c))
                return;
            link = object_next_in_row(g, obj);
        }
        row_y = (uint16_t)(row_y - 2);
    } while ((int16_t)row_y >= 0);
    hide_leftovers(g);
    m_rts(s);
}

/* Placing, and the overflow of the last sprite's Y, which the callers read. */
static void place_and_leave_overflow(Snes *s, FieldObject obj, FieldObject high_of, bool reload)
{
    GameState *g = state_of(s);
    place_sprites(g, obj, high_of, reload);
    ScreenPoint at = object_screen_position(g, obj);
    unsigned last = screen_half(at.y) == SCREEN_OFF_TOP ? SPRITES_PLACED : SPRITES_PLACED - 1;
    uint8_t offset = field_sprite_records_byte(g, record_of(g, obj, last) + RECORD_Y_OFFSET);
    m_set_flag(s, CPU_FLAG_V, field_add_overflows8(offset, (uint8_t)at.y));
}

static void place_entry(Snes *s, bool reload)
{
    place_and_leave_overflow(s, (FieldObject)field_object_in_x(s), (FieldObject)field_current_object_slot(s), reload);
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

static bool in_table(uint16_t cursor) { return cursor >= SPRITE_TABLE && cursor <= LOW_RAM_END - LARGEST_DRAW; }

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
enum { RECORDS_ROOM = 0x10000 - 0x4BC0, LARGEST_LAYOUT = MOST_SPRITES * RECORD };

static bool records_in_bank(const GameState *g, FieldObject obj)
{
    return (uint32_t)field_object_sprite_records(g, obj) + LARGEST_LAYOUT + RECORD <= RECORDS_ROOM;
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

bool field_sprites_place_object(Snes *s)
{
    if (!field_current_object_low_bank(s) || !field_names_object(s->cpu.x))
        return false;
    vars_peek(true);
    bool ok = records_in_bank(state_of(s), (FieldObject)field_object_in_x(s));
    vars_peek(false);
    return ok;
}
