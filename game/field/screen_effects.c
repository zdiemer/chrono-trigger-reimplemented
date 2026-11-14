/* Field mode: the screen effects, shapes the windows cut into the picture.
 *
 * A script starts an effect (event $FF, game/event/screen.c) and the field's
 * main loop moves it on a step every frame until it ends itself. Each is a
 * window's shape drawn into the per-line table the window line effects read
 * (field/line_effects.c), and a cut: which layers the window hides, and on
 * which screens, or where it lets the colour blending through.
 * - A hole: a circle in which BG1 and BG2 aren't drawn, opening from a
 *   point to a radius or closing to nothing.
 * - An oval: a circle squashed to half its height, inside which alone the
 *   map's colour blending applies, opening and closing the same way; it ends
 *   with the screen's colour as the field starts it (OVAL_GONE).
 * - The searchlight: the same blending inside a four-cornered shape whose
 *   corners move over a number of frames (event $FE).
 * - The curtain: window 2, whose edge on each line sweeps across by a few
 *   pixels a frame, a ragged edge that uncovers the picture or covers it.
 *   Bank $C3's routine sweeps it; the field cuts the picture with it.
 * - BG3 in a circle at the top of the screen, or blended over the map round
 *   it, each drawn once into both table sets.
 * - The ripple: a copy of BG1, waving, seen through a circle that opens,
 *   holds and closes, over two and a half seconds; and the field's close
 *   into a scene. Both run on over frames of their own, and call on the
 *   field's other work between them (BG1 drawn, the effects refreshed, a
 *   frame waited, the field put aside), as the curtain calls on bank $C3's
 *   sweep: one sequence each, whose calls the program supplies
 *   (EffectCalls).
 *
 * The window tables are double-buffered with the line effects: a frame's
 * shape goes into the table of the set the line effects aren't reading, which
 * the next vblank shows. The circles are scene/circle.c's, the four-cornered
 * shape field/window_shape.c's. */
#include "field/screen_effects.h"
#include "field/battle_start.h"
#include "field/main_loop.h"

#include "charge.h"
#include "core/program.h"
#include "core/display.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/field.h"
#include "field/layers.h"
#include "field/parts.h"
#include "field/streaming.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "scene/scene.h"
#include "vars.h"

/* ---- The cut ----------------------------------------------------------------------------------- */

/* Which window decides each layer of a pair (field_window_shadows): window 1
 * inside its shape or outside it, or window 2 inside. */
enum { W1_INSIDE = VID_WIN_IN, W1_OUTSIDE = VID_WIN_OUT, W2_INSIDE = VID_WIN_IN << 2 };
#define PAIR(first, second) ((uint8_t)((first) | (second) << 4))

/* What an effect's window does: the layers it can hide on the main screen
 * and on the sub screen, and for each pair of layers which window decides
 * (the colour window, beside the sprites, decides where blending applies). */
typedef struct {
    uint8_t hides_on_main, hides_on_sub;
    uint8_t bg1_bg2, bg3_bg4, obj_colour;
} WindowCut;

enum { MAP_AND_SPRITES = VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_OBJ_BIT };

static const WindowCut HOLE = { MAP_AND_SPRITES, MAP_AND_SPRITES, PAIR(W1_INSIDE, W1_INSIDE), 0, 0 };
static const WindowCut BLENDING_INSIDE = { MAP_AND_SPRITES, MAP_AND_SPRITES, 0, 0, PAIR(0, W1_INSIDE) };
static const WindowCut CURTAIN_CUT = { MAP_AND_SPRITES, MAP_AND_SPRITES, PAIR(W2_INSIDE, W2_INSIDE),
                                       PAIR(W2_INSIDE, W2_INSIDE), PAIR(W2_INSIDE, W2_INSIDE) };
static const WindowCut BG3_INSIDE = { MAP_AND_SPRITES, 0, 0, PAIR(W1_OUTSIDE, 0), 0 };
static const WindowCut BG3_ROUND = { MAP_AND_SPRITES, 0, PAIR(0, W1_INSIDE), PAIR(W1_INSIDE, 0), 0 };
static const WindowCut NO_CUT = { 0, 0, 0, 0, 0 };

/* The screens' values outside the dialog window (field_line_screens' first). */
enum { SCREENS_OUTSIDE = 0 };

static void choose_windows(GameState *g, const WindowCut *c)
{
    set_field_window_shadows_bg1_bg2(g, c->bg1_bg2);
    set_field_window_shadows_bg3_bg4(g, c->bg3_bg4);
    set_field_window_shadows_obj_colour(g, c->obj_colour);
}

static void cut(GameState *g, const WindowCut *c)
{
    set_field_line_screens_main_masks(g, SCREENS_OUTSIDE, c->hides_on_main);
    set_field_line_screens_sub_masks(g, SCREENS_OUTSIDE, c->hides_on_sub);
    choose_windows(g, c);
}

/* The blending only inside the window, or only outside it. */
static void blend_inside_only(GameState *g)
{
    set_field_color_math_control(
        g, vid_math_control(&(VidColorMath){ .prevent_math = VID_REGION_OUTSIDE, .sub_screen = true }));
}
static void blend_outside_only(GameState *g)
{
    set_field_color_math_control(
        g, vid_math_control(&(VidColorMath){ .prevent_math = VID_REGION_INSIDE, .sub_screen = true }));
}

static void window_line_effect(GameState *g, FieldLineSlot slot, bool on)
{
    uint8_t bit = (uint8_t)(1u << slot), enabled = field_line_effect_enable(g);
    set_field_line_effect_enable(g, on ? (uint8_t)(enabled | bit) : (uint8_t)(enabled & ~bit));
}

/* ---- The shapes -------------------------------------------------------------------------------- */

/* The window table of the set the line effects aren't reading: they read the
 * second set while any display flag is set (the builders flip the sets every
 * frame). The table's place is a value the circle and the shape take, as the
 * line effects' own tables hold it (field/line_effects.c). */
enum { WINDOW_TABLES_BANK = RAM_FIELD_WINDOW_EDGE_LINES >> 16 };

static uint16_t table_not_shown(const GameState *g)
{
    return field_display_flags(g) != 0 ? RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES)
                                        : RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES_COPY);
}

/* A circle in the table not shown, through bank $C3's routine (scene/circle.c),
 * which takes its centre and radius as bytes in word cells. (The original
 * writes the bytes alone, and the routine clears the cells' high bytes.) */
enum { CIRCLE_ROUTINE = 0xC30008 };

static void draw_circle(GameState *g, uint8_t x, uint8_t y, uint8_t radius, uint8_t mode)
{
    set_scene_circle_center_x(g, x);
    set_scene_circle_center_y(g, y);
    set_scene_circle_radius(g, radius);
    set_scene_circle_table(g, table_not_shown(g));
    set_scene_circle_table_bank(g, WINDOW_TABLES_BANK);
    TIMED_AS(g, CIRCLE_ROUTINE, scene_circle_draw(g, mode));
}

/* ---- Holes and ovals -------------------------------------------------------------------------- */

typedef struct {
    const WindowCut *cut;
    bool blends_inside;
    uint8_t mode;
} CircleKind;

static const CircleKind HOLE_CIRCLE = { &HOLE, false, SCENE_CIRCLE_ROUND };
static const CircleKind OVAL_CIRCLE = { &BLENDING_INSIDE, true, SCENE_CIRCLE_SQUASHED };

static void draw_the_circle(GameState *g, const CircleKind *k, uint8_t radius)
{
    cut(g, k->cut);
    if (k->blends_inside)
        blend_inside_only(g);
    draw_circle(g, field_circle_x(g), field_circle_y(g), radius, k->mode);
    window_line_effect(g, SLOT_WINDOW1, true);
}

void screen_circle_opens(GameState *g, ScreenEffect opening, uint8_t x, uint8_t y, uint8_t radius)
{
    set_field_circle_x(g, x);
    set_field_circle_y(g, y);
    set_field_circle_target_radius(g, radius);
    set_field_circle_radius(g, 0);
    set_field_screen_effect(g, opening);
}

// $C0:220E, $C0:232A
/* A step of a circle opening: a line wider a frame. At its radius the effect
 * ends, and the last frame draws the circle before again, into the other
 * table set, so both sets show it. */
static void circle_opens_a_step(GameState *g, const CircleKind *k)
{
    uint8_t radius = field_circle_radius(g);
    if (radius >= field_circle_target_radius(g)) {
        set_field_screen_effect(g, NO_SCREEN_EFFECT);
        draw_the_circle(g, k, (uint8_t)(radius - 1));
        return;
    }
    draw_the_circle(g, k, radius);
    set_field_circle_radius(g, (uint8_t)(radius + 1));
}

/* The hole gone: the windows cut nothing, and window 1 is off. */
static void hole_gone(GameState *g)
{
    set_field_screen_effect(g, NO_SCREEN_EFFECT);
    cut(g, &NO_CUT);
    window_line_effect(g, SLOT_WINDOW1, false);
}

// $C0:23E2
/* The oval gone: the windows cut nothing, the colour blends as the field
 * starts it, everywhere and with no tint, and window 1 is off. */
static void oval_gone(GameState *g)
{
    set_field_screen_effect(g, NO_SCREEN_EFFECT);
    cut(g, &NO_CUT);
    set_field_color_math_control(g, field_starting_color_math());
    set_field_fixed_color(g, FIELD_NO_TINT);
    set_field_fixed_color_target(g, FIELD_NO_TINT);
    window_line_effect(g, SLOT_WINDOW1, false);
}

// $C0:2269, $C0:238A
/* A step of a circle closing: a line narrower a frame, until it is gone. */
static void circle_closes_a_step(GameState *g, const CircleKind *k, void (*gone)(GameState *))
{
    uint8_t radius = field_circle_radius(g);
    if (radius == 0) {
        gone(g);
        return;
    }
    radius--;
    set_field_circle_radius(g, radius);
    draw_the_circle(g, k, radius);
}

/* ---- The curtain ------------------------------------------------------------------------------ */

/* How the curtain sweeps (scene_curtain_sweep), and which of window 2's edges
 * it moves (field_window2_edge): its left edge, which uncovers the picture
 * rightward and covers it leftward, or its right one; and, where it covers,
 * that the picture stays covered when it is done. The curtain always starts
 * from the first set's table. */
enum { CURTAIN_STILL = 0, SWEEPS_RIGHTWARD = 1, SWEEPS_LEFTWARD = 2 };
enum { MOVES_RIGHT_EDGE = 0x01, LEAVES_IT_COVERED = 0x02 };

static const struct {
    uint8_t sweep, edge;
} curtains[] = {
    [CURTAIN_UNCOVERS_RIGHTWARD] = { SWEEPS_RIGHTWARD, 0 },
    [CURTAIN_COVERS_RIGHTWARD] = { SWEEPS_RIGHTWARD, MOVES_RIGHT_EDGE | LEAVES_IT_COVERED },
    [CURTAIN_UNCOVERS_LEFTWARD] = { SWEEPS_LEFTWARD, MOVES_RIGHT_EDGE },
    [CURTAIN_COVERS_LEFTWARD] = { SWEEPS_LEFTWARD, LEAVES_IT_COVERED },
};

static bool curtain_sweeping(const GameState *g) { return scene_curtain_sweep(g) != CURTAIN_STILL; }
/* The step reads the edge's cell as a number, 2 and more leaving the picture
 * covered, as the four curtains' values have it. */
static bool leaves_it_covered(const GameState *g) { return field_window2_edge(g) >= LEAVES_IT_COVERED; }

enum { SCREEN_LEFT_EDGE = 0, SCREEN_RIGHT_EDGE = 0xFF, FULL_BRIGHTNESS = 0x0F };

/* The curtain's cut: window 2 over every layer, its fixed edges the screen's;
 * the other edge is the curtain's, a line at a time. */
static void cut_with_the_curtain(GameState *g)
{
    cut(g, &CURTAIN_CUT);
    set_field_window_shadows_window2_left(g, SCREEN_LEFT_EDGE);
    set_field_window_shadows_window2_right(g, SCREEN_RIGHT_EDGE);
}

/* After its sweep each frame: window 2's line effect on, the picture at full
 * brightness behind it. */
static void curtain_swept_a_step(GameState *g)
{
    window_line_effect(g, SLOT_WINDOW2, true);
    set_field_brightness(g, FULL_BRIGHTNESS);
}

/* Once it has swept across, a curtain that uncovered the picture is taken
 * away; one that covered it stays. */
static void take_the_curtain_away(GameState *g)
{
    cut(g, &NO_CUT);
    set_field_window_shadows_window2_right(g, SCREEN_LEFT_EDGE);
    window_line_effect(g, SLOT_WINDOW2, false);
}

static void curtain_done(GameState *g)
{
    if (!leaves_it_covered(g))
        take_the_curtain_away(g);
}

// $C0:405D, $C0:40B3, $C0:40EF, $C0:4145
bool screen_curtain_drawn(GameState *g, Curtain c)
{
    ScreenEffect now = field_screen_effect_now(g);
    if (now == NO_SCREEN_EFFECT) {
        if (!curtain_sweeping(g)) {
            set_scene_curtain_steps(g, 0);
            set_scene_curtain_sweep(g, curtains[c].sweep);
            set_scene_curtain_table(g, RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES));
            set_scene_curtain_table_bank(g, WINDOW_TABLES_BANK);
            set_field_screen_effect(g, CURTAIN);
            set_field_window2_edge(g, curtains[c].edge);
        }
        return false;
    }
    if (now != CURTAIN || curtain_sweeping(g))
        return false;
    set_field_screen_effect(g, NO_SCREEN_EFFECT);
    if (curtains[c].edge & LEAVES_IT_COVERED)
        set_field_brightness(g, FIELD_DARK);
    else
        take_the_curtain_away(g);
    return true;
}

/* ---- The searchlight ------------------------------------------------------------------------- */

uint16_t screen_searchlight_speed(uint8_t from, uint8_t to, uint8_t frames)
{
    if (to >= from)
        return (uint16_t)(((to - from) << 8) / frames);
    return (uint16_t)-(((from - to) << 8) / frames);
}

// $C0:3F26
void screen_searchlight_moves(GameState *g, const SearchlightMove *move, uint8_t frames)
{
    set_field_searchlight_frames(g, frames);
    for (unsigned k = 0; k < SEARCHLIGHT_CORNERS; k++) {
        WindowPoint from = move->from[k], to = move->to[k];
        set_field_searchlight_corners_x(g, k, (uint16_t)(from.x << 8));
        set_field_searchlight_corners_y(g, k, (uint16_t)(from.y << 8));
        set_field_searchlight_speeds_x(g, k, screen_searchlight_speed(from.x, to.x, frames));
        set_field_searchlight_speeds_y(g, k, screen_searchlight_speed(from.y, to.y, frames));
    }
    set_field_screen_effect(g, SEARCHLIGHT);
}

/* The shape where the corners are, drawn with field/window_shape.c. */
enum { SHAPE_ROUTINE = 0xC30011 };

static void draw_the_searchlight(GameState *g)
{
    cut(g, &BLENDING_INSIDE);
    blend_inside_only(g);
    for (unsigned k = 0; k < SEARCHLIGHT_CORNERS; k++) {
        set_field_window_shape_corners_x(g, k, (uint8_t)(field_searchlight_corners_x(g, k) >> 8));
        set_field_window_shape_corners_y(g, k, (uint8_t)(field_searchlight_corners_y(g, k) >> 8));
    }
    window_line_effect(g, SLOT_WINDOW1, true);
    set_field_window_shape_table_address(g, table_not_shown(g));
    set_field_window_shape_table_bank(g, WINDOW_TABLES_BANK);
    TIMED_AS(g, SHAPE_ROUTINE, field_draw_window_shape(g));
}

// $C0:2409
/* A frame of the searchlight: drawn where its corners are, then each corner
 * moved on. When its frames are up the effect ends; that frame the corners'
 * across movement stops, but the original lets the down movement run one
 * frame more, which the port keeps (it shows for a frame in the other set). */
static void searchlight_step(GameState *g)
{
    uint8_t frames = field_searchlight_frames(g);
    if (frames != 0) {
        set_field_searchlight_frames(g, (uint8_t)(frames - 1));
    } else {
        set_field_screen_effect(g, NO_SCREEN_EFFECT);
        for (unsigned k = 0; k < SEARCHLIGHT_CORNERS; k++)
            set_field_searchlight_speeds_x(g, k, 0);
    }
    draw_the_searchlight(g);
    for (unsigned k = 0; k < SEARCHLIGHT_CORNERS; k++) {
        set_field_searchlight_corners_x(g, k,
                                        (uint16_t)(field_searchlight_corners_x(g, k) + field_searchlight_speeds_x(g, k)));
        set_field_searchlight_corners_y(g, k,
                                        (uint16_t)(field_searchlight_corners_y(g, k) + field_searchlight_speeds_y(g, k)));
    }
}

/* ---- BG3 in a circle ------------------------------------------------------------------------- */

/* Both draw the same circle at the top of the screen over BG3, and set BG2
 * and BG3 where the picture behind it wants them; the second blends BG3,
 * alone on the main screen, over the rest of the map on the sub screen. */
enum { TOP_CIRCLE_X = 0x7C, TOP_CIRCLE_Y = 0x06, BG3_INSIDE_RADIUS = 0x29, BG3_ROUND_RADIUS = 0x27 };
enum { BG2_SCROLL_DOWN = 0x0100, BG3_SCROLL_ACROSS = 0xF01D, BG3_SCROLL_DOWN = 0x01C0 };

static void bg3_circle_drawn(GameState *g, ScreenEffect first)
{
    set_field_screen_effect(g, field_screen_effect_now(g) == first ? first + 1 : NO_SCREEN_EFFECT);
}

// $C0:2514
static void bg3_in_a_circle(GameState *g)
{
    cut(g, &BG3_INSIDE);
    blend_outside_only(g);
    set_field_layer_scroll_v(g, FIELD_BG2, BG2_SCROLL_DOWN);
    set_field_layer_scroll_h(g, FIELD_BG3, BG3_SCROLL_ACROSS);
    set_field_layer_scroll_v(g, FIELD_BG3, BG3_SCROLL_DOWN);
    window_line_effect(g, SLOT_WINDOW1, true);
    draw_circle(g, TOP_CIRCLE_X, TOP_CIRCLE_Y, BG3_INSIDE_RADIUS, SCENE_CIRCLE_ROUND);
    bg3_circle_drawn(g, BG3_IN_A_CIRCLE);
}

// $C0:2589
static void bg3_blended_round_a_circle(GameState *g)
{
    enum { BG3_ALONE = VID_BG3_BIT, THE_REST = VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT };
    cut(g, &BG3_ROUND);
    blend_outside_only(g);
    set_field_color_math_layers(g, BG3_ALONE);
    set_field_fixed_color_math_layers(g, BG3_ALONE);
    set_field_main_screen(g, BG3_ALONE);
    set_field_line_screens_main_layers(g, SCREENS_OUTSIDE, BG3_ALONE);
    set_field_sub_screen(g, THE_REST);
    set_field_line_screens_sub_layers(g, SCREENS_OUTSIDE, THE_REST);
    set_field_layer_scroll_v(g, FIELD_BG2, BG2_SCROLL_DOWN);
    window_line_effect(g, SLOT_WINDOW1, true);
    draw_circle(g, TOP_CIRCLE_X, TOP_CIRCLE_Y, BG3_ROUND_RADIUS, SCENE_CIRCLE_ROUND);
    bg3_circle_drawn(g, BG3_BLENDED_ROUND_A_CIRCLE);
}

/* ---- The ripple ------------------------------------------------------------------------------- */

/* BG1's tiles go to BG2's place and BG2 waves; BG1 and the sprites are hidden
 * inside a circle near the top of the screen, through which the waving copy
 * shows. BG2 reads its tile map one screen wide meanwhile. The original puts
 * the same byte, BG2's tile map without its size, in the sub screen's masks
 * too, which the port keeps. */
enum { RIPPLE_X = 0x78, RIPPLE_Y = 0x20, RIPPLE_START_RADIUS = 1, RIPPLE_GROWTH = 2 };
enum { BG2_WAVES = 0x11, LAYERS_WITH_THE_COPY = VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT };
enum { RIPPLE_HIDES = VID_BG1_BIT | VID_OBJ_BIT };
enum { BG_MAPS_OUTSIDE = 0, BG2_MAP_SHIFT = 8 };

static uint8_t bg2_map_one_screen(const GameState *g) { return (uint8_t)(field_layer_map_base(g, FIELD_BG2) >> 8); }

static void set_bg2_map(GameState *g, uint8_t map)
{
    uint32_t maps = field_line_bg_maps(g, BG_MAPS_OUTSIDE);
    set_field_line_bg_maps(g, BG_MAPS_OUTSIDE, (maps & ~(0xFFu << BG2_MAP_SHIFT)) | (uint32_t)map << BG2_MAP_SHIFT);
}

/* Its stretches, in frames: the copy settling before the circle, the circle
 * growing, held, and shrinking. */
enum { RIPPLE_SETTLES = 0x20, RIPPLE_GROWS = 0x14, RIPPLE_HOLDS = 0x20, RIPPLE_SHRINKS = 0x14 };

static void ripple_begins(GameState *g)
{
    set_field_line_screens_main_layers(g, SCREENS_OUTSIDE, LAYERS_WITH_THE_COPY);
    set_field_line_screens_main_masks(g, SCREENS_OUTSIDE, RIPPLE_HIDES);
    set_field_line_wave_bg2(g, (uint16_t)((field_line_wave_bg2(g) & 0xFF00) | BG2_WAVES));
    uint8_t map = bg2_map_one_screen(g);
    set_bg2_map(g, map);
    set_field_line_screens_sub_masks(g, SCREENS_OUTSIDE, map);
}

static void ripple_circle_opens(GameState *g)
{
    choose_windows(g, &HOLE);
    set_scene_circle_center_x(g, RIPPLE_X);
    set_scene_circle_center_y(g, RIPPLE_Y);
    set_field_circle_radius(g, RIPPLE_START_RADIUS);
    window_line_effect(g, SLOT_WINDOW1, true);
}

// $C0:2725
/* The ripple's circle at the radius it has reached, over what the table
 * holds outside it. The center survives a held frame; only its low bytes
 * are state here. The circle renderer's compatibility write clears the high
 * bytes and leaves these low bytes unchanged. */
static void draw_the_ripple(GameState *g)
{
    draw_circle(g, (uint8_t)scene_circle_center_x(g), (uint8_t)scene_circle_center_y(g),
                field_circle_radius(g), SCENE_CIRCLE_KEEPS_OUTSIDE);
}

static void ripple_ends(GameState *g)
{
    set_field_screen_effect(g, RIPPLE_PLAYED);
    set_field_line_wave_bg2(g, (uint16_t)(field_line_wave_bg2(g) & 0xFF00));
    cut(g, &NO_CUT);
    window_line_effect(g, SLOT_WINDOW1, false);
    set_bg2_map(g, (uint8_t)(bg2_map_one_screen(g) | field_layer_map_size(g, FIELD_BG2)));
}

// $C0:41C8
bool screen_ripple_played(GameState *g)
{
    switch (field_screen_effect_now(g)) {
    case NO_SCREEN_EFFECT: set_field_screen_effect(g, RIPPLE); return false;
    case RIPPLE_PLAYED: return true;
    default: return false;
    }
}

/* ---- Starting the rest ------------------------------------------------------------------------ */

void screen_effect_start(GameState *g, ScreenEffect effect) { set_field_screen_effect(g, effect); }

/* ---- The steps that run over frames ------------------------------------------------------------
 *
 * The curtain's sweep, the field's close into a scene and the ripple call
 * on the field's other work between their own: the scene engine's sweep, BG1
 * drawn as at a map's load, the effects refreshed, a frame waited, the
 * battle's start that puts the field aside. */

/* The effects refreshed, and a frame waited. */
static bool refresh_the_effects(GameState *g)
{
    bool done = false;
    TIMED_AS(g, FIELD_REFRESH_EFFECTS_ENTRY, done = field_effects_refreshed(g));
    return done;
}

static void wait_for_the_frame(GameState *g, Platform *pf) { TIMED_AS(g, FIELD_FRAME_WAIT, field_finish_the_frame(g, pf)); }

/* A frame refreshed and waited for. */
static bool a_frame(GameState *g, Platform *pf)
{
    if (!refresh_the_effects(g))
        return false;
    wait_for_the_frame(g, pf);
    return true;
}

/* The curtain's step, scene/curtain.c's; its stir takes the curtain's place
 * in the effects' table, doubled, as the original's dispatch hands it on. */
enum { CURTAIN_INDEX = (CURTAIN - 1) * 2 };

/* A frame of the curtain: the picture cut with it, the sweep a step on, and
 * window 2's line effect showing it. */
static bool curtain_a_frame(GameState *g, Platform *pf)
{
    cut_with_the_curtain(g);
    if (!scene_curtain_stepped(g, pf, CURTAIN_INDEX))
        return false;
    curtain_swept_a_step(g);
    return true;
}

/* The field's last frames before the scene: BG1 drawn and sent, a frame, the
 * field's state put aside as for a battle, a frame; then the vblank and the
 * controllers' interrupts alone, no line effects, and the line interrupt
 * off; and the scene starts. */
static void field_hands_over(GameState *g, Platform *pf)
{
    tm_enable_interrupts(pf, PF_AT(0xC02633), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    vid_enable_line_effects(pf, PF_AT(0xC02639), 0);
    field_line_interrupt_works(g, false);
}

// $C0:260E
/* It never comes back to the main loop: false always. */
static bool close_into_the_scene(GameState *g, Platform *pf)
{
    if (!field_layer_drawn_at_the_load(g, FIELD_BG1))
        return false;
    field_put_the_layer_at_its_start(g, FIELD_BG1);
    if (!refresh_the_effects(g))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG1);
    wait_for_the_frame(g, pf);
    FieldBattleStart start = FIELD_BATTLE_START_STOPPED;
    TIMED_AS(g, FIELD_BATTLE_BEGINS_ENTRY, start = field_battle_set_up(g, pf));
    if (start == FIELD_BATTLE_START_STOPPED || !a_frame(g, pf))
        return false;
    field_hands_over(g, pf);
    field_closed_into_the_scene(g);
    return false;
}

/* The ripple's stretches: how many frames each lasts, and how its circle
 * grows a frame where it shows. The original counts the frames in a cell
 * across the waits, which the port leaves as it has it there. */
static const struct {
    uint8_t frames;
    bool shown, opens;
    int8_t growth;
} ripple_stretches[] = {
    { RIPPLE_SETTLES, false, false, 0 },              /* the copy settling */
    { RIPPLE_GROWS, true, true, RIPPLE_GROWTH },      /* the circle opening */
    { RIPPLE_HOLDS, true, false, 0 },                 /* held */
    { RIPPLE_SHRINKS, true, false, -RIPPLE_GROWTH },  /* and closing */
};
enum { RIPPLE_STRETCHES = sizeof ripple_stretches / sizeof ripple_stretches[0] };

static bool ripple_stretch(GameState *g, Platform *pf, unsigned stretch, uint8_t left)
{
    for (; left != 0; left--) {
        if (ripple_stretches[stretch].shown) {
            draw_the_ripple(g);
            set_field_circle_radius(g, (uint8_t)(field_circle_radius(g) + ripple_stretches[stretch].growth));
        }
        set_field_ripple_frames_left(g, left);
        if (!a_frame(g, pf))
            return false;
    }
    set_field_ripple_frames_left(g, 0);
    return true;
}

/* The stretch and its remaining frames survive each held frame boundary.
 * The counter is game state: resuming a ripple reads it. */
static bool ripple_continues(GameState *g, Platform *pf, unsigned stretch, uint8_t left)
{
    if (!ripple_stretch(g, pf, stretch, left))
        return false;
    for (stretch++; stretch < RIPPLE_STRETCHES; stretch++) {
        if (ripple_stretches[stretch].opens)
            ripple_circle_opens(g);
        if (!ripple_stretch(g, pf, stretch, ripple_stretches[stretch].frames))
            return false;
    }
    ripple_ends(g);
    return a_frame(g, pf);
}

// $C0:264F
/* The ripple, over its frames: BG1 drawn and sent and the copy waving, a
 * frame; the copy settling, then its circle opening, held and closing; the
 * copy gone, the last frame; the script sees it played. */
static bool play_the_ripple(GameState *g, Platform *pf)
{
    if (!field_layer_drawn_at_the_load(g, FIELD_BG1))
        return false;
    field_put_the_layer_at_its_start(g, FIELD_BG2);
    field_put_the_layer_at_its_start(g, FIELD_BG3);
    if (!refresh_the_effects(g))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG2);
    ripple_begins(g);
    set_field_ripple_frames_left(g, RIPPLE_SETTLES);
    wait_for_the_frame(g, pf);
    return ripple_continues(g, pf, 0, RIPPLE_SETTLES);
}

/* ---- A frame's step ----------------------------------------------------------------------------- */

// $C0:21E1
/* False where it went no further: a call the port refused, or the field's
 * close, which ends it. */
static bool move_on_a_step(GameState *g, Platform *pf)
{
    switch (field_screen_effect_now(g)) {
    case HOLE_OPENING: circle_opens_a_step(g, &HOLE_CIRCLE); return true;
    case HOLE_CLOSING: circle_closes_a_step(g, &HOLE_CIRCLE, hole_gone); return true;
    case OVAL_OPENING: circle_opens_a_step(g, &OVAL_CIRCLE); return true;
    case OVAL_CLOSING: circle_closes_a_step(g, &OVAL_CIRCLE, oval_gone); return true;
    case OVAL_GONE: oval_gone(g); return true;
    case SEARCHLIGHT: searchlight_step(g); return true;
    case BG3_IN_A_CIRCLE:
    case BG3_IN_A_CIRCLE_SECOND: bg3_in_a_circle(g); return true;
    case BG3_BLENDED_ROUND_A_CIRCLE:
    case BG3_BLENDED_ROUND_A_CIRCLE_SECOND: bg3_blended_round_a_circle(g); return true;
    case CURTAIN:
        if (!curtain_sweeping(g)) {
            curtain_done(g);
            return true;
        }
        return curtain_a_frame(g, pf);
    case ON_TO_THE_SCENE: return close_into_the_scene(g, pf);
    case RIPPLE: return play_the_ripple(g, pf);
    default: return true;   /* none, or the ripple played and waiting to be seen */
    }
}

/* The effects the port draws: one the original's table has (past them it
 * runs into its error trap); and for the searchlight, a shape
 * field/window_shape.c draws as the original does. */
static bool effect_fits(const GameState *g)
{
    ScreenEffect effect = field_screen_effect_now(g);
    return effect < SCREEN_EFFECTS && (effect != SEARCHLIGHT || field_window_shape_starts_on_a_line(g));
}

enum { SCREEN_EFFECT = 0xC021E1 };

bool field_screen_effect_moved_on(GameState *g, Platform *pf)
{
    if (!PORT_CHECK(effect_fits(g))) {
        program_breaks_a_contract(g, SCREEN_EFFECT, "a screen effect the port doesn't draw");
        return false;
    }
    return move_on_a_step(g, pf);
}
