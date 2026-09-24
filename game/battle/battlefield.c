/* Battle: the battlefield grid.
 *
 * The battlefield is a grid of 16 by 16 cells, a cell 16 pixels square. The
 * battle builds it at its start from the location's cells (battle/setup.c,
 * battle_base_grid), and rebuilds it whenever objects have moved: the base
 * grid, with each combatant's cell marked as taken. A battle can leave the
 * party off the grid; the original then marks eleven slots from the first
 * enemy's, three of them past the combatants' tables, and so do we. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "original_effects.h"
#include "vars.h"

enum { GRID_CELLS = 256, CELL_PIXELS_SHIFT = 4, CELL_TAKEN = 0x40, PARTY_OFF_GRID = 0x04 };

/* The combatants' objects follow the battle's first twelve objects. */
enum { FIRST_COMBATANT_OBJECT = 12 };

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

/* The cells the original works in: the first slot, the count left (0), and
 * the last row marked (where any was). */
enum { NO_ROW = 0x100 };

static void leave_marking_cells(GameState *g, uint8_t first, unsigned last_row)
{
    set_battle_work(g, 0, first);
    set_battle_work(g, 2, 0);
    if (last_row != NO_ROW)
        set_battle_work(g, 4, (uint8_t)last_row);
}

// $CC:F110
/* Rebuild the grid with the combatants' cells marked. */
static void mark_combatants(GameState *g)
{
    for (unsigned cell = 0; cell < GRID_CELLS; cell++)
        set_battle_grid(g, cell, battle_base_grid(g, cell));
    uint8_t first = (battle_setup_flags(g) & PARTY_OFF_GRID) ? BATTLE_FIRST_ENEMY : 0;
    unsigned last_row = NO_ROW;
    for (unsigned slot = first; slot < first + (unsigned)BATTLE_COMBATANTS; slot++) {
        if (!slot_present(g, slot))
            continue;
        uint8_t y = object_y(g, FIRST_COMBATANT_OBJECT + slot);
        last_row = y & 0xF0;
        set_battle_grid(g, cell_of(object_x(g, FIRST_COMBATANT_OBJECT + slot), y), CELL_TAKEN);
    }
    ORIGINAL_EFFECT(leave_marking_cells(g, first, last_row));
}

/* A's high byte is left 0 (the original's block move leaves it to a TDC),
 * and A 8-bit. */
void battle_mark_object_grid(Snes *s)
{
    mark_combatants(state_of(s));
    s->cpu.a = 0;
    m_sep(s, CPU_FLAG_M);
    m_rtl(s);
}
