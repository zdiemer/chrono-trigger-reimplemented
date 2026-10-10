/* What the scenes played on the sprite-object mode's map call by address
 * from their scripts (menu/object_script.c's call command), and the objects
 * of theirs with handlers of their own.
 *
 * A routine a script calls answers in the mode's first cell
 * (menu_object_scratch_answer), which the script's next command tests: that
 * is how the scripts take a decision they have no command for. */
#include "menu/map_scenes.h"

#include "charge.h"
#include "menu/map.h"
#include "menu/objects.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- Whether an object is in view ---------------------------------------------------------------- */

/* The view the map is shown in: the original's screen, with a margin round
 * it in which an object still counts as in view (its sprites reach into the
 * screen from there): sixteen pixels left, right and above, thirty-two
 * below. */
enum { VIEW_WIDTH = 256, VIEW_HEIGHT = 224, MARGIN = 16, MARGIN_BELOW = 32 };

/* Where the object is from the view's top-left, round the map, and moved
 * on by the margin, so that what is in view counts from 0. */
static uint16_t into_view(uint16_t at, uint16_t scroll, uint16_t round)
{
    int16_t from = (int16_t)(at - scroll + MARGIN);
    return (uint16_t)(from < 0 ? from + round : from);
}

static void leave_measured(GameState *g, uint16_t across, uint16_t down)
{
    set_menu_object_scratch_view_across(g, across);
    set_menu_object_scratch_view_down(g, down);
}

// $C2:78A1
/* Whether the running object is in view of the map's second plane. */
bool menu_scene_object_in_view(GameState *g)
{
    MenuObject self = menu_object_running(g);
    uint16_t across = into_view(menu_object_slots_x_pixel(g, self), menu_object_shadows_bg2_scroll_x(g), MENU_MAP_WIDTH);
    uint16_t down = into_view(menu_object_slots_y_pixel(g, self), menu_object_shadows_bg2_scroll_y(g), MENU_MAP_HEIGHT);
    ORIGINAL_EFFECT(leave_measured(g, across, down));
    return across < VIEW_WIDTH + 2 * MARGIN && down < VIEW_HEIGHT + MARGIN + MARGIN_BELOW;
}

/* ---- A drifting map ---------------------------------------------------------------------------- */

// $C2:7849
/* An object that drifts the map's first plane along, a tile across every
 * fourth frame, for as long as the scene keeps it. */
void menu_scene_drift_the_map(GameState *g, MenuObject self)
{
    enum { EVERY_FOURTH = 0x03, FIRST_PLANE = 1, A_TILE = 1 };
    uint8_t count = (uint8_t)(menu_object_slots_drift_count(g, self) + 1);
    set_menu_object_slots_drift_count(g, self, count);
    if (count & EVERY_FOURTH)
        return;
    ORIGINAL_EFFECT(set_menu_object_scratch_plane(g, FIRST_PLANE));
    ORIGINAL_EFFECT(set_menu_object_scratch_step(g, A_TILE));
    TIMED_AS(g, 0xC20568, menu_map_scroll(g, FIRST_PLANE, MENU_ACROSS, A_TILE));
}

/* ---- The entries ------------------------------------------------------------------------------- */

/* The routine leaves the running object's slot in X and the carry of its
 * last comparison (set out of view). The script's call command goes on to
 * its length, 3, and the interpreter after it reads neither (it loads X
 * again and clears the carry for the next command), but liveness follows
 * the interpreter's branch for a length of 0, which the call commands never
 * return, and keeps them live there: they are left for shadow mode's
 * comparison at the command's return. */
static void leave_in_view(Snes *s, bool in_view)
{
    s->cpu.x = menu_object_current(state_of(s));
    m_set_flag(s, CPU_FLAG_C, !in_view);
}

// $C2:78A1
/* The answer goes to the mode's first cell, which the script tests next. */
void menu_scene_object_in_view_entry(Snes *s)
{
    GameState *g = state_of(s);
    bool in_view = menu_scene_object_in_view(g);
    set_menu_object_scratch_answer(g, in_view);
    ORIGINAL_EFFECT(leave_in_view(s, in_view));
    m_rts(s);
}

// $C2:7849
/* A handler, never done: the carry clear, which the objects' loop reads
 * (the width it leaves, the loop sets again: DC_WIDTH). */
void menu_scene_drift_the_map_entry(Snes *s)
{
    GameState *g = state_of(s);
    menu_scene_drift_the_map(g, menu_object_running(g));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

bool menu_scene_object_in_view_runs(Snes *s) { return menu_object_can_move(s); }

/* It scrolls the plane by a tile, which the map keeps room for. */
bool menu_scene_drift_runs(Snes *s)
{
    if (!menu_object_handler_runs(s))
        return false;
    vars_peek(true);
    bool ok = menu_map_can_scroll(state_of(s), 1, menu_map_scroll_records(MENU_ACROSS, 1));
    vars_peek(false);
    return ok;
}
