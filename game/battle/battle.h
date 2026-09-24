/* Battle mode.
 *
 * A battle is fought by eleven combatants, each in a battle slot: slots 0-2
 * are the party, 3-10 the enemies. Each has a stat block (its HP and MP, its
 * status), an object on the battlefield that draws it, and a turn timer that
 * fills until it may act; the party chooses its actions in the battle menu,
 * and the enemies' come from their scripts. */
#ifndef CT_GAME_BATTLE_H
#define CT_GAME_BATTLE_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"
#include "vars.h"

/* The combatants, by battle slot. */
enum { BATTLE_PARTY_SIZE = 3, BATTLE_ENEMIES = 8, BATTLE_COMBATANTS = BATTLE_PARTY_SIZE + BATTLE_ENEMIES,
       BATTLE_FIRST_ENEMY = BATTLE_PARTY_SIZE };

static inline bool battle_slot_is_party(unsigned slot) { return slot < BATTLE_PARTY_SIZE; }

/* The battlefield's objects: the combatants' are the eleven from $0C on, in
 * battle slot order. */
enum { BATTLE_FIRST_COMBATANT_OBJECT = 0x0C };

static inline unsigned battle_object_of(unsigned slot) { return BATTLE_FIRST_COMBATANT_OBJECT + slot; }

/* Where a combatant stands. Positions on the battlefield are a byte each,
 * which wraps. */
static inline uint8_t combatant_x(const GameState *g, unsigned slot) { return battle_object_x(g, battle_object_of(slot)); }
static inline uint8_t combatant_y(const GameState *g, unsigned slot) { return battle_object_y(g, battle_object_of(slot)); }

#endif
