/* What the menus' ported routines leave in the registers for the original's
 * sake, worked out from the state they left. Nothing here is the game's: the
 * logic calls none of it but through ORIGINAL_EFFECT, and a build without the
 * original's effects has none of it.
 *
 * The typesetter's X and Y. The original leaves them as its last steps do,
 * and liveness keeps them live after the label setting ($C2:F90C): the save
 * screen's slot rows, which set their labels with it, end in the menus' task
 * yield ($C2:CF5B jumps to $C2:821E), which keeps X and Y for the task, and
 * the screen handlers the menus' thread dispatches next ($C2:828E) start with
 * Y live. Two of them, $C2:C7E0 (the save screen's slot choice) and $C2:E236
 * (the load screen's), use none of it: along every path each takes, Y is
 * set before it is read, or only saved and given back (the task yield, the
 * sound driver's entry $C7:0004, $C2:8820). The analysis finds Y live there
 * because the cursor's input ($C2:E984), which both call first, leaves Y as
 * it found it on some paths, and its summary joins what is read after every
 * call of it: the menus' loop, as far as the mode's return to its caller
 * ($C2:82E0), which it can't follow. So nothing reads them, but the analysis
 * can't show it, and shadow mode compares them; the port works them out
 * here, and a port of those handlers' callers drops it. */
#include "menu/leaves.h"

#include "menu/menu_data.h"
#include "menu/typeset.h"
#include "port/machine.h"
#include "vars.h"

/* The offset of the tile column a glyph was drawn in, with the pen just past
 * it, in the layout's kind of tiles. */
static uint16_t glyph_column(const GameState *g, uint16_t glyph)
{
    uint8_t width = menu_glyph_width(g, menu_text_glyph_index(glyph));
    unsigned column = (uint8_t)(menu_text_reading_pen(g) - width) / 8u;
    bool four_colour = (menu_text_layout(g) & MENU_TEXT_TILE_KIND) == MENU_TEXT_FOUR_COLOUR;
    return four_colour ? menu_text_column_offset(g, column) : menu_text_column_offset_16_colour(g, column);
}

void menu_text_hand_on(Snes *s, MenuIndexLeft left, uint16_t a)
{
    s->cpu.a = a;
    s->cpu.x = left.x;
    if (left.y_set)
        s->cpu.y = left.y;
}

/* The typesetter's arguments begin the text page. */
enum { ARGUMENTS = 0x0200 };

MenuIndexLeft menu_text_started_leaves(const GameState *g)
{
    return (MenuIndexLeft){ ARGUMENTS, (uint16_t)(2u * menu_text_string(g)), true };
}

/* What the call read last, found by reading the string again from where it
 * began to where it stopped: a piece, or nothing when the whole call went on
 * an insert begun before it. */
static bool last_piece(const GameState *g, uint32_t from, uint32_t to, MenuTextPiece *last)
{
    bool any = false;
    uint16_t left = (uint16_t)(to - from);
    uint32_t at = from;
    while (left != 0) {
        MenuTextPiece p = menu_text_piece_at(g, at);
        *last = p;
        any = true;
        at = menu_text_past(at, p);
        left = p.bytes >= left ? 0 : (uint16_t)(left - p.bytes);
    }
    return any;
}

/* The original reads a control code into Y and dispatches on it doubled in
 * X; draws a glyph leaving Y at the offset of the pen's tile column and X at
 * the glyph's index; and runs out of budget in an insert dispatching on
 * whether some of the insert and some of the budget are left, doubled: X is 2
 * with some of the insert left and 0 without. A text already ended leaves X
 * at 0 and Y as it was. */
MenuIndexLeft menu_text_went_on_leaves(const GameState *g, bool begun, uint32_t from)
{
    if (!begun)
        return (MenuIndexLeft){ 0, 0, false };
    uint16_t glyph = menu_text_reading_glyph(g);
    MenuTextPiece last = { MENU_TEXT_INSERT, 0, 0, 0 };
    bool read = last_piece(g, from, menu_text_reading_at(g), &last);
    if (read && last.kind == MENU_TEXT_STOP)
        return (MenuIndexLeft){ (uint16_t)(last.code * 2), last.code, true };
    uint16_t column = glyph_column(g, glyph);
    if (read && last.kind == MENU_TEXT_GLYPH)
        return (MenuIndexLeft){ menu_text_glyph_index(glyph), column, true };
    return (MenuIndexLeft){ (uint16_t)(menu_text_in_an_insert(g) ? 2 : 0), column, true };
}

MenuIndexLeft menu_label_set_leaves(const GameState *g)
{
    return menu_text_went_on_leaves(g, true, menu_text_string_begins(g));
}
