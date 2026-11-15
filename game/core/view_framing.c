/* A smaller view's framing (core/view_framing.h). */
#include "core/view_framing.h"

enum { THIRDS = 3, GLIDE = 4 };

/* Where along one axis the camera stands, from `aim`, to keep `focus`
 * between the edges of the middle third of the `open` dots from `open_at`
 * in the view. */
static int kept_in_the_middle(int aim, int focus, int open_at, int open)
{
    int near = aim + open_at + open / THIRDS, far = aim + open_at + open - open / THIRDS;
    if (focus < near)
        return aim - (near - focus);
    if (focus > far)
        return aim + (focus - far);
    return aim;
}

/* A quarter of the way from `from` to `to`, at least a dot. */
static int16_t glide(int from, int to)
{
    int step = (to - from) / GLIDE;
    if (step == 0 && to != from)
        step = to > from ? 1 : -1;
    return (int16_t)(from + step);
}

static void glide_to(Platform *pf, VidView view, VidAim now, VidAim to)
{
    to = vid_aim_held_to_the_screen(view, to);
    vid_aim_the_view(pf, (VidAim){ glide(now.left, to.left), glide(now.top, to.top) });
}

void view_follow(Platform *pf, ViewFocus focus, ViewLines open)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    VidAim now = vid_view_aim(pf);
    glide_to(pf, view, now,
             (VidAim){ (int16_t)kept_in_the_middle(now.left, focus.x, 0, view.width),
                       (int16_t)kept_in_the_middle(now.top, focus.y, open.at, open.lines) });
}

void view_centre_on(Platform *pf, ViewFocus focus, ViewLines open)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    glide_to(pf, view, vid_view_aim(pf),
             (VidAim){ (int16_t)(focus.x - view.width / 2), (int16_t)(focus.y - open.at - open.lines / 2) });
}

void view_keep_lines(Platform *pf, int16_t top, uint16_t lines, int16_t at)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view) || lines == 0)
        return;
    VidViewPanel panel = { top, lines, (int16_t)((VID_ORIGINAL_VIEW_WIDTH - view.width) / 2), at };
    vid_keep_a_panel_in_view(pf, &panel);
}
