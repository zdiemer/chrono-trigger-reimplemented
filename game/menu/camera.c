/* The menus: a smaller view's camera (menu/camera.h). */
#include "menu/camera.h"

#include "charge.h"
#include "core/view_framing.h"
#include "menu/cursor.h"
#include "overworld/setup.h"
#include "vars.h"

/* The cursor's place is its hand's top left; the camera keeps what it
 * points at, a little right of it and below, in frame. */
enum { POINTED_AT_RIGHT = 16, POINTED_AT_BELOW = 4 };

static ViewFocus what_the_player_follows(const GameState *g)
{
    if (overworld_on_a_world_map(g) || !menu_cursor_can_be_placed(g))
        return view_screen_middle();
    uint8_t at = menu_cursor_index(g);
    return (ViewFocus){ (int16_t)(menu_cursor_places_x(g, at) + POINTED_AT_RIGHT),
                        (int16_t)(menu_cursor_places_y(g, at) + POINTED_AT_BELOW) };
}

void menu_frame_the_view(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    uint32_t mark = charge_uncounted_begin(g);
    view_follow(pf, what_the_player_follows(g), view_all_lines(view));
    charge_uncounted_end(g, mark);
}
