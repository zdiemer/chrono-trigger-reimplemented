/* The cartridge's saves, through the save service (platform/save.h): three
 * games, each SAVED_GAME_BYTES, and at the save memory's end a record of them
 * -- the slot saved last, how many times the game has been finished and
 * whether it has, and each slot's sum and mark. The save code that checks a
 * slot, saves the game into it and loads it back is menu/saves.c. */
#ifndef CT_GAME_MENU_SAVES_H
#define CT_GAME_MENU_SAVES_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/save.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

enum { MENU_SAVE_SLOTS = 3 };

/* Where things are in save memory. A slot's game is SAVED_GAME_BYTES from its
 * place on; the record of them is at the end. */
enum { SAVED_GAME_BYTES = 0xA00 };
enum { SAVES_LAST_SLOT = 0x1FE0, SAVES_TIMES_FINISHED = 0x1FE1, SAVES_FINISHED = 0x1FE2, SAVES_SLOT_SUMS = 0x1FF0,
       SAVES_SLOT_MARKS = 0x1FF8 };

/* The original's code keeps where a slot's game is as it addresses save
 * memory on the bus, from SAVES_ON_THE_BUS on (menu_slot_game_at). */
enum { SAVES_ON_THE_BUS = 0x6000 };

static inline uint16_t saves_game_in_memory(uint16_t on_the_bus) { return (uint16_t)(on_the_bus - SAVES_ON_THE_BUS); }

static inline uint8_t saves_last_slot(Platform *pf, PfOrigin o) { return sv_read8(pf, o, SAVES_LAST_SLOT); }
static inline uint8_t saves_times_finished(Platform *pf, PfOrigin o) { return sv_read8(pf, o, SAVES_TIMES_FINISHED); }

/* Once the game has been finished, the load screen offers a New Game+. */
static inline bool saves_finished(Platform *pf, PfOrigin o) { return sv_read8(pf, o, SAVES_FINISHED) != 0; }
static inline void saves_forget_finished(Platform *pf, PfOrigin o) { sv_write8(pf, o, SAVES_FINISHED, 0); }
static inline void saves_forget_times_finished(Platform *pf, PfOrigin o) { sv_write8(pf, o, SAVES_TIMES_FINISHED, 0); }
static inline void saves_note_finished(Platform *pf, PfOrigin o) { sv_write8(pf, o, SAVES_FINISHED, 0xFF); }

/* A slot holds a game while its mark (a word) is the one the save writes; the
 * erase writes over the mark's first byte. */
static inline void saves_spoil_mark(Platform *pf, PfOrigin o, uint8_t slot, uint8_t v)
{
    sv_write8(pf, o, (uint16_t)(SAVES_SLOT_MARKS + 2 * slot), v);
}

/* ---- The save code (menu/saves.c) --------------------------------------------------------- */

/* What the check finds in a slot: a game; no game (the slot has never been
 * saved to, or was erased); or a game whose bytes no longer add up to its
 * sum. */
typedef enum { SAVED_GAME_HELD, SAVED_GAME_NONE, SAVED_GAME_SPOILT } SavedGameCheck;

// $FF:F9C4
SavedGameCheck saves_check_slot(GameState *g, Platform *pf, uint8_t slot);

// $FF:F9FB
void saves_save_game(GameState *g, Platform *pf, uint8_t slot);

// $FF:FAA3
void saves_load_game(GameState *g, Platform *pf, uint8_t slot);

/* A New Game+ from the finished game in a slot (menu/saves.c). */
void saves_carry_over(GameState *g, Platform *pf, uint8_t slot);

/* How the game's first moments go, which the story's memory keeps: at
 * power on, with a game on the cartridge the load screen comes first, and
 * with none a new game starts; the load screen marks the new game it starts. */
typedef enum { SAVES_NOTHING_SAVED = 0, SAVES_NEW_GAME = 1, SAVES_GAME_TO_LOAD = 2 } SavesAtPowerOn;
enum { SAVES_HOW_IT_STARTS = 0x61 };
static inline void saves_note_how_it_starts(GameState *g, SavesAtPowerOn how) { set_event_memory_byte(g, SAVES_HOW_IT_STARTS, (uint8_t)how); }

/* The check as the menus make it, from the menus' page or the clock's: the
 * slot's place noted there (menu_slot_game_at on the menus'), for the slot's
 * summary to read. */
SavedGameCheck saves_check_slot_noting(GameState *g, Platform *pf, SaveCheckPage page, uint8_t slot);

// $C2:8D6A
SavesAtPowerOn saves_power_on(GameState *g, Platform *pf);

/* What a game starts from before it is loaded or begun ($C2:95BB), and a new
 * game's state ($C2:956E). */
void saves_clear_the_game(GameState *g);
void saves_start_a_new_game(GameState *g);

/* Where the slot's game is, as the menus keep it for the slot's summary
 * (menu_slot_game_at): the save and the loads note it there; the check's
 * callers do, since the check runs on its caller's page. */
uint16_t saves_slot_on_the_bus(const GameState *g, uint8_t slot);

/* The registered entries (the original's code calls them; the menus' ports
 * call the C above). */
void menu_check_save_slot(Snes *s);
void menu_save_game(Snes *s);
void menu_load_game(Snes *s);
void menu_load_carried_over(Snes *s);
void menu_start_a_new_game(Snes *s);
bool menu_new_game_can_start(Snes *s);
void menu_power_on(Snes *s);
bool menu_power_on_runs(Snes *s);
bool menu_carry_over_runs(Snes *s);
bool menu_save_slot_is_checkable(Snes *s);
bool menu_save_code_runs(Snes *s);

/* What setting up a new game's state leaves in the registers, for a port
 * that sets one up inline where the original called $C2:956E
 * (scaffolding). */
void menu_leave_new_game_set_up(Snes *s);

#endif
