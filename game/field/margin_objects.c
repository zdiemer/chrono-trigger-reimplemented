/* Field objects in a wider view's margins.
 *
 * Which objects run is a rule of the game's: an object comes into view --
 * takes a palette slot and a block of sprite tiles, has its animation ticked
 * and its frames drawn, and is put in the sprite table -- only inside the
 * view's window (field/object_view.c, $C0:A88D), two map tiles past the
 * original's screen. A view wider than that would show the map with the
 * rest of its people missing. So, as a wider view's own work, each frame
 * the field finishes lists the objects that can be seen but are out of view
 * and stand where the view could show them, and every vblank shows them in
 * the view's margins (vid_show_a_sprite_in_the_margins), nearest the viewer
 * on top: each in the frame its animation is on, which out of view stands
 * still (the frame it was last placed in, or the first of the animation it
 * was given), drawn from its graphics set and its colours as the frame
 * builder would draw them, at its place on the map.
 *
 * Nothing of this runs the object: no animation timer, no frame queued, no
 * tiles in VRAM, no palette slot. It reads the game's state outside its time
 * and keeps its list beside the game's memory, so the game runs as at the
 * original's view. */
#include "field/margin_objects.h"

#include <string.h>

#include "charge.h"
#include "field/frames.h"
#include "field/graphics.h"
#include "field/palettes.h"
#include "platform/video.h"
#include "vars.h"

/* ---- Which objects ------------------------------------------------------------------------- */

/* An object the field would show if its window reached it: one it updates
 * (not left alone, nor one that only comes and goes with the screen), that
 * can be seen, and that is out of view. Its pictures are where the port draws
 * an object's from: frames the builder draws (not the 24-sprite figure), its
 * descriptions in the ROM, its graphics in the ROM below the last bank or
 * built in bank $7F, its frame tables in their bank. */
enum { FIRST_ROM_BANK = 0xC0, LAST_BANK = 0xFF };

static bool shows_past_the_window(const GameState *g, FieldObject obj)
{
    ObjectRole role = object_role(g, obj);
    if (role.disabled || role.kind == ROLE_LEFT_ALONE || role.kind == ROLE_WINDOW_ONLY)
        return false;
    if (!object_can_be_seen(g, obj) || object_view(g, obj) != OUT_OF_VIEW)
        return false;
    ObjectGraphics set = object_graphics(g, obj);
    return object_sprite_layout(g, obj) != LAYOUT_24 && object_frame_descriptions(g, obj).bank >= FIRST_ROM_BANK &&
           (set.bank == FIELD_BUILD_BANK || (set.bank >= FIRST_ROM_BANK && set.bank != LAST_BANK)) &&
           object_frame_tables_in_bank(g, obj);
}

/* How far past the view an object's point can stand and still have a sprite
 * in it: a frame's sprites are offset from the point by a signed byte. */
enum { REACH = 128 };

static bool near_the_view(VidView view, ScreenPoint at)
{
    return at.x >= vid_view_left(view) - REACH && at.x < vid_view_right(view) + REACH && at.y >= -REACH &&
           at.y < (int)view.height + REACH;
}

/* Into the list, nearest the viewer (lowest on the screen) first, as the
 * sprite table draws the rows from the bottom of the screen up. */
static void list(FieldObjectsPast *past, FieldObjectPast o)
{
    unsigned k = past->count++;
    for (; k > 0 && past->object[k - 1].y < o.y; k--)
        past->object[k] = past->object[k - 1];
    past->object[k] = o;
}

void field_forget_the_objects_past_the_window(GameState *g) { field_objects_past(g)->count = 0; }

void field_list_the_objects_past_the_window(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_wider(view))
        return;
    FieldObjectsPast *past = field_objects_past(g);
    uint32_t mark = charge_uncounted_begin(g);
    past->count = 0;
    uint8_t count = event_object_count(g);
    for (FieldObject obj = 0; obj < count && obj < FIELD_OBJECTS; obj++) {
        if (!shows_past_the_window(g, obj))
            continue;
        ScreenPoint at = object_place_on_the_screen(g, obj);
        ObjectFrame frame = object_frame_shown(g, obj);
        if (near_the_view(view, at) && frame.has_frame)
            list(past, (FieldObjectPast){ obj, frame.frame, at.x, at.y });
    }
    charge_uncounted_end(g, mark);
}

/* ---- Showing them -------------------------------------------------------------------------- */

/* A sprite's attributes, as the frames keep them: its palette and its
 * priority. */
enum { ATTRIBUTE_PALETTE_SHIFT = 1, ATTRIBUTE_PRIORITY_SHIFT = 4, PALETTE_BITS = 7, PRIORITY_BITS = 3 };

/* Whether a sprite at (x, y) shows in the view's margins: in the view, and
 * not all of it on the original's screen, which the sprite table alone
 * draws. */
static bool in_the_margins(VidView view, int x, int y)
{
    return x + VID_PICTURE_DOTS > vid_view_left(view) && x < vid_view_right(view) && y + VID_PICTURE_DOTS > 0 &&
           y < (int)view.height && (x < 0 || x + VID_PICTURE_DOTS > VID_ORIGINAL_VIEW_WIDTH);
}

static void show_sprite(const GameState *g, Platform *pf, ObjectGraphics set, const FrameSpriteLook *look,
                        const uint16_t colour[FIELD_COLOURS_A_PALETTE], int16_t x, int16_t y)
{
    VidSpritePicture picture;
    memcpy(picture.colour, colour, sizeof picture.colour);
    for (unsigned q = 0; q < FIELD_SPRITE_QUARTERS; q++) {
        if (look->has_tile[q])
            field_read_frame_tile(g, set, look->tile[q], picture.tile[q]);
        else
            memset(picture.tile[q], 0, sizeof picture.tile[q]);
    }
    VidMarginSprite s = { x, y, 0, (uint8_t)(look->attributes >> ATTRIBUTE_PALETTE_SHIFT & PALETTE_BITS),
                          (uint8_t)(look->attributes >> ATTRIBUTE_PRIORITY_SHIFT & PRIORITY_BITS), false, false, true };
    vid_show_a_sprite_in_the_margins(pf, &s, &picture);
}

static void show_object(const GameState *g, Platform *pf, VidView view, const FieldObjectPast *o)
{
    FrameSpriteLook looks[FIELD_FRAME_MOST_SPRITES];
    unsigned sprites = object_frame_look(g, o->object, o->frame, looks);
    uint16_t colour[FIELD_COLOURS_A_PALETTE];
    object_colours(g, o->object, colour);
    ObjectGraphics set = object_graphics(g, o->object);
    for (unsigned n = 0; n < sprites; n++) {
        int16_t x = (int16_t)(o->x + looks[n].x), y = (int16_t)(o->y + looks[n].y);
        if (in_the_margins(view, x, y))
            show_sprite(g, pf, set, &looks[n], colour, x, y);
    }
}

void field_show_the_objects_past_the_window(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    const FieldObjectsPast *past = field_objects_past(g);
    if (!vid_view_is_wider(view) || past->count == 0)
        return;
    uint32_t mark = charge_uncounted_begin(g);
    for (unsigned k = 0; k < past->count; k++)
        show_object(g, pf, view, &past->object[k]);
    charge_uncounted_end(g, mark);
}
