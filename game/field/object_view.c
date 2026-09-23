/* Field objects coming into view and leaving it.
 *
 * Only objects in view take part in the field's frame: the object update
 * brings each object into view when it is inside a window a little larger
 * than the screen (so that it is set up before it shows), and out of view
 * when it leaves. Coming into view, it starts showing its animation afresh,
 * joins its row, and the field sets up its sprites and palette; leaving,
 * those are released, it leaves its row, a dialog waiting on it is ended,
 * and a one-off animation it was playing is cut to its last play.
 *
 * Objects that only matter while on screen (a state of their own) are simply
 * marked in view or not by the screen itself. */
#include "field/field.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { NO_BATTLE_PLACE = 0xFF };   /* field_object_battle_info */
enum { DIALOG_ENDED_BY_LEAVING = 0x0D };   /* dialog_state */

/* The window objects come into view in: the screen's edges, in tiles doubled
 * as the positions are compared, widened a little, and held to the map. */
typedef struct {
    uint8_t left, right, top, bottom;
} ViewWindow;

static uint8_t widen_down(uint8_t v, uint8_t by) { return v >= by ? (uint8_t)(v - by) : 0; }
static uint8_t widen_up(uint8_t v, uint8_t by) { return v + by > 0xFF ? 0xFF : (uint8_t)(v + by); }

/* For the original's code and shadow mode (field/scratch.h): the window's
 * edges, which it keeps at $C3-$C6. */
static void leave_window(GameState *g, ViewWindow w)
{
    original_cell8(g, 0xC3, w.left);
    original_cell8(g, 0xC4, w.right);
    original_cell8(g, 0xC5, w.top);
    original_cell8(g, 0xC6, w.bottom);
}

static bool enabled_with_sprite(const GameState *g, FieldObject obj)
{
    if (object_disabled(g, obj))
        return false;
    int8_t sprite = (int8_t)field_object_sprite_flag(g, obj);
    return sprite > 0;
}

/* Where the object is, in tiles doubled. */
static uint8_t tile_x2(const GameState *g, FieldObject obj) { return (uint8_t)(field_object_x_tile(g, obj) << 1); }
static uint8_t tile_y2(const GameState *g, FieldObject obj) { return (uint8_t)(field_object_y_tile(g, obj) << 1); }

// $C0:A88D
/* Whether the object is in the window it comes into view in: four tiles
 * either side of the screen, two above and five below. An object disabled or
 * with no sprite never is. */
static bool inside_view_window(GameState *g, FieldObject obj)
{
    if (!enabled_with_sprite(g, obj))
        return false;
    ViewWindow w = { widen_down(field_screen_left(g), 4), widen_up(field_screen_right(g), 4),
                     widen_down(field_screen_top(g), 2), widen_up(field_screen_bottom(g), 5) };
    leave_window(g, w);
    uint8_t x = tile_x2(g, obj), y = tile_y2(g, obj);
    return x >= w.left && x < w.right && y >= w.top && y < w.bottom;
}

/* Coming into view: it is marked in view, out of any row, with its animation
 * starting afresh, its sprites to be built, and no place kept for a battle. */
static void come_into_view(GameState *g, FieldObject obj)
{
    set_field_object_row_link_previous(g, obj, FIELD_NO_LINK);
    set_field_object_row_link_next(g, obj, FIELD_NO_LINK);
    set_field_object_in_view(g, obj, OBJECT_IN_VIEW);
    set_field_object_animation_frame(g, obj, 0);
    set_field_object_frame_delay(g, obj, 0);
    set_field_object_sprite_state(g, obj, 0);
    set_field_object_battle_info(g, obj, NO_BATTLE_PLACE);
}

/* Left view: a dialog waiting on the object ends, unless the dialog box has
 * its place. */
static void after_leaving_view(GameState *g, FieldObject obj)
{
    if (event_object_wait_dialog(g, obj) != 0 && dialog_box_position(g) == 0)
        set_dialog_state(g, DIALOG_ENDED_BY_LEAVING);
}

// $C0:A947
/* The screen's own check for objects that only matter on it: in view while
 * its tile is inside the screen, widened four tiles to the sides and five
 * below, compared by the difference's sign as the original compares, which
 * wraps at the map's edges. */
static bool check_on_screen(GameState *g, FieldObject obj)
{
    ViewWindow w = { (uint8_t)(field_screen_left(g) - 4), (uint8_t)(field_screen_right(g) + 4), field_screen_top(g),
                     (uint8_t)(field_screen_bottom(g) + 5) };
    leave_window(g, w);
    uint8_t x = tile_x2(g, obj), y = tile_y2(g, obj);
    bool inside = (int8_t)(x - w.left) >= 0 && (int8_t)(x - w.right) < 0 && (int8_t)(y - w.top) >= 0 &&
                  (int8_t)(y - w.bottom) < 0;
    set_field_object_in_view(g, obj, inside ? OBJECT_IN_VIEW : OBJECT_OUT_OF_VIEW);
    return inside;
}

/* ---- Where it meets the original ---------------------------------------------------------- */

/* The overflow of the window's lower edge's sum, which the callers read where
 * nothing after it sets one. */
static bool bottom_overflows(const GameState *g, uint8_t by)
{
    return field_add_overflows8(field_screen_bottom(g), by);
}

/* Setting up and releasing an object's sprites and palette are still the
 * original's code, and joining and leaving a row are entries of their own;
 * the field calls them in turn, going on where each came back with the
 * machine as it runs, and stopping at the call's return otherwise. None of
 * them reads the registers it is entered with but the overflow. */
enum { VIEW_CALL_DONT_CARE = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool call_out(Snes *s, uint32_t site, uint32_t routine)
{
    uint8_t bank = s->cpu.db;
    m_jsr_dc(s, site, routine, VIEW_CALL_DONT_CARE);
    if (field_machine_back(s, true, FIELD_PAGE, bank))
        return true;
    m_stop_at_own(s, site + 3, 0);
    return false;
}

/* The current object joins or leaves the rows (field/object_update.c), run
 * as C and timed as the routine it is; where the rows' links are ones its
 * entry's guard would turn away, the entry is called, and the original runs
 * it. */
enum { LEAVE_ROW = 0xC0A98A, JOIN_ROW = 0xC0A9CD };

static bool rows_step(Snes *s, uint32_t site, bool join)
{
    GameState *g = state_of(s);
    uint8_t link = (uint8_t)field_current_object(g);
    FieldObject obj = field_object_of_link(link);
    if ((link & 1) || !field_rows_are_exact(g, obj))
        return join ? call_out(s, site, JOIN_ROW) : call_out(s, site, LEAVE_ROW);
    if (join)
        NATIVE_TIME_AS(s, JOIN_ROW, object_join_row(g, obj));
    else
        NATIVE_TIME_AS(s, LEAVE_ROW, object_leave_row(g, obj));
    return true;
}

/* Whether the view step leaves the object as it was, in view or out of it:
 * then it calls none of the original's routines. */
bool field_view_unchanged(const GameState *g, unsigned obj)
{
    bool enabled = enabled_with_sprite(g, obj);
    ViewWindow w = { widen_down(field_screen_left(g), 4), widen_up(field_screen_right(g), 4),
                     widen_down(field_screen_top(g), 2), widen_up(field_screen_bottom(g), 5) };
    uint8_t x = tile_x2(g, obj), y = tile_y2(g, obj);
    bool inside = enabled && x >= w.left && x < w.right && y >= w.top && y < w.bottom;
    return inside == object_in_view(g, obj);
}

// $C0:A88D
bool field_update_visibility(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_current_object_slot(s);
    bool checked = enabled_with_sprite(g, obj);
    bool inside = inside_view_window(g, obj);
    if (checked)
        m_set_flag(s, CPU_FLAG_V, bottom_overflows(g, 5));
    bool was_in_view = object_in_view(g, obj);
    if (inside) {
        if (!was_in_view) {
            come_into_view(g, obj);
            if (!call_out(s, 0xC0A905, 0xC07170) || !call_out(s, 0xC0A908, 0xC06F9A) || !rows_step(s, 0xC0A90B, true))
                return false;
        }
        m_set_flag(s, CPU_FLAG_C, true);
        return true;
    }
    if (was_in_view) {
        set_field_object_in_view(g, obj, OBJECT_OUT_OF_VIEW);
        if (!call_out(s, 0xC0A918, 0xC0EA42) || !call_out(s, 0xC0A91B, 0xC0734C) ||
            !call_out(s, 0xC0A91E, 0xC07056) || !rows_step(s, 0xC0A921, false))
            return false;
        obj = (FieldObject)field_current_object_slot(s);
        after_leaving_view(g, obj);
    }
    object_hurry_animation(g, obj);
    m_set_flag(s, CPU_FLAG_C, false);
    return true;
}

void field_update_object_visibility(Snes *s)
{
    if (field_update_visibility(s))
        m_rts(s);
}

// $C0:A947
void field_update_in_window(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, check_on_screen(g, (FieldObject)field_current_object_slot(s)));
    m_set_flag(s, CPU_FLAG_V, bottom_overflows(g, 5));
}

/* The carry says whether the object is on screen; the overflow is that of the
 * lower edge's sum. */
void field_update_object_in_window(Snes *s)
{
    field_update_in_window(s);
    m_rts(s);
}
