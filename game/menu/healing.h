/* The healing a tonic or a tech gives in the menus, and the numbers that
 * rise to show it (game/menu/healing.c; its task and entries are
 * healing_bridge.c). */
#ifndef CT_GAME_MENU_HEALING_H
#define CT_GAME_MENU_HEALING_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/cursor.h"
#include "snes/system.h"
#include "state.h"

/* The healing: the member it is used on (menu_use_member), or every active
 * member, given HP and MP as menu_use_effect, menu_heal_power and
 * menu_heal_factor say, the amounts noted in menu_heal_shown (the HP's, then
 * from MP_SHOWN the MP's, a member's at its place). */
enum { MP_SHOWN = 3 };
void menu_heal(GameState *g);

/* The numbers that rise: the HP's, then the MP's. Which rise, a bit each. */
typedef enum { MENU_HP_AMOUNTS, MENU_MP_AMOUNTS, MENU_AMOUNT_KINDS } MenuAmountKind;
enum { MENU_HP_SHOWN = 1 << MENU_HP_AMOUNTS, MENU_MP_SHOWN = 1 << MENU_MP_AMOUNTS };

/* Before they rise: the digits' tiles built and queued, the amounts shown
 * as digits for the members that have one, where those members' sprites
 * stand, and which kinds rise (menu_amounts_kinds). */
void menu_ready_the_amounts(GameState *g);

/* A frame of a kind's numbers rising, at `step` along their path of
 * heights: their sprites' high bits cleared and each listed member's number
 * (from `shown_from` among those shown, `places` digits) drawn that high.
 * False, with nothing drawn, once the path has ended. */
bool menu_raise_numbers_a_frame(GameState *g, uint8_t shown_from, uint8_t places, uint8_t step);

/* A kind's sound, where its path starts, its digits, and where its
 * amounts start among those shown. */
MenuSound menu_amount_sound(MenuAmountKind kind);
uint8_t menu_amounts_path_start(MenuAmountKind kind);
uint8_t menu_amounts_digits(MenuAmountKind kind);
uint8_t menu_amounts_shown_from(MenuAmountKind kind);

/* The registered entries ($C2:8F55, $C2:8F6C, $C2:8E2D, the task $C2:9016
 * and a kind's rise $C2:9047) and their guards. */
void menu_heal_and_show(Snes *s);
void menu_show_amounts(Snes *s);
void menu_heal_entry(Snes *s);
void menu_raise_amounts(Snes *s);
void menu_raise_amount_kind(Snes *s);
bool menu_healing_runs(Snes *s);
bool menu_amounts_can_be_shown(Snes *s);
bool menu_amounts_can_rise(Snes *s);

#endif
