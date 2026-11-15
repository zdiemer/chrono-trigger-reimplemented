/* Field mode: a smaller view's camera (field/camera.h). */
#include "field/camera.h"

#include "charge.h"
#include "core/view_framing.h"
#include "event/party.h"
#include "field/dialog_box.h"
#include "field/field.h"
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

void field_frame_the_view(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    uint32_t mark = charge_uncounted_begin(g);
    ViewLines open = field_lay_out_the_dialog_box(g, pf, view);
    view_follow(pf, what_the_player_follows(g), open);
    charge_uncounted_end(g, mark);
}
