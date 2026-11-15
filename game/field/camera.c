/* Field mode: a smaller view's camera (field/camera.h). */
#include "field/camera.h"

#include "charge.h"
#include "core/view_framing.h"
#include "event/party.h"
#include "field/field.h"
#include "field/line_effects.h"
#include "field/object.h"
#include "field/party.h"

/* The leader's point is at its feet; the camera frames its middle. */
enum { LEADER_MIDDLE_ABOVE_ITS_FEET = 16 };

static ViewFocus what_the_player_follows(const GameState *g)
{
    if (!place_taken(g, PARTY_LEADER))
        return view_screen_middle();
    FieldObject leader = member_object(g, PARTY_LEADER);
    if (!object_can_be_seen(g, leader) || object_view(g, leader) == OUT_OF_VIEW)
        return view_screen_middle();
    ScreenPoint at = object_place_on_the_screen(g, leader);
    if (at.x < 0 || at.x >= VID_ORIGINAL_VIEW_WIDTH || at.y < 0 || at.y >= VID_ORIGINAL_VIEW_HEIGHT)
        return view_screen_middle();
    return (ViewFocus){ at.x, (int16_t)(at.y - LEADER_MIDDLE_ABOVE_ITS_FEET) };
}

/* The dialog window kept in view at the view's top or bottom, its middle
 * as far in as the window opens, so that it opens and shuts there as on the
 * screen; the lines it leaves open are the camera's. While it is placed
 * the camera keeps to those lines, whether it is open yet or not. */
static ViewLines keep_the_dialog_in_view(const GameState *g, Platform *pf, VidView view)
{
    WindowPosition at = dialog_window_at(g);
    if (at != WINDOW_AT_TOP && at != WINDOW_AT_BOTTOM)
        return view_all_lines(view);
    FieldWindowBand band = field_dialog_window_band(g, (uint8_t)at);
    int16_t middle = at == WINDOW_AT_TOP ? (int16_t)band.full : (int16_t)(view.height - band.full);
    view_keep_lines(pf, (int16_t)(band.middle - band.half), (uint16_t)(2 * band.half), (int16_t)(middle - band.half));
    uint16_t open = (uint16_t)(view.height - 2 * band.full);
    return at == WINDOW_AT_TOP ? (ViewLines){ (int16_t)(2 * band.full), open } : (ViewLines){ 0, open };
}

void field_frame_the_view(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    uint32_t mark = charge_uncounted_begin(g);
    ViewLines open = keep_the_dialog_in_view(g, pf, view);
    view_follow(pf, what_the_player_follows(g), open);
    charge_uncounted_end(g, mark);
}
