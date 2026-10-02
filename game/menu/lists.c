/* The menus' scrolling lists of items as their rows sit in work RAM
 * (game/menu/lists.h). */
#include "menu/lists.h"

#include "charge.h"
#include "original_effects.h"
#include "vars.h"
#include "wram.h"

/* The step's low byte's sign is the way the list slid. */
enum { STEP_UP = 0x80 };

bool menu_list_slid_up(const GameState *g) { return ((uint8_t)menu_scroll_step(g) & STEP_UP) != 0; }

enum { WORK_RAM = 0x7E0000 };

void menu_move_list_rows(GameState *g, uint16_t rows_at, unsigned shown_bytes, unsigned row_bytes, bool up)
{
    uint32_t first = WORK_RAM | rows_at, past = WORK_RAM | (uint16_t)(rows_at + shown_bytes);
    if (!up) {
        wram_copy(g, first + row_bytes, first, shown_bytes);
        ORIGINAL_EFFECT(charge_block_move(g, shown_bytes));
        return;
    }
    for (unsigned row = shown_bytes / row_bytes - 1; row-- > 0;)
        wram_copy(g, first + row * row_bytes, first + (row + 1) * row_bytes, row_bytes);
    wram_copy(g, past, first, row_bytes);
    ORIGINAL_EFFECT(charge_block_move(g, shown_bytes));
}
