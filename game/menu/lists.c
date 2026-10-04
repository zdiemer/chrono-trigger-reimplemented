/* The menus' scrolling lists of items as their rows sit in work RAM
 * (game/menu/lists.h). */
#include "menu/lists.h"

#include "charge.h"
#include "menu/cursor.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The step's low byte's sign is the way the list slid. */
enum { STEP_UP = 0x80 };

bool menu_list_scrolls(const GameState *g) { return menu_list_scroll_most(g) != 0; }

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

/* ---- Paging ------------------------------------------------------------------------------------- */

/* The shoulder assignments page a list: the first back a page, the second
 * on a page. */
static const MenuButton PAGE_BACK = MENU_PAGE_ONE_WAY, PAGE_ON = MENU_PAGE_THE_OTHER;

/* For the original's code and shadow mode: the paging takes the list's
 * first row in its cell and leaves the new one there. */
static void leave_paged_top(GameState *g, uint8_t top) { set_menu_paging_top(g, top); }

// $C2:93BF
/* A page past either end goes to the end instead, its cursor to the end's
 * place. The sums are a byte's, as the original's: a page back from near
 * the start wraps past the last first row, which is how it finds the
 * start. */
uint8_t menu_turn_page(GameState *g, uint8_t top)
{
    uint8_t rows = menu_paging_rows(g), last_top = menu_paging_last_top(g), to = top;
    if (menu_asked(g, PAGE_BACK)) {
        to = (uint8_t)(top - rows);
        if (to >= last_top) {
            set_menu_cursor_index(g, menu_list_first_place(g));
            to = 0;
        }
    } else if (menu_asked(g, PAGE_ON)) {
        unsigned on = (unsigned)top + rows;
        to = (uint8_t)on;
        if (on > 0xFF || to >= last_top) {
            set_menu_cursor_index(g, menu_list_last_place(g));
            to = last_top;
        }
    }
    if (to != top) {
        set_menu_scroll_bar_thumb(g, to);
        set_menu_cursor_frame_start(g, MENU_CURSOR_MOVED_ANYWAY);
    }
    ORIGINAL_EFFECT(leave_paged_top(g, to));
    return to;
}

/* The paging's answer in A: whether a page turned. */
enum { TURNED = 0xFF, NOT_TURNED = 0x00 };

// $C2:93BF
void menu_turn_page_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t top = menu_paging_top(g);
    m_set_a8(s, menu_turn_page(g, top) != top ? TURNED : NOT_TURNED);
    m_rts(s);
}
