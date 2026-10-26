/* Battle: what moves on the battlefield each frame while effects play,
 * besides the paths (battle/effect_paths.c).
 *
 * The frame's update (battle/frame.c) runs these once a frame:
 * - the damage and healing numbers over the combatants rise, a row of a
 *   table at a time, until the table ends;
 * - a combatant on a scripted move (a hop back, a lunge) takes the move's next
 *   step, eight steps a move;
 * - the anchors (battle/effects.h) follow what they follow: a combatant, or
 *   another anchor;
 * - a combatant that leaves a trail records where its sprite is every few
 *   frames, the last four places, which its afterimages are drawn at
 *   (battle/draw_objects.c);
 * - a party member whose colors cycle (a glow while a tech charges) takes the
 *   cycle's next color. */
#include "battle/effects.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/sprite_data.h"
#include "original_effects.h"
#include "vars.h"

/* The loops' counters the original keeps in the effect loops' first cell:
 * each ends at its last value, which the wrapper leaves. */
static void leave_loop_cell(GameState *g, uint8_t low, uint8_t high)
{
    set_battle_effect_cells(g, 0, low);
    set_battle_effect_cells(g, 1, high);
}

/* The effect loops' first cell, as a count down past 0 leaves it. */
enum { COUNTED_PAST_ZERO = 0xFF };

/* ---- The damage numbers --------------------------------------------------------------- */

/* Each combatant's number is four digit sprites. */
enum { NUMBER_DIGITS = 4 };

static bool numbers_rising(const GameState *g) { return battle_numbers_rising(g) != 0; }
static bool rises_by_second_table(const GameState *g, unsigned slot) { return battle_number_rise_alt(g, slot) != 0; }

static void raise_number(GameState *g, unsigned slot, uint8_t by)
{
    for (unsigned digit = 0; digit < NUMBER_DIGITS; digit++) {
        unsigned sprite = slot * NUMBER_DIGITS + digit;
        set_battle_number_sprites_y(g, sprite, (uint8_t)(battle_number_sprites_y(g, sprite) + by));
    }
}

// $CF:E781
/* While the numbers rise: move every combatant's number by the rise's next
 * row (its row of the second table for those that rise by it). A row that
 * ends the first table ends the rise. */
void battle_raise_numbers(GameState *g)
{
    if (!numbers_rising(g))
        return;
    uint8_t row = battle_numbers_row(g);
    if (battle_number_rise_table(g, false, row) == BATTLE_NUMBER_RISE_END) {
        set_battle_numbers_rising(g, false);
        return;
    }
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_number_rise(g, slot, battle_number_rise_table(g, rises_by_second_table(g, slot), row));
    set_battle_numbers_row(g, (uint8_t)(row + 1));
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        raise_number(g, slot, battle_number_rise(g, slot));
    ORIGINAL_EFFECT(set_battle_effect_cells(g, 0, 0));
}

/* ---- Scripted moves ------------------------------------------------------------------- */

/* A move's steps are counted down from the last to the first; the count
 * runs below zero once the move is done. */
enum { LAST_STEP = BATTLE_SCRIPTED_MOVE_STEPS - 1, MOVE_DONE = 0x80 };

static bool on_scripted_move(const GameState *g, unsigned slot)
{
    return battle_path_active(g, slot) && !(battle_path_steps(g, slot) & MOVE_DONE);
}

/* The step a move is on, as the original indexes it: a byte, which for a
 * count past the last step wraps round. */
static uint8_t step_of(const GameState *g, unsigned slot) { return (uint8_t)(LAST_STEP - battle_path_steps(g, slot)); }

// $CF:EC78
/* Move each combatant on a scripted move by its move's next step. */
void battle_step_scripted_moves(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!on_scripted_move(g, slot))
            continue;
        uint8_t move = battle_path(g, slot), step = step_of(g, slot);
        place_combatant_x(g, slot, (uint8_t)(combatant_x(g, slot) + battle_scripted_move_dx(g, move, step)));
        place_combatant_y(g, slot, (uint8_t)(combatant_y(g, slot) + battle_scripted_move_dy(g, move, step)));
        set_battle_path_steps(g, slot, (uint8_t)(battle_path_steps(g, slot) - 1));
        ORIGINAL_EFFECT(leave_loop_cell(g, step, 0));
    }
}

/* ---- The anchors ---------------------------------------------------------------------- */

/* What an anchor follows: a combatant (its battle slot), or with the second
 * bit another anchor (in the low three bits); with the top bit, nothing, and
 * it stays where it is. */
enum { FOLLOWS_NOTHING = 0x80, FOLLOWS_ANCHOR = 0x40, FOLLOWED_ANCHOR = 0x07 };

// $CF:ECC2
/* Put each anchor where what it follows is, the last anchor first, so that
 * an anchor following one before it takes where that one was put last frame.
 * An anchor following a combatant is where the combatant stands. */
void battle_update_anchors(GameState *g)
{
    for (int anchor = BATTLE_ANCHORS - 1; anchor >= 0; anchor--) {
        uint8_t follows = battle_anchor_sources(g, (unsigned)anchor);
        if (follows & FOLLOWS_NOTHING)
            continue;
        if (follows & FOLLOWS_ANCHOR) {
            unsigned leader = follows & FOLLOWED_ANCHOR;
            place_anchor(g, (unsigned)anchor, anchor_x(g, leader), anchor_y(g, leader));
        } else {
            place_anchor(g, (unsigned)anchor, combatant_x(g, follows), combatant_y(g, follows));
        }
    }
    ORIGINAL_EFFECT(leave_loop_cell(g, COUNTED_PAST_ZERO, 0));
}

/* ---- Trails --------------------------------------------------------------------------- */

/* A trail is the last four places the combatant's sprite was recorded at,
 * the newest first, a word a coordinate. */
enum { TRAIL_POINTS = 4 };

typedef struct {
    int16_t x, y;
} TrailPoint;

static TrailPoint trail_point(const GameState *g, unsigned point, unsigned slot)
{
    return (TrailPoint){ (int16_t)(battle_trail_low_x_byte(g, point, slot) | battle_trail_high_x_byte(g, point, slot) << 8),
                         (int16_t)(battle_trail_low_y_byte(g, point, slot) | battle_trail_high_y_byte(g, point, slot) << 8) };
}

static void set_trail_point(GameState *g, unsigned point, unsigned slot, TrailPoint p)
{
    set_battle_trail_low_x_byte(g, point, slot, (uint8_t)p.x);
    set_battle_trail_high_x_byte(g, point, slot, (uint8_t)((uint16_t)p.x >> 8));
    set_battle_trail_low_y_byte(g, point, slot, (uint8_t)p.y);
    set_battle_trail_high_y_byte(g, point, slot, (uint8_t)((uint16_t)p.y >> 8));
}

/* Where the combatant's sprite is: where it stands, moved by its sprite's
 * offset. */
static TrailPoint sprite_position(const GameState *g, unsigned slot)
{
    return (TrailPoint){ (int16_t)(combatant_x(g, slot) + combatant_sprite_offset_x(g, slot)),
                         (int16_t)(combatant_y(g, slot) + combatant_sprite_offset_y(g, slot)) };
}

static bool trail_due(GameState *g, unsigned slot)
{
    uint8_t left = (uint8_t)(battle_trail_countdown(g, slot) - 1);
    set_battle_trail_countdown(g, slot, left ? left : battle_trail_period(g, slot));
    return left == 0;
}

// $CF:ED18
/* Every few frames, each combatant leaving a trail records where its sprite
 * is, dropping the oldest of its four places. */
void battle_record_trails(GameState *g)
{
    for (int slot = BATTLE_COMBATANTS - 1; slot >= 0; slot--) {
        if (!battle_trail_on(g, (unsigned)slot) || !trail_due(g, (unsigned)slot))
            continue;
        for (unsigned point = TRAIL_POINTS - 1; point > 0; point--)
            set_trail_point(g, point, (unsigned)slot, trail_point(g, point - 1, (unsigned)slot));
        set_trail_point(g, 0, (unsigned)slot, sprite_position(g, (unsigned)slot));
    }
}

/* ---- The party's color cycles --------------------------------------------------------- */

/* A party member's sprite palette, as its sprites' attribute bits (twice the
 * palette); the one that isn't the member's own takes no cycle. The two
 * colors of the member's palette the cycle writes, and how the cycle steps
 * through its colors and wraps. */
enum { SHARED_PALETTE = 0x08, PALETTE_COLORS = 16, COLOR_BYTES = 2, FIRST_SPRITE_COLOR = 0x80 };
enum { CYCLED_COLOR = 1, CYCLED_COLOR_AGAIN = 12 };
enum { CYCLE_STEP = 2, CYCLE_END = 0x7C };

/* A sprite palette's first color. A palette is sixteen colors, and the
 * attribute bits are twice the palette, so its offset is the bits times 16
 * bytes, which the original works out in a byte. */
static unsigned first_color_of(uint8_t attribute_bits)
{
    uint8_t offset = (uint8_t)(attribute_bits * PALETTE_COLORS);
    return FIRST_SPRITE_COLOR + offset / COLOR_BYTES;
}

static bool colors_cycle(const GameState *g, unsigned member)
{
    return battle_party_palette_cycle_on(g, member) && !battle_sprite_palettes_flipped(g)
           && battle_object_palette(g, member) != SHARED_PALETTE;
}

// $CF:E7D7
/* Each party member whose colors cycle shows the cycle's next color in the
 * two cycling colors of its sprite palette, the next for its pose. */
void battle_cycle_party_colors(GameState *g)
{
    for (int member = BATTLE_PARTY_SIZE - 1; member >= 0; member--) {
        if (!colors_cycle(g, (unsigned)member))
            continue;
        uint8_t at = battle_party_palette_cycle(g, (unsigned)member);
        uint16_t color = battle_color_cycle_color(
            g, (uint16_t)(at + battle_color_cycle_start(g, battle_object_pose(g, (unsigned)member))));
        unsigned first = first_color_of(battle_object_palette(g, (unsigned)member));
        set_palette_working(g, first + CYCLED_COLOR, color);
        set_palette_working(g, first + CYCLED_COLOR_AGAIN, color);
        at = (uint8_t)(at + CYCLE_STEP);
        set_battle_party_palette_cycle(g, (unsigned)member, at == CYCLE_END ? 0 : at);
    }
    ORIGINAL_EFFECT(leave_loop_cell(g, COUNTED_PAST_ZERO, 0));
}

/* ---- The entries ---------------------------------------------------------------------- */

/* Each runs with A's high byte 0 (the guard), which the callers read and the
 * logic leaves as it is; the rest of what the original leaves is dead
 * (the registry's don't-care sets). */

void battle_raise_numbers_entry(Snes *s)
{
    battle_raise_numbers(state_of(s));
    m_rts(s);
}

void battle_step_scripted_moves_entry(Snes *s)
{
    battle_step_scripted_moves(state_of(s));
    m_rts(s);
}

void battle_update_anchors_entry(Snes *s)
{
    battle_update_anchors(state_of(s));
    m_rts(s);
}

void battle_record_trails_entry(Snes *s)
{
    battle_record_trails(state_of(s));
    m_rts(s);
}

/* The caller reads Y, which the original leaves as the last member's color
 * index when that member's colors cycled (its palette's first color, as a
 * byte offset into the palette's sprite colors), and 0 otherwise. */
static uint16_t y_after_color_cycle(GameState *g)
{
    vars_peek(true);
    uint16_t y = colors_cycle(g, 0) ? (uint8_t)(battle_object_palette(g, 0) << 4) : 0;
    vars_peek(false);
    return y;
}

void battle_cycle_party_colors_entry(Snes *s)
{
    GameState *g = state_of(s);
    battle_cycle_party_colors(g);
    s->cpu.y = y_after_color_cycle(g);
    m_rts(s);
}
