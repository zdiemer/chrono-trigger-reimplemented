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
#include "vars.h"

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

/* The bytes of a combatant's stat block the battle's code reads by their
 * offsets, as the enemies' scripts name a stat. */
typedef enum {
    STAT_OTHER = 0x01,
    STAT_HP = 0x03,              /* a word */
    STAT_MAX_HP = 0x05,          /* a word */
    STAT_FLAGS = 0x19,
    STAT_ATTACK_DETAIL = 0x1B,   /* an enemy's attack's detail */
    STAT_CONDITION = 0x1D,       /* knocked out, and the enemy's flag */
    STAT_STATUS_1 = 0x1E,
    STAT_STATUS_2 = 0x1F,
    STAT_STATUS_3 = 0x20,
    STAT_STATUS_4 = 0x21,
    STAT_STATUS_IGNORED = 0x23,  /* the first status byte's bits it shrugs off */
    STAT_KEPT_STATUS_4 = 0x26,   /* the fourth status byte's bits it keeps whatever happens */
} StatOffset;

/* The condition byte's bit for knocked out. */
enum { CONDITION_KNOCKED_OUT = 0x80 };

/* An enemy waiting in reserve to come in (battle_enemy_standing). */
enum { ENEMY_IN_RESERVE = 0x80 };

/* An enemy's last condition held (battle_enemy_last_condition) where none
 * is. */
enum { NO_CONDITION = 0xFF };

/* The id battle_combatant_fighting holds for a combatant out of the fight,
 * and battle_combatant_id for a slot nobody fills. */
enum { COMBATANT_NONE = 0xFF };

/* The animation a stopped enemy shows, and the one it goes back to. */
enum { ENEMY_ANIMATION_IDLE = 0, ENEMY_ANIMATION_STOPPED = 4 };

Statuses combatant_statuses(const GameState *g, unsigned slot);

/* A byte of a combatant's stat block by its offset, as the enemies' scripts
 * name a stat. The original reaches it two ways, which agree for a combatant:
 * across the blocks from the first, where an offset past the block reads on
 * into the next ones; and through the table of the blocks' places, where a
 * slot that names no combatant (a marked candidate, none) is read wherever
 * the table's word past its end takes it. */
uint8_t combatant_stat_byte(const GameState *g, uint8_t slot, uint16_t offset);
/* A stat by a stat block's offset among the blocks, as the effect code keeps
 * one it didn't work out itself (battle_effect_other_block). */
uint8_t combatant_stat_byte_at_block(const GameState *g, uint16_t block, uint16_t offset);
void set_combatant_stat_byte(GameState *g, uint8_t slot, uint16_t offset, uint8_t v);
uint8_t combatant_stat_byte_by_place(const GameState *g, uint8_t slot, uint16_t offset);
uint16_t combatant_stat_word_by_place(const GameState *g, uint8_t slot, uint16_t offset);
void set_combatant_stat_byte_by_place(GameState *g, uint8_t slot, uint16_t offset, uint8_t v);
/* The same where the original doubles the slot in a word rather than a byte
 * (a slot of 128 or more then reads further into the table's neighbours). */
uint8_t combatant_stat_byte_by_wide_place(const GameState *g, uint8_t slot, uint16_t offset);
uint16_t combatant_stat_word_by_wide_place(const GameState *g, uint8_t slot, uint16_t offset);
bool combatant_knocked_out(const GameState *g, unsigned slot);
bool combatant_in_fight(const GameState *g, unsigned slot);

/* What a slot is fighting as (battle_combatant_fighting), what it is, and its
 * turn wait, for a slot the scripts name: one past the combatants reads on
 * into the tables after, as the original's indexed reads do. */
uint8_t combatant_fighting_as(const GameState *g, unsigned slot);
uint8_t combatant_id(const GameState *g, unsigned slot);
uint8_t combatant_turn_wait(const GameState *g, unsigned slot);
void set_combatant_turn_wait(GameState *g, unsigned slot, uint8_t wait);

/* HP at an eighth of its maximum or less: the party's digits blink and the
 * member takes its weak pose. */
bool combatant_hp_low(const GameState *g, unsigned slot);

/* Note whether the combatant's HP is low, for the window and the poses. */
void combatant_note_low_hp(GameState *g, unsigned slot);

/* Whether the combatant's HP was last noted low (the low bit of its stat
 * block's first byte): a party member then takes its weak pose. */
static inline bool combatant_low_hp_noted(const GameState *g, unsigned slot)
{
    return (battle_stats_low_hp(g, slot) & 0x01) != 0;
}

/* Bring the combatant's status timers in step with its statuses, and what its
 * statuses decide besides (battle/combatants.c). */
void combatant_follow_statuses(GameState *g, unsigned slot);

/* The combatant's part of the per-turn update (battle/combatants.c): back in
 * the fight, or knocked out. A party member knocked out that comes back once
 * (combatant_comes_back) is left there, its coming back still the original's
 * code to do. */
typedef enum { UPDATED, COMES_BACK_ONCE } UpdateEnd;
UpdateEnd combatant_update(GameState *g, unsigned slot);
bool combatant_comes_back(const GameState *g, unsigned slot);
/* Whether the update would knock the combatant out now and it comes back. */
bool combatant_update_comes_back(const GameState *g, unsigned slot);

/* After a combatant's update: an enemy out of the fight whose other part is
 * out too loses its turn's script. And after every combatant's: the party all
 * out is a defeat. */
void combatant_after_update(GameState *g, unsigned slot);
void combatants_after_update(GameState *g);

#endif
