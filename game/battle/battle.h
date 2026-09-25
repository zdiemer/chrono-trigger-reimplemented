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

/* A combatant's stat block (battle_stats) is $80 bytes. */
enum { BATTLE_STAT_BLOCK_BYTES = 0x80 };

/* How the battle was set up (battle_setup_flags; the vblank keeps a copy,
 * battle_setup_flags_copy). */
enum {
    BATTLE_SETUP_PARTY_OFF_GRID = 0x04, /* the party stands off the battlefield's grid */
    BATTLE_SETUP_DEMO = 0x20,           /* a demo battle: the party acts at once, and any button ends it */
    BATTLE_SETUP_NO_RUNNING = 0x80,     /* L and R held don't run away */
};

static inline bool battle_is_demo(const GameState *g) { return (battle_setup_flags(g) & BATTLE_SETUP_DEMO) != 0; }

/* Between actions (battle/damage.c): the action's damage table cleared, and
 * the numbers shown over the combatants it hit. */
void battle_clear_damage_table(GameState *g);
void battle_clear_numbers_shown(GameState *g);

/* The battlefield's objects: the combatants' are the eleven from $0C on, in
 * battle slot order. */
enum { BATTLE_FIRST_COMBATANT_OBJECT = 0x0C };

static inline unsigned battle_object_of(unsigned slot) { return BATTLE_FIRST_COMBATANT_OBJECT + slot; }

/* Where a combatant stands. Positions on the battlefield are a byte each,
 * which wraps. A slot past the combatants (no battle names one) reads on into
 * the objects after, as the original's indexed reads do. */
static inline uint8_t combatant_x(const GameState *g, unsigned slot)
{
    unsigned object = battle_object_of(slot);
    return object < BATTLE_OBJECT_X_COUNT ? battle_object_x(g, object) : battle_object_x_unchecked(g, object);
}
static inline uint8_t combatant_y(const GameState *g, unsigned slot)
{
    unsigned object = battle_object_of(slot);
    return object < BATTLE_OBJECT_Y_COUNT ? battle_object_y(g, object) : battle_object_y_unchecked(g, object);
}

#endif
