/* Battle: what the animation scripts' runner, places, walks and sounds read
 * from the ROM.
 *
 * Every read of cartridge data battle/scripts.c, script_places.c,
 * script_walk.c and script_sound.c make goes through here, named for what
 * the data is (the asset seam, docs/PLAN.md Phase 3), at the index the
 * original uses. The accessors take the game's state (game/rom.h). */
#ifndef CT_GAME_BATTLE_SCRIPT_DATA_H
#define CT_GAME_BATTLE_SCRIPT_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* ---- The runner ------------------------------------------------------------------------------ */

/* The original's handler for a command byte, in bank C1: its dispatch table
 * ($C1:7A6B, a word a command byte). */
enum { SCRIPT_HANDLER_BANK = 0xC1 };

/* ---- Places ------------------------------------------------------------------------------- */

/* A place part way from one point to another: how many eighths of the way
 * each end weighs ($CC:FB7F, two bytes an entry: the first end's, the
 * second's). */
typedef struct {
    uint8_t first, second;
} PlaceWeights;

/* The cells a heading's step looks at, as offsets in the grid from the cell
 * the walker is in ($CC:F454 on, by the step's number): two cells straight
 * on, the cell diagonally ahead to one side and to the other, and the cell
 * beside the walker that a step round each corner passes. */
typedef enum {
    TWO_AHEAD = 0x00, DIAGONAL_ONE_WAY = 0x01, DIAGONAL_OTHER_WAY = 0x02, CORNER_ONE_WAY = 0x21, CORNER_OTHER_WAY = 0x22
} WalkProbe;

static inline uint32_t battle_script_handler(const GameState *g, uint8_t command)
{
    return (uint32_t)SCRIPT_HANDLER_BANK << 16 | asset_read16(g, ASSET_BATTLE_SCRIPT_HANDLER, 2u * command);
}

static inline PlaceWeights battle_place_weights(const GameState *g, uint8_t entry)
{
    uint32_t at = (uint16_t)(entry * 2);
    return (PlaceWeights){ asset_read8(g, ASSET_BATTLE_PLACE_WEIGHTS, at),
                           asset_read8(g, ASSET_BATTLE_PLACE_WEIGHTS, at + 1) };
}

/* ---- The grid walk ------------------------------------------------------------------------ */

/* Where each combatant's copy of the battlefield grid is kept while it walks
 * ($CC:F67C, a word a combatant: its offset in the grids' store). */
static inline uint16_t battle_walk_grid_store(const GameState *g, uint8_t slot)
{
    uint32_t at = (uint16_t)(slot * 2);
    return (uint16_t)(asset_read8(g, ASSET_BATTLE_WALK_GRID_STORE, at) |
                      asset_read8(g, ASSET_BATTLE_WALK_GRID_STORE, at + 1) << 8);
}

/* A step of the walk, by its number (eight bytes a heading, $CC:F414 on): the
 * heading the walker takes after it, and the facing it shows. */
static inline uint8_t battle_walk_step_heading(const GameState *g, uint8_t step)
{
    return asset_read8(g, ASSET_BATTLE_WALK_STEP_HEADING, step);
}

static inline uint8_t battle_walk_step_facing(const GameState *g, uint8_t step)
{
    return asset_read8(g, ASSET_BATTLE_WALK_STEP_FACING, step);
}

static inline uint8_t battle_walk_probe(const GameState *g, uint8_t step, WalkProbe probe)
{
    return asset_read8(g, ASSET_BATTLE_WALK_PROBE, step + probe);
}

/* ---- Sounds ------------------------------------------------------------------------------- */

/* The sound a weapon makes, by the attacker's weapon: as it swings
 * ($CD:6D4C), and as it hits ($CD:6DAC). */
static inline uint8_t battle_weapon_sound(const GameState *g, uint8_t weapon, bool hit)
{
    return rom_read8(g, (hit ? 0xCD6DACu : 0xCD6D4Cu) + weapon);
}

#endif
