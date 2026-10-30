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
#include "field/scratch.h"
#include "port/machine.h"
#include "port/native.h"
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

/* For the original's code and shadow mode (field/scratch.h): the screen's
 * fine scroll ($D9, $DB) and its origin ($DD, $DF), as it works them out. */
static void leave_screen_origin(GameState *g, ScreenOrigin o)
{
    original_cell16(g, 0xD9, o.fine_x);
    original_cell16(g, 0xDB, o.fine_y);
    original_cell16(g, 0xDD, o.x);
    original_cell16(g, 0xDF, o.y);
}

/* The original keeps each screen coordinate in nine bits, the ninth set where
 * it is negative (field.h). */
ScreenPoint object_screen_position(const GameState *g, FieldObject obj)
{
    return (ScreenPoint){ screen_coordinate_of(field_object_screen_x(g, obj)),
                          screen_coordinate_of(field_object_screen_y(g, obj)) };
}

// $C0:AB45
void object_update_screen_position(GameState *g, FieldObject obj)
{
    ScreenOrigin o = screen_origin(g);
    ORIGINAL_EFFECT(leave_screen_origin(g, o));
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

/* For the original's code and shadow mode (field/scratch.h): joining a row
 * that has objects keeps the row at $C5. */
static void leave_joined_row(GameState *g, unsigned row) { original_cell8(g, 0xC5, (uint8_t)(row * 2)); }

/* For the original's memory and shadow mode: taking an object out of the
 * middle of its row, the original links "the object after it" back to the
 * one before even when there is none after. A none link names a slot past
 * the 64, and that slot's link back is the byte of the next table, the
 * in-view flag of slot (link - $80) / 2, usually the first object's, which it
 * overwrites with the link before (object.h, OVERWRITTEN_BY_A_LINK). Shadow
 * mode compares the byte and the view reads it; a build without the
 * original's memory has no such byte. */
static void leave_stray_in_view_write(GameState *g, FieldLink none_after, FieldLink before)
{
    set_field_object_in_view(g, (unsigned)(field_linked_object(none_after) - FIELD_OBJECTS), field_link_byte(before));
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
        ORIGINAL_EFFECT(leave_stray_in_view_write(g, after, before));
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
    ORIGINAL_EFFECT(leave_joined_row(g, row));
    set_previous_in_row(g, field_linked_object(first), field_link_to(obj));
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

/* For the original's code and shadow mode: the update keeps where its loop
 * ends, the object count doubled, in the byte event_object_end's low byte
 * is. */
static void leave_loop_end(GameState *g, uint8_t count)
{
    set_event_object_end(g, (uint16_t)((event_object_end(g) & 0xFF00) | (uint8_t)(count * 2)));
}

/* Which of the update's calls a step is made from: an object's update, a
 * party member's, or the party's re-sort, for its second member or its
 * first. On the machine each is the original's call of the step, which a
 * step the port doesn't take is made from. */
typedef enum { OBJECTS_UPDATE, PARTY_UPDATE, RESORT_SECOND, RESORT_FIRST } UpdateCall;

/* The update's steps, run as C or on the machine (below): the object's move,
 * its view, its animation's tick, its row left or joined, its place on the
 * screen, and the screen's own check for an object that matters only on it.
 * The view, the tick and the rows are false where they didn't come back. */
typedef struct {
    void (*move)(void *ctx, FieldObject obj, bool party_member);
    bool (*view)(void *ctx, UpdateCall call, FieldObject obj);
    bool (*tick)(void *ctx, UpdateCall call, FieldObject obj);
    bool (*rows)(void *ctx, UpdateCall call, FieldObject obj, bool join);
    void (*place)(void *ctx, FieldObject obj);
    void (*window_only)(void *ctx, FieldObject obj);
} UpdateSteps;

/* An object's step and, in view, the rest of its update. */
static bool update_object(GameState *g, const UpdateSteps *steps, void *ctx, UpdateCall call, FieldObject obj)
{
    steps->move(ctx, obj, call == PARTY_UPDATE);
    if (!steps->view(ctx, call, obj))
        return false;
    if (!object_in_view(g, obj))
        return true;
    if (!steps->tick(ctx, call, obj) || !steps->rows(ctx, call, obj, false))
        return false;
    steps->place(ctx, obj);
    return steps->rows(ctx, call, obj, true);
}

/* After everyone has moved: the second party member and then the first,
 * where in view, are put back at the front of their rows, so that they are
 * drawn over the other objects on the same row. */
static bool resort_party(GameState *g, const UpdateSteps *steps, void *ctx)
{
    static const struct {
        unsigned member;
        UpdateCall call;
    } order[2] = { { 1, RESORT_SECOND }, { 0, RESORT_FIRST } };
    for (unsigned i = 0; i < 2; i++) {
        FieldLink link = party_member_link(g, order[i].member);
        if (!field_link_is_object(link) || object_view(g, field_linked_object(link)) == OUT_OF_VIEW)
            continue;
        FieldObject obj = field_linked_object(link);
        object_make_current(g, obj);
        if (!steps->rows(ctx, order[i].call, obj, false) || !steps->rows(ctx, order[i].call, obj, true))
            return false;
    }
    return true;
}

// $C0:A810
/* Every frame: update each object slot up to the event's object count, and
 * re-sort the party. A map whose last object is a party member leaves the
 * party as it is: the original's party members' update returns from the
 * loop without the re-sort. False where a step didn't come back. */
static bool update_objects(GameState *g, const UpdateSteps *steps, void *ctx)
{
    uint8_t count = event_object_count(g);
    ORIGINAL_EFFECT(leave_loop_end(g, count));
    UpdateKind kind;
    for (FieldObject obj = 0;; obj++) {
        object_make_current(g, obj);
        kind = update_kind(g, obj);
        if (kind == UPDATE_WINDOW_ONLY)
            steps->window_only(ctx, obj);
        else if (kind != UPDATE_SKIP &&
                 !update_object(g, steps, ctx, kind == UPDATE_PARTY ? PARTY_UPDATE : OBJECTS_UPDATE, obj))
            return false;
        if (obj + 1 >= count)
            break;
    }
    return kind == UPDATE_PARTY || resort_party(g, steps, ctx);
}

/* The steps' entries, which the update runs inline, and calls where it can't. */
enum {
    MOVE = 0xC0AA07,
    MOVE_PARTY_MEMBER = 0xC0AAFD,
    VIEW = 0xC0A88D,
    WINDOW_ONLY = 0xC0A947,
    TICK = 0xC0C98A,
    PLACE_ON_SCREEN = 0xC0AB45,
};

/* ---- The rows' step ------------------------------------------------------------------------- */

/* The rows' step inline, timed as the routine it is, where the rows' links
 * are ones its entry's guard takes; false where they aren't. */
static bool rows_step_taken(GameState *g, FieldObject obj, bool join)
{
    if (!field_rows_are_exact(g, obj))
        return false;
    TIMED_AS(g, join ? FIELD_JOIN_ROW_ENTRY : FIELD_LEAVE_ROW_ENTRY,
             join ? object_join_row(g, obj) : object_leave_row(g, obj));
    return true;
}

bool field_rows_step(Snes *s, FieldObject obj, bool join) { return rows_step_taken(state_of(s), obj, join); }

bool object_rows_step(GameState *g, FieldObject obj, bool join)
{
    if (rows_step_taken(g, obj, join))
        return true;
    program_needs_the_machine(g, join ? FIELD_JOIN_ROW_ENTRY : FIELD_LEAVE_ROW_ENTRY,
                              "a row whose links the port doesn't take");
    return false;
}

/* ---- As C ------------------------------------------------------------------------------------ */

static void move(void *ctx, FieldObject obj, bool party_member)
{
    GameState *g = ctx;
    if (party_member)
        TIMED_AS(g, MOVE_PARTY_MEMBER, (void)party_member_take_step(g, obj));
    else
        TIMED_AS(g, MOVE, (void)object_take_step(g, obj));
}

static bool view(void *ctx, UpdateCall call, FieldObject obj)
{
    (void)call;
    GameState *g = ctx;
    bool viewed = false;
    TIMED_AS(g, VIEW, viewed = object_view_updated(g, obj));
    return viewed;
}

static bool tick(void *ctx, UpdateCall call, FieldObject obj)
{
    (void)call;
    GameState *g = ctx;
    if (!field_tick_is_exact(g, obj)) {
        program_needs_the_machine(g, TICK, "an animation tick the port doesn't take");
        return false;
    }
    TIMED_AS(g, TICK, object_animation_tick(g, obj));
    return true;
}

static bool rows(void *ctx, UpdateCall call, FieldObject obj, bool join)
{
    (void)call;
    return object_rows_step(ctx, obj, join);
}

static void place(void *ctx, FieldObject obj)
{
    GameState *g = ctx;
    TIMED_AS(g, PLACE_ON_SCREEN, object_update_screen_position(g, obj));
}

static void window_only(void *ctx, FieldObject obj)
{
    GameState *g = ctx;
    TIMED_AS(g, WINDOW_ONLY, object_window_only_checked(g, obj));
}

bool field_objects_updated(GameState *g)
{
    static const UpdateSteps in_the_program = { move, view, tick, rows, place, window_only };
    return update_objects(g, &in_the_program, g);
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* The overflow of the object's vertical position's difference from the
 * screen's, which placing it on the screen leaves for the original's code. */
static bool screen_position_overflow(const GameState *g, FieldObject obj)
{
    uint16_t pixels = (uint16_t)(object_position(g, obj).y / POSITION_PIXEL);
    return field_subtract_overflows16(pixels, screen_origin(g).y);
}

/* On the machine the steps leave the flags their entries leave, for the
 * original's code after the update. Where an object comes into view or
 * leaves it, the view step is called as its entry, from the update's own
 * call: shadow mode compares the original's routines it reaches as the
 * view's calls, not the update's. And where the state is one the tick's or
 * the rows' entries' guards would turn away, the entry is called instead, and
 * the original runs it. The update goes on after a call only where it came
 * back with 8-bit A and X, the field's page and the data bank it had. Nothing
 * the steps are entered with is read but the overflow, which they carry
 * through. Whether the view step left the object in view is read from the
 * object, not from the carry it answers in. */
enum { STEP_ENTRY_DONT_CARE = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* Where the update calls each step from, by call. */
static const struct {
    uint32_t view, tick, leave_row, join_row;
} sites[] = {
    [OBJECTS_UPDATE] = { 0xC0A82D, 0xC0A832, 0xC0A835, 0xC0A83B },
    [PARTY_UPDATE] = { 0xC0A873, 0xC0A878, 0xC0A87B, 0xC0A881 },
    [RESORT_SECOND] = { 0, 0, 0xC0A852, 0xC0A855 },
    [RESORT_FIRST] = { 0, 0, 0xC0A864, 0xC0A867 },
};

static bool call_step(Snes *s, uint32_t site, uint32_t routine)
{
    return bridge_call(s, site, routine, STEP_ENTRY_DONT_CARE, on_the_field_page(s, true));
}

static void move_on_the_machine(void *ctx, FieldObject obj, bool party_member)
{
    Snes *s = ctx;
    if (party_member)
        NATIVE_TIME_AS(s, MOVE_PARTY_MEMBER, field_move_object_step(s, obj, true));
    else
        NATIVE_TIME_AS(s, MOVE, field_move_object_step(s, obj, false));
}

static bool view_on_the_machine(void *ctx, UpdateCall call, FieldObject obj)
{
    Snes *s = ctx;
    if (field_view_changes(state_of(s), obj))
        return call_step(s, sites[call].view, VIEW);
    bool done;
    NATIVE_TIME_AS(s, VIEW, done = field_update_visibility(s, obj));
    return done;
}

static bool tick_on_the_machine(void *ctx, UpdateCall call, FieldObject obj)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    if (!field_tick_is_exact(g, obj))
        return call_step(s, sites[call].tick, TICK);
    NATIVE_TIME_AS(s, TICK, object_animation_tick(g, obj));
    return true;
}

static bool rows_on_the_machine(void *ctx, UpdateCall call, FieldObject obj, bool join)
{
    Snes *s = ctx;
    if (rows_step_taken(state_of(s), obj, join))
        return true;
    return join ? call_step(s, sites[call].join_row, FIELD_JOIN_ROW_ENTRY)
                : call_step(s, sites[call].leave_row, FIELD_LEAVE_ROW_ENTRY);
}

static void place_on_the_machine(void *ctx, FieldObject obj)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, PLACE_ON_SCREEN, object_update_screen_position(g, obj));
    m_set_flag(s, CPU_FLAG_V, screen_position_overflow(g, obj));
}

static void window_only_on_the_machine(void *ctx, FieldObject obj)
{
    Snes *s = ctx;
    NATIVE_TIME_AS(s, WINDOW_ONLY, field_update_in_window(s, obj));
}

void field_update_objects(Snes *s)
{
    static const UpdateSteps on_the_machine = { move_on_the_machine, view_on_the_machine, tick_on_the_machine,
                                                rows_on_the_machine, place_on_the_machine, window_only_on_the_machine };
    if (update_objects(state_of(s), &on_the_machine, s))
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
    object_leave_row(state_of(s), object_current(state_of(s)));
    rows_entry_return(s);
}

void field_join_object_row(Snes *s)
{
    object_join_row(state_of(s), object_current(state_of(s)));
    rows_entry_return(s);
}

/* The rows' guard: the object and the objects it is linked to are named by
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

bool field_object_rows_linked(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t current = (uint8_t)field_current_object(g);
    bool ok = field_names_object(current) && field_rows_are_exact(g, object_current(g));
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
