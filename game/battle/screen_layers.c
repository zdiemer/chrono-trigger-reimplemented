/* Battle: the screen script's layer, moved about.
 *
 * Each screen script channel moves a layer of its own (battle_screen_layer;
 * channel 0's is BG3, which the frame work scrolls by it while the scripts
 * run): its position, which a script puts at an animation position or on an
 * orbit, and an offset on top of it, which a script nudges eight ways, swings
 * (screen_motion.c) or drops by how far an effect object is above the
 * battlefield's middle. The layer is placed by its scroll, so it shows a
 * point at the screen's corner by being scrolled to minus that point.
 *
 * The orbit moves the layer round a center on three sines at once: a circle
 * of its own, a bob up and down and a sway across, at their own speeds, the
 * sizes from a table of two orbits, or widening as the orbit goes on. */
#include "battle/screen_script.h"

#include "battle/screen_data.h"
#include "cpu_io.h"
#include "original_effects.h"

/* ---- The animation positions ------------------------------------------------------------ */

/* Animation positions are kept inside the battlefield, a pair at a time,
 * before a script takes one up: 8 to 248 across and 8 to 212 down. The first
 * pair is the one most commands keep. */
enum { EDGE_LOW = 8, EDGE_RIGHT = 0xF8, EDGE_BOTTOM = 0xD4 };

static uint8_t kept_between(uint8_t v, uint8_t high) { return v < EDGE_LOW ? EDGE_LOW : v >= high ? high : v; }

BattlePoint screen_kept_on_the_battlefield(BattlePoint p)
{
    return (BattlePoint){ kept_between(p.x, EDGE_RIGHT), kept_between(p.y, EDGE_BOTTOM) };
}

// $CD:2433, $CD:2436
void screen_keep_pair_on_the_battlefield(GameState *g, uint8_t pair)
{
    for (unsigned p = SCREEN_PAIR_POSITIONS * pair; p < SCREEN_PAIR_POSITIONS * (pair + 1u); p++) {
        uint16_t at = battle_animation_positions(g, p);
        BattlePoint kept = screen_kept_on_the_battlefield((BattlePoint){ (uint8_t)at, (uint8_t)(at >> 8) });
        set_battle_animation_positions(g, p, (uint16_t)(kept.y << 8 | kept.x));
    }
}

void screen_keep_positions_on_the_battlefield(GameState *g) { screen_keep_pair_on_the_battlefield(g, 0); }

uint8_t screen_position_across(const GameState *g, uint8_t position) { return (uint8_t)battle_animation_positions(g, position); }
uint8_t screen_position_down(const GameState *g, uint8_t position) { return (uint8_t)(battle_animation_positions(g, position) >> 8); }

bool screen_names_a_position(uint8_t position) { return position < BATTLE_ANIMATION_POSITIONS_COUNT; }

// $CD:3C00
void screen_leave_sine_product(GameState *g, uint8_t angle, uint8_t amount)
{
    hw_set_multiplicand(g, battle_sine_magnitude(g, angle));
    hw_multiply_by(g, screen_magnitude_of(amount));
}

/* ---- Placing the layer --------------------------------------------------------------------- */

/* The layer shows the point (across, down) at the screen's corner. */
static void show_point(GameState *g, ScreenChannel channel, uint8_t across, uint8_t down)
{
    set_battle_screen_layer_x(g, channel, (uint16_t)-across);
    set_battle_screen_layer_y(g, channel, (uint16_t)-down);
}

// $CD:1C32
/* Put the layer at an animation position. */
static ScreenFlow put_layer_at_position(GameState *g, ScreenChannel channel, uint8_t position)
{
    screen_keep_positions_on_the_battlefield(g);
    show_point(g, channel, screen_position_across(g, position), screen_position_down(g, position));
    return screen_on();
}
SCREEN_OPERATION(put_layer_at_position, channel, position)

static bool names_a_position(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return screen_names_a_position(c->position);
}

/* The eight ways a nudge moves the layer's offset, by the direction in the
 * operand's top three bits, a step across and down. */
static const struct {
    int8_t across, down;
} nudges[8] = {
    { +1, -1 }, { 0, -1 }, { -1, -1 }, { +1, 0 }, { -1, 0 }, { +1, +1 }, { 0, +1 }, { -1, +1 },
};

// $CD:2DD6
/* Nudge the layer's offset one of eight ways, by 1 to 32 pixels. */
static ScreenFlow nudge_layer(GameState *g, ScreenChannel channel, uint8_t direction, uint8_t distance)
{
    uint16_t by = (uint16_t)(distance + 1);
    set_battle_screen_layer_offset_x(g, channel, (uint16_t)(battle_screen_layer_offset_x(g, channel) + nudges[direction].across * by));
    set_battle_screen_layer_offset_y(g, channel, (uint16_t)(battle_screen_layer_offset_y(g, channel) + nudges[direction].down * by));
    screen_leave_for_the_drift(g, by);
    return screen_on();
}
SCREEN_OPERATION(nudge_layer, channel, direction, distance)

/* The line the battlefield's effects are drawn about, and the effect object
 * whose height commands compare with it. */
enum { MIDDLE_LINE = 0x6F, EFFECT_OBJECT = 15 };

static uint8_t object_rise(const GameState *g) { return (uint8_t)(MIDDLE_LINE - battle_object_y(g, EFFECT_OBJECT)); }

// $CD:1B9F
/* Drop the layer's offset by how far the effect object is above the middle
 * line (as a byte: an object below it drops it nearly a screen). */
static ScreenFlow drop_layer_by_effect_object(GameState *g, ScreenChannel channel)
{
    uint16_t rise = object_rise(g);
    set_battle_screen_layer_offset_y(g, channel, (uint16_t)(battle_screen_layer_offset_y(g, channel) + rise));
    screen_leave_for_the_drift(g, rise);
    return screen_on();
}
SCREEN_OPERATION(drop_layer_by_effect_object, channel)

uint8_t screen_effect_object_rise(const GameState *g) { return object_rise(g); }

/* ---- The swing ------------------------------------------------------------------------------- */

/* The swing's speed starts as fast as it goes and slows by the step each
 * frame, so the step decides how many frames the layer swings down: about
 * `frames` (a hardware division, which takes 0 as giving the largest
 * quotient). It swings about the offset the layer has. */
enum { FASTEST = 0xFFFF, SWING_START = 0x0000FFFF };

static uint16_t divided(uint16_t n, uint8_t d) { return d ? (uint16_t)(n / d) : 0xFFFF; }

static void leave_division(GameState *g, uint8_t frames)
{
    hw_set_dividend(g, FASTEST);
    hw_divide_by(g, frames);
}

// $CD:1AEB
/* Set the layer swinging down from where it is, slowing over `frames` frames
 * (screen_motion.c swings it). */
static ScreenFlow start_swing(GameState *g, ScreenChannel channel, uint8_t frames)
{
    ORIGINAL_EFFECT(leave_division(g, frames));
    set_battle_screen_layer_swing_rising(g, channel, 0);
    set_battle_screen_swing_speed(g, channel, FASTEST);
    set_battle_screen_swing_position(g, channel, SWING_START);
    set_battle_screen_swing_step(g, channel, divided(FASTEST, frames));
    set_battle_screen_layer_swing_center(g, channel, battle_screen_layer_offset_y(g, channel));
    return screen_on();
}
SCREEN_OPERATION(start_swing, channel, frames)

/* A script can keep one of the swing's four words aside and bring it back:
 * the speed, the step, and the position's low and high words. */
enum { SWING_WORDS = 4, SPEED, STEP, POSITION_LOW, POSITION_HIGH };

static uint16_t swing_word(const GameState *g, ScreenChannel channel, uint8_t which)
{
    switch (which + SPEED) {
    case SPEED: return battle_screen_swing_speed(g, channel);
    case STEP: return battle_screen_swing_step(g, channel);
    case POSITION_LOW: return (uint16_t)battle_screen_swing_position(g, channel);
    default: return (uint16_t)(battle_screen_swing_position(g, channel) >> 16);
    }
}

static void set_swing_word(GameState *g, ScreenChannel channel, uint8_t which, uint16_t v)
{
    uint32_t position = battle_screen_swing_position(g, channel);
    switch (which + SPEED) {
    case SPEED: set_battle_screen_swing_speed(g, channel, v); break;
    case STEP: set_battle_screen_swing_step(g, channel, v); break;
    case POSITION_LOW: set_battle_screen_swing_position(g, channel, (position & 0xFFFF0000u) | v); break;
    default: set_battle_screen_swing_position(g, channel, (position & 0xFFFFu) | (uint32_t)v << 16); break;
    }
}

// $CD:2958 (keep it back into the swing), $CD:2999 (keep it aside)
static ScreenFlow exchange_kept_word(GameState *g, ScreenChannel channel, uint8_t which, bool to_swing)
{
    if (to_swing)
        set_swing_word(g, channel, which, battle_screen_kept_word(g, channel));
    else
        set_battle_screen_kept_word(g, channel, swing_word(g, channel, which));
    return screen_on();
}
SCREEN_OPERATION(exchange_kept_word, channel, which, to_swing)

static bool names_a_swing_word(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->which < SWING_WORDS;
}

/* ---- The orbit ------------------------------------------------------------------------------- */

/* An orbit's four sizes: the circle's across and down, the sway's and the
 * bob's. */
typedef struct {
    uint8_t circle_across, circle_down, sway, bob;
} OrbitSizes;

/* The orbit's phases move on by these each step. */
enum { CIRCLE_SPEED = 2, BOB_SPEED = 5, SWAY_SPEED = 6 };

static void leave_orbit(GameState *g, const OrbitSizes *o, uint8_t across, uint8_t down, uint8_t circle_x,
                        uint8_t circle_y, ScreenChannel channel)
{
    screen_leave_cell(g, 0x45, circle_x);
    screen_leave_cell(g, 0x47, circle_y);
    screen_leave_cell(g, 0x49, o->circle_across);
    screen_leave_cell(g, 0x4A, o->circle_down);
    screen_leave_cell(g, 0x4B, o->sway);
    screen_leave_cell(g, 0x4C, o->bob);
    screen_leave_cell(g, 0x4D, across);
    screen_leave_cell(g, 0x4E, down);
    screen_leave_sine_product(g, battle_screen_orbit_bob(g, channel), o->bob);
}

// $CD:1CD5
/* Move the layer a step along its orbit about a center. */
static void orbit_layer(GameState *g, ScreenChannel channel, const OrbitSizes *o, uint8_t across, uint8_t down)
{
    uint8_t sway = (uint8_t)(battle_screen_orbit_sway(g, channel) + SWAY_SPEED);
    uint8_t bob = (uint8_t)(battle_screen_orbit_bob(g, channel) + BOB_SPEED);
    uint8_t circle = (uint8_t)(battle_screen_orbit_circle(g, channel) + CIRCLE_SPEED);
    set_battle_screen_orbit_sway(g, channel, sway);
    set_battle_screen_orbit_bob(g, channel, bob);
    set_battle_screen_orbit_circle(g, channel, circle);
    uint8_t circle_x = screen_signed_cosine(g, circle, o->circle_across);
    uint8_t circle_y = screen_signed_sine(g, circle, o->circle_down);
    show_point(g, channel, (uint8_t)(screen_signed_cosine(g, sway, o->sway) + across + circle_x),
               (uint8_t)(screen_signed_sine(g, bob, o->bob) + down + circle_y));
    ORIGINAL_EFFECT(leave_orbit(g, o, across, down, circle_x, circle_y, channel));
}

static OrbitSizes orbit_sizes(const GameState *g, uint8_t orbit)
{
    return (OrbitSizes){ orbit_size(g, orbit, 0), orbit_size(g, orbit, 1), orbit_size(g, orbit, 2), orbit_size(g, orbit, 3) };
}

// $CD:1C63
/* Orbit the layer about the first animation position, at one of the table's
 * sizes. */
static ScreenFlow orbit_position(GameState *g, ScreenChannel channel, uint8_t orbit)
{
    screen_keep_positions_on_the_battlefield(g);
    OrbitSizes o = orbit_sizes(g, orbit);
    orbit_layer(g, channel, &o, screen_position_across(g, 0), screen_position_down(g, 0));
    return screen_on();
}
SCREEN_OPERATION(orbit_position, channel, orbit)

/* The middle of the battlefield the widening orbit goes round. */
enum { BATTLEFIELD_MIDDLE_ACROSS = 0x80, BATTLEFIELD_MIDDLE_DOWN = 0x87, GROWS_EVERY = 0x03 };

/* The widening orbit flickers: every other frame the layer shows the point
 * opposite (its scroll's low bytes turned about). */
static void turn_about(GameState *g, ScreenChannel channel)
{
    uint16_t y = battle_screen_layer_y(g, channel), x = battle_screen_layer_x(g, channel);
    set_battle_screen_layer_y(g, channel, (uint16_t)((y & 0xFF00) | (uint8_t)-(uint8_t)y));
    set_battle_screen_layer_x(g, channel, (uint16_t)((x & 0xFF00) | (uint8_t)-(uint8_t)x));
}

// $CD:1C7B
/* Orbit the layer about the middle of the battlefield: at the table's first
 * size, or (any other orbit) widening by one every fourth frame, its sway
 * twice as wide; and flicker. */
static ScreenFlow orbit_middle(GameState *g, ScreenChannel channel, uint8_t orbit)
{
    uint8_t frame = (uint8_t)battle_frame_count(g);
    if ((frame & GROWS_EVERY) == 0)
        set_battle_screen_orbit_growth(g, (uint8_t)(battle_screen_orbit_growth(g) + 1));
    uint8_t growth = battle_screen_orbit_growth(g);
    OrbitSizes o = orbit == 0 ? orbit_sizes(g, 0) : (OrbitSizes){ growth, growth, (uint8_t)(growth * 2), growth };
    orbit_layer(g, channel, &o, BATTLEFIELD_MIDDLE_ACROSS, BATTLEFIELD_MIDDLE_DOWN);
    if (frame & 1)
        turn_about(g, channel);
    return screen_on();
}
SCREEN_OPERATION(orbit_middle, channel, orbit)

SCREEN_OPCODES(screen_layer_opcodes,
    { 0xAA, OPERATION(put_layer_at_position), .needs = names_a_position, .length = 1, .position = LOW_SEVEN(0) },
    { 0x82, OPERATION(nudge_layer), .length = 1, .direction = TOP_THREE(0), .distance = LOW_FIVE(0) },
    { 0xC8, OPERATION(drop_layer_by_effect_object), .length = 1 },
    { 0xCD, OPERATION(start_swing), .length = 1, .frames = BYTE(0) },
    { 0x8C, OPERATION(exchange_kept_word), .needs = names_a_swing_word, .length = 1, .which = LOW_SEVEN(0), .to_swing = CONSTANT(true) },
    { 0x8D, OPERATION(exchange_kept_word), .needs = names_a_swing_word, .length = 1, .which = LOW_SEVEN(0), .to_swing = CONSTANT(false) },
    { 0xC5, OPERATION(orbit_position), .length = 1, .orbit = BYTE(0) },
    { 0xC4, OPERATION(orbit_middle), .length = 1, .orbit = BYTE(0) },
);
