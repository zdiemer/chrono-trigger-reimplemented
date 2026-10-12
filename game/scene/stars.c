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
 * draws for the stars and word draws for the drift, each going on from the
 * one before. A star's sums carry from one to the next (sum_after), as the
 * original's do; the stars' places, and through them the stir, depend on it.
 * What the draws are salted with is what the original has at hand: the
 * task's slot, or what the code before it left. */
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
/* Every star at a random place and depth, each place and depth a byte draw
 * salted with the star's entry; the scatter keeps the entry in the scene's
 * second call cell, which the stir mixes in. Each star's draws start from the
 * last star's depth. */
void scene_scatter_stars(GameState *g, Platform *pf, SceneDraw first)
{
    SceneDraw d = first;
    for (uint16_t entry = 0; entry < STARS * STAR_BYTES; entry += STAR_BYTES) {
        d = scene_draw_random(g, pf, PF_AT(0x7E577A), d, entry, TM_ENTROPY_BYTES);
        set_scene_stars_across(g, star_at(entry), (uint8_t)d);
        d = scene_draw_random(g, pf, PF_AT(0x7E5780), d, entry, TM_ENTROPY_BYTES);
        set_scene_stars_down(g, star_at(entry), (uint8_t)d);
        d = scene_draw_random(g, pf, PF_AT(0x7E5786), d, entry, TM_ENTROPY_BYTES);
        d &= FARTHEST;
        set_scene_star_depths(g, star_at(entry), (uint8_t)d);
        set_scene_call_second(g, scene_program_own(), entry);
    }
}

/* ---- The flight ------------------------------------------------------------------------------- */

/* A star's sums, one after another -- its depth moved, its distance, its
 * place drifted across and down, and where it lands across and down -- are
 * byte sums that never start afresh: each takes in the bit the sum before it
 * spilled over 255. With two operands under 128 that bit is set only when the
 * sum passes 255; with one from 128 up (a negative drift, speed or offset,
 * which wraps round), it is set unless the true sum went below 0, so it is
 * nearly always set and the next sum comes out one more. The original adds so,
 * and the stars' places, and through them the stir, follow it. A sum is kept
 * as nine bits, the spilled one on top. */
static uint16_t sum_after(uint16_t before, uint8_t a, uint8_t b) { return (uint16_t)(a + b + (before >> 8 & 1)); }

/* A star's distance from the middle of the screen along one axis: its place
 * over its distance, halved, on the side its place is, by the CPU's divider.
 * The quotient's low byte is what counts: past a byte (a place of -128 at the
 * nearest distance), it wraps. The farthest star with a bit spilled into its
 * distance has a distance of 0, where the divider gives $FFFF. */
static uint8_t offset_from_middle(int8_t place, uint8_t distance)
{
    uint8_t size = (uint8_t)(place < 0 ? -place : place);
    uint8_t offset = (uint8_t)scene_divider_quotient((uint16_t)(size << 8), distance) >> 1;
    return place < 0 ? (uint8_t)-offset : offset;
}

typedef struct {
    uint8_t last_distance;  /* what the last star's projection divided by */
    int8_t last_down;
} Flight;

/* A star moved by the field's speed; one that leaves the field comes back
 * at a new random place, its depth wrapping round to the other end. Flying
 * away, a star leaves at the far end or past it (a depth of 255, 127 at the
 * fastest speed, wraps round unseen); coming nearer, past the viewer, where
 * its depth wraps below 0. Returns the moved depth's sum: what goes on into
 * the distance is, flying away, whether the star reached the far end (the
 * test the original makes), coming nearer the sum's own spilled bit, and for
 * a star that comes back, the last draw's. */
enum { PAST_THE_FAR_END = 0xFF, SPILLED = 0x100 };

static uint16_t move_star(GameState *g, Platform *pf, unsigned star, uint16_t before, uint16_t salt)
{
    int8_t speed = field_speed(g);
    uint16_t sum = sum_after(before, scene_star_depths(g, star), (uint8_t)speed);
    uint8_t moved = (uint8_t)sum;
    bool leaves;
    if (speed >= 0) {
        leaves = moved >= FARTHEST && moved != PAST_THE_FAR_END;
        sum = (uint16_t)(moved | (moved >= FARTHEST ? SPILLED : 0));
    } else {
        leaves = moved > FARTHEST;
    }
    if (leaves) {
        SceneDraw d = scene_draw_random(g, pf, PF_AT(0x7E57B3), sum, salt, TM_ENTROPY_BYTES);
        set_scene_stars_across(g, star, (uint8_t)d);
        d = scene_draw_random(g, pf, PF_AT(0x7E57B9), d, salt, TM_ENTROPY_BYTES);
        set_scene_stars_down(g, star, (uint8_t)d);
        sum = (uint16_t)(moved | (d & SPILLED));
    }
    set_scene_star_depths(g, star, (uint8_t)(moved & FARTHEST));
    return sum;
}

static uint8_t star_attributes(const GameState *g, uint8_t tile)
{
    if (scene_stars_depth_branch(g) == EVERY_STAR_BEHIND || tile >= FIRST_FAR_TILE)
        return STAR_BEHIND;
    return STAR_IN_FRONT;
}

/* A star drifted, and drawn into its sprite entry, its sums following on from
 * its move. */
static void draw_star(GameState *g, Flight *f, unsigned star, uint16_t entry, uint16_t moved)
{
    uint16_t distance = sum_after(moved, scene_star_depths(g, star), NEAREST);
    uint16_t across = sum_after(distance, scene_stars_across(g, star), (uint8_t)scene_channel_register(g, FIELD_CHANNEL, FIELD_DRIFT_ACROSS));
    uint16_t down = sum_after(across, scene_stars_down(g, star), (uint8_t)scene_channel_register(g, FIELD_CHANNEL, FIELD_DRIFT_DOWN));
    set_scene_stars_across(g, star, (uint8_t)across);
    set_scene_stars_down(g, star, (uint8_t)down);

    uint8_t near = (uint8_t)distance;
    uint8_t tile = (uint8_t)(FIRST_STAR_TILE + ((near >> 3) & 0x0F));
    uint16_t x = sum_after(down, offset_from_middle((int8_t)across, near), MIDDLE_ACROSS);
    uint16_t y = sum_after(x, offset_from_middle((int8_t)down, near), MIDDLE_DOWN);
    scene_place_sprite(g, entry, (SceneSprite){ .x = (uint8_t)x, .y = (uint8_t)y, .tile = tile, .attributes = star_attributes(g, tile) });
    f->last_distance = near;
    f->last_down = (int8_t)down;
}

// $7E:579B
/* Every star from the first free sprite entry on moved and drawn. The first
 * star's sums start with `first_spilled`, what the task was called with; the
 * others' start clean (the loop's own test leaves nothing). */
static Flight fly_stars(GameState *g, Platform *pf, bool first_spilled, uint16_t salt)
{
    Flight f = { 0, 0 };
    uint16_t before = first_spilled ? SPILLED : 0;
    for (uint16_t entry = scene_sprite_index(g); entry != STARS * STAR_BYTES; entry += STAR_BYTES) {
        uint16_t moved = move_star(g, pf, star_at(entry), before, salt);
        draw_star(g, &f, star_at(entry), entry, moved);
        before = 0;
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
    SceneDraw last;   /* the draws go on from one pick to the next */
    uint16_t drift;   /* -4 to 3 */
} Drift;

/* What drifts, in the order the original picks them: across, down and the
 * speed. */
enum { DRIFTS = 3 };
static const unsigned drift_register[DRIFTS] = { FIELD_DRIFT_ACROSS, FIELD_DRIFT_DOWN, FIELD_SPEED };

enum { DRIFT_RANGE = 8, DRIFT_LEAST = 4, LONGEST_DRIFT = 0x400, WORD_SPILLED = 0x10000 };

// $7E:98E3
/* A new drift, -4 to 3, and how many frames it holds, 1 to 1024, both word
 * draws salted with the drift before (the second with the new one). The
 * second draw goes on from the drift as the pick less four, its spilled bit
 * set where that didn't go below 0; the frames keep the second draw's
 * spilled bit, for the next pick. */
static Drift pick_drift(GameState *g, Platform *pf, Drift d, uint32_t site)
{
    SceneDraw pick = scene_draw_random(g, pf, PF_AT(site), d.last, d.drift, TM_ENTROPY_WORDS) & (DRIFT_RANGE - 1);
    SceneDraw from_drift = pick + WORD_SPILLED - DRIFT_LEAST;
    d.drift = (uint16_t)from_drift;
    SceneDraw frames = scene_draw_random(g, pf, PF_AT(0x7E98EE), from_drift, d.drift, TM_ENTROPY_WORDS);
    d.last = (frames & WORD_SPILLED) | ((frames & (LONGEST_DRIFT - 1)) + 1);
    return d;
}

static uint16_t frames_held(const Drift *d) { return (uint16_t)d->last; }

// $7E:9897
/* The drift starts: each of the three holds for a random time first. */
static Drift start_drift(GameState *g, Platform *pf, Drift d)
{
    static const uint32_t sites[DRIFTS] = { 0x7E9897, 0x7E989C, 0x7E98A1 };
    for (unsigned i = 0; i < DRIFTS; i++) {
        d = pick_drift(g, pf, d, sites[i]);
        set_scene_stars_drift_frames(g, i, frames_held(&d));
    }
    return d;
}

// $7E:98AC
/* A frame of the drift: each whose time is up takes a new value and a new
 * time. */
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
        set_scene_stars_drift_frames(g, i, frames_held(&d));
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
    scene_scatter_stars(state_of(s), m_platform(s), scene_draw_from(s->cpu.a, m_flag(s, CPU_FLAG_C), TM_ENTROPY_BYTES));
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, scene_random_last_overflow()));
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

/* The flight, the first star's sums starting with `spilled`, and `salt`
 * salting the draws. */
static void fly(Snes *s, bool spilled, uint16_t salt)
{
    Flight f = fly_stars(state_of(s), m_platform(s), spilled, salt);
    ORIGINAL_EFFECT(leave_flight(s, &f));
    m_set_flag(s, CPU_FLAG_M, true);
    m_rts(s);
}

/* A frame task, entered with 16-bit A: the slot it runs in salts the draws,
 * and the list's carry is what the first star's sums start with. */
void scene_stars_fly(Snes *s) { fly(s, m_flag(s, CPU_FLAG_C), s->cpu.x); }

/* Here the first star's sums always start with the bit set: the original
 * comes to the flight from a compare that finds the room made, or from the
 * group's hiding, whose count of a group's sprites then salts the draws in
 * place of the slot. */
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
static Drift called_with(Snes *s) { return (Drift){ scene_draw_from(s->cpu.a, m_flag(s, CPU_FLAG_C), TM_ENTROPY_WORDS), s->cpu.x }; }

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
