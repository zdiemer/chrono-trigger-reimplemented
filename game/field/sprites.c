/* Field objects' sprites: the frame's sprite table.
 *
 * Every frame the field builds the OAM image (field_sprite_table, which the
 * vblank uploads) from the objects in view, walking the rows from the bottom
 * of the screen up (field/object_update.c) so that nearer objects come first.
 * The table is in three ranges by an object's priority: the objects drawn in
 * front of everything, the ordinary ones, and those drawn behind; each range
 * has its next entry and its next byte of the high table, and entries a range
 * used last frame and not this one are hidden.
 *
 * An object's sprites are 8-byte records (field_sprite_records, from the
 * object's offset): its sprite's X and Y on the screen, its offset from the
 * object's screen position, and its tile and attributes. There are 4, 8, 12
 * or 24 of them by the object's layout. Before it is drawn an object brings
 * its records up to date: only their screen positions where only the object
 * moved, or rebuilt from its frame's data after a new frame was built. The
 * entries go into the table through the work RAM port. */
#include "field/sprites.h"
#include "field/object.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { SPRITE_TABLE = 0x0700, OAM_ENTRY = 4, HIDDEN_Y = 0xE0 };
/* The ranges' cursors reach the table through a data bank of 0, which reaches
 * low RAM up to LOW_RAM_END; a draw writes at most LARGEST_DRAW bytes. */
enum { LOW_RAM_END = 0x2000, LARGEST_DRAW = 24 * OAM_ENTRY };
enum { RECORD = 8, RECORD_X = 0, RECORD_Y = 1, RECORD_X_OFFSET = 2, RECORD_Y_OFFSET = 4, RECORD_TILE = 6, RECORD_ATTRIBUTES = 7 };
enum { FRAME_X_OFFSET = 2, FRAME_Y_OFFSET = 4, FRAME_TILE = 6 };   /* a frame's sprites, in field_sprite_frame_data */
enum { MIDDLE, BACK, FRONT, RANGES };   /* the ranges, as field_sprite_range_* index them */
enum { PRIORITY_RANGE = 0x0C, PRIORITY_BACK = 0x04 };   /* field_object_priority_mode */

/* The high table bytes of an object's sprites, two bits a sprite: X's ninth
 * bit and large size. Every field sprite is large. */
enum { ALL_LARGE = 0xAA };

/* The layouts: how many sprites, and high table bytes. */
typedef struct {
    uint8_t sprites, high_bytes;
} Layout;
static const Layout layouts[4] = { { 4, 1 }, { 8, 2 }, { 12, 3 }, { 24, 6 } };

static Layout layout_of(const GameState *g, FieldObject obj) { return layouts[field_object_sprite_layout(g, obj) & 3]; }

enum { FIELD_ROWS = 64 };

/* For the original's code and shadow mode (field/scratch.h): the object's
 * sprite count, which it counts down in field_sprites_left as it sends them. */
static void leave_sprites_left(GameState *g) { set_field_sprites_left(g, 0); }

/* ---- Building the table ------------------------------------------------------------------ */

/* Where each range starts: its first entry, and its first high table byte. */
static const uint16_t range_first_entry[RANGES] = { 0x0770, 0x08A0, 0x0710 };
static const uint16_t range_first_high[RANGES] = { 0x0907, 0x091A, 0x0901 };
/* The entry after each range's last. */
static const uint16_t range_limit[RANGES] = { 0x08A0, 0x0900, 0x0770 };

static void start_ranges(GameState *g)
{
    static const unsigned order[RANGES] = { FRONT, MIDDLE, BACK };
    for (unsigned i = 0; i < RANGES; i++) {
        unsigned r = order[i];
        set_field_sprite_range_high(g, r, range_first_high[r]);
        set_field_sprite_range_spare(g, r, 0);
    }
    set_field_sprite_range_next(g, FRONT, range_first_entry[FRONT]);
    set_field_sprite_range_next(g, MIDDLE, range_first_entry[MIDDLE]);
    hw_port_seek_bank(g, 0);
    set_field_current_object(g, (uint16_t)(field_current_object(g) & 0x00FF));
    set_field_sprite_range_next(g, BACK, range_first_entry[BACK]);
}

/* Hide the entries each range used last frame beyond this frame's last, and
 * remember where this frame's ended. */
static void hide_leftovers(GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++) {
        uint16_t next = field_sprite_range_next(g, r);
        for (uint16_t entry = next; entry < field_sprite_range_end(g, r); entry = (uint16_t)(entry + OAM_ENTRY)) {
            set_field_sprite_table_byte(g, (unsigned)(uint16_t)(entry - SPRITE_TABLE) + 1, HIDDEN_Y);
            if ((uint16_t)(entry + OAM_ENTRY) >= range_limit[r])
                break;
        }
        set_field_sprite_range_end(g, r, next);
    }
}

// $C0:B309
/* Draw the object into its range: its high table bytes at the range's high
 * table pointer, and its sprites' X, Y, tile and attributes through the work
 * RAM port from the range's next entry. */
/* The range an object's sprites go in, by its priority. */
static unsigned range_of(const GameState *g, FieldObject obj)
{
    uint8_t priority = field_object_priority_mode(g, obj) & PRIORITY_RANGE;
    return priority == 0 ? MIDDLE : (priority & PRIORITY_BACK) ? BACK : FRONT;
}

static void draw_object(GameState *g, FieldObject obj)
{
    Layout layout = layout_of(g, obj);
    unsigned range = range_of(g, obj);

    uint16_t high[3] = { field_object_sprite_high(g, obj), field_object_sprite_high_more(g, obj),
                         field_object_sprite_high_last(g, obj) };
    uint16_t at = field_sprite_range_high(g, range);
    for (unsigned k = 0; k < layout.high_bytes; k++)
        set_field_sprite_table_byte(g, (unsigned)(uint16_t)(at + k - SPRITE_TABLE), (uint8_t)(high[k / 2] >> (8 * (k & 1))));
    set_field_sprite_range_high(g, range, (uint16_t)(at + layout.high_bytes));

    uint16_t entry = field_sprite_range_next(g, range);
    hw_port_seek_address(g, entry);
    set_field_sprite_range_next(g, range, (uint16_t)(entry + layout.sprites * OAM_ENTRY));

    uint16_t record = field_object_sprite_records(g, obj);
    for (unsigned n = 0; n < layout.sprites; n++, record = (uint16_t)(record + RECORD)) {
        static const uint8_t sent[4] = { RECORD_X, RECORD_Y, RECORD_TILE, RECORD_ATTRIBUTES };
        for (unsigned k = 0; k < 4; k++)
            hw_port_write(g, field_sprite_records_byte(g, (unsigned)record + sent[k]));
    }
    leave_sprites_left(g);
}

/* ---- The records -------------------------------------------------------------------------- */

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

/* The object's screen position's rows: whether its sprites that are drawn
 * where their Y wraps round the screen show. An object below the screen's
 * top edge (its screen Y past 255, as the nine-bit position is kept) shows
 * only the sprites whose Y came round past 0 and to $E0 or more; one in the
 * screen's lower half hides the sprites that came round; one in its upper
 * half hides those whose Y went past $7F to below $E0. The original writes a
 * fifth sprite's Y for an object off the top, which the port writes too. */
typedef enum { SCREEN_OFF_TOP, SCREEN_LOWER, SCREEN_UPPER } ScreenHalf;

static ScreenHalf screen_half(uint16_t screen_y)
{
    if (screen_y >> 8)
        return SCREEN_OFF_TOP;
    return (screen_y & 0x80) ? SCREEN_LOWER : SCREEN_UPPER;
}

static uint8_t sprite_y(ScreenHalf half, uint8_t offset, uint8_t screen_y)
{
    uint16_t sum = (uint16_t)(offset + screen_y);
    uint8_t y = (uint8_t)sum;
    bool wrapped = sum > 0xFF;
    switch (half) {
    case SCREEN_OFF_TOP:
        return wrapped && y >= HIDDEN_Y ? y : HIDDEN_Y;
    case SCREEN_LOWER:
        return wrapped && y >= HIDDEN_Y ? HIDDEN_Y : y;
    case SCREEN_UPPER:
        return (y & 0x80) && y < HIDDEN_Y ? HIDDEN_Y : y;
    }
    return y;
}

/* What placing an object's sprites leaves for the original's code and shadow
 * mode (field/scratch.h): its screen position ($C3, $C5), its records'
 * offset ($D9) and the high bits it gathered ($E5, $E6). */
typedef struct {
    uint16_t screen_x, screen_y, records;
    uint8_t high_but_first, first_x9;
} PlaceLeftovers;

static void leave_place(GameState *g, const PlaceLeftovers *l)
{
    original_cell16(g, 0xC3, l->screen_x);
    original_cell16(g, 0xC5, l->screen_y);
    original_cell16(g, 0xD9, l->records);
    original_cell8(g, 0xE5, l->high_but_first);
    original_cell8(g, 0xE6, l->first_x9);
}

enum { SPRITES_PLACED = 4 };

// $C0:B788, $C0:B8CA
/* Place the object's four sprites at its screen position: each sprite's X
 * and Y are its offset from the object plus the object's screen position,
 * and the X's ninth bits go into the object's high table byte. `reload`
 * first takes the offsets, tiles and attributes from the object's new frame. */
static void place_sprites(GameState *g, FieldObject obj, FieldObject high_of, bool reload)
{
    PlaceLeftovers l = { field_object_screen_x(g, obj), (uint16_t)(field_object_screen_y(g, obj) & 0x01FF),
                         field_object_sprite_records(g, obj), 0, 0 };
    uint8_t high = 0;
    for (int n = SPRITES_PLACED - 1; n >= 0; n--) {
        unsigned record = (uint16_t)(l.records + n * RECORD);
        if (reload)
            set_record_word(g, record + RECORD_X_OFFSET, frame_word(g, record + FRAME_X_OFFSET));
        uint16_t x = (uint16_t)(record_word(g, record + RECORD_X_OFFSET) + l.screen_x);
        set_field_sprite_records_byte(g, record + RECORD_X, (uint8_t)x);
        high = (uint8_t)(high << 2 | ((x >> 8) & 1));
        if (n == 1)
            l.high_but_first = high;
        if (n == 0)
            l.first_x9 = (uint8_t)((x >> 8) & 1);
    }
    set_field_object_sprite_high(g, high_of, (uint16_t)((field_object_sprite_high(g, high_of) & 0xFF00) | (high | ALL_LARGE)));

    ScreenHalf half = screen_half(l.screen_y);
    unsigned count = half == SCREEN_OFF_TOP ? SPRITES_PLACED + 1 : SPRITES_PLACED;
    for (unsigned n = 0; n < count; n++) {
        unsigned record = (uint16_t)(l.records + n * RECORD);
        if (reload && n < SPRITES_PLACED)
            set_field_sprite_records_byte(g, record + RECORD_Y_OFFSET, field_sprite_frame_data_byte(g, record + FRAME_Y_OFFSET));
        uint8_t offset = field_sprite_records_byte(g, record + RECORD_Y_OFFSET);
        set_field_sprite_records_byte(g, record + RECORD_Y, sprite_y(half, offset, (uint8_t)l.screen_y));
    }
    if (reload)
        for (unsigned n = 0; n < SPRITES_PLACED; n++) {
            unsigned record = (uint16_t)(l.records + n * RECORD);
            set_record_word(g, record + RECORD_TILE, frame_word(g, record + FRAME_TILE));
        }
    leave_place(g, &l);
}

/* ---- Where it meets the original ---------------------------------------------------------- */

/* The sprite table's steps that are entries of their own, and the records'
 * rebuilding for 8 and 12 sprites, which is the original's code: called as
 * the original calls them, going on where each came back with 8-bit A and
 * 16-bit X on the field's page and the bank it had, and stopping at the
 * call's return otherwise. */
static bool call_out(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    uint8_t bank = s->cpu.db;
    m_jsr_dc(s, site, routine, dont_care);
    if (field_machine_back(s, false, FIELD_PAGE, bank))
        return true;
    m_stop_at_own(s, site + 3, 0);
    return false;
}

/* The rows are read by the rows' first objects and their links: the object
 * each names is drawn. The draw reads Y and the carry through (the original
 * keeps the row in Y); nothing else it is entered with. */
enum { DRAW_DONT_CARE = DC_A | DC_X | DC_N | DC_Z };

static void draw_inline(Snes *s);

/* Draw the object a row's link names, as the current object, from one of
 * the two call sites (the row's first object, the rest): as C, or by calling
 * the draw's entry. */
static bool draw(Snes *s, uint8_t link, uint16_t *row, uint32_t site, bool as_c)
{
    GameState *g = state_of(s);
    field_make_link_current(g, link);
    s->cpu.y = *row;
    if (as_c) {
        draw_inline(s);
        return true;
    }
    if (!call_out(s, site, 0xC0B309, DRAW_DONT_CARE))
        return false;
    *row = s->cpu.y;
    return true;
}

static bool draws_as_c(const GameState *g);

static uint8_t next_in_row(const GameState *g)
{
    return field_object_row_link_next(g, field_object_of_link((uint8_t)field_current_object(g)));
}

// $C0:B271
void field_build_sprite_table(Snes *s)
{
    GameState *g = state_of(s);
    start_ranges(g);
    bool as_c = draws_as_c(g);
    /* The row is the original's Y, which the draw carries through. */
    uint16_t row = 2 * (FIELD_ROWS - 1);
    do {
        uint8_t link = field_row_first_object(g, row / 2);
        if (field_link_is_object(link)) {
            if (!draw(s, link, &row, 0xC0B2AF, as_c))
                return;
            while (field_link_is_object(link = next_in_row(g)))
                if (!draw(s, link, &row, 0xC0B2BB, as_c))
                    return;
        }
        row = (uint16_t)(row - 2);
    } while ((int16_t)row >= 0);
    hide_leftovers(g);
    m_rts(s);
}

/* The rebuilding and placing routines read the object in X. */
enum { RECORDS_DONT_CARE = DC_A | DC_N | DC_Z | DC_C };

typedef enum { RECORDS_NOTHING, RECORDS_PLACE, RECORDS_REBUILD } RecordsWork;

/* What an object's records need: nothing to draw, only placing, or a
 * rebuild. The 4-sprite layout places when only moved, rebuilds otherwise;
 * the 8- and 12-sprite ones need a count of rebuilt sprites of at least 2 or
 * 3 to rebuild, and place (or where the records weren't moved, draw nothing)
 * with fewer. */
enum { RECORDS_CURRENT = 0x80, RECORDS_COUNT = 0x7F };

static RecordsWork records_work(const GameState *g, FieldObject obj, uint8_t at_least)
{
    uint8_t state = field_object_sprite_state(g, obj);
    if (state == 0)
        return RECORDS_NOTHING;
    if (state & RECORDS_CURRENT) {
        uint8_t count = state & RECORDS_COUNT;
        if (at_least == 0)
            return count == 0 ? RECORDS_PLACE : RECORDS_REBUILD;
        return count < at_least ? RECORDS_PLACE : RECORDS_REBUILD;
    }
    return state < at_least ? RECORDS_NOTHING : RECORDS_REBUILD;
}

static bool records_in_bank(const GameState *g, FieldObject obj);
static void place_and_leave_overflow(Snes *s, FieldObject obj, FieldObject high_of, bool reload);

enum { POSITION_SPRITES = 0xC0B788 };

/* A layout's placing or rebuilding routine. Placing the 4-sprite layout is
 * this file's own (place_sprites), run as C and timed as the routine it is
 * where its entry's guard would take the object, and called as the entry
 * otherwise. Its rebuild is an entry too, but the release after it reads the
 * X the original's rebuild leaves, which the port doesn't make, so it is
 * called as the original calls it; so are the 8- and 12-sprite layouts'
 * routines, which are the original's. */
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

/* The records' work, done by the layout's routines: place them, or rebuild
 * them and release the object's old sprites (the original's code). */
static RecordsOutcome do_records_work(Snes *s, RecordsWork work, uint32_t place_site, uint32_t place,
                                      uint32_t rebuild_site, uint32_t rebuild, uint32_t release)
{
    GameState *g = state_of(s);
    s->cpu.x = field_current_object(g);
    if (work == RECORDS_PLACE && !records_step(s, place_site, place))
        return RECORDS_STOPPED;
    if (work == RECORDS_REBUILD) {
        if (!records_step(s, rebuild_site, rebuild) || !call_out(s, rebuild_site + 3, release, RECORDS_DONT_CARE))
            return RECORDS_STOPPED;
        set_field_object_sprite_state(g, (FieldObject)field_current_object_slot(s), RECORDS_CURRENT);
    }
    return work == RECORDS_NOTHING ? RECORDS_NONE_TO_DRAW : RECORDS_READY;
}

enum { LAYOUT_4, LAYOUT_8, LAYOUT_12, LAYOUT_24 };

static unsigned layout_kind(const GameState *g, FieldObject obj) { return field_object_sprite_layout(g, obj) & 3; }

// $C0:B701
/* Bring the current object's records up to date for drawing, for the 4-, 8-
 * and 12-sprite layouts (the 24-sprite one is still the original's, from
 * $C0:C73A). */
static RecordsOutcome bring_records_up_to_date(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    switch (layout_kind(g, obj)) {
    case LAYOUT_4:
        return do_records_work(s, records_work(g, obj, 0), 0xC0B732, 0xC0B788, 0xC0B71F, 0xC0B8CA, 0xC0E9E2);
    case LAYOUT_8:
        return do_records_work(s, records_work(g, obj, 2), 0xC0B759, 0xC0BA65, 0xC0B744, 0xC0BCDC, 0xC0E9FF);
    default:
        return do_records_work(s, records_work(g, obj, 3), 0xC0B780, 0xC0BFF2, 0xC0B76B, 0xC0C2BF, 0xC0EA1F);
    }
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

static bool records_work_inline(const GameState *g, FieldObject obj);

/* The records, brought up to date as the original's draw does from $C0:B309:
 * as C where that calls nothing, and by calling the routine otherwise. */
static RecordsOutcome records_for_draw(Snes *s)
{
    GameState *g = state_of(s);
    if (records_work_inline(g, (FieldObject)field_current_object_slot(s)))
        return bring_records_up_to_date(s);
    if (!call_out(s, 0xC0B309, 0xC0B701, DC_A | DC_X | DC_N | DC_Z))
        return RECORDS_STOPPED;
    return m_flag(s, CPU_FLAG_C) ? RECORDS_NONE_TO_DRAW : RECORDS_READY;
}

// $C0:B309
/* The current object's (16-bit X); Y is what the records' step left. */
void field_draw_object_sprites(Snes *s)
{
    RecordsOutcome r = records_for_draw(s);
    if (r == RECORDS_STOPPED)
        return;
    if (r == RECORDS_READY) {
        GameState *g = state_of(s);
        FieldObject obj = (FieldObject)field_current_object_slot(s);
        draw_object(g, obj);
        m_set_flag(s, CPU_FLAG_V, records_end_overflow(g, obj));
    }
    m_set_flag(s, CPU_FLAG_C, r == RECORDS_NONE_TO_DRAW);
    m_rts(s);
}

static bool cursors_in_table(const GameState *g);

/* The table's building draws its objects as C, each timed as the draw it is,
 * where every draw of the frame is one the draw's entry's guard would take
 * and whose records need no routine but placing; otherwise it calls the
 * draw's entry for every object, as the original does. It is all or none
 * because shadow mode matches the building's calls to the original's by call
 * site, and every draw is made from the same two. */
enum { DRAW = 0xC0B309, MOST_DRAWS = 2 * FIELD_OBJECTS };

/* Whether bringing the records up to date calls nothing: they need nothing,
 * or only placing, which is this file's own for the 4-sprite layout. */
static bool records_work_inline(const GameState *g, FieldObject obj)
{
    switch (layout_kind(g, obj)) {
    case LAYOUT_4: return records_work(g, obj, 0) != RECORDS_REBUILD;
    case LAYOUT_8: return records_work(g, obj, 2) == RECORDS_NOTHING;
    case LAYOUT_12: return records_work(g, obj, 3) == RECORDS_NOTHING;
    default: return false;
    }
}

static void draw_inline(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    RecordsOutcome r = bring_records_up_to_date(s);
    if (r == RECORDS_READY) {
        NATIVE_TIME_AS(s, DRAW, draw_object(g, obj));
        m_set_flag(s, CPU_FLAG_V, records_end_overflow(g, obj));
    }
    m_set_flag(s, CPU_FLAG_C, r == RECORDS_NONE_TO_DRAW);
}

static bool in_table(uint16_t cursor) { return cursor >= SPRITE_TABLE && cursor <= LOW_RAM_END - LARGEST_DRAW; }

/* Whether this frame's draws can all be C: the rows walked as the building
 * walks them, each object's draw checked as its guard would check it, with
 * the ranges' cursors moved on as its draw will move them. */
static bool draws_as_c(const GameState *g)
{
    uint16_t high[RANGES], next[RANGES];
    for (unsigned r = 0; r < RANGES; r++) {
        high[r] = field_sprite_range_high(g, r);
        next[r] = field_sprite_range_next(g, r);
    }
    unsigned draws = 0;
    for (int row = FIELD_ROWS - 1; row >= 0; row--) {
        for (uint8_t link = field_row_first_object(g, (unsigned)row); field_link_is_object(link);
             link = field_object_row_link_next(g, field_object_of_link(link))) {
            FieldObject obj = field_object_of_link(link);
            if (++draws > MOST_DRAWS || !field_names_object(link) || !records_in_bank(g, obj) ||
                !records_work_inline(g, obj))
                return false;
            for (unsigned r = 0; r < RANGES; r++)
                if (!in_table(high[r]) || !in_table(next[r]))
                    return false;
            unsigned work = layout_kind(g, obj) == LAYOUT_4 ? 0 : layout_kind(g, obj) == LAYOUT_8 ? 2 : 3;
            if (records_work(g, obj, (uint8_t)work) == RECORDS_NOTHING)
                continue;
            unsigned range = range_of(g, obj);
            Layout layout = layout_of(g, obj);
            high[range] = (uint16_t)(high[range] + layout.high_bytes);
            next[range] = (uint16_t)(next[range] + layout.sprites * OAM_ENTRY);
        }
    }
    return true;
}

/* Placing, and the overflow of the last sprite's Y, which the callers read. */
static void place_and_leave_overflow(Snes *s, FieldObject obj, FieldObject high_of, bool reload)
{
    GameState *g = state_of(s);
    place_sprites(g, obj, high_of, reload);
    unsigned count = screen_half((uint16_t)(field_object_screen_y(g, obj) & 0x01FF)) == SCREEN_OFF_TOP ? 5 : 4;
    uint8_t offset = field_sprite_records_byte(g, (uint16_t)(field_object_sprite_records(g, obj) + (count - 1) * RECORD) +
                                                      RECORD_Y_OFFSET);
    m_set_flag(s, CPU_FLAG_V, field_add_overflows8(offset, (uint8_t)field_object_screen_y(g, obj)));
}

/* The object in X (16-bit); its high table byte is the current object's. */
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
 * past low RAM on the I/O registers; no frame has them there. */

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
enum { RECORDS_ROOM = 0x10000 - 0x4BC0, LARGEST_LAYOUT = 24 * RECORD };

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
