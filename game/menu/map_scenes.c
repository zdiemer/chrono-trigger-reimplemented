/* What the scenes played on the sprite-object mode's map call by address
 * from their scripts (menu/object_script.c's call command), and the objects
 * of theirs with handlers of their own.
 *
 * A routine a script calls answers in the mode's first cell
 * (menu_object_scratch_answer), which the script's next command tests: that
 * is how the scripts take a decision they have no command for. */
#include "menu/effect_noise.h"
#include "menu/map_scenes.h"

#include "charge.h"
#include "menu/handlers.h"
#include "menu/map.h"
#include "menu/objects.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- Whether an object is in view ---------------------------------------------------------------- */

/* The view the map is shown in: the original's screen, with a margin round
 * it in which an object still counts as in view (its sprites reach into the
 * screen from there): sixteen pixels left, right and above, thirty-two
 * below. A rule, not presentation: the scripts branch on the answer, so it
 * stays the original's screen whatever the host shows (vid_view). */
enum { VIEW_WIDTH = 256, VIEW_HEIGHT = 224, MARGIN = 16, MARGIN_BELOW = 32 };

/* Where the object is from the view's top-left, round the map, and moved
 * on by the margin, so that what is in view counts from 0. */
static uint16_t into_view(uint16_t at, uint16_t scroll, uint16_t round)
{
    int16_t from = (int16_t)(at - scroll + MARGIN);
    return (uint16_t)(from < 0 ? from + round : from);
}

// $C2:78A1
/* Whether the running object is in view of the map's second plane. */
bool menu_scene_object_in_view(GameState *g)
{
    MenuObject self = menu_object_running(g);
    uint16_t across = into_view(menu_object_slots_x_pixel(g, self), menu_object_shadows_bg2_scroll_x(g), MENU_MAP_WIDTH);
    uint16_t down = into_view(menu_object_slots_y_pixel(g, self), menu_object_shadows_bg2_scroll_y(g), MENU_MAP_HEIGHT);
    menu_note_effect_noise(g, across);
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
    TIMED_AS(g, 0xC20568, menu_map_scroll(g, FIRST_PLANE, MENU_ACROSS, A_TILE));
}

/* ---- Where the Epoch arrives -------------------------------------------------------------------- */

// $C6:EE31
/* A scene of the Epoch arriving somewhere through time keeps where in the
 * story (story_epoch_arrivals), for the scenes after it to go by: the script
 * hands the place over in the mode's first cell, where the routines it calls
 * answer it too. */
void menu_scene_note_the_epoch_s_arrival(GameState *g) { set_story_epoch_arrivals(g, menu_object_scratch_answer(g)); }

/* A routine a script calls to take a decision answers in the mode's first
 * cell, which the script's next command tests. */
static bool answer_whether_in_view(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    set_menu_object_scratch_answer(g, menu_scene_object_in_view(g));
    return true;
}

static bool note_the_epoch_s_arrival(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    menu_scene_note_the_epoch_s_arrival(g);
    return true;
}

MENU_SCRIPT_ROUTINES(menu_map_scene_routines,
    { 0xC278A1, "whether the object is in view", NULL, answer_whether_in_view },
    { 0xC6EE31, "the Epoch's arrival noted", NULL, note_the_epoch_s_arrival },
);

/* It scrolls the plane by a tile, which the map keeps room for. */
bool menu_scene_drift_takes(const GameState *g, MenuObject self)
{
    (void)self;
    return menu_map_can_scroll(g, 1, menu_map_scroll_records(MENU_ACROSS, 1));
}
