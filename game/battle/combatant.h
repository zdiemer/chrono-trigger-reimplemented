/* Battle: the combatants' state -- their HP and MP, their statuses, whether
 * they are in the fight -- as the rest of the battle asks for it.
 *
 * A combatant is its battle slot (battle.h). Its stat block holds its HP and
 * MP, a condition byte that says it is knocked out, and four status bytes
 * whose bits are the statuses it has; two more bytes hold statuses it keeps
 * whatever happens (its equipment's), ORed with the third and fourth. Some
 * statuses last a while and run a timer each (battle/timers.h), and the turn
 * timer is what makes a combatant ready to act.
 *
 * Apart from its stat block the battle keeps, per slot, what the combatant is
 * (its id, which picks an enemy's script), and that id again while it is in
 * the fight, $FF once it is out: the party all out is a defeat. */
#ifndef CT_GAME_BATTLE_COMBATANT_H
#define CT_GAME_BATTLE_COMBATANT_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/battle.h"
#include "state.h"

/* A combatant's statuses as one value: the four status bytes, the first in
 * the low byte, with the kept statuses ORed into the third and fourth. What
 * each bit is as the player knows it isn't all worked out (docs/TODO.md); the
 * battle's code names them for what it does with them. */
typedef uint32_t Statuses;

/* The status bits this code tests, by what they do in the battle. */
enum {
    /* The combatant gets no turns while it has this: its turn timer stops. */
    STATUS_NO_TURNS = 0x02,
    /* Its enemy's script starts over when this comes or goes. */
    STATUS_RESTARTS_SCRIPT = 0x04,
    /* A party member with it can't use techs, nor join a combination tech. */
    STATUS_NO_TECHS = 0x08,
    /* Stop: while it lasts the combatant's other timers stand still, and
     * only this status's own counts (battle/timers.c). */
    STATUS_STOP = 0x80,
};

/* The id battle_combatant_fighting holds for a combatant out of the fight,
 * and battle_combatant_id for a slot nobody fills. */
enum { COMBATANT_NONE = 0xFF };

/* The animation a stopped enemy shows, and the one it goes back to. */
enum { ENEMY_ANIMATION_IDLE = 0, ENEMY_ANIMATION_STOPPED = 4 };

Statuses combatant_statuses(const GameState *g, unsigned slot);
bool combatant_knocked_out(const GameState *g, unsigned slot);
bool combatant_in_fight(const GameState *g, unsigned slot);

/* HP at an eighth of its maximum or less: the party's digits blink and the
 * member takes its weak pose. */
bool combatant_hp_low(const GameState *g, unsigned slot);

/* Note whether the combatant's HP is low, for the window and the poses. */
void combatant_note_low_hp(GameState *g, unsigned slot);

/* Bring the combatant's status timers in step with its statuses, and what its
 * statuses decide besides (battle/combatants.c). */
void combatant_follow_statuses(GameState *g, unsigned slot);

/* The combatant's part of the per-turn update (battle/combatants.c): true if it
 * is done, false if it was knocked out, which is still the original's to
 * handle. */
bool combatant_update(GameState *g, unsigned slot);

/* After a combatant's update: an enemy out of the fight whose other part is
 * out too loses its turn's script. And after every combatant's: the party all
 * out is a defeat. */
void combatant_after_update(GameState *g, unsigned slot);
void combatants_after_update(GameState *g);

#endif
