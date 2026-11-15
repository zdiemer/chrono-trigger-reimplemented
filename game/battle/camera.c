/* Battle: a smaller view's camera (core/view_framing.h).
 *
 * A battle is fought on the field's screen, the combatants wherever they
 * stand on it, and the battle window across its foot. A smaller view keeps
 * the window whole at its own foot, and frames the combatants in the lines
 * above it: the middle of the box round every combatant that is in the
 * fight and drawn, or the screen's middle while none is. */
#include "battle/camera.h"

#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/combatant.h"
#include "charge.h"
#include "core/view_framing.h"
#include "vars.h"

/* The window's lines on the screen, as its line tables lay it out: 48 lines
 * from line 160, above the screen's last 16. */
enum { WINDOW_TOP = 160, WINDOW_LINES = 48 };
/* A combatant's point is at its feet; the box takes its middle. */
enum { MIDDLE_ABOVE_THE_FEET = 16 };

static ViewFocus the_combatants_middle(const GameState *g)
{
    int left = VID_ORIGINAL_VIEW_WIDTH, right = -1, top = VID_ORIGINAL_VIEW_HEIGHT, bottom = -1;
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!combatant_in_fight(g, slot) || battle_object_hidden(g, slot))
            continue;
        unsigned obj = battle_combatant_object(slot);
        int x = battle_object_x(g, obj), y = battle_object_y(g, obj) - MIDDLE_ABOVE_THE_FEET;
        left = x < left ? x : left;
        right = x > right ? x : right;
        top = y < top ? y : top;
        bottom = y > bottom ? y : bottom;
    }
    if (right < 0)
        return view_screen_middle();
    return (ViewFocus){ (int16_t)((left + right) / 2), (int16_t)((top + bottom) / 2) };
}

void battle_frame_the_view(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    uint32_t mark = charge_uncounted_begin(g);
    int16_t window_at = (int16_t)(view.height - WINDOW_LINES);
    view_keep_lines(pf, WINDOW_TOP, WINDOW_LINES, window_at);
    view_centre_on(pf, the_combatants_middle(g), (ViewLines){ 0, (uint16_t)window_at });
    charge_uncounted_end(g, mark);
}
