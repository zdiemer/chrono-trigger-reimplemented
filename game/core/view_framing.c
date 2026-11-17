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

/* The camera's top moved off the screen's lines `open` keeps clear of, to
 * the nearer side, where its lines would show any of them. */
static int16_t clear_of_the_panel(int16_t top, ViewLines open)
{
    int shown = top + open.at, end = shown + open.lines;
    if (open.clear_to <= open.clear_of || end <= open.clear_of || shown >= open.clear_to)
        return top;
    int up = end - open.clear_of, down = open.clear_to - shown;
    return (int16_t)(up <= down ? top - up : top + down);
}

static void glide_to(Platform *pf, VidView view, VidAim now, VidAim to, ViewLines open)
{
    to.top = clear_of_the_panel(to.top, open);
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
                       (int16_t)kept_in_the_middle(now.top, focus.y, open.at, open.lines) },
             open);
}

static VidAim centred_on(VidView view, ViewFocus focus, ViewLines open)
{
    return (VidAim){ (int16_t)(focus.x - view.width / 2), (int16_t)(focus.y - open.at - open.lines / 2) };
}

void view_centre_on(Platform *pf, ViewFocus focus, ViewLines open)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    glide_to(pf, view, vid_view_aim(pf), centred_on(view, focus, open), open);
}

void view_start_centred_on(Platform *pf, ViewFocus focus, ViewLines open)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    VidAim to = centred_on(view, focus, open);
    to.top = clear_of_the_panel(to.top, open);
    vid_aim_the_view(pf, to);
}

void view_keep_lines(Platform *pf, int16_t top, uint16_t lines, int16_t at)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view) || lines == 0)
        return;
    VidViewPanel panel = { .top = top, .lines = lines, .left = vid_view_left(view), .at = at };
    vid_keep_a_panel_in_view(pf, &panel);
}

void view_keep_lines_beside(Platform *pf, int16_t top, uint16_t lines, int16_t at, int16_t column_top,
                            int16_t column_end)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view) || lines == 0)
        return;
    int16_t left = vid_view_left(view);
    VidViewPanel panel = { .top = top, .lines = lines, .left = left, .at = at, .column_top = column_top,
                           .column_end = (int16_t)(column_end - left) };
    vid_keep_a_panel_in_view(pf, &panel);
}

void view_keep_lines_cut(Platform *pf, int16_t top, uint16_t lines, int16_t at, int16_t cut_from, uint16_t cut_dots)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view) || lines == 0)
        return;
    VidViewPanel panel = { .top = top, .lines = lines, .at = at, .cut_at = cut_from, .cut_dots = cut_dots };
    vid_keep_a_panel_in_view(pf, &panel);
}

void view_keep_nothing(Platform *pf, int16_t at, uint16_t lines)
{
    /* the lines below the original's screen, where it shows nothing */
    view_keep_lines(pf, VID_ORIGINAL_VIEW_HEIGHT, lines, at);
}
