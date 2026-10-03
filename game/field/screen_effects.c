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
 *   original's routines between them.
 *
 * The window tables are double-buffered with the line effects: a frame's
 * shape goes into the table of the set the line effects aren't reading, which
 * the next vblank shows. The circles are scene/circle.c's, the four-cornered
 * shape field/window_shape.c's. */
#include "field/screen_effects.h"
#include "field/battle_start.h"
#include "field/main_loop.h"

#include "charge.h"
#include "core/display.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/field.h"
#include "field/parts.h"
#include "field/streaming.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "scene/scene.h"
#include "vars.h"

/* ---- The cut ----------------------------------------------------------------------------------- */

/* Which window decides each layer of a pair (field_window_shadows): window 1
 * inside its shape or outside it, or window 2 inside. */
enum { W1_INSIDE = VID_WIN_IN, W1_OUTSIDE = VID_WIN_OUT, W2_INSIDE = VID_WIN_IN << 2 };
#define PAIR(first, second) ((uint8_t)((first) | (second) << 4))
enum { BG1_BG2, BG3_BG4, OBJ_COLOUR, WINDOW2_LEFT, WINDOW2_RIGHT };

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

/* The screens' values outside the dialog window (field_line_screens' first):
 * the layers on the main and sub screens, then the window's masks of each. */
enum { SCREENS_OUTSIDE = 0, MAIN_LAYERS = 0, SUB_LAYERS = 8, MAIN_MASKS = 16, SUB_MASKS = 24 };

static void set_screens_byte(GameState *g, unsigned shift, uint8_t v)
{
    uint32_t screens = field_line_screens(g, SCREENS_OUTSIDE);
    set_field_line_screens(g, SCREENS_OUTSIDE, (screens & ~(0xFFu << shift)) | (uint32_t)v << shift);
}

static void choose_windows(GameState *g, const WindowCut *c)
{
    set_field_window_shadows_byte(g, BG1_BG2, c->bg1_bg2);
    set_field_window_shadows_byte(g, BG3_BG4, c->bg3_bg4);
    set_field_window_shadows_byte(g, OBJ_COLOUR, c->obj_colour);
}

static void cut(GameState *g, const WindowCut *c)
{
    set_screens_byte(g, MAIN_MASKS, c->hides_on_main);
    set_screens_byte(g, SUB_MASKS, c->hides_on_sub);
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
    set_field_window_shadows_byte(g, WINDOW2_LEFT, SCREEN_LEFT_EDGE);
    set_field_window_shadows_byte(g, WINDOW2_RIGHT, SCREEN_RIGHT_EDGE);
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
    set_field_window_shadows_byte(g, WINDOW2_RIGHT, SCREEN_LEFT_EDGE);
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
    set_screens_byte(g, MAIN_LAYERS, BG3_ALONE);
    set_field_sub_screen(g, THE_REST);
    set_screens_byte(g, SUB_LAYERS, THE_REST);
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
    set_screens_byte(g, MAIN_LAYERS, LAYERS_WITH_THE_COPY);
    set_screens_byte(g, MAIN_MASKS, RIPPLE_HIDES);
    set_field_line_wave_bg2(g, (uint16_t)((field_line_wave_bg2(g) & 0xFF00) | BG2_WAVES));
    uint8_t map = bg2_map_one_screen(g);
    set_bg2_map(g, map);
    set_screens_byte(g, SUB_MASKS, map);
}

static void ripple_circle_opens(GameState *g)
{
    choose_windows(g, &HOLE);
    set_field_circle_radius(g, RIPPLE_START_RADIUS);
    window_line_effect(g, SLOT_WINDOW1, true);
}

// $C0:2725
/* The ripple's circle at the radius it has reached, over what the table
 * holds outside it. */
static void draw_the_ripple(GameState *g)
{
    draw_circle(g, RIPPLE_X, RIPPLE_Y, field_circle_radius(g), SCENE_CIRCLE_KEEPS_OUTSIDE);
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

/* ---- A frame's step ----------------------------------------------------------------------------- */

/* What a frame's step leaves to the machine: nothing, the curtain's sweep
 * between the field's two halves of the step, or the field's close into the
 * scene or the ripple, which run on with the original's routines. */
typedef enum { STEP_DONE, STEP_SWEEPS_THE_CURTAIN, STEP_CLOSES_THE_FIELD, STEP_PLAYS_THE_RIPPLE } EffectStep;

// $C0:21E1
static EffectStep move_on_a_step(GameState *g)
{
    switch (field_screen_effect_now(g)) {
    case HOLE_OPENING: circle_opens_a_step(g, &HOLE_CIRCLE); return STEP_DONE;
    case HOLE_CLOSING: circle_closes_a_step(g, &HOLE_CIRCLE, hole_gone); return STEP_DONE;
    case OVAL_OPENING: circle_opens_a_step(g, &OVAL_CIRCLE); return STEP_DONE;
    case OVAL_CLOSING: circle_closes_a_step(g, &OVAL_CIRCLE, oval_gone); return STEP_DONE;
    case OVAL_GONE: oval_gone(g); return STEP_DONE;
    case SEARCHLIGHT: searchlight_step(g); return STEP_DONE;
    case BG3_IN_A_CIRCLE:
    case BG3_IN_A_CIRCLE_SECOND: bg3_in_a_circle(g); return STEP_DONE;
    case BG3_BLENDED_ROUND_A_CIRCLE:
    case BG3_BLENDED_ROUND_A_CIRCLE_SECOND: bg3_blended_round_a_circle(g); return STEP_DONE;
    case CURTAIN:
        if (!curtain_sweeping(g)) {
            curtain_done(g);
            return STEP_DONE;
        }
        cut_with_the_curtain(g);
        return STEP_SWEEPS_THE_CURTAIN;
    case ON_TO_THE_SCENE: return STEP_CLOSES_THE_FIELD;
    case RIPPLE: return STEP_PLAYS_THE_RIPPLE;
    default: return STEP_DONE;   /* none, or the ripple played and waiting to be seen */
    }
}

/* ---- Where it meets the original ---------------------------------------------------------------
 *
 * The curtain's sweep, the ripple and the field's close call the original's
 * routines between the field's own work; each is a bridge, calling them as
 * the original does on the field's page and stopping where they don't come
 * back as it. */

enum {
    CURTAIN_SWEEP = 0xC3000E,     /* bank $C3's step of the curtain ($C3:01E4) */
    FRAME_WAIT = 0xC0EC60,        /* waits for the next frame */
    DRAW_BG1 = 0xC075E9,          /* BG1's tiles drawn into the staging, on the layers' page */
    SEND_BG1 = 0xC074D4, SEND_BG2 = 0xC074E8, SEND_BG3 = 0xC074F7,   /* a staged tile map readied for its send */
};
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
/* The curtain's routine saves every register and its flags and puts them
 * back, and reads none but X, which it saves on the stack where the analysis
 * counts it read: the port hands it the original's, the step's index in the
 * dispatch's table, doubled. */
enum { CURTAIN_READS_X = DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C, CURTAIN_INDEX = (CURTAIN - 1) * 2 };

static bool call(Snes *s, uint32_t site, uint32_t routine)
{
    return bridge_call(s, site, routine, READS_NOTHING, on_the_field_page(s, false));
}

/* The effects refreshed for the next frame, a port of the field's
 * (field/main_loop.c) run as C. */
static bool refresh(Snes *s, uint32_t site)
{
    return field_run(s, site, FIELD_REFRESH_EFFECTS_ENTRY, READS_NOTHING, field_refresh_effects,
                     on_the_field_page(s, false));
}
static bool draw_bg1(Snes *s, uint32_t site)
{
    return field_run_on_page(s, site, DRAW_BG1, READS_NOTHING, field_draw_bg1_at_the_load, FIELD_LAYERS_PAGE);
}

// $C0:22D8
static bool sweep_the_curtain(Snes *s)
{
    s->cpu.x = CURTAIN_INDEX;
    if (!bridge_call_long(s, 0xC022FA, CURTAIN_SWEEP, CURTAIN_READS_X, on_the_field_page(s, false)))
        return false;
    curtain_swept_a_step(state_of(s));
    return true;
}

/* The field's last frames before the scene: BG1 drawn and sent, a frame, the
 * field's state put aside, a frame; then the vblank and the controllers'
 * interrupts alone, no line effects, and the line interrupt off. The original
 * starts the scene on a fresh stack ($C0:2643), which is its to do. */
enum { THE_SCENE_STARTS = 0xC02643, AS_THE_SCENE_STARTS = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static void field_hands_over(GameState *g, Platform *pf)
{
    tm_enable_interrupts(pf, PF_AT(0xC02633), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    vid_enable_line_effects(pf, PF_AT(0xC02639), 0);
    field_line_interrupt_works(g, false);
}

// $C0:260E
static void close_into_the_scene(Snes *s)
{
    GameState *g = state_of(s);
    if (!draw_bg1(s, 0xC02617) || !call(s, 0xC0261B, SEND_BG1) || !refresh(s, 0xC0261E))
        return;
    set_field_map_transition(g, FIELD_MOVE_TO_BG1);
    if (!call(s, 0xC02625, FRAME_WAIT) || !field_run(s, 0xC02628, FIELD_BATTLE_BEGINS_ENTRY, READS_NOTHING, field_battle_begins, on_the_field_page(s, false)) || !refresh(s, 0xC0262B) ||
        !call(s, 0xC0262E, FRAME_WAIT))
        return;
    field_hands_over(g, m_platform(s));
    m_stop_at_own(s, THE_SCENE_STARTS, AS_THE_SCENE_STARTS);
}

/* A stretch of the ripple: each frame the circle drawn (where it shows) and
 * its radius moved on, the frame's effects refreshed, and a frame waited.
 * The original counts the frames in a cell across the waits, which the port
 * leaves as it has it there. */
typedef enum { CIRCLE_NOT_SHOWN, CIRCLE_SHOWN } RippleCircle;

static bool ripple_stretch(Snes *s, uint8_t frames, RippleCircle circle, int8_t growth, uint32_t refresh_site,
                           uint32_t wait_site)
{
    GameState *g = state_of(s);
    for (uint8_t left = frames; left != 0; left--) {
        if (circle == CIRCLE_SHOWN) {
            draw_the_ripple(g);
            set_field_circle_radius(g, (uint8_t)(field_circle_radius(g) + growth));
        }
        ORIGINAL_EFFECT(set_field_ripple_frames_left(g, left));
        if (!refresh(s, refresh_site) || !call(s, wait_site, FRAME_WAIT))
            return false;
    }
    ORIGINAL_EFFECT(set_field_ripple_frames_left(g, 0));
    return true;
}

/* The original jumps to the frame wait at its end, which returns to the main
 * loop itself; the port stops there. */
enum { THE_LAST_WAIT = DC_A | DC_N | DC_Z };

// $C0:264F
static void play_the_ripple(Snes *s)
{
    GameState *g = state_of(s);
    if (!draw_bg1(s, 0xC02658) || !call(s, 0xC0265C, SEND_BG2) || !call(s, 0xC0265F, SEND_BG3) ||
        !refresh(s, 0xC02662))
        return;
    set_field_map_transition(g, FIELD_MOVE_TO_BG2);
    ripple_begins(g);
    ORIGINAL_EFFECT(set_field_ripple_frames_left(g, RIPPLE_SETTLES));
    if (!call(s, 0xC02688, FRAME_WAIT))
        return;
    if (!ripple_stretch(s, RIPPLE_SETTLES, CIRCLE_NOT_SHOWN, 0, 0xC0268B, 0xC0268E))
        return;
    ripple_circle_opens(g);
    if (!ripple_stretch(s, RIPPLE_GROWS, CIRCLE_SHOWN, RIPPLE_GROWTH, 0xC026C1, 0xC026C4) ||
        !ripple_stretch(s, RIPPLE_HOLDS, CIRCLE_SHOWN, 0, 0xC026D4, 0xC026D7) ||
        !ripple_stretch(s, RIPPLE_SHRINKS, CIRCLE_SHOWN, -RIPPLE_GROWTH, 0xC026EB, 0xC026EE))
        return;
    ripple_ends(g);
    if (!refresh(s, 0xC0271F))
        return;
    m_stop_at_own(s, FRAME_WAIT, THE_LAST_WAIT);
}

/* On the field's page, with 8-bit A and 16-bit index registers. A, X, Y and
 * the flags but V are dead after it; V is as it came, but for the
 * searchlight, which leaves the overflow of its last corner's move. */
void field_move_screen_effect_on(Snes *s)
{
    GameState *g = state_of(s);
    bool searchlight = field_screen_effect_now(g) == SEARCHLIGHT;
    switch (move_on_a_step(g)) {
    case STEP_SWEEPS_THE_CURTAIN:
        if (!sweep_the_curtain(s))
            return;
        break;
    case STEP_CLOSES_THE_FIELD: close_into_the_scene(s); return;
    case STEP_PLAYS_THE_RIPPLE: play_the_ripple(s); return;
    default: break;
    }
    if (searchlight)
        m_set_flag(s, CPU_FLAG_V, field_searchlight_overflow(g));
    m_rts(s);
}

/* The guard: on the field's page with the data bank $00; an effect the
 * original's table has (past them it runs into its error trap); and for the
 * searchlight, a shape field/window_shape.c draws as the original does. */
bool field_screen_effect_can_move_on(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    ScreenEffect effect = field_screen_effect_now(g);
    bool ok = effect < SCREEN_EFFECTS && (effect != SEARCHLIGHT || field_window_shape_starts_on_a_line(g));
    vars_peek(false);
    return ok;
}
