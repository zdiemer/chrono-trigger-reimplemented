/* Field objects coming into view and leaving it.
 *
 * Only objects in view take part in the field's frame: the object update
 * brings each object into view when it is inside a window a little larger
 * than the screen (so that it is set up before it shows), and out of view
 * when it leaves. Coming into view, it starts its animation afresh, is given
 * a palette and a block of sprite records, and joins its row; leaving, those
 * are released, it leaves its row, a dialog waiting on it is ended, and a
 * one-off animation it was playing is cut to its last pass.
 *
 * Objects that only matter while on screen (a role of their own) are simply
 * marked in view or not by the screen itself. */
#include "field/field.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { DIALOG_ENDED_BY_LEAVING = 0x0D };   /* dialog_state */

/* The window objects come into view in: the screen's edges, in tiles doubled
 * as the positions are compared, widened a little. */
typedef struct {
    uint8_t left, right, top, bottom;
} ViewWindow;

static uint8_t widen_down(uint8_t v, uint8_t by) { return v >= by ? (uint8_t)(v - by) : 0; }
static uint8_t widen_up(uint8_t v, uint8_t by) { return v + by > 0xFF ? 0xFF : (uint8_t)(v + by); }

/* Where the object is, in tiles doubled. */
static uint8_t tile_x2(const GameState *g, FieldObject obj) { return (uint8_t)(field_object_x_tile(g, obj) << 1); }
static uint8_t tile_y2(const GameState *g, FieldObject obj) { return (uint8_t)(field_object_y_tile(g, obj) << 1); }

/* For the original's code and shadow mode (field/scratch.h): the window's
 * edges, which it keeps at $C3-$C6. */
static void leave_window(GameState *g, ViewWindow w)
{
    original_cell8(g, 0xC3, w.left);
    original_cell8(g, 0xC4, w.right);
    original_cell8(g, 0xC5, w.top);
    original_cell8(g, 0xC6, w.bottom);
}

/* ---- The in-view flag: its storage ---------------------------------------------------------- */

/* The original keeps a flag byte, $80 in view and 0 out of it; a row's link
 * can overwrite it (object.h). */
enum { IN_VIEW_BIT = 0x80, IN_VIEW_BYTE = 0x80, OUT_OF_VIEW_BYTE = 0x00 };

ObjectView object_view(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_in_view(g, obj);
    if (stored & IN_VIEW_BIT)
        return IN_VIEW;
    return stored == OUT_OF_VIEW_BYTE ? OUT_OF_VIEW : OVERWRITTEN_BY_A_LINK;
}

void object_set_in_view(GameState *g, FieldObject obj, bool in_view)
{
    set_field_object_in_view(g, obj, in_view ? IN_VIEW_BYTE : OUT_OF_VIEW_BYTE);
}

/* ---- The screen ------------------------------------------------------------------------------ */

/* The screen's left and top edges, in the objects' tiles (the field keeps
 * them in 8-pixel tiles, objects stand on 16-pixel ones). */
static uint8_t screen_left_tile(const GameState *g) { return (uint8_t)(field_screen_left(g) >> 1); }
static uint8_t screen_top_tile(const GameState *g) { return (uint8_t)(field_screen_top(g) >> 1); }

/* How far in from the screen's edges a tile must be to be well on screen:
 * past the first two tiles, short of the last two or three. The distances
 * are bytes, so a tile left of the screen is far past its right. */
enum { WELL_IN = 2, WELL_ACROSS = 14, WELL_DOWN = 13 };

static bool well_inside(uint8_t tile, uint8_t edge, uint8_t extent)
{
    uint8_t in = (uint8_t)(tile - edge);
    return in >= WELL_IN && in < extent;
}

// $C0:6655
bool tile_well_on_screen(const GameState *g, MapTile tile)
{
    return well_inside(tile.x, screen_left_tile(g), WELL_ACROSS) && well_inside(tile.y, screen_top_tile(g), WELL_DOWN);
}

/* ---- Coming into view and leaving it ------------------------------------------------------ */

/* How far past the screen's edges the window objects come into view in
 * reaches, in the doubled tiles the edges are compared in: to either side,
 * above and below. */
enum { WIDER_ACROSS = 4, WIDER_ABOVE = 2, WIDER_BELOW = 5 };

/* The window objects come into view in: the screen's, widened, held to the
 * map's edges. */
static ViewWindow view_window(const GameState *g)
{
    return (ViewWindow){ widen_down(field_screen_left(g), WIDER_ACROSS), widen_up(field_screen_right(g), WIDER_ACROSS),
                         widen_down(field_screen_top(g), WIDER_ABOVE), widen_up(field_screen_bottom(g), WIDER_BELOW) };
}

static bool in_window(const GameState *g, ViewWindow w, FieldObject obj)
{
    uint8_t x = tile_x2(g, obj), y = tile_y2(g, obj);
    return x >= w.left && x < w.right && y >= w.top && y < w.bottom;
}

/* Whether the object belongs in view now. */
static bool belongs_in_view(const GameState *g, FieldObject obj)
{
    return object_can_be_seen(g, obj) && in_window(g, view_window(g), obj);
}

/* Coming into view: it is in view, in no row yet, with its animation
 * starting afresh, its sprite records to be built, and no place kept for a
 * battle. */
static void come_into_view(GameState *g, FieldObject obj)
{
    object_out_of_rows(g, obj);
    object_set_in_view(g, obj, true);
    object_restart_animation(g, obj);
    object_records_to_build(g, obj);
    object_let_the_battle_place(g, obj);
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
 * below. Each edge is compared by the difference's sign as a byte, as the
 * original compares, so the window wraps round the map's edges. */
static bool check_on_screen(GameState *g, FieldObject obj)
{
    ViewWindow w = { (uint8_t)(field_screen_left(g) - WIDER_ACROSS), (uint8_t)(field_screen_right(g) + WIDER_ACROSS),
                     field_screen_top(g), (uint8_t)(field_screen_bottom(g) + WIDER_BELOW) };
    ORIGINAL_EFFECT(leave_window(g, w));
    uint8_t x = tile_x2(g, obj), y = tile_y2(g, obj);
    bool inside = (int8_t)(x - w.left) >= 0 && (int8_t)(x - w.right) < 0 && (int8_t)(y - w.top) >= 0 &&
                  (int8_t)(y - w.bottom) < 0;
    object_set_in_view(g, obj, inside);
    return inside;
}

/* ---- Where it meets the original ---------------------------------------------------------- */

/* The overflow of the window's lower edge's sum, which the callers read where
 * nothing after it sets one. */
static bool bottom_overflows(const GameState *g, uint8_t by)
{
    return field_add_overflows8(field_screen_bottom(g), by);
}

/* An object coming into view is given a palette and a block of sprite records
 * by the original's routines, each by the object's role, and joins its row;
 * leaving, it gives back its slots in the frame builder, its palette and its
 * sprite records, and leaves its row. The field calls them in turn, going on
 * where each came back with the machine as it runs, and stopping at the
 * call's return otherwise. None of them reads the registers it is entered
 * with but the overflow. */
enum {
    GIVE_PALETTE = 0xC07170,          /* a palette slot (of the six at $0B80), its colors loaded */
    GIVE_SPRITE_RECORDS = 0xC06F9A,   /* a block of sprite records, for the roles that need one */
    FREE_FRAME_SLOTS = 0xC0EA42,      /* its slots among the frame builder's four */
    FREE_PALETTE = 0xC0734C,
    FREE_SPRITE_RECORDS = 0xC07056,
};
enum { VIEW_CALL_DONT_CARE = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool call_out(Snes *s, uint32_t site, uint32_t routine)
{
    return bridge_call(s, site, routine, VIEW_CALL_DONT_CARE, on_the_field_page(s, true));
}

/* The current object joins or leaves its row (field/object_update.c), run
 * as C and timed as the routine it is; where the rows' links are ones its
 * entry's guard would turn away, the entry is called, and the original runs
 * it. */
static bool rows_step(Snes *s, uint32_t site, FieldObject obj, bool join)
{
    if (field_rows_step_as_c(s, obj, join))
        return true;
    return join ? call_out(s, site, FIELD_JOIN_ROW_ENTRY) : call_out(s, site, FIELD_LEAVE_ROW_ENTRY);
}

bool field_view_changes(const GameState *g, unsigned obj)
{
    return belongs_in_view(g, (FieldObject)obj) != object_in_view(g, (FieldObject)obj);
}

// $C0:A88D
/* The carry says whether the object is in view. False where the bridge
 * stopped. The object is the current one, which the routines it calls read. */
bool field_update_visibility(Snes *s, FieldObject obj)
{
    GameState *g = state_of(s);
    bool inside = belongs_in_view(g, obj);
    if (object_can_be_seen(g, obj)) {
        ORIGINAL_EFFECT(leave_window(g, view_window(g)));
        m_set_flag(s, CPU_FLAG_V, bottom_overflows(g, WIDER_BELOW));
    }
    bool was_in_view = object_in_view(g, obj);
    if (inside) {
        if (!was_in_view) {
            come_into_view(g, obj);
            if (!call_out(s, 0xC0A905, GIVE_PALETTE) || !call_out(s, 0xC0A908, GIVE_SPRITE_RECORDS) ||
                !rows_step(s, 0xC0A90B, obj, true))
                return false;
        }
        m_set_flag(s, CPU_FLAG_C, true);
        return true;
    }
    if (was_in_view) {
        object_set_in_view(g, obj, false);
        if (!call_out(s, 0xC0A918, FREE_FRAME_SLOTS) || !call_out(s, 0xC0A91B, FREE_PALETTE) ||
            !call_out(s, 0xC0A91E, FREE_SPRITE_RECORDS) || !rows_step(s, 0xC0A921, obj, false))
            return false;
        after_leaving_view(g, obj);
    }
    object_hurry_animation(g, obj);
    m_set_flag(s, CPU_FLAG_C, false);
    return true;
}

/* The current object's (8-bit X). */
void field_update_object_visibility(Snes *s)
{
    if (field_update_visibility(s, (FieldObject)field_current_object_slot(s)))
        m_rts(s);
}

/* The carry says whether the object is on screen; the overflow is that of the
 * lower edge's sum. */
void field_update_in_window(Snes *s, FieldObject obj)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, check_on_screen(g, obj));
    m_set_flag(s, CPU_FLAG_V, bottom_overflows(g, WIDER_BELOW));
}

void field_update_object_in_window(Snes *s)
{
    field_update_in_window(s, (FieldObject)field_current_object_slot(s));
    m_rts(s);
}
