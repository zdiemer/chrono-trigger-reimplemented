/* Field objects, every frame: the object update, where each object is on
 * the screen, and the rows the sprite table is drawn from.
 *
 * Every frame the field updates each object slot in turn, up to the event's
 * object count: it takes its step (field/motion.c) and is brought into or out
 * of view (field/object_view.c); and while in view it has its animation
 * ticked (field/animation.c), its place on the screen worked out, and its
 * place in the rows moved to where it now is. Objects that only come and go
 * with the view are checked against the screen and nothing else, and some
 * are left alone. Then the party's members are put back at the front of
 * their rows, so that they are drawn over the other objects on the same row.
 *
 * The rows are how the sprite table draws objects in order of depth: an
 * object in view is in the row for its screen position's Y (four pixels a
 * row), a list linked both ways whose first object the row keeps, and the
 * sprite table walks the rows from the bottom of the screen up. An object
 * that isn't in a row is marked so. */
#include "field/object_update.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/field.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- Screen positions --------------------------------------------------------------------- */

enum { SCREEN_FINE = 0x07, TILE_PIXELS = 8, POSITION_PIXEL = 16 };

/* The screen's top left corner on the map, in pixels: its tile, and the
 * layer's fine scroll within it. */
typedef struct {
    uint16_t x, y, fine_x, fine_y;
} ScreenOrigin;

static ScreenOrigin screen_origin(const GameState *g)
{
    ScreenOrigin o;
    o.fine_x = field_layer_scroll_h(g, FIELD_BG1) & SCREEN_FINE;
    o.fine_y = field_layer_scroll_v(g, FIELD_BG1) & SCREEN_FINE;
    o.x = (uint16_t)(field_screen_left(g) * TILE_PIXELS + o.fine_x);
    o.y = (uint16_t)(field_screen_top(g) * TILE_PIXELS + o.fine_y);
    return o;
}

/* A position on the map, in 1/16 pixel, as a screen coordinate. */
static int16_t on_screen(uint16_t position, uint16_t origin)
{
    return (int16_t)((uint16_t)(position / POSITION_PIXEL) - origin);
}

/* For the original's code and shadow mode (field/scratch.h): the screen's
 * fine scroll ($D9, $DB) and its origin ($DD, $DF), as it works them out. */
static void leave_screen_origin(GameState *g, ScreenOrigin o)
{
    original_cell16(g, 0xD9, o.fine_x);
    original_cell16(g, 0xDB, o.fine_y);
    original_cell16(g, 0xDD, o.x);
    original_cell16(g, 0xDF, o.y);
}

// $C0:AB45
void object_update_screen_position(GameState *g, FieldObject obj)
{
    ScreenOrigin o = screen_origin(g);
    leave_screen_origin(g, o);
    FieldPosition at = object_position(g, obj);
    set_field_object_screen_x(g, obj, stored_screen_coordinate(on_screen(at.x, o.x)));
    set_field_object_screen_y(g, obj, stored_screen_coordinate(on_screen(at.y, o.y)));
}

/* ---- Rows --------------------------------------------------------------------------------- */

enum { ROW_PIXELS = 4 };

/* The row for where the object is on the screen: its screen Y's low byte, a
 * row every four pixels. */
static unsigned row_of(const GameState *g, FieldObject obj)
{
    return (uint8_t)field_object_screen_y(g, obj) / ROW_PIXELS;
}

/* Storage: an object's links in its row, and whether it is in a row, which
 * the original keeps as a mark beside the row's first object: 0 in a row,
 * none (or the none link it was left with) out of one. */
static FieldLink previous_in_row(const GameState *g, FieldObject obj) { return field_object_row_link_previous(g, obj); }
FieldLink object_next_in_row(const GameState *g, FieldObject obj) { return field_object_row_link_next(g, obj); }
FieldLink row_first_object(const GameState *g, unsigned row) { return field_row_first_object(g, row); }

enum { IN_A_ROW = 0 };

static bool in_a_row(const GameState *g, FieldObject obj) { return field_link_is_object(field_object_unlinked(g, obj)); }
static void mark_in_a_row(GameState *g, FieldObject obj) { set_field_object_unlinked(g, obj, IN_A_ROW); }

void object_out_of_rows(GameState *g, FieldObject obj)
{
    set_field_object_row_link_previous(g, obj, FIELD_NO_LINK);
    set_field_object_row_link_next(g, obj, FIELD_NO_LINK);
}

/* For the original's code and shadow mode (field/scratch.h): joining a row
 * that has objects keeps the row at $C5. */
static void leave_joined_row(GameState *g, unsigned row) { original_cell8(g, 0xC5, (uint8_t)(row * 2)); }

/* For the original's code and shadow mode: taking an object out of the middle
 * of its row, the original links "the object after it" back to the one
 * before even when there is none after. A none link names a slot past the
 * 64, and that slot's link back is the byte of the next table, the in-view
 * flag of slot (link - $80) / 2, which it writes. The gate needs the byte
 * written, since shadow mode compares it and the view reads it; a build
 * without the original's memory layout has no such byte. */
static void leave_stray_in_view_write(GameState *g, FieldLink none_after, FieldLink before)
{
    set_field_object_in_view(g, (unsigned)(field_object_of_link(none_after) - FIELD_OBJECTS), before);
}

// $C0:A98A
/* Take the object out of its row: the objects either side of it are linked
 * to each other. */
void object_leave_row(GameState *g, FieldObject obj)
{
    if (!in_a_row(g, obj))
        return;
    FieldLink before = previous_in_row(g, obj);
    FieldLink after = object_next_in_row(g, obj);
    if (!field_link_is_object(before)) {
        set_field_row_first_object(g, row_of(g, obj), after);
        if (field_link_is_object(after))
            set_field_object_row_link_previous(g, field_object_of_link(after), FIELD_NO_LINK);
        /* The object is left marked with what it had after it: none. */
        FieldLink mark = field_link_is_object(after) ? FIELD_NO_LINK : after;
        set_field_object_row_link_next(g, obj, mark);
        set_field_object_unlinked(g, obj, mark);
        return;
    }
    set_field_object_row_link_next(g, field_object_of_link(before), after);
    if (field_link_is_object(after))
        set_field_object_row_link_previous(g, field_object_of_link(after), before);
    else
        leave_stray_in_view_write(g, after, before);
    object_out_of_rows(g, obj);
    set_field_object_unlinked(g, obj, FIELD_NO_LINK);
}

// $C0:A9CD
/* Put the object at the front of the row for where it is on the screen. It
 * keeps whatever link before it it had: nothing reads it at the front. */
void object_join_row(GameState *g, FieldObject obj)
{
    if (in_a_row(g, obj))
        return;
    unsigned row = row_of(g, obj);
    FieldLink first = row_first_object(g, row);
    if (!field_link_is_object(first)) {
        set_field_row_first_object(g, row, field_object_link(obj));
        mark_in_a_row(g, obj);
        object_out_of_rows(g, obj);
        return;
    }
    leave_joined_row(g, row);
    set_field_object_row_link_previous(g, field_object_of_link(first), field_object_link(obj));
    set_field_object_row_link_next(g, obj, first);
    set_field_row_first_object(g, row, field_object_link(obj));
    mark_in_a_row(g, obj);
}

/* ---- The update --------------------------------------------------------------------------- */

typedef enum { UPDATE_SKIP, UPDATE_WINDOW_ONLY, UPDATE_PARTY, UPDATE_OBJECT } UpdateKind;

/* The party's members step freely; objects that only appear while on screen
 * are checked against the screen and nothing else; some are left alone. */
static UpdateKind update_kind(const GameState *g, FieldObject obj)
{
    if (object_is_party_member(g, obj))
        return UPDATE_PARTY;
    ObjectRole role = object_role(g, obj);
    if (!role.disabled && role.kind == ROLE_LEFT_ALONE)
        return UPDATE_SKIP;
    return !role.disabled && role.kind == ROLE_WINDOW_ONLY ? UPDATE_WINDOW_ONLY : UPDATE_OBJECT;
}

/* ---- Where the update meets the original ------------------------------------------------ */

/* The overflow of the object's vertical position's difference from the
 * screen's, which placing it on the screen leaves for the original's code. */
static bool screen_position_overflow(const GameState *g, FieldObject obj)
{
    uint16_t pixels = (uint16_t)(object_position(g, obj).y / POSITION_PIXEL);
    return field_subtract_overflows16(pixels, screen_origin(g).y);
}

/* The object update's steps are this module's own (the move, the view, the
 * animation's tick, the rows, the screen position), run as C and timed as the
 * routines they are. The update is still a bridge: where an object comes
 * into view or leaves it, the view step calls the original's routines, so
 * there it is called as its entry; and where the state is one the tick's or
 * the rows' entries' guards would turn away, the entry is called instead, and
 * the original runs it. The update goes on after a call only where it came
 * back with 8-bit A and X, the field's page and the data bank it had. Nothing
 * the steps are entered with is read but the overflow, which they carry
 * through. Whether the view step left the object in view is read from the
 * object, not from the carry it answers in. */
enum { STEP_ENTRY_DONT_CARE = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };
enum { JSR_SIZE = 3 };   /* a call's return is the instruction after it */

static bool call_step(Snes *s, uint32_t site, uint32_t routine)
{
    uint8_t bank = s->cpu.db;
    m_jsr_dc(s, site, routine, STEP_ENTRY_DONT_CARE);
    if (field_machine_back(s, true, FIELD_PAGE, bank))
        return true;
    m_stop_at_own(s, site + JSR_SIZE, 0);
    return false;
}

/* The steps' entries, which the update runs as C and calls where it can't. */
enum {
    MOVE = 0xC0AA07,
    MOVE_PARTY_MEMBER = 0xC0AAFD,
    VIEW = 0xC0A88D,
    WINDOW_ONLY = 0xC0A947,
    TICK = 0xC0C98A,
    LEAVE_ROW = 0xC0A98A,
    PLACE_ON_SCREEN = 0xC0AB45,
    JOIN_ROW = 0xC0A9CD,
};

/* Where the update calls each step from: the other objects' update, and the
 * party members'. */
enum { OBJECTS_UPDATE, PARTY_UPDATE };
static const uint32_t view_sites[2] = { 0xC0A82D, 0xC0A873 };
static const uint32_t tick_sites[2] = { 0xC0A832, 0xC0A878 };
static const uint32_t leave_row_sites[2] = { 0xC0A835, 0xC0A87B };
static const uint32_t join_row_sites[2] = { 0xC0A83B, 0xC0A881 };

/* And where the party's re-sort calls the rows, for the second member and
 * the first. */
static const uint32_t resort_leave_sites[2] = { 0xC0A852, 0xC0A864 };
static const uint32_t resort_join_sites[2] = { 0xC0A855, 0xC0A867 };

static bool tick(Snes *s, unsigned update, FieldObject obj)
{
    GameState *g = state_of(s);
    if (!field_tick_is_exact(g, obj))
        return call_step(s, tick_sites[update], TICK);
    NATIVE_TIME_AS(s, TICK, object_animation_tick(g, obj));
    return true;
}

static bool leave_row(Snes *s, uint32_t site, FieldObject obj)
{
    GameState *g = state_of(s);
    if (!field_rows_are_exact(g, obj))
        return call_step(s, site, LEAVE_ROW);
    NATIVE_TIME_AS(s, LEAVE_ROW, object_leave_row(g, obj));
    return true;
}

static bool join_row(Snes *s, uint32_t site, FieldObject obj)
{
    GameState *g = state_of(s);
    if (!field_rows_are_exact(g, obj))
        return call_step(s, site, JOIN_ROW);
    NATIVE_TIME_AS(s, JOIN_ROW, object_join_row(g, obj));
    return true;
}

static void place_on_screen(Snes *s, FieldObject obj)
{
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, PLACE_ON_SCREEN, object_update_screen_position(g, obj));
    m_set_flag(s, CPU_FLAG_V, screen_position_overflow(g, obj));
}

static void move(Snes *s, unsigned update)
{
    if (update == PARTY_UPDATE)
        NATIVE_TIME_AS(s, MOVE_PARTY_MEMBER, field_move_current_object(s, true));
    else
        NATIVE_TIME_AS(s, MOVE, field_move_current_object(s, false));
}

/* The view step, as C where the object stays in or out of view (it calls
 * nothing then), and called as its entry where it comes or goes: those call
 * the original's routines, which shadow mode compares as the view's own
 * calls, not the update's. */
static bool view(Snes *s, unsigned update, FieldObject obj)
{
    if (field_view_changes(state_of(s), obj))
        return call_step(s, view_sites[update], VIEW);
    bool done;
    NATIVE_TIME_AS(s, VIEW, done = field_update_visibility(s));
    return done;
}

/* An object's step and, in view, the rest of its update. False where the
 * port stopped. */
static bool update_object(Snes *s, unsigned update, FieldObject obj)
{
    move(s, update);
    if (!view(s, update, obj))
        return false;
    if (!object_in_view(state_of(s), obj))
        return true;
    if (!tick(s, update, obj) || !leave_row(s, leave_row_sites[update], obj))
        return false;
    place_on_screen(s, obj);
    return join_row(s, join_row_sites[update], obj);
}

/* After everyone has moved: the second party member and then the first,
 * where in view, are put back at the front of their rows, so that they are
 * drawn over the other objects on the same row. */
static bool resort_party(Snes *s)
{
    GameState *g = state_of(s);
    static const unsigned members[2] = { 1, 0 };
    for (unsigned i = 0; i < 2; i++) {
        FieldLink link = field_party_objects(g, members[i]);
        if (!field_link_is_object(link) || !object_marked_in_view(g, field_object_of_link(link)))
            continue;
        field_make_link_current(g, link);
        FieldObject obj = field_object_of_link(link);
        if (!leave_row(s, resort_leave_sites[i], obj) || !join_row(s, resort_join_sites[i], obj))
            return false;
    }
    return true;
}

// $C0:A810
/* Every frame: update each object slot up to the event's object count, and
 * re-sort the party. A map whose last object is a party member leaves the
 * party as it is: the original's party members' update returns from the
 * loop without the re-sort. */
void field_update_objects(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t count = event_object_count(g);
    uint8_t end = (uint8_t)(count * 2);
    set_event_object_end(g, (uint16_t)((event_object_end(g) & 0xFF00) | end));
    FieldObject obj = 0;
    UpdateKind kind;
    for (;;) {
        object_make_current(g, obj);
        kind = update_kind(g, obj);
        bool ok = true;
        if (kind == UPDATE_WINDOW_ONLY)
            NATIVE_TIME_AS(s, WINDOW_ONLY, field_update_in_window(s));
        else if (kind == UPDATE_PARTY)
            ok = update_object(s, PARTY_UPDATE, obj);
        else if (kind == UPDATE_OBJECT)
            ok = update_object(s, OBJECTS_UPDATE, obj);
        if (!ok)
            return;
        if (field_object_link(obj) + 2 >= end)
            break;
        obj++;
    }
    if (kind == UPDATE_PARTY || resort_party(s))
        m_rts(s);
}

/* ---- Entries ------------------------------------------------------------------------------ */

/* The current object's (8-bit X). The overflow of the vertical position's
 * difference from the screen's is read after. */
void field_update_screen_position(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    object_update_screen_position(g, obj);
    m_set_flag(s, CPU_FLAG_V, screen_position_overflow(g, obj));
    m_set_flag(s, CPU_FLAG_M, true);
    m_rts(s);
}

/* The rows' entries leave 8-bit registers, whatever they came with. */
static void rows_entry_return(Snes *s)
{
    m_set_flag(s, CPU_FLAG_M, true);
    m_set_flag(s, CPU_FLAG_X, true);
    s->cpu.x &= 0x00FF;
    s->cpu.y &= 0x00FF;
    m_rts(s);
}

void field_leave_object_row(Snes *s)
{
    object_leave_row(state_of(s), field_object_of_link((FieldLink)field_current_object(state_of(s))));
    rows_entry_return(s);
}

void field_join_object_row(Snes *s)
{
    object_join_row(state_of(s), field_object_of_link((FieldLink)field_current_object(state_of(s))));
    rows_entry_return(s);
}

/* The rows' guard: the object and the objects it is linked to are named by
 * their slots doubled (any other link is one the rows never hold). */
static bool link_or_none(FieldLink link) { return !(link & 1); }

bool field_rows_are_exact(const GameState *g, unsigned obj)
{
    return link_or_none(previous_in_row(g, (FieldObject)obj)) && link_or_none(object_next_in_row(g, (FieldObject)obj)) &&
           link_or_none(row_first_object(g, row_of(g, (FieldObject)obj)));
}

bool field_object_rows_linked(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    FieldLink current = (FieldLink)field_current_object(g);
    bool ok = !(current & 1) && field_rows_are_exact(g, field_object_of_link(current));
    vars_peek(false);
    return ok;
}

/* The update's guard: on the field's page, with no more objects than there
 * are slots. The original would go on past the 64th into the tables beyond,
 * which no map has. */
bool field_objects_counted(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    bool ok = event_object_count(state_of(s)) <= FIELD_OBJECTS;
    vars_peek(false);
    return ok;
}
