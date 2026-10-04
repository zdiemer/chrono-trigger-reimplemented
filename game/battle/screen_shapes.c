/* Battle: the screen script's triangles.
 *
 * A screen script can shape either window as a triangle (the circles are
 * screen_circles.c's). A triangle is kept as a center and, for each corner, a
 * distance and an angle about it: flat, stretched across and down by two
 * scales; or turned in space about three axes, its corners lying at a depth
 * out of the screen's plane, and drawn at a size. A triangle can also be one
 * of the models of three points in the ROM, turned and drawn the same way.
 * Its corners (battle_shape_corners) are worked out from those when a script
 * moves it, or set straight from animation positions or effect objects, and
 * its center can be put where its medians cross. Drawing it fills the window
 * of its own number between its corners (screen_triangles.c): brought in or
 * out from the center by its size, or as they are, or as a wedge hung from
 * the top of the screen. Scripts put up to eight triangles aside and bring
 * them back.
 *
 * Angles are 256 a turn. The original works with the sine table's magnitudes
 * (battle_sine_magnitude) through the hardware multiplier, keeping the high
 * byte of each product; the port does the same arithmetic in C, and leaves
 * what the original leaves in the multiplier and its cells as scaffolding. */
#include "battle/screen_script.h"

#include "battle/bridge.h"
#include "battle/screen_data.h"
#include "battle/screen_lines.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "ram.h"

/* ---- A triangle's record ------------------------------------------------------------------ */

enum { CORNERS = 3, AXES = 3, ACROSS = 0, DOWN = 1, DEPTH = 2 };

static uint8_t radius_of(const GameState *g, uint8_t shape, unsigned corner) { return battle_shape_radii(g, corner * 2 + shape); }
static void set_radius_of(GameState *g, uint8_t shape, unsigned corner, uint8_t v) { set_battle_shape_radii(g, corner * 2 + shape, v); }
static uint8_t angle_of(const GameState *g, uint8_t shape, unsigned corner) { return battle_shape_angles(g, corner * 2 + shape); }
static void set_angle_of(GameState *g, uint8_t shape, unsigned corner, uint8_t v) { set_battle_shape_angles(g, corner * 2 + shape, v); }
static uint8_t turn_of(const GameState *g, uint8_t shape, unsigned axis) { return battle_shape_turns(g, axis * 2 + shape); }
static void set_turn_of(GameState *g, uint8_t shape, unsigned axis, uint8_t v) { set_battle_shape_turns(g, axis * 2 + shape, v); }

static uint8_t corner_at(const GameState *g, uint8_t shape, unsigned corner, unsigned axis)
{
    return battle_shape_corners(g, (corner * 2 + axis) * 2 + shape);
}

static void set_corner(GameState *g, uint8_t shape, unsigned corner, uint8_t across, uint8_t down)
{
    set_battle_shape_corners(g, (corner * 2 + ACROSS) * 2 + shape, across);
    set_battle_shape_corners(g, (corner * 2 + DOWN) * 2 + shape, down);
}

static ScreenTriangle corners_of(const GameState *g, uint8_t shape)
{
    ScreenTriangle t;
    for (unsigned corner = 0; corner < CORNERS; corner++)
        t.corner[corner] = (BattlePoint){ corner_at(g, shape, corner, ACROSS), corner_at(g, shape, corner, DOWN) };
    return t;
}

ScreenTriangle screen_triangle(const GameState *g, uint8_t shape) { return corners_of(g, shape); }

void screen_set_triangle(GameState *g, uint8_t shape, ScreenTriangle t)
{
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_corner(g, shape, corner, t.corner[corner].x, t.corner[corner].y);
}

static bool names_a_shape(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->shape < SCREEN_SHAPES;
}

/* The record as the original keeps it, byte by byte: from the center to the
 * size, ten arrays, both triangles' bytes interleaved in each. */
enum { SHAPE_RECORD_BYTES = 0x2A, RECORD_ARRAYS = 10 };
typedef enum { CENTER_X, CENTER_Y, DEPTH_OF, RADII, SCALE_X, SCALE_Y, ANGLES, CORNERS_OF, TURNS, SIZE_OF } RecordArray;
static const uint8_t array_starts[RECORD_ARRAYS] = { 0, 2, 4, 6, 12, 14, 16, 22, 34, 40 };

static RecordArray array_holding(unsigned at, unsigned *i)
{
    unsigned a = 0;
    while (a + 1 < RECORD_ARRAYS && array_starts[a + 1] <= at)
        a++;
    *i = at - array_starts[a];
    return (RecordArray)a;
}

static uint8_t record_byte(const GameState *g, unsigned at)
{
    unsigned i;
    switch (array_holding(at, &i)) {
    case CENTER_X: return battle_shape_center_x(g, i);
    case CENTER_Y: return battle_shape_center_y(g, i);
    case DEPTH_OF: return battle_shape_depth(g, i);
    case RADII: return battle_shape_radii(g, i);
    case SCALE_X: return battle_shape_scale_x(g, i);
    case SCALE_Y: return battle_shape_scale_y(g, i);
    case ANGLES: return battle_shape_angles(g, i);
    case CORNERS_OF: return battle_shape_corners(g, i);
    case TURNS: return battle_shape_turns(g, i);
    case SIZE_OF: return battle_shape_size(g, i);
    }
    return 0;
}

static void set_record_byte(GameState *g, unsigned at, uint8_t v)
{
    unsigned i;
    switch (array_holding(at, &i)) {
    case CENTER_X: set_battle_shape_center_x(g, i, v); break;
    case CENTER_Y: set_battle_shape_center_y(g, i, v); break;
    case DEPTH_OF: set_battle_shape_depth(g, i, v); break;
    case RADII: set_battle_shape_radii(g, i, v); break;
    case SCALE_X: set_battle_shape_scale_x(g, i, v); break;
    case SCALE_Y: set_battle_shape_scale_y(g, i, v); break;
    case ANGLES: set_battle_shape_angles(g, i, v); break;
    case CORNERS_OF: set_battle_shape_corners(g, i, v); break;
    case TURNS: set_battle_shape_turns(g, i, v); break;
    case SIZE_OF: set_battle_shape_size(g, i, v); break;
    }
}

/* The slots triangles are put aside in, as the ROM's table of them places
 * them in battle_saved_shapes. */
enum { SAVED_SHAPE_SLOTS = BATTLE_SAVED_SHAPES_COUNT };

static bool names_a_slot(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->slot < SAVED_SHAPE_SLOTS;
}

static uint8_t saved_byte(const GameState *g, uint16_t offset, unsigned at)
{
    unsigned byte = offset + at;
    return battle_saved_shapes_byte(g, byte / SHAPE_RECORD_BYTES, byte % SHAPE_RECORD_BYTES);
}

static void set_saved_byte(GameState *g, uint16_t offset, unsigned at, uint8_t v)
{
    unsigned byte = offset + at;
    set_battle_saved_shapes_byte(g, byte / SHAPE_RECORD_BYTES, byte % SHAPE_RECORD_BYTES, v);
}

// $CD:2690
/* Put both triangles aside in a slot. */
static ScreenFlow put_shapes_aside(GameState *g, uint8_t slot)
{
    uint16_t offset = saved_shape_offset(g, slot);
    for (unsigned at = 0; at < SHAPE_RECORD_BYTES; at++)
        set_saved_byte(g, offset, at, record_byte(g, at));
    return screen_on();
}
SCREEN_OPERATION(put_shapes_aside, slot)

// $CD:26C0
/* Bring the triangles put aside in a slot back. */
static ScreenFlow bring_shapes_back(GameState *g, uint8_t slot)
{
    uint16_t offset = saved_shape_offset(g, slot);
    for (unsigned at = 0; at < SHAPE_RECORD_BYTES; at++)
        set_record_byte(g, at, saved_byte(g, offset, at));
    return screen_on();
}
SCREEN_OPERATION(bring_shapes_back, slot)

/* ---- Flat triangles ------------------------------------------------------------------------ */

/* A new triangle's corners lie at these angles, a third of a turn apart, and
 * it is drawn unstretched (a scale of a quarter turn is the sine's whole). */
enum { FIRST_CORNER_ANGLE = 0xC0, SECOND_CORNER_ANGLE = 0x15, THIRD_CORNER_ANGLE = 0x6A, UNSTRETCHED = 0x40 };

// $CD:278E
/* Make a flat triangle about a center, its corners all at one distance. */
static ScreenFlow make_triangle(GameState *g, uint8_t shape, uint8_t x, uint8_t y, uint8_t radius)
{
    static const uint8_t angles[CORNERS] = { FIRST_CORNER_ANGLE, SECOND_CORNER_ANGLE, THIRD_CORNER_ANGLE };
    set_battle_shape_center_x(g, shape, x);
    set_battle_shape_center_y(g, shape, y);
    for (unsigned corner = 0; corner < CORNERS; corner++) {
        set_radius_of(g, shape, corner, radius);
        set_angle_of(g, shape, corner, angles[corner]);
    }
    set_battle_shape_scale_x(g, shape, UNSTRETCHED);
    set_battle_shape_scale_y(g, shape, UNSTRETCHED);
    return screen_on();
}
SCREEN_OPERATION(make_triangle, shape, x, y, radius)

// $CD:26E0
/* Set each corner's angle and distance. */
static ScreenFlow shape_triangle(GameState *g, uint8_t shape, ScreenTriple angles, ScreenTriple radii)
{
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_angle_of(g, shape, corner, angles.of[corner]);
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_radius_of(g, shape, corner, radii.of[corner]);
    return screen_on();
}
SCREEN_OPERATION(shape_triangle, shape, angles, radii)

/* Half a radius times the sine of an angle (the sine's magnitude halved
 * first), as a signed byte. */
static uint8_t half_sine_of(const GameState *g, uint8_t angle, uint8_t radius)
{
    uint8_t v = (uint8_t)((battle_sine_magnitude(g, angle) >> 1) * radius >> 8);
    return (angle & SCREEN_HALF_TURN) ? (uint8_t)-v : v;
}

/* A corner's distance along an axis: half its radius times the sine of its
 * angle, stretched by the sine of the scale. */
static uint8_t stretched(const GameState *g, uint8_t angle, uint8_t radius, uint8_t scale)
{
    uint8_t along = (uint8_t)((battle_sine_magnitude(g, angle) >> 1) * radius >> 8);
    uint8_t v = (uint8_t)(along * battle_sine_magnitude(g, scale) >> 8);
    return (angle & SCREEN_HALF_TURN) ? (uint8_t)-v : v;
}

/* What the original leaves after the last corner's stretch across: the
 * scale, the radius and the scale's sine in its cells, and the stretch's
 * product in the multiplier. */
static void leave_stretch(GameState *g, uint8_t angle, uint8_t radius, uint8_t scale)
{
    uint8_t along = (uint8_t)((battle_sine_magnitude(g, angle) >> 1) * radius >> 8);
    screen_leave_cell(g, 0x45, scale);
    screen_leave_cell(g, 0x47, radius);
    screen_leave_cell(g, 0x49, battle_sine_magnitude(g, scale));
    hw_set_multiplicand(g, along);
    hw_multiply_by(g, battle_sine_magnitude(g, scale));
}

// $CD:28D2
/* Work a flat triangle's corners out from its center, angles, distances and
 * scales. */
static void place_flat_corners(GameState *g, uint8_t shape)
{
    uint8_t across = battle_shape_center_x(g, shape), down = battle_shape_center_y(g, shape);
    uint8_t scale_x = battle_shape_scale_x(g, shape), scale_y = battle_shape_scale_y(g, shape);
    for (unsigned corner = 0; corner < CORNERS; corner++) {
        uint8_t angle = angle_of(g, shape, corner), radius = radius_of(g, shape, corner);
        set_corner(g, shape, corner, (uint8_t)(across + stretched(g, angle, radius, scale_x)),
                   (uint8_t)(down + stretched(g, (uint8_t)(angle + SCREEN_QUARTER_TURN), radius, scale_y)));
    }
    ORIGINAL_EFFECT(leave_stretch(g, angle_of(g, shape, 2), radius_of(g, shape, 2), scale_x));
}

// $CD:2717
/* Move a flat triangle, change its corners' distances, stretch it and turn
 * its corners about its center, and work its corners out. */
static ScreenFlow move_flat_triangle(GameState *g, uint8_t shape, uint8_t x, uint8_t y, uint8_t radius,
                                     uint8_t scale_x, uint8_t scale_y, uint8_t angle)
{
    set_battle_shape_center_x(g, shape, (uint8_t)(battle_shape_center_x(g, shape) + x));
    set_battle_shape_center_y(g, shape, (uint8_t)(battle_shape_center_y(g, shape) + y));
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_radius_of(g, shape, corner, (uint8_t)(radius_of(g, shape, corner) + radius));
    set_battle_shape_scale_x(g, shape, (uint8_t)(battle_shape_scale_x(g, shape) + scale_x));
    set_battle_shape_scale_y(g, shape, (uint8_t)(battle_shape_scale_y(g, shape) + scale_y));
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_angle_of(g, shape, corner, (uint8_t)(angle_of(g, shape, corner) + angle));
    place_flat_corners(g, shape);
    return screen_on();
}
SCREEN_OPERATION(move_flat_triangle, shape, x, y, radius, scale_x, scale_y, angle)

// $CD:1D56
/* Grow each corner's distance by its own amount, short of a limit: a corner
 * that would reach it stays as it is. */
static ScreenFlow grow_corners(GameState *g, uint8_t shape, uint8_t limit, ScreenTriple radii)
{
    for (unsigned corner = 0; corner < CORNERS; corner++) {
        uint8_t grown = (uint8_t)(radius_of(g, shape, corner) + radii.of[corner]);
        if (grown < limit)
            set_radius_of(g, shape, corner, grown);
    }
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x45, limit));
    return screen_on();
}
SCREEN_OPERATION(grow_corners, shape, limit, radii)

/* ---- Triangles in space -------------------------------------------------------------------- */

/* A point in space, as signed bytes: across, down, and out of the screen. */
typedef struct {
    uint8_t across, down, depth;
} SpacePoint;

// $CD:3C8F
/* Turn a point about the down axis, then about the across axis, by two of
 * the triangle's turns: each a rotation of two of the point's coordinates, a
 * term at a time, every term a signed byte. */
static SpacePoint turn_about_down_and_across(const GameState *g, SpacePoint p, const uint8_t turns[AXES])
{
    uint8_t about_across = turns[ACROSS], about_down = turns[DOWN];
    uint8_t x = (uint8_t)(screen_signed_cosine(g, about_down, p.across) + screen_signed_sine(g, about_down, p.depth));
    uint8_t z = (uint8_t)(screen_signed_cosine(g, about_down, p.depth) + screen_signed_sine(g, about_down, (uint8_t)-p.across));
    uint8_t y = (uint8_t)(screen_signed_cosine(g, about_across, p.down) - screen_signed_sine(g, about_across, z));
    z = (uint8_t)(screen_signed_cosine(g, about_across, z) + screen_signed_sine(g, about_across, p.down));
    return (SpacePoint){ x, y, z };
}

/* ... and then about the depth axis, by the third. */
static SpacePoint turn_point(const GameState *g, SpacePoint p, const uint8_t turns[AXES])
{
    SpacePoint q = turn_about_down_and_across(g, p, turns);
    uint8_t about_depth = turns[DEPTH];
    return (SpacePoint){ (uint8_t)(screen_signed_cosine(g, about_depth, q.across) - screen_signed_sine(g, about_depth, q.down)),
                         (uint8_t)(screen_signed_cosine(g, about_depth, q.down) + screen_signed_sine(g, about_depth, q.across)),
                         q.depth };
}

/* The point as the original hands it to the turn, in battle_shape_point: a
 * model's points as words, a flat corner's as the low bytes alone. */
static void leave_point(GameState *g, SpacePoint p, bool whole_words)
{
    const uint8_t at[AXES] = { p.across, p.down, p.depth };
    for (unsigned axis = 0; axis < AXES; axis++) {
        set_battle_shape_point(g, axis, at[axis]);
        if (whole_words)
            set_battle_shape_point_high(g, axis, 0);
    }
}

/* What the original leaves of a turn: the point before and after it, its
 * last term (the sine of the depth turn's down part) in its cell, and its
 * last product (the cosine of that turn times the across part) in the
 * multiplier. */
static void leave_turn(GameState *g, SpacePoint before, SpacePoint after, const uint8_t turns[AXES], bool whole_words)
{
    SpacePoint q = turn_about_down_and_across(g, before, turns);
    leave_point(g, before, whole_words);
    set_battle_shape_turned_point(g, ACROSS, after.across);
    set_battle_shape_turned_point(g, DOWN, after.down);
    set_battle_shape_turned_point(g, DEPTH, after.depth);
    screen_leave_cell(g, 0x47, screen_signed_sine(g, turns[DEPTH], q.down));
    screen_leave_sine_product(g, (uint8_t)(turns[DEPTH] + SCREEN_QUARTER_TURN), q.across);
}

/* Turn a corner by the triangle's turns and place it about its center. */
static void place_turned_corner(GameState *g, uint8_t shape, unsigned corner, SpacePoint p, bool whole_words)
{
    const uint8_t turns[AXES] = { turn_of(g, shape, ACROSS), turn_of(g, shape, DOWN), turn_of(g, shape, DEPTH) };
    SpacePoint turned = turn_point(g, p, turns);
    set_corner(g, shape, corner, (uint8_t)(turned.across + battle_shape_center_x(g, shape)),
               (uint8_t)(turned.down + battle_shape_center_y(g, shape)));
    ORIGINAL_EFFECT(leave_turn(g, p, turned, turns, whole_words));
}

/* What the original leaves of the turns it hands the rotation, and of the
 * size: as bytes, or as words. */
static void leave_turns(GameState *g, uint8_t shape, bool whole_words)
{
    static const uint8_t cells[AXES] = { 0x49, 0x4B, 0x4D };
    for (unsigned axis = 0; axis < AXES; axis++) {
        if (whole_words)
            screen_leave_word(g, cells[axis], turn_of(g, shape, axis));
        else
            screen_leave_cell(g, cells[axis], turn_of(g, shape, axis));
    }
    screen_leave_cell(g, 0x21, battle_shape_size(g, shape));
}

// $CD:2843, $CD:28A1
/* Work a turning triangle's corners out: each lies at its angle and
 * distance about the center, at the triangle's depth, before the turns. */
static void place_turned_corners(GameState *g, uint8_t shape)
{
    for (unsigned corner = 0; corner < CORNERS; corner++) {
        uint8_t angle = angle_of(g, shape, corner), radius = radius_of(g, shape, corner);
        SpacePoint p = { half_sine_of(g, angle, radius), half_sine_of(g, (uint8_t)(angle + SCREEN_QUARTER_TURN), radius),
                         battle_shape_depth(g, shape) };
        place_turned_corner(g, shape, corner, p, false);
    }
    ORIGINAL_EFFECT(leave_turns(g, shape, false));
}

// $CD:2660
/* Set a triangle turning: its turns, at the screen's plane, full size. */
enum { FULL_SIZE = 0xFF };

static ScreenFlow set_turning_triangle(GameState *g, uint8_t shape, ScreenTriple turns)
{
    for (unsigned axis = 0; axis < AXES; axis++)
        set_turn_of(g, shape, axis, turns.of[axis]);
    set_battle_shape_depth(g, shape, 0);
    set_battle_shape_size(g, shape, FULL_SIZE);
    return screen_on();
}
SCREEN_OPERATION(set_turning_triangle, shape, turns)

// $CD:27CA
/* Move a turning triangle, lift it out of the plane, change its corners'
 * distances, turn it and resize it, and work its corners out. */
static ScreenFlow move_turning_triangle(GameState *g, uint8_t shape, uint8_t x, uint8_t y, uint8_t depth, uint8_t radius,
                                        ScreenTriple turns, uint8_t size)
{
    set_battle_shape_center_x(g, shape, (uint8_t)(battle_shape_center_x(g, shape) + x));
    set_battle_shape_center_y(g, shape, (uint8_t)(battle_shape_center_y(g, shape) + y));
    set_battle_shape_depth(g, shape, (uint8_t)(battle_shape_depth(g, shape) + depth));
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_radius_of(g, shape, corner, (uint8_t)(radius_of(g, shape, corner) + radius));
    for (unsigned axis = 0; axis < AXES; axis++)
        set_turn_of(g, shape, axis, (uint8_t)(turn_of(g, shape, axis) + turns.of[axis]));
    set_battle_shape_size(g, shape, (uint8_t)(battle_shape_size(g, shape) + size));
    place_turned_corners(g, shape);
    return screen_on();
}
SCREEN_OPERATION(move_turning_triangle, shape, x, y, depth, radius, turns, size)

// $CD:1E45
/* Turn a triangle further and lift it out of the plane, without working its
 * corners out. */
static ScreenFlow turn_triangle(GameState *g, uint8_t shape, ScreenTriple turns, uint8_t depth)
{
    for (unsigned axis = 0; axis < AXES; axis++)
        set_turn_of(g, shape, axis, (uint8_t)(turn_of(g, shape, axis) + turns.of[axis]));
    set_battle_shape_depth(g, shape, (uint8_t)(battle_shape_depth(g, shape) + depth));
    return screen_on();
}
SCREEN_OPERATION(turn_triangle, shape, turns, depth)

// $CD:20D8
static ScreenFlow resize_triangle(GameState *g, uint8_t shape, uint8_t size)
{
    set_battle_shape_size(g, shape, (uint8_t)(battle_shape_size(g, shape) + size));
    return screen_on();
}
SCREEN_OPERATION(resize_triangle, shape, size)

/* The rocking sways each turn by the sine of a corner's angle: the first two
 * by this much, the third a little more. */
enum { ROCK = 0x20, ROCK_ABOUT_DEPTH = 0x24 };

// $CD:1BCE
/* Rock a triangle: move its angles on, and set each of its turns to a sway
 * plus the sine of the matching angle. */
static ScreenFlow rock_triangle(GameState *g, uint8_t shape, ScreenTriple angles, ScreenTriple sways)
{
    static const uint8_t rocks[AXES] = { ROCK, ROCK, ROCK_ABOUT_DEPTH };
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_angle_of(g, shape, corner, (uint8_t)(angle_of(g, shape, corner) + angles.of[corner]));
    for (unsigned axis = 0; axis < AXES; axis++)
        set_turn_of(g, shape, axis,
                    (uint8_t)(screen_signed_sine(g, angle_of(g, shape, axis), rocks[axis]) + sways.of[axis]));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x45, ROCK_ABOUT_DEPTH));
    ORIGINAL_EFFECT(screen_leave_sine_product(g, angle_of(g, shape, 2), ROCK_ABOUT_DEPTH));
    return screen_on();
}
SCREEN_OPERATION(rock_triangle, shape, angles, sways)

// $CD:1BAB
/* Raise a triangle by how far the effect object is above the battlefield's
 * middle line. */
static ScreenFlow raise_triangle_with_effect_object(GameState *g, uint8_t shape)
{
    uint8_t rise = screen_effect_object_rise(g);
    set_battle_shape_center_y(g, shape, (uint8_t)(battle_shape_center_y(g, shape) - rise));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x45, rise));
    return screen_on();
}
SCREEN_OPERATION(raise_triangle_with_effect_object, shape)

/* ---- Corners from the battlefield ----------------------------------------------------------- */

/* The effect objects a triangle can be strung between. */
enum { FIRST_CORNER_OBJECT = 12 };

// $CD:20F3
static ScreenFlow corners_at_effect_objects(GameState *g, uint8_t shape)
{
    for (unsigned corner = 0; corner < CORNERS; corner++)
        set_corner(g, shape, corner, battle_object_x(g, FIRST_CORNER_OBJECT + corner),
                   battle_object_y(g, FIRST_CORNER_OBJECT + corner));
    return screen_on();
}
SCREEN_OPERATION(corners_at_effect_objects, shape)

/* ---- Drawing ----------------------------------------------------------------------------- */

/* A corner brought in toward the center (or out from it) by the size, in
 * 256ths, along one axis: the distance from the center, scaled, and laid off
 * from the center the same way. The original works the result out as a word,
 * whose low byte is the corner. */
static uint16_t sized(uint8_t corner, uint8_t center, uint8_t size)
{
    int16_t from_center = (int16_t)(corner - center);
    uint8_t moved = (uint8_t)((uint8_t)(from_center < 0 ? -from_center : from_center) * size >> 8);
    return (uint16_t)(from_center < 0 ? center - moved : center + moved);
}

/* What the original leaves of the sizing: the size, the last corner down
 * with the center and their difference, and the last product in the
 * multiplier. (The corner's own word, which takes in the controller, the
 * runner leaves: screen_script.c.) */
static void leave_sizing(GameState *g, uint8_t size, uint8_t center_down, uint8_t corner_down)
{
    int16_t from_center = (int16_t)(corner_down - center_down);
    screen_leave_cell(g, 0x21, size);
    screen_leave_word(g, 0x45, corner_down);
    screen_leave_word(g, 0x47, center_down);
    screen_leave_word(g, 0x49, (uint16_t)from_center);
    hw_set_multiplicand(g, (uint8_t)(from_center < 0 ? -from_center : from_center));
    hw_multiply_by(g, size);
}

/* The original hands its drawing the window in battle_window_quad_index, and
 * clears the cell $20 (which nothing reads) before it. */
static void leave_window_handed_over(GameState *g, uint8_t window)
{
    set_battle_window_quad_index(g, window);
    screen_leave_cell(g, 0x20, 0);
}

/* A triangle's corners as it is drawn: brought in toward its center by its
 * size. */
static ScreenTriangle sized_corners(const GameState *g, uint8_t shape, ScreenTriangle t)
{
    uint8_t size = battle_shape_size(g, shape);
    uint8_t across = battle_shape_center_x(g, shape), down = battle_shape_center_y(g, shape);
    for (unsigned corner = 0; corner < CORNERS; corner++)
        t.corner[corner] = (BattlePoint){ (uint8_t)sized(t.corner[corner].x, across, size),
                                          (uint8_t)sized(t.corner[corner].y, down, size) };
    return t;
}

static bool sized_triangle_fits(const GameState *g, const ScreenCommand *c)
{
    return screen_triangle_fits_the_table(g, sized_corners(g, c->shape, corners_of(g, c->shape)));
}

// $CD:2173, $CD:211B
/* Draw a triangle as the window of its own number, its corners brought in
 * toward its center by its size. */
static ScreenFlow draw_triangle(GameState *g, uint8_t shape)
{
    ScreenTriangle t = corners_of(g, shape);
    ORIGINAL_EFFECT(leave_sizing(g, battle_shape_size(g, shape), battle_shape_center_y(g, shape), t.corner[CORNERS - 1].y));
    ORIGINAL_EFFECT(leave_window_handed_over(g, shape));
    screen_draw_window_triangle(g, shape, sized_corners(g, shape, t));
    return screen_on();
}
SCREEN_OPERATION(draw_triangle, shape)

static bool names_a_shape_that_fits(const GameState *g, const ScreenCommand *c)
{
    return names_a_shape(g, c) && screen_triangle_fits_the_table(g, corners_of(g, c->shape));
}

// $CD:2952, $CD:3657
/* Draw a triangle at its corners as they are, not brought in by its size, as
 * the window of its own number. */
static ScreenFlow draw_triangle_at_its_corners(GameState *g, uint8_t shape)
{
    ORIGINAL_EFFECT(leave_window_handed_over(g, shape));
    screen_draw_window_triangle(g, shape, corners_of(g, shape));
    return screen_on();
}
SCREEN_OPERATION(draw_triangle_at_its_corners, shape)

// $CD:1F47, $CD:3924
/* Hang a wedge from the top of the screen on a triangle's first two corners,
 * as the window of its own number: the right triangle they make, and the
 * window above it filled up to the screen's top edge. */
static ScreenFlow hang_wedge(GameState *g, uint8_t shape)
{
    ScreenTriangle t = corners_of(g, shape);
    ORIGINAL_EFFECT(leave_window_handed_over(g, shape));
    screen_hang_window_triangle(g, shape, t.corner[0], t.corner[1]);
    return screen_on();
}
SCREEN_OPERATION(hang_wedge, shape)

static bool names_a_shape_whose_wedge_fits(const GameState *g, const ScreenCommand *c)
{
    if (!names_a_shape(g, c))
        return false;
    ScreenTriangle t = corners_of(g, c->shape);
    t.corner[2] = t.corner[0];
    return screen_triangle_fits_the_table(g, t);
}

// $CD:21C0
/* Center a triangle on its corners, where its medians cross, at full size. */
static ScreenFlow center_triangle_on_its_corners(GameState *g, uint8_t shape)
{
    set_battle_shape_size(g, shape, FULL_SIZE);
    BattlePoint center = screen_triangle_center(g, corners_of(g, shape));
    set_battle_shape_center_x(g, shape, center.x);
    set_battle_shape_center_y(g, shape, center.y);
    return screen_on();
}
SCREEN_OPERATION(center_triangle_on_its_corners, shape)

// $CD:1FA0
/* Center a triangle on its corners, and put the corner of the channel's
 * layer on its center. */
static ScreenFlow center_layer_on_triangle(GameState *g, ScreenChannel channel, uint8_t shape)
{
    center_triangle_on_its_corners(g, shape);
    set_battle_screen_layer_x(g, channel, (uint16_t)-battle_shape_center_x(g, shape));
    set_battle_screen_layer_y(g, channel, (uint16_t)-battle_shape_center_y(g, shape));
    return screen_on();
}
SCREEN_OPERATION(center_layer_on_triangle, channel, shape)

static bool names_a_shape_whose_center_divides(const GameState *g, const ScreenCommand *c)
{
    return names_a_shape(g, c) && screen_triangle_center_divides(corners_of(g, c->shape));
}

/* The original hands the models' rotation the triangle's size as a word, in
 * the battle page's cells $21-$22, and its high byte, 0, lands on the low byte
 * of the slot whose turn it is ($22). The game reads that cell (it is the
 * battle's), so the store is kept, here, as what it is (docs/TODO.md). */
static void clear_the_turn_slots_low_byte(GameState *g)
{
    set_battle_turn_slot(g, battle_turn_slot(g) & 0xFF00);
}

/* A model's points are four bytes apart in its sixteen (screen_data.h). */
enum { MODEL_BYTES = 16 };

/* What the original leaves of a model's drawing: the model and the depth it
 * added in its cells. */
static void leave_model(GameState *g, uint8_t model_at, uint8_t added_depth)
{
    screen_leave_cell(g, 0x4F, model_at);
    screen_leave_cell(g, 0x51, added_depth);
}

/* A spinning model turns a little further about each axis each time it is
 * drawn. */
static const uint8_t spin[AXES] = { 2, 1, 1 };

static SpacePoint model_point(const GameState *g, uint8_t model, unsigned corner, uint8_t added_depth)
{
    ShapeModelPoint m = shape_model_point(g, model, corner);
    return (SpacePoint){ m.across, m.down, (uint8_t)(m.depth + added_depth) };
}

/* A model's corners, turned by `turns` and placed about the triangle's
 * center. */
static ScreenTriangle model_corners(const GameState *g, uint8_t shape, uint8_t model, uint8_t added_depth,
                                    const uint8_t turns[AXES])
{
    ScreenTriangle t;
    for (unsigned corner = 0; corner < CORNERS; corner++) {
        SpacePoint turned = turn_point(g, model_point(g, model, corner, added_depth), turns);
        t.corner[corner] = (BattlePoint){ (uint8_t)(turned.across + battle_shape_center_x(g, shape)),
                                          (uint8_t)(turned.down + battle_shape_center_y(g, shape)) };
    }
    return t;
}

static uint8_t added_depth(const GameState *g, uint8_t shape, bool own_depth) { return own_depth ? battle_shape_depth(g, shape) : 0; }

/* Whether the model, drawn, stays in the window table. */
static bool model_fits(const GameState *g, const ScreenCommand *c)
{
    uint8_t turns[AXES];
    for (unsigned axis = 0; axis < AXES; axis++)
        turns[axis] = (uint8_t)(turn_of(g, c->shape, axis) + (c->spins ? spin[axis] : 0));
    ScreenTriangle t = model_corners(g, c->shape, c->model, added_depth(g, c->shape, c->own_depth), turns);
    return screen_triangle_fits_the_table(g, sized_corners(g, (uint8_t)(c->shape & 1), t));
}

static bool names_a_shape_and_its_model_fits(const GameState *g, const ScreenCommand *c)
{
    return names_a_shape(g, c) && model_fits(g, c);
}

// $CD:1EAA, $CD:1EF7
/* Turn one of the ROM's models by a triangle's turns, lifted by its depth or
 * not, place its corners about the triangle's center, and draw it. One that
 * spins turns a little further each time. */
static ScreenFlow draw_model(GameState *g, uint8_t shape, uint8_t model, bool own_depth, bool spins)
{
    if (spins)
        for (unsigned axis = 0; axis < AXES; axis++)
            set_turn_of(g, shape, axis, (uint8_t)(turn_of(g, shape, axis) + spin[axis]));
    uint8_t added = added_depth(g, shape, own_depth);
    for (unsigned corner = 0; corner < CORNERS; corner++)
        place_turned_corner(g, shape, corner, model_point(g, model, corner, added), true);
    ORIGINAL_EFFECT(leave_turns(g, shape, true));
    ORIGINAL_EFFECT(leave_model(g, (uint8_t)(model * MODEL_BYTES), added));
    clear_the_turn_slots_low_byte(g);
    return draw_triangle(g, (uint8_t)(shape & 1));
}
SCREEN_OPERATION(draw_model, shape, model, own_depth, spins)

SCREEN_OPCODES(screen_shape_opcodes,
    { 0xA5, OPERATION(put_shapes_aside), .needs = names_a_slot, .length = 1, .slot = BYTE(0) },
    { 0xA6, OPERATION(bring_shapes_back), .needs = names_a_slot, .length = 1, .slot = BYTE(0) },
    { 0xA2, OPERATION(make_triangle), .needs = names_a_shape, .length = 4, .shape = BYTE(0), .x = BYTE(1), .y = BYTE(2), .radius = BYTE(3) },
    { 0xA4, OPERATION(shape_triangle), .needs = names_a_shape, .length = 7, .shape = BYTE(0), .angles = TRIPLE(1), .radii = TRIPLE(4) },
    { 0xA3, OPERATION(move_flat_triangle), .needs = names_a_shape, .length = 7, .shape = BYTE(0), .x = BYTE(1), .y = BYTE(2), .radius = BYTE(3), .scale_x = BYTE(4), .scale_y = BYTE(5), .angle = BYTE(6) },
    { 0xC2, OPERATION(grow_corners), .needs = names_a_shape, .length = 5, .shape = BYTE(0), .limit = BYTE(1), .radii = TRIPLE(2) },
    { 0xA7, OPERATION(set_turning_triangle), .needs = names_a_shape, .length = 4, .shape = BYTE(0), .turns = TRIPLE(1) },
    { 0xA8, OPERATION(move_turning_triangle), .needs = names_a_shape, .length = 9, .shape = BYTE(0), .x = BYTE(1), .y = BYTE(2), .depth = BYTE(3), .radius = BYTE(4), .turns = TRIPLE(5), .size = BYTE(8) },
    { 0xBC, OPERATION(turn_triangle), .needs = names_a_shape, .length = 5, .shape = BYTE(0), .turns = TRIPLE(1), .depth = BYTE(4) },
    { 0xB1, OPERATION(resize_triangle), .needs = names_a_shape, .length = 2, .shape = BYTE(0), .size = BYTE(1) },
    { 0xC6, OPERATION(rock_triangle), .needs = names_a_shape, .length = 7, .shape = BYTE(0), .angles = TRIPLE(1), .sways = TRIPLE(4) },
    { 0xC7, OPERATION(raise_triangle_with_effect_object), .needs = names_a_shape, .length = 1, .shape = BYTE(0) },
    { 0xAF, OPERATION(corners_at_effect_objects), .needs = names_a_shape, .length = 1, .shape = BYTE(0) },
    { 0xA1, OPERATION(draw_triangle_at_its_corners), .needs = names_a_shape_that_fits, .length = 1, .shape = BYTE(0) },
    { 0xB8, OPERATION(hang_wedge), .needs = names_a_shape_whose_wedge_fits, .length = 1, .shape = BYTE(0) },
    { 0xAD, OPERATION(center_triangle_on_its_corners), .needs = names_a_shape_whose_center_divides, .length = 1, .shape = BYTE(0) },
    { 0xB4, OPERATION(center_layer_on_triangle), .needs = names_a_shape_whose_center_divides, .length = 1, .shape = BYTE(0) },
    { 0xAE, OPERATION(draw_triangle), .needs = sized_triangle_fits, .length = 1, .shape = LOW_BIT(0) },
    { 0xBA, OPERATION(draw_model), .needs = names_a_shape_and_its_model_fits, .length = 1, .shape = BYTE(0), .model = BYTE(0), .own_depth = CONSTANT(false), .spins = CONSTANT(true) },
    { 0xBB, OPERATION(draw_model), .needs = model_fits, .length = 1, .shape = LOW_BIT(0), .model = BYTE(0), .own_depth = CONSTANT(true), .spins = CONSTANT(false) },
    { 0xBE, OPERATION(draw_model), .needs = names_a_shape_and_its_model_fits, .length = 1, .shape = BYTE(0), .model = CONSTANT(2), .own_depth = CONSTANT(true), .spins = CONSTANT(false) },
);
