/* Field objects, every frame: the object update, and where each object is on
 * the screen.
 *
 * Every frame the field updates each object slot in turn (the current object,
 * field_current_object, is the one being updated): it takes its step
 * (field/motion.c), is brought into or out of view (field/object_view.c),
 * and while in view has its animation ticked (field/animation.c), its screen
 * position worked out and its place in the rows moved to where it now is.
 *
 * The rows are how the sprite table draws objects in order of depth: an
 * object in view is linked into the row for its screen position's Y (four
 * pixels a row), a doubly linked list whose first object the row keeps, and
 * the sprite table walks the rows from the bottom of the screen up. Objects
 * are linked by their slot doubled; a negative link is none, and an object
 * that isn't in a row has its "unlinked" mark set (negative). */
#include "field/object_update.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/field.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* field_object_state_state: how the update treats the object. */
enum { STATE_PARTY_END = 3, STATE_WINDOW_ONLY = 7, STATE_SKIPPED = 8 };

/* ---- Screen positions --------------------------------------------------------------------- */

enum { SCREEN_FINE = 0x07, TILE_PIXELS = 8, POSITION_PIXEL = 16, SCREEN_BITS = 0x1FF, SCREEN_NEGATIVE = 0x100 };

/* The screen's top left corner on the map, in pixels: its tile, and the
 * layer's fine scroll within it. */
typedef struct {
    uint16_t x, y, fine_x, fine_y;
} ScreenOrigin;

static ScreenOrigin screen_origin(const GameState *g)
{
    ScreenOrigin o;
    o.fine_x = field_layer_scroll_h(g, 0) & SCREEN_FINE;
    o.fine_y = field_layer_scroll_v(g, 0) & SCREEN_FINE;
    o.x = (uint16_t)(field_screen_left(g) * TILE_PIXELS + o.fine_x);
    o.y = (uint16_t)(field_screen_top(g) * TILE_PIXELS + o.fine_y);
    return o;
}

/* A position's place on the screen: nine bits, the ninth set where it is off
 * the screen's left or top edge. */
static uint16_t on_screen(uint16_t position, uint16_t origin)
{
    uint16_t pixels = (uint16_t)(position / POSITION_PIXEL);
    uint16_t at = (uint16_t)(pixels - origin);
    if (pixels < origin)
        at |= SCREEN_NEGATIVE;
    return at & SCREEN_BITS;
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
    set_field_object_screen_x(g, obj, on_screen(at.x, o.x));
    set_field_object_screen_y(g, obj, on_screen(at.y, o.y));
}

/* ---- Rows --------------------------------------------------------------------------------- */

enum { ROW_PIXELS = 4 };

static unsigned row_of(const GameState *g, FieldObject obj)
{
    return (uint8_t)field_object_screen_y(g, obj) / ROW_PIXELS;
}

static bool in_a_row(const GameState *g, FieldObject obj)
{
    return field_link_is_object(field_object_unlinked(g, obj));
}

/* For the original's code and shadow mode (field/scratch.h): joining a row
 * that has objects keeps the row at $C5. */
static void leave_row(GameState *g, unsigned row) { original_cell8(g, 0xC5, (uint8_t)(row * 2)); }

// $C0:A98A
/* Take the object out of its row: the objects either side of it are linked
 * to each other. An object with one before it and none after links "the one
 * after" all the same, which writes the in-view mark of the slot the none
 * link names; the port writes it too. */
void object_leave_row(GameState *g, FieldObject obj)
{
    if (!in_a_row(g, obj))
        return;
    uint8_t before = field_object_row_link_previous(g, obj);
    uint8_t after = field_object_row_link_next(g, obj);
    if (!field_link_is_object(before)) {
        set_field_row_first_object(g, row_of(g, obj), after);
        if (field_link_is_object(after))
            set_field_object_row_link_previous(g, field_object_of_link(after), FIELD_NO_LINK);
        uint8_t mark = field_link_is_object(after) ? FIELD_NO_LINK : after;
        set_field_object_row_link_next(g, obj, mark);
        set_field_object_unlinked(g, obj, mark);
        return;
    }
    set_field_object_row_link_next(g, field_object_of_link(before), after);
    set_field_object_row_link_previous(g, field_object_of_link(after), before);
    set_field_object_row_link_previous(g, obj, FIELD_NO_LINK);
    set_field_object_row_link_next(g, obj, FIELD_NO_LINK);
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
    uint8_t first = field_row_first_object(g, row);
    if (!field_link_is_object(first)) {
        set_field_row_first_object(g, row, field_object_link(obj));
        set_field_object_unlinked(g, obj, 0);
        set_field_object_row_link_previous(g, obj, FIELD_NO_LINK);
        set_field_object_row_link_next(g, obj, FIELD_NO_LINK);
        return;
    }
    leave_row(g, row);
    set_field_object_row_link_previous(g, field_object_of_link(first), field_object_link(obj));
    set_field_object_row_link_next(g, obj, first);
    set_field_row_first_object(g, row, field_object_link(obj));
    set_field_object_unlinked(g, obj, 0);
}

/* ---- The update --------------------------------------------------------------------------- */

typedef enum { UPDATE_SKIP, UPDATE_WINDOW_ONLY, UPDATE_PARTY, UPDATE_OBJECT } UpdateKind;

/* The party's members step freely; objects that only appear while on screen
 * are checked against the screen and nothing else; some are left alone. */
static UpdateKind update_kind(const GameState *g, FieldObject obj)
{
    uint8_t state = field_object_state_state(g, obj);
    if (state < STATE_PARTY_END)
        return UPDATE_PARTY;
    if (state == STATE_SKIPPED)
        return UPDATE_SKIP;
    return state == STATE_WINDOW_ONLY ? UPDATE_WINDOW_ONLY : UPDATE_OBJECT;
}

/* The object after the current one, and whether the update goes on to it:
 * while the difference from the end is negative as a byte, as the original
 * compares, which a count past 64 objects wraps. */
static bool next_object(const GameState *g, FieldObject *obj)
{
    uint8_t link = (uint8_t)(field_current_object(g) + 2);
    *obj = field_object_of_link(link);
    return (int8_t)(link - (uint8_t)event_object_end(g)) < 0;
}

/* ---- Where the update meets the original ------------------------------------------------ */

/* The object update's steps are entries of their own (the original calls
 * them), so the update calls them as the original does: a bridge, which goes
 * on after each only where the step came back with the machine as it runs
 * (8-bit A and X, the field's page and the data bank it had), and otherwise
 * stops at the call's return. Nothing the steps are entered with is read but
 * the overflow, which they carry through. Whether the view step left the
 * object in view is read from the object, not from the carry it answers in:
 * shadow mode gives a call's don't-care flag back to the port where the
 * callee leaves it as it was. */
enum { STEP_ENTRY_DONT_CARE = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool call_step(Snes *s, uint32_t site, uint32_t routine)
{
    uint8_t bank = s->cpu.db;
    m_jsr_dc(s, site, routine, STEP_ENTRY_DONT_CARE);
    if (field_machine_back(s, true, FIELD_PAGE, bank))
        return true;
    m_stop_at_own(s, site + 3, 0);
    return false;
}

/* The steps of one object in view, from the party's copy of the loop or the
 * other objects' (their calls lie the same distances from `first`). False
 * where the port stopped. */
static bool update_in_view(Snes *s, uint32_t first, uint32_t move)
{
    if (!call_step(s, first, move) || !call_step(s, first + 3, 0xC0A88D))
        return false;
    GameState *g = state_of(s);
    if (!(field_object_in_view(g, field_object_of_link((uint8_t)field_current_object(g))) & 0x80))
        return true;
    return call_step(s, first + 8, 0xC0C98A) && call_step(s, first + 11, 0xC0A98A) &&
           call_step(s, first + 14, 0xC0AB45) && call_step(s, first + 17, 0xC0A9CD);
}

/* The party members' objects re-sorted into their rows after the update, the
 * second and then the first, where they are in view. */
static bool resort_party(Snes *s)
{
    GameState *g = state_of(s);
    static const struct { unsigned member; uint32_t leave_row, join_row; } members[2] = {
        { 1, 0xC0A852, 0xC0A855 }, { 0, 0xC0A864, 0xC0A867 },
    };
    for (unsigned i = 0; i < 2; i++) {
        uint8_t link = field_party_objects(g, members[i].member);
        if (!field_link_is_object(link) || field_object_in_view(g, field_object_of_link(link)) == 0)
            continue;
        set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | link));
        if (!call_step(s, members[i].leave_row, 0xC0A98A) || !call_step(s, members[i].join_row, 0xC0A9CD))
            return false;
    }
    return true;
}

// $C0:A810
/* Every frame: update each object slot up to the event's object count. */
void field_update_objects(Snes *s)
{
    GameState *g = state_of(s);
    set_event_object_end(g, (uint16_t)((event_object_end(g) & 0xFF00) | (uint8_t)(event_object_count(g) * 2)));
    FieldObject obj = 0;
    UpdateKind kind;
    do {
        set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | field_object_link(obj)));
        kind = update_kind(g, obj);
        bool ok = true;
        if (kind == UPDATE_WINDOW_ONLY)
            ok = call_step(s, 0xC0A86B, 0xC0A947);
        else if (kind == UPDATE_PARTY)
            ok = update_in_view(s, 0xC0A870, 0xC0AAFD);
        else if (kind == UPDATE_OBJECT)
            ok = update_in_view(s, 0xC0A82A, 0xC0AA07);
        if (!ok)
            return;
    } while (next_object(g, &obj));
    /* The party's copy of the loop returns without re-sorting the party. */
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
    uint16_t pixels = (uint16_t)(object_position(g, obj).y / POSITION_PIXEL), origin = screen_origin(g).y;
    uint16_t d = (uint16_t)(pixels - origin);
    m_set_flag(s, CPU_FLAG_V, ((pixels ^ origin) & (pixels ^ d) & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_M, true);
    m_rts(s);
}

void field_unlink_object_row(Snes *s)
{
    object_leave_row(state_of(s), field_object_of_link((uint8_t)field_current_object(state_of(s))));
    m_set_flag(s, CPU_FLAG_M, true);
    m_set_flag(s, CPU_FLAG_X, true);
    s->cpu.x &= 0x00FF;
    s->cpu.y &= 0x00FF;
    m_rts(s);
}

void field_link_object_row(Snes *s)
{
    object_join_row(state_of(s), field_object_of_link((uint8_t)field_current_object(state_of(s))));
    m_set_flag(s, CPU_FLAG_M, true);
    m_set_flag(s, CPU_FLAG_X, true);
    s->cpu.x &= 0x00FF;
    s->cpu.y &= 0x00FF;
    m_rts(s);
}

/* The rows' guard: the object and the objects it is linked to are named by
 * their slots doubled (any other link is one the rows never hold). */
static bool link_or_none(uint8_t link) { return !(link & 1); }

bool field_object_rows_linked(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t current = (uint8_t)field_current_object(g);
    bool ok = !(current & 1);
    if (ok) {
        FieldObject obj = field_object_of_link(current);
        ok = link_or_none(field_object_row_link_previous(g, obj)) && link_or_none(field_object_row_link_next(g, obj)) &&
             link_or_none(field_row_first_object(g, row_of(g, obj)));
    }
    vars_peek(false);
    return ok;
}
