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

#include "original_effects.h"
#include "state.h"
#include "vars.h"

/* The combatants, by battle slot. */
enum { BATTLE_PARTY_SIZE = 3, BATTLE_ENEMIES = 8, BATTLE_COMBATANTS = BATTLE_PARTY_SIZE + BATTLE_ENEMIES,
       BATTLE_FIRST_ENEMY = BATTLE_PARTY_SIZE };

static inline bool battle_slot_is_party(unsigned slot) { return slot < BATTLE_PARTY_SIZE; }

/* How the battle ended (battle_result), which the field reads when it puts
 * the party back (field/battle_return.c). */
typedef enum { BATTLE_WON_OR_RAN = 0, BATTLE_DEFEATED = 1 } BattleResult;
static inline BattleResult battle_how_it_ended(const GameState *g) { return (BattleResult)battle_result(g); }
static inline void battle_ends(GameState *g, BattleResult result) { set_battle_result(g, (uint8_t)result); }

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

/* The battle's steps ($C1:0003), each a stretch of the battle's own frame
 * work the main loop and the turns ask for by number (A). */
typedef enum {
    BATTLE_STEP_SET_UP = 0,           /* set the battle up */
    BATTLE_STEP_OPEN_MENU = 1,        /* a party member's turn: its command menu opens */
    BATTLE_STEP_CALLED_OFF = 2,       /* a member's queued action called off */
    BATTLE_STEP_NEXT_FRAME = 3,       /* the frame's work, and a wait for the next */
    BATTLE_STEP_SHOW_ACTION = 4,      /* an action's showing starts */
    BATTLE_STEP_ENEMIES_BEATEN = 6,   /* the enemies beaten, seen to */
    BATTLE_STEP_VICTORY = 8,          /* the party's victory */
    BATTLE_STEP_DEFEAT = 9,           /* the party's defeat */
} BattleStep;

/* A party member's action record (battle_member_actions). Its choice: the
 * first set of these bits decides what the member does; bit 4, whose meaning
 * isn't worked out (docs/TODO.md), is dropped when the member is knocked out,
 * and a choice of it alone leaves the member's turn as it is. Its partners:
 * a partner's party slot in each nibble, $F for none, and $FF for no
 * partners at all. */
enum { CHOSE_AN_ATTACK = 0x80, CHOSE_AN_ITEM = 0x40, CHOSE_A_TECH = 0x20, CHOICE_HELD = 0x10 };
enum { NO_PARTNER = 0x0F, NO_PARTNERS = 0xFF };

static inline uint8_t first_partner(uint8_t partners) { return partners & 0x0F; }
static inline uint8_t second_partner(uint8_t partners) { return (uint8_t)(partners >> 4); }

/* The action stops at its first target (battle_action_state): an attack. */
enum { ACTION_STOPS_AT_FIRST = 0x02 };

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

/* ---- The direct page's working cells (battle_low_scratch) --------------------------------- */

/* The battle's code works in the direct page's first cells. What a port
 * leaves there for the original's code or for shadow mode alone is
 * scaffolding, written with these through ORIGINAL_EFFECT. */
static inline void battle_leave_byte(GameState *g, unsigned at, uint8_t v) { set_battle_low_scratch(g, at, v); }
static inline void battle_leave_word(GameState *g, unsigned at, uint16_t v)
{
    set_battle_low_scratch(g, at, (uint8_t)v);
    set_battle_low_scratch(g, at + 1, (uint8_t)(v >> 8));
}

/* A word in the cells, as an entry takes an operand its caller hands it
 * there. */
static inline uint16_t battle_cell_word(const GameState *g, unsigned at)
{
    return (uint16_t)(battle_low_scratch(g, at) | battle_low_scratch(g, at + 1) << 8);
}

/* Some of them carry a value on to a later reader that takes it as game
 * input, so the enemies' scripts, their turns and the selectors keep what
 * they write there plainly (battle_keep_*):
 *
 *   - $0B is the high byte of the stat offset the scripts' stat test reads
 *     (battle/enemy_conditions.c): whatever the last word the choosing or a
 *     selector kept at $0A-$0B left there;
 *   - $0E is what an enemy tech's hits are noted as (battle/enemy_actions.c)
 *     where the tech's targeting ($C1:C1DD, the original's) doesn't set it,
 *     which is whatever was last left there: the tech's number, the end of a
 *     further target's selection, or before the tech anything an action or a
 *     condition kept there;
 *   - $00 is the word the clock's modifier adds half of to the damage where
 *     it means the damage (battle/effect_modifiers.c, battle_clock_word):
 *     whatever its writers left there last -- the amount healed or drained
 *     (battle/effect_amounts.c), the targets' marking (battle/targets.c), the
 *     party tech's members' record and the used item's use byte's place
 *     (battle/party_work.c), the steal's giving (battle/effect_steal.c), and
 *     the member's turn, which clears it and then puts the used item's
 *     number there (battle/member_turn.c);
 *   - $06 holds the place of the member's action record as its turn or its
 *     update reaches it (battle/member_turn.c, battle/combatants.c: for the
 *     original's code), and after a steal the item it took
 *     (battle/effect_steal.c), which the member's turn names in its message
 *     (battle_item_taken);
 *   - $08 (a number a condition tests), $0A and $0E are read as stats: a
 *     candidate past the combatants (the stat test's doubled walk goes past
 *     the list, a candidate that is none) is read wherever the table of the
 *     stat blocks' places points, which can be these cells, so the
 *     conditions that read candidates' stats keep what they write there
 *     before and while they read. */
enum {
    BATTLE_CLOCK_READS = 0x00,
    BATTLE_MEMBER_CELL = 0x06,
    BATTLE_NUMBER_TESTED = 0x08,
    BATTLE_STAT_OFFSET_LOW = 0x0A,
    BATTLE_STAT_OFFSET_HIGH = 0x0B,
    BATTLE_TECH_HITS_LEFT = 0x0E,
};

static inline void battle_keep_byte(GameState *g, unsigned at, uint8_t v) { set_battle_low_scratch(g, at, v); }

static inline uint8_t battle_item_taken(const GameState *g) { return battle_low_scratch(g, BATTLE_MEMBER_CELL); }

static inline uint16_t battle_clock_word(const GameState *g)
{
    return (uint16_t)(battle_low_scratch(g, BATTLE_CLOCK_READS) | battle_low_scratch(g, BATTLE_CLOCK_READS + 1) << 8);
}
static inline void battle_keep_word(GameState *g, unsigned at, uint16_t v) { battle_leave_word(g, at, v); }

/* A word kept at $0A-$0B, whose high byte is the stat offset's. */
static inline void battle_keep_stat_offset_word(GameState *g, uint16_t v) { battle_keep_word(g, BATTLE_STAT_OFFSET_LOW, v); }
static inline uint8_t battle_stat_offset_high(const GameState *g) { return battle_low_scratch(g, BATTLE_STAT_OFFSET_HIGH); }

static inline void battle_keep_tech_hits_left(GameState *g, uint8_t v) { set_battle_low_scratch(g, BATTLE_TECH_HITS_LEFT, v); }
static inline uint8_t battle_tech_hits_left(const GameState *g) { return battle_low_scratch(g, BATTLE_TECH_HITS_LEFT); }

#endif
