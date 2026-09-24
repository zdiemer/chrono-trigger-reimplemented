/* Battle: the enemies as a battle starts.
 *
 * The event that starts a battle leaves eight enemy records
 * (battle_enemy_placement): where each enemy stands, the frame it starts on,
 * its enemy number (its top bit set for none), its graphics kind, palette and
 * graphics. The enemy number picks the enemy's header in the ROM, which says
 * how it animates, where its cursor points and how big it is, how it moves,
 * and flags: drawn on top of the others, the one a formation keeps its
 * places around, its numbers shown at its cursor point, and one the battle's rule
 * applies to. The battle sets up each enemy's object and state from these,
 * works out the formation's places around its leader, takes in any rule an
 * enemy brings into the battle (a battle the party can't run from, a
 * background that cycles its colors), and counts the kinds of enemy in the
 * fight for the window, which lists up to three kinds by name
 * (battle/setup_names.c). */
#include "battle/battle.h"
#include "battle/setup.h"

#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

enum { TILE_PIXELS = 16, ENEMY_RECORD_BYTES = 12 };

/* The enemy record's kind byte: the graphics kind, and whether the enemy
 * starts hidden. An enemy number with its top bit set is no enemy. */
enum { KIND_BITS = 0x07, STARTS_HIDDEN = 0x80, NO_ENEMY = 0x8000 };

/* An enemy header's flags. */
enum {
    DRAWN_ON_TOP = 0x80,      /* a second, topmost entry in the draw list */
    NUMBERS_AT_CURSOR = 0x20, /* its numbers show at its cursor point */
    LEADS_FORMATION = 0x08,   /* the formation keeps its places around it */
    UNDER_THE_RULE = 0x04,    /* the battle's rule applies to it */
};

/* Enemies drawn a pixel lower than their records place them. */
static bool stands_lower(uint16_t number) { return number == 0xD8 || number == 0x94 || number == 0xAD; }

static unsigned record_of(const GameState *g, uint8_t enemy) { return setup_enemy_record_at(g, enemy) / ENEMY_RECORD_BYTES; }

static bool record_has_enemy(const GameState *g, unsigned record)
{
    return !(battle_enemy_placement_enemy(g, record) & NO_ENEMY);
}

/* ---- Where the enemies start ---------------------------------------------------------------- */

static void keep_start_positions(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        unsigned slot = BATTLE_FIRST_ENEMY + enemy;
        set_battle_start_positions_tile_x(g, slot, battle_enemy_placement_tile_x(g, enemy));
        set_battle_start_positions_tile_y(g, slot, battle_enemy_placement_tile_y(g, enemy));
        set_battle_start_positions_x_offset(g, slot, battle_enemy_placement_x_offset(g, enemy));
        set_battle_start_positions_y_offset(g, slot, battle_enemy_placement_y_offset(g, enemy));
        set_battle_start_positions_frame(g, slot, battle_enemy_placement_frame(g, enemy));
    }
    ORIGINAL_EFFECT(setup_leave(g, 0x80, BATTLE_ENEMIES));
    ORIGINAL_EFFECT(setup_leave(g, 0x81, 5));
}

/* ---- One enemy ------------------------------------------------------------------------------ */

/* The animation lists an enemy draws with, and how far apart its facings are
 * in its frame lists: a quarter of the way to the next set's. */
enum { FACINGS = 4 };

static uint16_t animation_at(uint8_t set) { return (uint16_t)(set << 1); }
static uint16_t next_animation_at(uint8_t set) { return (uint16_t)((set + 1) << 1); }

static void animate_as(GameState *g, unsigned slot, uint8_t set)
{
    uint16_t frames = setup_frame_lists(g, animation_at(set)), delays = setup_delay_lists(g, animation_at(set));
    set_battle_object_frame_lists(g, slot, (uint32_t)SETUP_ANIMATION_BANK << 16 | frames);
    set_battle_object_delay_lists(g, slot, (uint32_t)SETUP_ANIMATION_BANK << 16 | delays);
    uint16_t stride = (uint16_t)(setup_frame_lists(g, next_animation_at(set)) - frames) / FACINGS;
    set_battle_facing_stride_low(g, slot, (uint8_t)stride);
    set_battle_facing_stride_high(g, slot, (uint8_t)(stride >> 8));
}

/* The flags an enemy's header gives it. */
static void take_flags(GameState *g, uint8_t enemy, uint8_t flags)
{
    unsigned slot = BATTLE_FIRST_ENEMY + enemy;
    set_battle_enemy_flags(g, enemy, flags);
    if (flags & DRAWN_ON_TOP) {
        set_battle_slot_draw_bits(g, slot, flags & DRAWN_ON_TOP);
        set_battle_object_extra_draw_on(g, slot, flags & DRAWN_ON_TOP);
    }
    if (flags & LEADS_FORMATION) {
        set_battle_formation(g, (uint8_t)(battle_formation(g) + 1));
        set_battle_formation_leader(g, enemy);
    }
    if (flags & NUMBERS_AT_CURSOR)
        set_battle_number_at_cursor(g, slot, (uint8_t)(battle_number_at_cursor(g, slot) + 1));
    set_battle_rule_applies(g, slot, flags & UNDER_THE_RULE);
}

/* How an enemy moves: its orbit's radius step (bits 0-2 of its header's
 * motion byte) and period (bits 4-7), and the pace of its effects, which the
 * two pick and which each enemy starts a frame later than the one before. */
enum { RADIUS_STEP_BITS = 0x07, PERIOD_SHIFT = 4 };

static uint8_t effects_pace_row(uint8_t period) { return (uint8_t)((period >> 1) << 2); }

static void take_motion(GameState *g, uint8_t enemy, uint8_t motion)
{
    uint8_t radius_step = motion & RADIUS_STEP_BITS, period = (uint8_t)(motion >> PERIOD_SHIFT);
    set_battle_orbit_radius_step(g, enemy, radius_step);
    set_battle_orbit_period(g, enemy, period);
    set_battle_orbit_countdown(g, enemy, period);
    uint8_t pace = setup_enemy_effect_period(g, (uint8_t)(effects_pace_row(period) + (radius_step >> 1)));
    set_battle_enemy_effect_period(g, enemy, pace);
    set_battle_enemy_effect_delay(g, enemy, (uint8_t)(pace + enemy));
}

/* An enemy's record is the one its start position was kept from (the table
 * of records' offsets names them in order), so it takes its place from that. */
// $CC:EA6A
static void set_up_enemy(GameState *g, uint8_t enemy, unsigned record)
{
    unsigned slot = BATTLE_FIRST_ENEMY + enemy, object = battle_object_of(slot);
    uint8_t kind_byte = battle_enemy_placement_kind(g, record);
    uint8_t kind = kind_byte & KIND_BITS;
    set_battle_object_kind(g, slot, kind);
    set_battle_object_part_count(g, object, setup_kind_sprites(g, kind));
    setup_place_combatant(g, slot);
    set_battle_object_facing_loaded(g, slot, 0);
    uint8_t palette = (uint8_t)(battle_enemy_placement_palette(g, record) << 1);
    set_battle_object_palette_initial(g, slot, palette);
    set_battle_object_palette(g, slot, palette);
    set_battle_object_hidden(g, slot, kind_byte & STARTS_HIDDEN);
    set_battle_object_graphics(g, slot, battle_enemy_placement_graphics(g, record));

    uint16_t number = battle_enemy_placement_enemy(g, record);
    EnemyHeader header = setup_enemy_header(g, number);
    set_battle_object_tile_lists(g, slot, setup_tile_lists(g, (uint16_t)(header.graphics_set * 3)));
    animate_as(g, slot, header.animation_set);
    take_flags(g, enemy, header.flags);
    set_battle_object_shadow(g, slot, 0);
    set_battle_cursor_offset_x(g, slot, header.cursor_x);
    set_battle_cursor_offset_y(g, slot, header.cursor_y);
    set_battle_object_half_width(g, slot, header.half_width);
    set_battle_object_half_height(g, slot, header.half_height);
    take_motion(g, enemy, header.motion);
    set_battle_enemy_numbers(g, enemy, number);
    if (stands_lower(number))
        set_battle_object_y(g, object, (uint8_t)(battle_object_y(g, object) + 1));
    set_battle_slot_present(g, slot, (uint8_t)(battle_slot_present(g, slot) + 1));
}

/* ---- The formation, the rules and the kinds -------------------------------------------------- */

/* The formation's places: each enemy's position from its leader's. */
// $CC:EC31
static void place_formation(GameState *g)
{
    unsigned leader = battle_object_of(BATTLE_FIRST_ENEMY + battle_formation_leader(g));
    uint8_t x = battle_object_x(g, leader), y = battle_object_y(g, leader);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        unsigned object = battle_object_of(BATTLE_FIRST_ENEMY + enemy);
        if (!battle_slot_present(g, BATTLE_FIRST_ENEMY + enemy))
            continue;
        set_battle_formation_offset_x(g, enemy, (uint8_t)(battle_object_x(g, object) - x));
        set_battle_formation_offset_y(g, enemy, (uint8_t)(battle_object_y(g, object) - y));
    }
    ORIGINAL_EFFECT(setup_leave(g, 0x80, x));
    ORIGINAL_EFFECT(setup_leave(g, 0x81, y));
}

/* The rules an enemy can bring: the party can't run away, another enemy's
 * sprites follow the first enemy's while it flickers
 * (battle_effect_mirror_slot3), or any
 * other, which is the battle's rule (battle_enemy_rule) with its parameter
 * in the byte after it. An enemy number is looked up whether or not its
 * enemy is in the fight. */
enum { RULE_NO_ESCAPE = 3, RULE_MIRROR = 4, ESCAPE_BLOCKED = 0x01, RULE_END = 0x8000 };

static void leave_rules(GameState *g, bool no_escape, uint8_t mirror)
{
    setup_leave_word(g, 0x80, no_escape);
    setup_leave_word(g, 0x82, mirror);
}

// $CC:EC63
static void take_rules(GameState *g)
{
    bool no_escape = false;
    uint8_t mirror = 0;
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        uint16_t number = battle_enemy_numbers(g, enemy);
        for (uint16_t at = 0; !(setup_rule_enemy(g, at) & RULE_END); at += SETUP_RULE_ENTRY_BYTES) {
            if (setup_rule_enemy(g, at) != number)
                continue;
            uint16_t rule = setup_rule(g, at);
            if (rule == RULE_NO_ESCAPE)
                no_escape = true;
            else if (rule == RULE_MIRROR)
                mirror = RULE_MIRROR;
            else {
                set_battle_enemy_rule(g, (uint8_t)rule);
                set_battle_background_cycle(g, (uint8_t)(rule >> 8));
            }
        }
    }
    set_battle_fight_flags(g, battle_fight_flags(g) | (no_escape ? ESCAPE_BLOCKED : 0));
    set_battle_effect_mirror_slot3(g, mirror);
    ORIGINAL_EFFECT(leave_rules(g, no_escape, mirror));
}

/* The kinds of enemy in the fight, in the order they first appear, and how
 * many of each, among the enemies that aren't hidden. The game's battles
 * have at most three (setup_enemies_are_real). */
static bool counts_as_kind(const GameState *g, unsigned enemy)
{
    unsigned slot = BATTLE_FIRST_ENEMY + enemy;
    return battle_slot_present(g, slot) && !(battle_object_hidden(g, slot) & STARTS_HIDDEN);
}

static void leave_kinds(GameState *g, const uint16_t counts[SETUP_ENEMY_KINDS], uint8_t kinds)
{
    for (unsigned kind = 0; kind < SETUP_ENEMY_KINDS; kind++)
        setup_leave_word(g, (uint8_t)(0x80 + 2 * kind), counts[kind]);
    setup_leave_word(g, 0x86, (uint16_t)(2 * kinds));
    setup_leave_word(g, 0x88, BATTLE_ENEMIES);
}

// $CC:ECB2
static void count_kinds(GameState *g)
{
    uint16_t counts[SETUP_ENEMY_KINDS] = { 0 };
    uint8_t kinds = 0;
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        if (!counts_as_kind(g, enemy))
            continue;
        uint16_t number = battle_enemy_numbers(g, enemy);
        uint8_t kind = 0;
        while (kind < SETUP_ENEMY_KINDS && battle_enemy_kinds(g, kind) != number)
            kind++;
        if (kind == SETUP_ENEMY_KINDS) {
            kind = kinds++;
            set_battle_enemy_kinds(g, kind, number);
        }
        counts[kind]++;
    }
    for (unsigned kind = 0; kind < SETUP_ENEMY_KINDS; kind++)
        set_battle_enemy_kind_counts(g, kind, (uint8_t)counts[kind]);
    ORIGINAL_EFFECT(leave_kinds(g, counts, kinds));
}

/* Whether the enemies are ones the game makes, as the setup counts on: their
 * slots empty before it fills them, a formation leader among them, and at
 * most three kinds of enemy in view. */
bool setup_enemies_are_real(const GameState *g)
{
    if (battle_formation(g) != 0 && battle_formation_leader(g) >= BATTLE_ENEMIES)
        return false;
    uint16_t seen[BATTLE_ENEMIES];
    unsigned kinds = 0;
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        if (battle_slot_present(g, BATTLE_FIRST_ENEMY + enemy))
            return false;
        unsigned record = record_of(g, enemy);
        if (!record_has_enemy(g, record) || (battle_enemy_placement_kind(g, record) & STARTS_HIDDEN))
            continue;
        uint16_t number = battle_enemy_placement_enemy(g, record);
        unsigned k = 0;
        while (k < kinds && seen[k] != number)
            k++;
        if (k == kinds)
            seen[kinds++] = number;
    }
    return kinds <= SETUP_ENEMY_KINDS;
}

/* What the original leaves in its working cells from the last enemy it set
 * up: its number, its animation set and the lengths of its lists, and the
 * row of the effects' pace table its period picked. */
static void leave_last_enemy(GameState *g)
{
    setup_leave_word(g, 0x80, BATTLE_ENEMIES);
    int last = BATTLE_ENEMIES;
    while (last-- > 0 && !record_has_enemy(g, record_of(g, (uint8_t)last)))
        ;
    if (last < 0)
        return;
    uint16_t number = battle_enemy_numbers(g, (unsigned)last);
    uint8_t set = setup_enemy_header(g, number).animation_set;
    uint16_t frames_next = setup_frame_lists(g, next_animation_at(set));
    uint16_t delays = setup_delay_lists(g, animation_at(set)), delays_next = setup_delay_lists(g, next_animation_at(set));
    setup_leave(g, 0x82, effects_pace_row(battle_orbit_period(g, (unsigned)last)));
    setup_leave(g, 0x83, (uint8_t)((uint16_t)(number << 1) >> 8));
    setup_leave_word(g, 0x84, (uint16_t)(battle_facing_stride_high(g, BATTLE_FIRST_ENEMY + (unsigned)last) << 8
                                         | battle_facing_stride_low(g, BATTLE_FIRST_ENEMY + (unsigned)last)));
    setup_leave_word(g, 0x86, (uint16_t)(delays_next - delays));
    setup_leave_word(g, 0x88, frames_next);
    setup_leave_word(g, 0x8A, delays_next);
    setup_leave_word(g, 0x8C, number);
    setup_leave_word(g, 0x8E, set);
}

// $CC:EA33
void setup_enemies(GameState *g)
{
    keep_start_positions(g);
    for (uint8_t enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        unsigned record = record_of(g, enemy);
        if (record_has_enemy(g, record))
            set_up_enemy(g, enemy, record);
    }
    ORIGINAL_EFFECT(leave_last_enemy(g));
    if (battle_formation(g))
        place_formation(g);
    take_rules(g);
    count_kinds(g);
}
