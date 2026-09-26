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

/* The status bits the battle's code tests, by what they do in the battle.
 * The first status byte (the low byte of Statuses): */
enum {
    /* Blind: its aim is a quarter as good, and it is easier to hit. */
    STATUS_BLIND = 0x01,
    /* Asleep: it gets no turns while it has this, its turn timer stopped; a
     * hit wakes it. */
    STATUS_ASLEEP = 0x02,
    /* Confused: its enemy's script starts over when this comes or goes; a hit
     * brings it to its senses, and it is easier to hit. */
    STATUS_CONFUSED = 0x04,
    /* A party member with it can't use techs, nor join a combination tech. */
    STATUS_NO_TECHS = 0x08,
    /* It loses a point of HP each time its own timer runs out, a timer that
     * runs as long as a turn's wait, half as long hasted, twice slowed. */
    STATUS_LOSES_HP = 0x10,
    /* Slow: its turns come half as fast, and it defends less well. */
    STATUS_SLOW = 0x20,
    /* Poisoned: its aim and its defence are lower, its hits weaker. */
    STATUS_POISON = 0x40,
    /* Stop: while it lasts the combatant's other timers stand still, and
     * only this status's own counts (battle/timers.c). */
    STATUS_STOP = 0x80,
};

/* A hit wakes the target and brings it to its senses. */
enum { STATUSES_A_HIT_ENDS = STATUS_ASLEEP | STATUS_CONFUSED };

/* The third status byte (with the kept ones, STAT_KEPT_STATUS_3): */
enum {
    /* Evades a little better: its evasion is doubled. */
    STATUS3_EVADES_BETTER = 0x01,
    /* Evades much better: its evasion is two and a half times. */
    STATUS3_EVADES_MUCH_BETTER = 0x40,
    /* Haste: its turns come twice as fast. */
    STATUS3_HASTE = 0x80,
};

/* The fourth status byte (with the kept ones, STAT_KEPT_STATUS_4): */
enum {
    /* Its hits do a quarter more. */
    STATUS4_HITS_A_QUARTER_HARDER = 0x02,
    /* A barrier: the hits the effect doesn't pass it with do seven tenths. */
    STATUS4_BARRIER = 0x04,
    /* Its hits do half as much again, the half rounded down first. */
    STATUS4_HITS_HALF_AGAIN_ROUNDED = 0x08,
    /* It regains 5 MP every tenth time its own timer runs out. */
    STATUS4_REGAINS_MP = 0x20,
    /* A shield: as the barrier, against the effects that pass the barrier. */
    STATUS4_SHIELD = 0x40,
    /* Its hits do half as much again. */
    STATUS4_HITS_HALF_AGAIN = 0x80,
};

/* The bytes of a combatant's stat block the battle's code reads by their
 * offsets, as the enemies' scripts name a stat. */
typedef enum {
    STAT_ELEMENTS = 0x01,        /* the elements of its attack, one bit each, the first from the top */
    STAT_HP = 0x03,              /* a word */
    STAT_MAX_HP = 0x05,          /* a word */
    STAT_MP = 0x07,              /* a word */
    STAT_MAX_MP = 0x09,          /* a word */
    STAT_LEVEL = 0x12,
    STAT_KIND = 0x19,            /* the kinds of effect it turns away; bit 7, it can't be felled outright */
    STAT_FAMILY = 0x1A,          /* the families some modifiers single out */
    STAT_ATTACK_DETAIL = 0x1B,   /* an enemy's attack's detail */
    STAT_CONDITION = 0x1D,       /* knocked out, and the enemy's flag */
    STAT_STATUS_1 = 0x1E,
    STAT_STATUS_2 = 0x1F,
    STAT_STATUS_3 = 0x20,
    STAT_STATUS_4 = 0x21,
    STAT_SHRUGS_OFF = 0x22,      /* the bits of the condition it shrugs off, then of the first and second status bytes */
    STAT_STATUS_IGNORED = 0x23,  /* the first status byte's bits it shrugs off */
    STAT_KEPT_STATUS_3 = 0x25,   /* the third status byte's bits it keeps whatever happens */
    STAT_KEPT_STATUS_4 = 0x26,   /* the fourth status byte's bits it keeps whatever happens */
    STAT_POWER = 0x36,
    STAT_HIT = 0x37,
    STAT_MAGIC = 0x39,
    STAT_STAMINA = 0x3A,
    STAT_SPEED = 0x3D,
    STAT_ELEMENT_AFFINITY = 0x3F, /* four, an element each: bit 7 absorbs it, bit 6 two less, the rest the scale's divisor */
    STAT_WEAPON_ATTACK = 0x43,   /* a word */
    STAT_CRITICAL_RATE = 0x45,
    STAT_WEAPON_MODIFIER = 0x46, /* the modifier its weapon applies */
    STAT_WEAPON_MODIFIER_ON = 0x47, /* bit 0: its weapon's modifier applies */
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
