/* The healing a tonic or a tech gives in the menus, and the numbers that
 * rise to show it (game/menu/healing.c; its task and entries are
 * healing_bridge.c). */
#ifndef CT_GAME_MENU_HEALING_H
#define CT_GAME_MENU_HEALING_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/cursor.h"
#include "platform/platform.h"
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

/* The healing shown ($C2:8F55), the amounts shown ($C2:8F6C), their task
 * ($C2:9016) and a kind's rise in it ($C2:9047) are one sequence each, which
 * the machine's entries (healing_bridge.c) and the program as C both run,
 * asking for what differs through HealingSteps.
 *
 * The places they wait or set the machine's registers from. */
typedef enum {
    HEALING_AT_THE_AMOUNTS_SHOWN,  /* the showing waiting for its task */
    HEALING_AT_THE_COLOURS,        /* the numbers' colours copied in */
    HEALING_AT_A_KIND,             /* a kind about to rise */
    HEALING_AT_A_RISING_FRAME,     /* a frame of a kind's rise */
    HEALING_AT_THE_RISING_WAIT,    /* ... and the wait for the next */
    HEALING_AT_THE_RISE_S_END,     /* the task's end */
} HealingPlace;

/* The routines they go on to: the amounts shown, a kind's rise (its kind the
 * value), the kind's sound (its sound the value). */
typedef enum { HEALING_SHOWS_THE_AMOUNTS, HEALING_RAISES_A_KIND, HEALING_SENDS_THE_KIND_S_SOUND } HealingRoutine;

/* Where the machine leaves what the original leaves its code. */
typedef enum { HEALING_NOTE_WAITED, HEALING_NOTE_KINDS_LEFT, HEALING_NOTE_A_FRAME_RISEN, HEALING_NOTE_RISEN } HealingNote;

/* What the sequences ask: a frame's wait, the machine's registers as the
 * original's code sets them, a routine run, a note, the task's end. Each
 * false where it went no further. */
typedef struct {
    bool (*wait)(void *ctx, HealingPlace at);
    void (*registers_at)(void *ctx, HealingPlace at);
    bool (*run)(void *ctx, HealingRoutine routine, uint8_t value);
    void (*note)(void *ctx, HealingNote note, uint8_t value);
    bool (*ends)(void *ctx);
} HealingSteps;

bool menu_heal_and_show_with(GameState *g, const HealingSteps *n, void *ctx);
bool menu_show_amounts_with(GameState *g, const HealingSteps *n, void *ctx);
bool menu_raise_amounts_with(GameState *g, const HealingSteps *n, void *ctx);
bool menu_raise_amount_kind_with(GameState *g, const HealingSteps *n, void *ctx);

/* As C: the healing shown, the amounts shown, where their entries' guards'
 * state halves take the state; the numbers' task, which the menus' tasks
 * run (menu/tasks.c). False where the program went no further. */
bool menu_heal_and_show(GameState *g, Platform *pf);
bool menu_show_amounts(GameState *g, Platform *pf);
void menu_raise_amounts_task(GameState *g, Platform *pf);
bool menu_healing_can_run(const GameState *g);
bool menu_amounts_can_show(const GameState *g);
bool menu_amounts_can_rise_now(const GameState *g);
enum { MENU_RAISE_AMOUNTS_TASK_START = 0x9016 };

/* The registered entries ($C2:8F55, $C2:8F6C, $C2:8E2D, the task $C2:9016
 * and a kind's rise $C2:9047) and their guards. */
void menu_heal_and_show_entry(Snes *s);
void menu_show_amounts_entry(Snes *s);
void menu_heal_entry(Snes *s);
void menu_raise_amounts(Snes *s);
void menu_raise_amount_kind(Snes *s);
bool menu_healing_runs(Snes *s);
bool menu_amounts_can_be_shown(Snes *s);
bool menu_amounts_can_rise(Snes *s);

#endif
