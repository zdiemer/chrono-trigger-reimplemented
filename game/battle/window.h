/* Battle: the windows, and the numbers shown in them.
 *
 * The battle window at the foot of the screen lists the party, a line a
 * member: its name, its HP (in one layout out of its maximum), its MP and, in
 * the other two layouts, a gauge that fills as its turn comes; beside the
 * party it names the kinds of enemy in the fight. While the party chooses
 * what to do, each member able to act has a command window at the window's
 * left (Attack, Tech or Combo, Item), whose frames are a second tilemap, and
 * the member whose menu is open has its name shown plain where the others'
 * are shaded. The menu window above shows the page of the menu that is open:
 * the tech list, with the party's MP and the selected tech's costs, or the
 * items; its cursors and the target cursors are sprites.
 *
 * Every window is a tilemap the frame work sends to VRAM: rows of 32 cells,
 * each a tile and its attributes. This header is the vocabulary the window's
 * files share: window.c redraws the battle window, status.c keeps its numbers
 * and gauges up to date as the fight goes, menu_window.c draws the menu
 * window and places its cursors, and digits.c turns numbers into digit tiles
 * for all of them. numbers.c, the numbers that rise over the combatants,
 * places combatants the way the cursors do (below). */
#ifndef CT_GAME_BATTLE_WINDOW_H
#define CT_GAME_BATTLE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/battle.h"
#include "original_effects.h"
#include "state.h"
#include "vars.h"

/* ---- The tilemaps ------------------------------------------------------------------------ */

/* A cell of a window tilemap, counted along its rows. */
enum { WINDOW_COLUMNS = 32 };
typedef uint16_t WindowCell;

static inline WindowCell window_cell(unsigned row, unsigned column) { return (WindowCell)(row * WINDOW_COLUMNS + column); }

/* The attributes the window's text is drawn with: plain, or shaded (the names
 * of the members not choosing, a gauge still filling, a member's numbers at
 * low HP, a tech that can't be used); and plain turned upside down, for the
 * lower of the item list's scroll arrows. */
enum { TEXT_PLAIN = 0x29, TEXT_SHADED = 0x2D, TEXT_PLAIN_UPSIDE_DOWN = 0xA9 };

/* Tiles every window uses: nothing (the battle window cleared), a blank, and
 * the four quarters of the hand cursor. */
enum { TILE_NOTHING = 0x00, TILE_BLANK = 0xFF, TILE_CURSOR = 0x60 };

/* The battle window's tilemap. */
static inline void window_put(GameState *g, WindowCell cell, uint8_t tile, uint8_t attributes)
{
    set_battle_window_tilemap_tile(g, cell, tile);
    set_battle_window_tilemap_attributes(g, cell, attributes);
}
static inline void window_set_tile(GameState *g, WindowCell cell, uint8_t tile)
{
    set_battle_window_tilemap_tile(g, cell, tile);
}
static inline void window_set_attributes(GameState *g, WindowCell cell, uint8_t attributes)
{
    set_battle_window_tilemap_attributes(g, cell, attributes);
}

/* The hand cursor, two cells by two, its top left at `cell`, in the battle
 * window (the tiles only) and in the menu window. */
void window_put_cursor(GameState *g, WindowCell cell);
void menu_put_cursor(GameState *g, WindowCell cell);

/* ---- The menu ------------------------------------------------------------------------- */

/* The menu's pages: the commands, the techs, and past them the items. */
typedef enum { PAGE_COMMANDS, PAGE_TECHS } MenuPage;

/* The menu keeps "nobody" in-band, in its member, its slots' members and the
 * targets: a byte with its top bit set. */
enum { MENU_NOBODY = 0x80, BATTLE_TARGET_ENTRIES = 6 };

static inline bool menu_nobody(uint8_t member) { return (member & MENU_NOBODY) != 0; }
static inline bool menu_slot_has_member(const GameState *g, unsigned slot)
{
    return !menu_nobody(battle_menu_slot_members(g, slot));
}

/* ---- The battle window's layouts ---------------------------------------------------------- */

/* The game's settings choose one of three layouts. The first shows each
 * member's HP out of its maximum and no gauge; the other two show a gauge
 * where the maximum was, the second with the numbers before the name and the
 * third with them after it. (A value past the third lays out as the third.) */
typedef enum { LAYOUT_MAX_HP, LAYOUT_NUMBERS_FIRST, LAYOUT_NUMBERS_AFTER } WindowLayout;

WindowLayout battle_window_layout_now(const GameState *g);
static inline bool layout_has_gauges(WindowLayout layout) { return layout != LAYOUT_MAX_HP; }

/* Where a member's line puts its parts: the lower half of its name (the upper
 * half is the row above, the name being two rows tall), the first of its HP
 * digits and of its MP's cells, and its gauge's left end. */
typedef struct {
    WindowCell name, hp, mp, gauge;
} MemberLine;

MemberLine battle_member_line(WindowLayout layout, unsigned member);

/* The first layout's maximum HP, after the HP and a slash. */
enum { MAX_HP_AFTER_HP = 4 };

/* A member's numbers are shaded while it has no HP, and while it is low on
 * the blink frames. */
bool member_numbers_shaded(const GameState *g, unsigned member);

/* Draw a member's HP (and in the first layout the shading of its maximum),
 * and its MP, at the line's places. */
void window_show_hp(GameState *g, WindowLayout layout, unsigned member, bool shaded);
void window_show_mp(GameState *g, WindowLayout layout, unsigned member, bool shaded);

/* Draw the member's gauge: how much of its wait for its turn has passed
 * (status.c, $C1:06F0). The original's callers hand it the member in a work
 * cell. */
void battle_show_gauge(GameState *g, unsigned member);
enum { BATTLE_GAUGE_MEMBER = 0x80 };

/* Redraw the whole battle window (window.c, $C1:0299), and the next member's
 * numbers and the gauges (status.c, $C1:05A7). The numbers are left alone
 * while a command window covers them. */
void battle_redraw_window(GameState *g);
void battle_show_party_status(GameState *g);
bool battle_status_numbers_covered(const GameState *g, WindowLayout layout);

/* Redraw the command cursor of the member whose menu is open, as asked
 * (battle_member_cells_redraw; window.c, $C1:0872). */
void battle_redraw_command_cursor(GameState *g);

/* ---- Numbers as digit tiles (digits.c) ------------------------------------------------ */

/* A number as the window shows it: the tiles of its hundreds, tens and ones,
 * each a digit's tile or a blank. */
typedef struct {
    uint8_t hundreds, tens, ones;
} Digits;

enum { DIGIT_BLANK = TILE_BLANK, TILE_DIGIT_ZERO = 0x73 };

/* Where the digit routines leave a number's digits for the original's
 * callers, by place in battle_digits. */
enum { BATTLE_DIGIT_THOUSANDS = 1, BATTLE_DIGIT_HUNDREDS, BATTLE_DIGIT_TENS, BATTLE_DIGIT_ONES };

/* The number's last `places` digits (three, or two with the hundreds blank). */
Digits battle_digits_of(GameState *g, uint16_t n, unsigned places);
/* The same with its zeros in front blanked; the ones always show. */
Digits battle_digits_blanked(GameState *g, Digits d);
static inline bool digit_is_zero(uint8_t tile) { return tile == TILE_DIGIT_ZERO; }

/* ---- Combatants on the battlefield ------------------------------------------------------ */

/* A combatant's object, and the point its cursor points at: the object's
 * position moved by the combatant's cursor offsets. Coordinates are the
 * battlefield's bytes, and wrap as the original's do. */
enum { BATTLE_COMBATANT_OBJECTS = 12 };

static inline uint8_t combatant_x(const GameState *g, unsigned slot) { return battle_object_x(g, BATTLE_COMBATANT_OBJECTS + slot); }
static inline uint8_t combatant_y(const GameState *g, unsigned slot) { return battle_object_y(g, BATTLE_COMBATANT_OBJECTS + slot); }
static inline uint8_t combatant_cursor_x(const GameState *g, unsigned slot)
{
    return (uint8_t)(combatant_x(g, slot) + battle_cursor_offset_x(g, slot));
}
static inline uint8_t combatant_cursor_y(const GameState *g, unsigned slot)
{
    return (uint8_t)(combatant_y(g, slot) + battle_cursor_offset_y(g, slot));
}

/* ---- What the original leaves in its work cells ---------------------------------------- */

/* The window's routines keep values on the way in the battle's work cells
 * ($80-$9D, battle_work), which only they read. A port keeps its values in C
 * and leaves the cells' last values, for shadow mode, through these (called
 * in ORIGINAL_EFFECT). `cell` is the original's direct-page operand. Some of
 * the original's callers hand a routine its number there too, which its
 * entry reads (battle_work16). */
enum { BATTLE_WORK_CELLS = 0x80 };

static inline uint16_t battle_work16(const GameState *g, uint8_t cell)
{
    return (uint16_t)(battle_work(g, (unsigned)(cell - BATTLE_WORK_CELLS))
                      | battle_work(g, (unsigned)(cell - BATTLE_WORK_CELLS + 1)) << 8);
}
static inline void battle_leave_work8(GameState *g, uint8_t cell, uint8_t v)
{
    set_battle_work(g, (unsigned)(cell - BATTLE_WORK_CELLS), v);
}
static inline void battle_leave_work16(GameState *g, uint8_t cell, uint16_t v)
{
    battle_leave_work8(g, cell, (uint8_t)v);
    battle_leave_work8(g, (uint8_t)(cell + 1), (uint8_t)(v >> 8));
}

/* What drawing a member's numbers leaves: its stat block where the original
 * works from it, and whether they are shaded (window.c). */
void battle_leave_member_drawn(GameState *g, unsigned member, bool shaded);

#endif
