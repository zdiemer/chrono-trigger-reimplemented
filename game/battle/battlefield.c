/* Battle: the battlefield grid.
 *
 * The battlefield is a grid of 16 by 16 cells, a cell 16 pixels square. The
 * battle builds it at its start from the location's cells (battle/setup_screen.c,
 * battle_base_grid), and rebuilds it whenever objects have moved: the base
 * grid, with each combatant's cell marked as taken. A battle can leave the
 * party off the grid; the original then marks eleven slots from the first
 * enemy's, three of them past the combatants' tables, and so do we. */
#include "charge.h"
#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "vars.h"

enum { GRID_CELLS = 256, CELL_PIXELS_SHIFT = 4, CELL_TAKEN = 0x40 };

/* A slot's presence and position, read as the original indexes them: past
 * the eleventh, into the tables after. */
static bool slot_present(const GameState *g, unsigned slot)
{
    return (battle_names_combatant(slot) ? battle_slot_present(g, slot) : battle_slot_present_unchecked(g, slot)) != 0;
}

static uint8_t object_x(const GameState *g, unsigned object)
{
    return object < BATTLE_OBJECT_X_COUNT ? battle_object_x(g, object) : battle_object_x_unchecked(g, object);
}

static uint8_t object_y(const GameState *g, unsigned object)
{
    return object < BATTLE_OBJECT_Y_COUNT ? battle_object_y(g, object) : battle_object_y_unchecked(g, object);
}

static uint8_t cell_of(uint8_t x, uint8_t y) { return (uint8_t)((y & 0xF0) | x >> CELL_PIXELS_SHIFT); }

// $CC:F110
/* Rebuild the grid with the combatants' cells marked. */
void battle_mark_combatants_on_grid(GameState *g)
{
    for (unsigned cell = 0; cell < GRID_CELLS; cell++)
        set_battle_grid(g, cell, battle_base_grid(g, cell));
    uint8_t first = (battle_setup_flags(g) & BATTLE_SETUP_PARTY_OFF_GRID) ? BATTLE_FIRST_ENEMY : 0;
    for (unsigned slot = first; slot < first + (unsigned)BATTLE_COMBATANTS; slot++) {
        if (!slot_present(g, slot))
            continue;
        uint8_t y = object_y(g, battle_object_of(slot));
        set_battle_grid(g, cell_of(object_x(g, battle_object_of(slot)), y), CELL_TAKEN);
    }
}
