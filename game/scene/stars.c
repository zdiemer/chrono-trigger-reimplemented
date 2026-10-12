/* Scenes: the star field the endings fly through.
 *
 * The staff roll's credits and the shared endings' star scenes show a field
 * of 128 stars coming at the viewer. Each star has a place across and down
 * from the middle of the view and a depth (scene_stars, scene_star_depths);
 * the field has a speed, which moves every star nearer or farther each frame,
 * and a drift, which moves every star across and down (channel 1's first
 * three registers, which the scripts set).
 *
 * - The scatter ($7E:5777) puts every star at a random place and depth.
 * - The flight ($7E:579B), a frame task, moves the stars and draws them: a
 *   star that passes the viewer, or the far end when the field flies away,
 *   comes back at the other end at a new random place; each is drawn as a
 *   sprite whose distance from the middle of the screen is its place over its
 *   distance, in one of sixteen sizes by depth, the nearer half in front of
 *   the layers unless the scene has made them all go behind (the staff roll,
 *   to keep them behind the credits).
 * - Behind The End, the field drifts at random ($7E:9897, then $7E:98AC every
 *   frame): the drift across, the drift down and the speed each hold for a
 *   random number of frames and then take a new random value.
 *
 * The random numbers are the program's stirred ones (scene/random.c), byte
 * draws for the stars and word draws for the drift, each handed on to the
 * next. The flight's sums keep the original's habit of adding in the last
 * sum's overflow, which moves a star a pixel now and then and is stirred into
 * the next draw; a port without it would draw other stars, so it is kept, as
 * a carry handed from sum to sum. What the draws are salted with is what the
 * original's X holds: the task's slot, or what the code before it left. */
#include "scene/scene.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The field's speed and drift, in channel 1's registers. */
enum { FIELD_CHANNEL = 1, FIELD_SPEED = 0, FIELD_DRIFT_ACROSS = 1, FIELD_DRIFT_DOWN = 2 };

enum { STARS = 128, STAR_BYTES = 4, FARTHEST = 0x7F };

/* The view: the middle of the screen the stars are drawn round, and the
 * nearest distance a star is divided by (its depth is added to it). */
enum { MIDDLE_ACROSS = 0x80, MIDDLE_DOWN = 0x70, NEAREST = 0x80 };

/* The stars' sprites: sixteen sizes from tile $60, the nearest, the eight
 * nearest drawn in front of the layers and the rest behind them. */
enum { FIRST_STAR_TILE = 0x60, FIRST_FAR_TILE = 0x68, STAR_BEHIND = 0x02, STAR_IN_FRONT = 0x32 };

/* How the scene has set the drawing (scene_stars_depth_branch): as the
 * program comes, or with every star behind the layers. */
enum { NEAR_STARS_IN_FRONT = 0x0430, EVERY_STAR_BEHIND = 0x0080 };

static int8_t field_speed(const GameState *g) { return (int8_t)scene_channel_register(g, FIELD_CHANNEL, FIELD_SPEED); }

static unsigned star_at(uint16_t entry) { return entry / STAR_BYTES; }

/* ---- The scatter ------------------------------------------------------------------------------ */

// $7E:5777
/* Every star at a random place and depth. The scatter keeps the star it is at
 * in the scene's second call cell, which the stir mixes in. Returns the last
 * draw. */
SceneRandom scene_scatter_stars(GameState *g, Platform *pf, SceneRandom r)
{
    for (uint16_t entry = 0; entry < STARS * STAR_BYTES; entry += STAR_BYTES) {
        r = scene_draw_random(g, pf, PF_AT(0x7E577A), r, entry, TM_ENTROPY_BYTES);
        set_scene_stars_across(g, star_at(entry), (uint8_t)r.value);
        r = scene_draw_random(g, pf, PF_AT(0x7E5780), r, entry, TM_ENTROPY_BYTES);
        set_scene_stars_down(g, star_at(entry), (uint8_t)r.value);
        r = scene_draw_random(g, pf, PF_AT(0x7E5786), r, entry, TM_ENTROPY_BYTES);
        r.value &= FARTHEST;
        set_scene_star_depths(g, star_at(entry), (uint8_t)r.value);
        set_scene_call_second(g, scene_program_own(), entry);
        r.carry = false;   /* the next star's first draw starts from this one's depth alone */
    }
    return r;
}

/* ---- The flight ------------------------------------------------------------------------------- */

/* A star's distance from the middle of the screen along one axis: its place
 * over its distance, halved, on the side its place is, by the CPU's divider.
 * The quotient's low byte is what counts: past a byte (a place of -128 at the
 * nearest distance), it wraps. The farthest star with a carry into its
 * distance wraps it to 0, where the divider gives $FFFF. The carry comes
 * from the sum before, as the original adds it in. */
static uint8_t projected(int8_t place, uint8_t distance, uint8_t middle, bool *carry)
{
    uint8_t size = (uint8_t)(place < 0 ? -place : place);
    uint8_t offset = (uint8_t)scene_divider_quotient((uint16_t)(size << 8), distance) >> 1;
    if (place < 0)
        offset = (uint8_t)-offset;
    uint16_t sum = (uint16_t)(offset + middle + *carry);
    *carry = sum > 0xFF;
    return (uint8_t)sum;
}

typedef struct {
    SceneRandom r;          /* the draws handed from one star to the next */
    uint8_t last_distance;  /* what the last star's projection divided by */
    int8_t last_down;
} Flight;

/* A star moved by the field's speed; one that leaves the field comes back
 * at a new random place, its depth wrapping round to the other end. Flying
 * away, a star leaves at the far end or past it (a depth of 255, 127 at the
 * fastest speed, wraps round unseen); coming nearer, past the viewer, where
 * its depth wraps below 0. */
enum { PAST_THE_FAR_END = 0xFF };

static void move_star(GameState *g, Platform *pf, Flight *f, unsigned star, uint16_t salt)
{
    int8_t speed = field_speed(g);
    uint8_t depth = scene_star_depths(g, star);
    uint16_t sum = (uint16_t)(depth + (uint8_t)speed + f->r.carry);
    uint8_t moved = (uint8_t)sum;
    bool leaves;
    if (speed >= 0) {
        f->r.carry = moved >= FARTHEST;
        leaves = moved >= FARTHEST && moved != PAST_THE_FAR_END;
    } else {
        f->r.carry = sum > 0xFF;
        leaves = moved > FARTHEST;
    }
    if (leaves) {
        SceneRandom r = scene_draw_random(g, pf, PF_AT(0x7E57B3), (SceneRandom){ moved, f->r.carry, false }, salt, TM_ENTROPY_BYTES);
        set_scene_stars_across(g, star, (uint8_t)r.value);
        r = scene_draw_random(g, pf, PF_AT(0x7E57B9), r, salt, TM_ENTROPY_BYTES);
        set_scene_stars_down(g, star, (uint8_t)r.value);
        f->r.carry = r.carry;
    }
    set_scene_star_depths(g, star, (uint8_t)(moved & FARTHEST));
}

/* The field's drift added to a star's place on one axis. */
static uint8_t drifted(uint8_t place, uint8_t drift, bool *carry)
{
    uint16_t sum = (uint16_t)(place + drift + *carry);
    *carry = sum > 0xFF;
    return (uint8_t)sum;
}

static uint8_t star_attributes(const GameState *g, uint8_t tile)
{
    if (scene_stars_depth_branch(g) == EVERY_STAR_BEHIND || tile >= FIRST_FAR_TILE)
        return STAR_BEHIND;
    return STAR_IN_FRONT;
}

/* A star drifted, and drawn into its sprite entry. */
static void draw_star(GameState *g, Flight *f, unsigned star, uint16_t entry)
{
    uint8_t depth = scene_star_depths(g, star);
    uint16_t distance_sum = (uint16_t)(depth + NEAREST + f->r.carry);
    uint8_t distance = (uint8_t)distance_sum;
    bool carry = distance_sum > 0xFF;

    uint8_t across = drifted(scene_stars_across(g, star), (uint8_t)scene_channel_register(g, FIELD_CHANNEL, FIELD_DRIFT_ACROSS), &carry);
    set_scene_stars_across(g, star, across);
    uint8_t down = drifted(scene_stars_down(g, star), (uint8_t)scene_channel_register(g, FIELD_CHANNEL, FIELD_DRIFT_DOWN), &carry);
    set_scene_stars_down(g, star, down);

    uint8_t tile = (uint8_t)(FIRST_STAR_TILE + ((distance >> 3) & 0x0F));
    uint8_t x = projected((int8_t)across, distance, MIDDLE_ACROSS, &carry);
    uint8_t y = projected((int8_t)down, distance, MIDDLE_DOWN, &carry);
    scene_place_sprite(g, entry, (SceneSprite){ .x = x, .y = y, .tile = tile, .attributes = star_attributes(g, tile) });
    f->r.carry = false;   /* the next star's moves start from the loop's compare */
    f->last_distance = distance;
    f->last_down = (int8_t)down;
}

// $7E:579B
/* Every star from the first free sprite entry on moved and drawn. `r` is
 * what the task was called with, which the first star's move carries in. */
static Flight fly_stars(GameState *g, Platform *pf, SceneRandom r, uint16_t salt)
{
    Flight f = { .r = r };
    for (uint16_t entry = scene_sprite_index(g); entry != STARS * STAR_BYTES; entry += STAR_BYTES) {
        move_star(g, pf, &f, star_at(entry), salt);
        draw_star(g, &f, star_at(entry), entry);
    }
    return f;
}

// $7E:999C
/* The Beyond Time ending's field takes the sprite entries from the 64th on,
 * after its captions' letters: with fewer drawn before it, the rest of their
 * group of four is put off the screen and the stars start at the 64th. */
enum { CAPTIONED_FIRST_STAR = 0x100, CAPTIONED_FIRST_STAR_HIGH = SCENE_SPRITE_HIGH_TABLE + 0x10 };

static bool make_room_after_the_captions(GameState *g)
{
    if (scene_sprite_index(g) >= CAPTIONED_FIRST_STAR)
        return false;
    scene_hide_rest_of_group(g);
    set_scene_sprite_index(g, CAPTIONED_FIRST_STAR);
    set_scene_sprite_high_pointer(g, CAPTIONED_FIRST_STAR_HIGH);
    return true;
}

/* ---- The drift behind The End ----------------------------------------------------------------- */

typedef struct {
    SceneRandom r;
    uint16_t drift;   /* -4 to 3 */
} Drift;

/* What drifts, in the order the original picks them: across, down and the
 * speed. */
enum { DRIFTS = 3 };
static const unsigned drift_register[DRIFTS] = { FIELD_DRIFT_ACROSS, FIELD_DRIFT_DOWN, FIELD_SPEED };

enum { DRIFT_RANGE = 8, DRIFT_LEAST = 4, LONGEST_DRIFT = 0x400 };

// $7E:98E3
/* A new drift, -4 to 3, and how many frames it holds, 1 to 1024. The second
 * draw is salted with the drift. */
static Drift pick_drift(GameState *g, Platform *pf, Drift d, uint32_t site)
{
    SceneRandom r = scene_draw_random(g, pf, PF_AT(site), d.r, d.drift, TM_ENTROPY_WORDS);
    uint16_t pick = r.value & (DRIFT_RANGE - 1);
    d.drift = (uint16_t)(pick - DRIFT_LEAST);
    r.value = d.drift;
    r.carry = pick >= DRIFT_LEAST;
    r = scene_draw_random(g, pf, PF_AT(0x7E98EE), r, d.drift, TM_ENTROPY_WORDS);
    r.value = (uint16_t)((r.value & (LONGEST_DRIFT - 1)) + 1);
    d.r = r;
    return d;
}

// $7E:9897
/* The drift starts: each of the three holds for a random time first. */
static Drift start_drift(GameState *g, Platform *pf, Drift d)
{
    static const uint32_t sites[DRIFTS] = { 0x7E9897, 0x7E989C, 0x7E98A1 };
    for (unsigned i = 0; i < DRIFTS; i++) {
        d = pick_drift(g, pf, d, sites[i]);
        set_scene_stars_drift_frames(g, i, d.r.value);
    }
    return d;
}

// $7E:98AC
/* A frame of the drift: each whose time is up takes a new value and a new
 * time. Returns the last draw. */
static Drift keep_drifting(GameState *g, Platform *pf, Drift d)
{
    static const uint32_t sites[DRIFTS] = { 0x7E98B0, 0x7E98BC, 0x7E98C8 };
    for (unsigned i = 0; i < DRIFTS; i++) {
        uint16_t left = (uint16_t)(scene_stars_drift_frames(g, i) - 1);
        set_scene_stars_drift_frames(g, i, left);
        if (left)
            continue;
        d = pick_drift(g, pf, d, sites[i]);
        set_scene_channel_register(g, FIELD_CHANNEL, drift_register[i], d.drift);
        set_scene_stars_drift_frames(g, i, d.r.value);
    }
    return d;
}

/* The first of the field's motions that isn't 0 (across, down, the speed), or
 * 0 where it stands still. */
static uint16_t first_motion(const GameState *g)
{
    for (unsigned i = 0; i < DRIFTS; i++)
        if (scene_channel_register(g, FIELD_CHANNEL, drift_register[i]))
            return scene_channel_register(g, FIELD_CHANNEL, drift_register[i]);
    return 0;
}

/* The field never stands still: with no drift and no speed, it flies away at
 * the slowest speed. */
static void keep_the_field_moving(GameState *g)
{
    if (!first_motion(g))
        set_scene_channel_register(g, FIELD_CHANNEL, FIELD_SPEED, 1);
}

/* ---- Entries ---------------------------------------------------------------------------------- */

/* The stars' routines run from the program's code, through a data bank on the
 * registers and low RAM, and draw through its stir ($7E:3443 to $C3:02DD). */
static bool stir_present(Snes *s) { return m_code_matches(s, 0x7E3443, 0x05, 0x5E005F3C3E12B1C5ull); }

/* The scatter's code, which the setups that scatter the stars run inline. */
bool scene_stars_scatter_code_present(Snes *s)
{
    return stir_present(s) && m_code_matches(s, 0x7E5777, 0x24, 0x9BECCE6703C0853Aull);
}

bool scene_stars_scatter_present(Snes *s) { return m_bank_reaches_io(s) && scene_stars_scatter_code_present(s); }

/* Entered with 8-bit A: the first draw starts from A and the carry; back, the
 * overflow is the last draw's. */
void scene_stars_scatter(Snes *s)
{
    SceneRandom last = scene_scatter_stars(state_of(s), m_platform(s), (SceneRandom){ s->cpu.a, m_flag(s, CPU_FLAG_C), false });
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, last.overflow));
    m_rts(s);
}

/* The flight's code, but for the branch the staff roll patches, which must be
 * one of the two the port knows; and its entries' place in the table. */
static bool flight_code_present(Snes *s)
{
    return m_bank_reaches_io(s) && scene_on_scene_page(s) && stir_present(s) && m_code_matches(s, 0x7E579B, 0x93, 0x2EEBC2AB028554EFull) &&
           m_code_matches(s, 0x7E5830, 0x25, 0x36CC17F40E00EC1Dull);
}

static bool flies_from(const GameState *g, uint16_t first)
{
    uint16_t branch = scene_stars_depth_branch(g);
    return (branch == NEAR_STARS_IN_FRONT || branch == EVERY_STAR_BEHIND) && first < STARS * STAR_BYTES && first % STAR_BYTES == 0;
}

bool scene_stars_fly_present(Snes *s)
{
    if (!flight_code_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = flies_from(g, scene_sprite_index(g));
    vars_peek(false);
    return ok;
}

/* The captioned field's room is made in the table: the cursor's high byte in
 * it where the group is finished. */
bool scene_stars_fly_after_captions_present(Snes *s)
{
    if (!flight_code_present(s) || !m_code_matches(s, 0x7E999C, 0x19, 0xA4690503889A36A6ull) ||
        !m_code_matches(s, 0x7E3870, 0x1B, 0xB778A96EC9EEDB9Aull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t first = scene_sprite_index(g), high = scene_sprite_high_pointer(g);
    bool ok = first < CAPTIONED_FIRST_STAR ? high >= SCENE_SPRITE_HIGH_TABLE && high < SCENE_SPRITE_HIGH_TABLE + SCENE_SPRITE_HIGH_TABLE_BYTES &&
                                                 flies_from(g, CAPTIONED_FIRST_STAR)
                                           : flies_from(g, first);
    vars_peek(false);
    return ok;
}

/* The task leaves Y past the table, and in A's high byte the last star's
 * place down as the divider took it (its size), where the frame's work may
 * read it after the last task; the divider and a cell of the scene's page
 * hold the last star's division. */
static void leave_flight(Snes *s, const Flight *f)
{
    GameState *g = state_of(s);
    uint8_t size = (uint8_t)(f->last_down < 0 ? -f->last_down : f->last_down);
    s->cpu.y = STARS * STAR_BYTES;
    s->cpu.a = (uint16_t)(size << 8 | (s->cpu.a & 0xFF));
    set_scene_stars_last_distance(g, f->last_distance);
    hw_set_dividend(g, (uint16_t)(size << 8));
    hw_divide_by(g, f->last_distance);
}

/* The flight, the first star's move carrying in `carry`, and `salt` salting
 * the draws. */
static void fly(Snes *s, bool carry, uint16_t salt)
{
    Flight f = fly_stars(state_of(s), m_platform(s), (SceneRandom){ 0, carry, false }, salt);
    ORIGINAL_EFFECT(leave_flight(s, &f));
    m_set_flag(s, CPU_FLAG_M, true);
    m_rts(s);
}

/* A frame task, entered with 16-bit A: the slot it runs in and the list's
 * carry are what the first star's move carries in. */
void scene_stars_fly(Snes *s) { fly(s, m_flag(s, CPU_FLAG_C), s->cpu.x); }

/* Here the carry the first move takes is always set: by the compare that
 * finds the room made, or by the end of the group's hiding, which leaves its
 * count of a group's sprites to salt the draws in place of the slot. */
enum { HIDING_LEAVES = 4 };

void scene_stars_fly_after_captions(Snes *s)
{
    bool made_room = make_room_after_the_captions(state_of(s));
    fly(s, true, made_room ? HIDING_LEAVES : s->cpu.x);
}

bool scene_stars_drift_present(Snes *s)
{
    return m_bank_reaches_io(s) && scene_on_scene_page(s) && stir_present(s) &&
           m_code_matches(s, 0x7E98AC, 0x37, 0x201936D7FF083E61ull) && m_code_matches(s, 0x7E98E3, 0x13, 0x08F9C085A7F4E16Dull);
}

bool scene_stars_start_drift_present(Snes *s)
{
    return scene_stars_drift_present(s) && m_code_matches(s, 0x7E9897, 0x15, 0xE86E6174BCBC7357ull);
}

/* The drift's tasks start from what they were called with: their own address
 * in A, the slot in X and the list's carry. */
static Drift called_with(Snes *s) { return (Drift){ { s->cpu.a, m_flag(s, CPU_FLAG_C), false }, s->cpu.x }; }

/* The list's exit for a task that hands its slot to another, the task in X. */
enum { REPLACE_THIS_TASK = 0x7E39B1, KEEP_DRIFTING = 0x98AC };

void scene_stars_start_drift(Snes *s)
{
    start_drift(state_of(s), m_platform(s), called_with(s));
    s->cpu.x = KEEP_DRIFTING;
    m_stop_at_own(s, REPLACE_THIS_TASK, DC_A | DC_N | DC_V | DC_Z | DC_C);
}

void scene_stars_keep_drifting(Snes *s)
{
    GameState *g = state_of(s);
    keep_drifting(g, m_platform(s), called_with(s));
    uint16_t moving = first_motion(g);
    keep_the_field_moving(g);
    ORIGINAL_EFFECT(s->cpu.a = moving);   /* the motion it found moving, or 0 */
    m_rts(s);
}
