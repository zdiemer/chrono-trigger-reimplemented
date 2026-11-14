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

#include "charge.h"
#include "core/program.h"
#include "field/bridge.h"
#include "field/field.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- Screen positions --------------------------------------------------------------------- */

enum { SCREEN_FINE = 0x07, POSITION_PIXEL = 16 };

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
    o.x = (uint16_t)(field_screen_left(g) * FIELD_ENTRY_PIXELS + o.fine_x);
    o.y = (uint16_t)(field_screen_top(g) * FIELD_ENTRY_PIXELS + o.fine_y);
    return o;
}

/* A position on the map, in 1/16 pixel, as a screen coordinate. */
static int16_t on_screen(uint16_t position, uint16_t origin)
{
    return (int16_t)((uint16_t)(position / POSITION_PIXEL) - origin);
}

/* The original keeps each screen coordinate in nine bits, the ninth set where
 * it is negative (field.h). */
ScreenPoint object_screen_position(const GameState *g, FieldObject obj)
{
    return (ScreenPoint){ screen_coordinate_of(field_object_screen_x(g, obj)),
                          screen_coordinate_of(field_object_screen_y(g, obj)) };
}

ScreenPoint object_place_on_the_screen(const GameState *g, FieldObject obj)
{
    ScreenOrigin o = screen_origin(g);
    FieldPosition at = object_position(g, obj);
    return (ScreenPoint){ on_screen(at.x, o.x), on_screen(at.y, o.y) };
}

// $C0:AB45
void object_update_screen_position(GameState *g, FieldObject obj)
{
    /* The original works the place out in four words of scratch memory;
     * their eight accesses stay in the screen position's time. */
    charge_accesses(g, 8);
    ScreenPoint at = object_place_on_the_screen(g, obj);
    set_field_object_screen_x(g, obj, stored_screen_coordinate(at.x));
    set_field_object_screen_y(g, obj, stored_screen_coordinate(at.y));
}

/* ---- Rows --------------------------------------------------------------------------------- */

enum { ROW_PIXELS = 4 };

/* The row for where the object is on the screen: its screen Y's low byte, a
 * row every four pixels. */
static unsigned row_of(const GameState *g, FieldObject obj)
{
    return (uint8_t)field_object_screen_y(g, obj) / ROW_PIXELS;
}

/* Storage: an object's links in its row, a row's first object, and whether
 * an object is in a row, which the original keeps as a mark: 0 in a row, and
 * out of one a none link, the one it was left with. */
static FieldLink previous_in_row(const GameState *g, FieldObject obj)
{
    return field_link_stored(field_object_row_link_previous(g, obj));
}
FieldLink object_next_in_row(const GameState *g, FieldObject obj) { return field_link_stored(field_object_row_link_next(g, obj)); }
FieldLink row_first_object(const GameState *g, unsigned row) { return field_link_stored(field_row_first_object(g, row)); }
static void set_previous_in_row(GameState *g, FieldObject obj, FieldLink l)
{
    set_field_object_row_link_previous(g, obj, field_link_byte(l));
}
static void set_next_in_row(GameState *g, FieldObject obj, FieldLink l) { set_field_object_row_link_next(g, obj, field_link_byte(l)); }
static void set_row_first(GameState *g, unsigned row, FieldLink l) { set_field_row_first_object(g, row, field_link_byte(l)); }

static bool in_a_row(const GameState *g, FieldObject obj)
{
    return field_link_is_object(field_link_stored(field_object_row_mark(g, obj)));
}
enum { IN_A_ROW = 0 };
static void mark_in_a_row(GameState *g, FieldObject obj) { set_field_object_row_mark(g, obj, IN_A_ROW); }
static void mark_out_of_rows(GameState *g, FieldObject obj, FieldLink none)
{
    set_field_object_row_mark(g, obj, field_link_byte(none));
}

void object_out_of_rows(GameState *g, FieldObject obj)
{
    set_previous_in_row(g, obj, field_no_link());
    set_next_in_row(g, obj, field_no_link());
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
        set_row_first(g, row_of(g, obj), after);
        if (field_link_is_object(after))
            set_previous_in_row(g, field_linked_object(after), field_no_link());
        /* The object is left marked with what it had after it: none. */
        FieldLink mark = field_link_is_object(after) ? field_no_link() : after;
        set_next_in_row(g, obj, mark);
        mark_out_of_rows(g, obj, mark);
        return;
    }
    set_next_in_row(g, field_linked_object(before), after);
    if (field_link_is_object(after))
        set_previous_in_row(g, field_linked_object(after), before);
    else
        object_apply_row_tail_visibility(g, after, before);
    object_out_of_rows(g, obj);
    mark_out_of_rows(g, obj, field_no_link());
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
        set_row_first(g, row, field_link_to(obj));
        mark_in_a_row(g, obj);
        object_out_of_rows(g, obj);
        return;
    }
    set_previous_in_row(g, field_linked_object(first), field_link_to(obj));
    charge_accesses(g, 1); /* the joined row's former byte selection */
    set_next_in_row(g, obj, first);
    set_row_first(g, row, field_link_to(obj));
    mark_in_a_row(g, obj);
}

// $C0:B204
/* A map's screen starts with every row empty and every object out of the
 * rows. (The original goes on to take the map's objects out of view, but its
 * word-wide writes through the work RAM port set the port's address back each
 * time, so it reaches only the first object's two bytes; its callers take
 * every object out of view next, objects_all_out_of_view, which writes them
 * again.) */
void field_empty_the_rows(GameState *g)
{
    for (unsigned row = 0; row < FIELD_ROW_FIRST_OBJECT_COUNT; row++)
        set_row_first(g, row, field_no_link());
    for (FieldObject obj = 0; obj < FIELD_OBJECTS; obj++) {
        mark_out_of_rows(g, obj, field_no_link());
        object_out_of_rows(g, obj);
    }
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

/* The update's steps, each the routine it is, which the update runs inline:
 * the object's move, its view, its animation's tick, its row left or joined,
 * its place on the screen, and the screen's own check for an object that
 * matters only on it. */
enum {
    MOVE = 0xC0AA07,
    MOVE_PARTY_MEMBER = 0xC0AAFD,
    VIEW = 0xC0A88D,
    WINDOW_ONLY = 0xC0A947,
    TICK = 0xC0C98A,
    PLACE_ON_SCREEN = 0xC0AB45,
};

/* ---- The rows' step ------------------------------------------------------------------------- */

/* The rows' step inline, timed as the routine it is, where the port takes
 * the rows' links (field_rows_are_exact); false where it doesn't. */
static bool rows_step_taken(GameState *g, FieldObject obj, bool join)
{
    if (!field_rows_are_exact(g, obj))
        return false;
    TIMED_AS(g, join ? FIELD_JOIN_ROW_ENTRY : FIELD_LEAVE_ROW_ENTRY,
             join ? object_join_row(g, obj) : object_leave_row(g, obj));
    return true;
}

bool object_rows_step(GameState *g, FieldObject obj, bool join)
{
    if (rows_step_taken(g, obj, join))
        return true;
    program_breaks_a_contract(g, join ? FIELD_JOIN_ROW_ENTRY : FIELD_LEAVE_ROW_ENTRY,
                              "a row whose links the port doesn't take");
    return false;
}

/* ---- An object's update ---------------------------------------------------------------------- */

/* The animation's tick, where the port takes it. */
static bool tick(GameState *g, FieldObject obj)
{
    if (!PORT_CHECK(field_tick_is_exact(g, obj))) {
        program_breaks_a_contract(g, TICK, "an animation tick the port doesn't take");
        return false;
    }
    TIMED_AS(g, TICK, object_animation_tick(g, obj));
    return true;
}

/* An object's step and, in view, the rest of its update. False where a step
 * didn't come back. */
static bool update_object(GameState *g, FieldObject obj, bool party_member)
{
    if (party_member)
        TIMED_AS(g, MOVE_PARTY_MEMBER, (void)party_member_take_step(g, obj));
    else
        TIMED_AS(g, MOVE, (void)object_take_step(g, obj));
    bool viewed = false;
    TIMED_AS(g, VIEW, viewed = object_view_updated(g, obj));
    if (!viewed)
        return false;
    if (!object_in_view(g, obj))
        return true;
    if (!tick(g, obj) || !object_rows_step(g, obj, false))
        return false;
    TIMED_AS(g, PLACE_ON_SCREEN, object_update_screen_position(g, obj));
    return object_rows_step(g, obj, true);
}

/* After everyone has moved: the second party member and then the first,
 * where in view, are put back at the front of their rows, so that they are
 * drawn over the other objects on the same row. */
static bool resort_party(GameState *g)
{
    static const unsigned order[2] = { 1, 0 };
    for (unsigned i = 0; i < 2; i++) {
        FieldLink link = party_member_link(g, order[i]);
        if (!field_link_is_object(link) || object_view(g, field_linked_object(link)) == OUT_OF_VIEW)
            continue;
        FieldObject obj = field_linked_object(link);
        object_make_current(g, obj);
        if (!object_rows_step(g, obj, false) || !object_rows_step(g, obj, true))
            return false;
    }
    return true;
}

// $C0:A810
/* Every frame: update each object slot up to the event's object count, and
 * re-sort the party. A map whose last object is a party member leaves the
 * party as it is: the original's party members' update returns from the
 * loop without the re-sort. False where a step didn't come back. */
bool field_objects_updated(GameState *g)
{
    uint8_t count = event_object_count(g);
    charge_accesses(g, 4); /* loop bound's former word read/write */
    UpdateKind kind;
    for (FieldObject obj = 0;; obj++) {
        object_make_current(g, obj);
        kind = update_kind(g, obj);
        if (kind == UPDATE_WINDOW_ONLY)
            TIMED_AS(g, WINDOW_ONLY, object_window_only_checked(g, obj));
        else if (kind != UPDATE_SKIP && !update_object(g, obj, kind == UPDATE_PARTY))
            return false;
        if (obj + 1 >= count)
            break;
    }
    return kind == UPDATE_PARTY || resort_party(g);
}

/* The rows the port takes: the object and the objects it is linked to are named by
 * their slots doubled, and a none link is even too: the original indexes the
 * tables with a none link where there is no object after (the stray write,
 * above), and an odd one would index a byte the port doesn't. */
static bool indexes_as_the_port_does(FieldLink link) { return !(field_link_byte(link) & 1); }

bool field_rows_are_exact(const GameState *g, unsigned obj)
{
    return indexes_as_the_port_does(previous_in_row(g, (FieldObject)obj)) &&
           indexes_as_the_port_does(object_next_in_row(g, (FieldObject)obj)) &&
           indexes_as_the_port_does(row_first_object(g, row_of(g, (FieldObject)obj)));
}
