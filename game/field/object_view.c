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
#include "field/dialog_window.h"
#include "field/field.h"
#include "field/object.h"
#include "field/palettes.h"
#include "field/cast.h"
#include "field/sprites.h"
#include "field/object_update.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "ram.h"
#include "field/bridge.h"
#include "port/vars_ram.h"
#include "vars.h"

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

/* ---- The in-view flag: its storage ---------------------------------------------------------- */

/* The original keeps a flag byte, $80 in view and 0 out of it; a row's link
 * can overwrite it (object.h). */
enum { IN_VIEW_BIT = 0x80, IN_VIEW_BYTE = 0x80, OUT_OF_VIEW_BYTE = 0x00 };

static ObjectView view_of(uint8_t stored)
{
    if (stored & IN_VIEW_BIT)
        return IN_VIEW;
    return stored == OUT_OF_VIEW_BYTE ? OUT_OF_VIEW : PRESENT_WITHOUT_UPDATES;
}

ObjectView object_view(const GameState *g, FieldObject obj) { return view_of(field_object_in_view(g, obj)); }

ObjectView object_view_past_the_tables(const GameState *g, FieldObject slot)
{
    return view_of(field_object_in_view_unchecked(g, slot));
}

void object_set_in_view(GameState *g, FieldObject obj, bool in_view)
{
    set_field_object_in_view(g, obj, in_view ? IN_VIEW_BYTE : OUT_OF_VIEW_BYTE);
}

/* Removing a row's tail also changes another object's visibility. This
 * happens to object zero during Lara's rescue: it stops updating, while
 * contact, scripts and sprite selection still regard it as present. When
 * the predecessor is object zero it becomes wholly out of view instead.
 * Preserve this observable rule independently of the old adjacent tables:
 * the end marker selects the affected object, and the predecessor supplies
 * its stored visibility. A normal visibility update replaces it again. */
void object_apply_row_tail_visibility(GameState *g, FieldLink end, FieldLink predecessor)
{
    FieldObject affected = (FieldObject)(field_linked_object(end) - FIELD_OBJECTS);
    set_field_object_in_view(g, affected, field_link_byte(predecessor));
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
 * starting afresh, its sprite records to be built, and its next frame drawn
 * whatever it is. */
static void come_into_view(GameState *g, FieldObject obj)
{
    object_out_of_rows(g, obj);
    object_set_in_view(g, obj, true);
    object_restart_animation(g, obj);
    object_records_to_build(g, obj);
    object_forget_frame_drawn(g, obj);
}

static void take_what_it_needs(GameState *g, FieldObject obj);

/* ---- The screen's start ------------------------------------------------------------------- */

// $C0:A7E9
void objects_all_out_of_view(GameState *g)
{
    unsigned objects = event_object_count(g);
    for (FieldObject obj = 0; obj < objects; obj++) {
        object_set_in_view(g, obj, false);
        object_forget_frame_drawn(g, obj);
    }
    charge_io_accesses(g, 3); /* the completed object cursor */
}

/* The window an object is in view in at the screen's start: the screen
 * widened four tiles to either side and five below, held at the map's top
 * and left (an edge past the middle of the map counts as off its top or
 * left, as the original's signed test has it); unlike the view's window as
 * the screen moves, nothing above. */
static ViewWindow start_window(const GameState *g)
{
    uint8_t left = (uint8_t)(field_screen_left(g) - WIDER_ACROSS), top = field_screen_top(g);
    return (ViewWindow){ (int8_t)left < 0 ? 0 : left, (uint8_t)(field_screen_right(g) + WIDER_ACROSS),
                         (int8_t)top < 0 ? 0 : top, (uint8_t)(field_screen_bottom(g) + WIDER_BELOW) };
}

// $C0:B127
/* At a map's screen's start, an object that shows comes into view where it
 * is in the start's window, taking its palette and its block as it does
 * whenever it comes into view. True where it did. */
bool object_in_view_at_the_start(GameState *g, FieldObject obj)
{
    if (!object_can_be_seen(g, obj))
        return false;
    ViewWindow w = start_window(g);
    charge_accesses(g, 4); /* the start window's temporary edges */
    if (!in_window(g, w, obj))
        return false;
    come_into_view(g, obj);
    take_what_it_needs(g, obj);
    return true;
}

/* Left view: a dialog waiting on the object ends, unless the dialog box has
 * its place. */
static void after_leaving_view(GameState *g, FieldObject obj)
{
    if (event_object_wait_dialog(g, obj) != 0 && dialog_box_position(g) == 0)
        dialog_end(g);
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
    charge_accesses(g, 4); /* the four temporary window edges */
    uint8_t x = tile_x2(g, obj), y = tile_y2(g, obj);
    bool inside = (int8_t)(x - w.left) >= 0 && (int8_t)(x - w.right) < 0 && (int8_t)(y - w.top) >= 0 &&
                  (int8_t)(y - w.bottom) < 0;
    object_set_in_view(g, obj, inside);
    return inside;
}

/* Coming into view, the object takes its palette slot and its block of
 * sprite tiles (field/palettes.c, field/cast.c) and joins its row; leaving,
 * it lets go of its frame areas, its slot and its block, and leaves its row. */
static void take_what_it_needs(GameState *g, FieldObject obj)
{
    TIMED_AS(g, FIELD_TAKE_PALETTE_ENTRY, (void)object_take_palette(g, obj));
    TIMED_AS(g, FIELD_TAKE_SPRITE_BLOCK_ENTRY, (void)object_take_sprite_block(g, obj));
}

static void give_up_what_it_had(GameState *g, FieldObject obj)
{
    TIMED_AS(g, FIELD_RELEASE_FRAME_AREAS_ENTRY, sprites_release_frame_areas(g, obj));
    TIMED_AS(g, FIELD_GIVE_UP_PALETTE_ENTRY, object_give_up_palette(g, obj));
    TIMED_AS(g, FIELD_GIVE_UP_SPRITE_BLOCK_ENTRY, object_give_up_sprite_block(g, obj));
}

/* ---- The view step ------------------------------------------------------------------------ */

typedef enum { OUT_OF_SIGHT, IN_SIGHT, VIEW_WENT_ELSEWHERE } ViewUpdate;

// $C0:A88D
/* Every frame, for each object the update runs: the object comes into view
 * when it is inside the view's window and can be seen, taking what it needs
 * and joining its row; it leaves view when it is outside, giving those up,
 * leaving its row and ending a dialog waiting on it; and out of view, a
 * one-off animation it was playing is hurried to its end. Its row joined or
 * left is the one step it makes that the port doesn't always take. */
static ViewUpdate update_view(GameState *g, FieldObject obj)
{
    bool disabled = object_disabled(g, obj);
    bool shown = !disabled && (int8_t)field_object_sprite_flag(g, obj) > 0;
    /* The view step's calibrated time counts the visibility's read twice;
     * the second is charged without being made again. */
    charge_as(g, 0xC0A88D, disabled ? 1 : 2);
    if (shown)
        charge_accesses(g, 8);
    bool inside = shown && in_window(g, view_window(g), obj);
    bool was_in_view = object_in_view(g, obj);
    if (inside) {
        if (!was_in_view) {
            come_into_view(g, obj);
            take_what_it_needs(g, obj);
            if (!object_rows_step(g, obj, true))
                return VIEW_WENT_ELSEWHERE;
        }
        return IN_SIGHT;
    }
    if (was_in_view) {
        object_set_in_view(g, obj, false);
        give_up_what_it_had(g, obj);
        if (!object_rows_step(g, obj, false))
            return VIEW_WENT_ELSEWHERE;
        after_leaving_view(g, obj);
    }
    object_hurry_animation(g, obj);
    return OUT_OF_SIGHT;
}

bool field_view_changes(const GameState *g, unsigned obj)
{
    return belongs_in_view(g, (FieldObject)obj) != object_in_view(g, (FieldObject)obj);
}

/* Whether what coming into view or leaving it reads of the tables is in
 * them: the palette slots it looks at, its palette's sharers, its kind and
 * its block (which the port checks before the view changes). */
bool field_view_change_in_the_tables(const GameState *g, FieldObject obj)
{
    if (object_in_view(g, obj))
        return object_palette_links_safe_to_give_up(g, obj) && object_sprite_block_in_the_tables(g, obj);
    ObjectRole role = object_role(g, obj);
    return object_takes_a_palette_the_tables_hold(g, obj) && (role.disabled || role.kind <= ROLE_LEFT_ALONE);
}

/* ---- The view step, where the port takes it ------------------------------------------------- */

enum { VIEW = 0xC0A88D };

bool object_view_updated(GameState *g, FieldObject obj)
{
    if (field_view_changes(g, obj) && !PORT_CHECK(field_view_change_in_the_tables(g, obj))) {
        program_breaks_a_contract(g, VIEW, "an object coming into view or leaving it with tables the port doesn't read");
        return false;
    }
    return update_view(g, obj) != VIEW_WENT_ELSEWHERE;
}

/* An object that matters only on the screen, checked against it. */
void object_window_only_checked(GameState *g, FieldObject obj) { (void)check_on_screen(g, obj); }

