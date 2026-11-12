/* The healing a tonic or a tech gives in the menus, and the numbers that
 * rise to show it (game/menu/healing.c). */
#ifndef CT_GAME_MENU_HEALING_H
#define CT_GAME_MENU_HEALING_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/cursor.h"
#include "platform/platform.h"
#include "state.h"
#include "vars.h"

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

/* The healing shown ($C2:8F55), the amounts shown ($C2:8F6C), where the
 * state is one they handle; the numbers' task ($C2:9016, each kind's rise
 * $C2:9047), which the menus' tasks run (menu/tasks.c). False where the
 * program went no further. */
bool menu_heal_and_show(GameState *g, Platform *pf);
bool menu_show_amounts(GameState *g, Platform *pf);
void menu_raise_amounts_task(GameState *g, Platform *pf);
bool menu_healing_can_run(const GameState *g);
bool menu_amounts_can_show(const GameState *g);
bool menu_amounts_kind_can_rise(const GameState *g);
enum { MENU_RAISE_AMOUNTS_TASK_START = 0x9016 };

#endif
