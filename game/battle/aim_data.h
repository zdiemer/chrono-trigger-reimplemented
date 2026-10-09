/* Battle: how the actions aim, as the ROM holds it (battle/aim.c).
 *
 * Every party tech, enemy tech and item says how it aims: the targets it
 * takes (an aim mode, which battle/aim.c's table reads) and, for a tech, a
 * set of area rules that then widen or replace them. A set of area rules is a
 * count and three bytes a rule: the shape the area search looks over, how the area is
 * placed on the combatants, and its size. */
#ifndef CT_GAME_BATTLE_AIM_DATA_H
#define CT_GAME_BATTLE_AIM_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* How an action aims: its mode, and its area rules' set (NO_AREA: none). */
typedef struct {
    uint8_t mode, area;
} ActionAim;

enum { NO_AREA = 0 };

/* $CC:1ACB: two bytes a party tech, the mode in the first's low seven bits
 * and the area rules' set in the second (the byte whose low six bits the
 * tech's menu record keeps as its target kind, battle_target_kind). */
static inline ActionAim party_tech_aim(const GameState *g, uint8_t tech)
{
    uint32_t at = 0xCC1ACBu + (uint32_t)(tech * 2);
    return (ActionAim){ (uint8_t)(rom_read8(g, at) & 0x7F), rom_read8(g, at + 1) };
}

/* $CC:86C9: the same for an enemy's tech, the mode a whole byte. */
static inline ActionAim enemy_tech_aim(const GameState *g, uint8_t tech)
{
    uint32_t at = 0xCC86C9u + (uint32_t)(tech * 2);
    return (ActionAim){ rom_read8(g, at), rom_read8(g, at + 1) };
}

/* $CC:1BB5: an item's mode, by its number past the items' first, in the low
 * seven bits. An item has no area rules of its own. */
static inline uint8_t item_aim_mode(const GameState *g, uint8_t item) { return (uint8_t)(rom_read8(g, 0xCC1BB5u + item) & 0x7F); }

/* ---- The area rules ------------------------------------------------------------------------- */

/* $CC:2AB0: where each set of area rules is in bank $CC, a word a set. */
static inline uint16_t area_rules_place(const GameState *g, uint8_t area) { return rom_read16(g, 0xCC2AB0u + (uint16_t)(area * 2)); }

/* How many rules the set at `place` has. */
static inline uint8_t area_rule_count(const GameState *g, uint16_t place) { return rom_read8(g, 0xCC0000u + place); }

/* A rule: the shape (an index into the area search's seven), how the area is
 * placed (battle/aim.c, AreaPlacing), whether it is the target search's
 * rather than the area search's (the placing byte's top bit), its size, and
 * the size byte's top bit, which some placings pass on. */
typedef struct {
    uint8_t shape, placing, size;
    bool searches_for_target, flag;
} AreaRule;

enum { AREA_RULE_BYTES = 3, AREA_RULE_TOP_BIT = 0x80 };

/* The rule `n` of the set at `place` (its offset taken in a word, which
 * wraps in the bank). */
static inline AreaRule area_rule(const GameState *g, uint16_t place, unsigned n)
{
    uint16_t at = (uint16_t)(place + 1 + n * AREA_RULE_BYTES);
    uint8_t placing = rom_read8(g, 0xCC0000u + (uint16_t)(at + 1)), size = rom_read8(g, 0xCC0000u + (uint16_t)(at + 2));
    return (AreaRule){ rom_read8(g, 0xCC0000u + at), (uint8_t)(placing & ~AREA_RULE_TOP_BIT), (uint8_t)(size & ~AREA_RULE_TOP_BIT),
                       (placing & AREA_RULE_TOP_BIT) != 0, (size & AREA_RULE_TOP_BIT) != 0 };
}

/* $CC:FB6F: the square of a distance of 0 to 15 cells of the battlefield's
 * grid, which the area search's circles measure with (a distance past 15
 * reads on into what follows). */
static inline uint8_t battle_grid_distance_squared(const GameState *g, uint16_t cells)
{
    return rom_read8(g, 0xCCFB6Fu + cells);
}

#endif
