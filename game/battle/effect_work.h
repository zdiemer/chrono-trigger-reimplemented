/* Battle: working out an action's effect on its targets.
 *
 * When an enemy's attack or tech lands, its effect is worked out on each of
 * its targets in turn (battle/effect_work.c): the effect's record says its
 * kind -- restore HP or MP, cure, inflict a status, hit, drain, steal, hit
 * several times -- and the numbers the kind takes. A hit first tests whether
 * it lands (the target's defences against the attacker's aim), then works its
 * damage out by one of ten formulas from the attacker's stats, takes the
 * target's defence off, and applies the action's modifier -- a critical hit,
 * an element, a chance of a status, of death, of stealing an item -- before it
 * is recorded (battle/effect_amounts.c, battle/effect_modifiers.c). Statuses
 * are the target's stat block's bits, with their timers
 * (battle/effect_statuses.c).
 *
 * What it works out is recorded, not yet done: the damage table (battle_damage)
 * for each hit and target, which the action's showing applies, and the numbers
 * shown over the combatants (battle_number_records). Only statuses and a few
 * of the modifiers' outcomes are the target's at once.
 *
 * The party's techs work their effects out with the same kinds, from code that
 * is the original's still; each kind is an entry of its own for it. */
#ifndef CT_GAME_BATTLE_EFFECT_WORK_H
#define CT_GAME_BATTLE_EFFECT_WORK_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/effect_data.h"
#include "state.h"

/* ---- The work ---------------------------------------------------------------------------- */

/* Two things an effect can do are the original's code still: stealing an
 * item gives it to the party ($FD:B655), and one modifier reads the
 * controller ($C2:8002). The work asks for them through the host, the machine
 * side of whoever started it, which makes the call as the original's code
 * would at that point: from inside the steps the work is in (`steps`). A call
 * that doesn't come back as the original's does stops the work there
 * (`stopped`): the original's code carries on from it. */
typedef enum {
    STEP_DISPATCH_TECH,     /* a tech's effect's kind run ($C1:D893) */
    STEP_DISPATCH_ATTACK,   /* an attack's ($C1:D999) */
    STEP_HIT_AGAIN,         /* a hit of an effect that hits several times ($C1:D431) */
    STEP_APPLY_DAMAGE,      /* a hit's damage applied ($C1:DCAD) */
    STEP_MODIFIERS,         /* the modifiers applied to it ($C1:EA6F) */
    STEP_ACTION_MODIFIER,   /* the action's own modifier */
    STEP_WEAPON_MODIFIER,   /* the attacker's weapon's modifier */
    STEP_STEAL_EFFECT,      /* the steal effect ($C1:D3BF) */
    STEP_STEAL_IN_HIT,      /* the chance to steal a hit carries */
    STEP_STEAL_MODIFIER,    /* the modifier a steal applies */
    STEP_MODIFIER_ROUTINE,  /* a modifier's routine */
} EffectStep;

enum { EFFECT_STEPS_MAX = 8 };

/* The calls: the stolen item given the party (the item in the direct page's
 * $06); the controller read; and an effect kind or a modifier's routine past
 * its table (`routine`), which is whatever code the original's table points
 * to past its end (none of the ROM's records has one). The carry a call comes
 * back with goes on into the work (`carry_back`). */
typedef enum {
    CALL_GIVE_STOLEN_ITEM,
    CALL_READ_CONTROLLER,
    CALL_UNLISTED_TECH_KIND,
    CALL_UNLISTED_ATTACK_KIND,
    CALL_UNLISTED_MODIFIER,
} EffectCall;

typedef struct EffectWork EffectWork;

typedef struct {
    bool (*call)(EffectWork *w, EffectCall call);
} EffectHost;

struct EffectWork {
    GameState *g;
    const EffectHost *host;
    void *host_data;
    bool stopped;
    uint8_t depth;
    EffectStep steps[EFFECT_STEPS_MAX];
    bool carry_back; /* the carry into a call, and back from it */
    uint8_t routine;
    /* What the original's code has in its Y register as the work goes, which
     * the code it calls saves on the stack (so the host hands it over). */
    uint16_t machine_y;
    /* ... and in its overflow flag, which the showing after the work takes
     * along. */
    bool machine_v;
    /* What a hit leaves of its status's working out, which a steal it carries
     * takes as its chance (the original's code hands both over in one cell). */
    uint8_t first_left;
};

/* The steps the work is in, for the host. */
static inline void effect_step_in(EffectWork *w, EffectStep step)
{
    if (w->depth < EFFECT_STEPS_MAX)
        w->steps[w->depth] = step;
    w->depth++;
}
static inline void effect_step_out(EffectWork *w) { w->depth--; }

/* ---- Working an action's effect out (battle/effect_work.c) --------------------------------- */

/* $C1:D7C4, $C1:D8D1: the effect of the acting enemy's tech or attack
 * (battle_action_detail its number) worked out on the action's targets. */
void effect_work_out_enemy_action(EffectWork *w, ActionTable table);

/* The effect kinds, by the effect record's first byte: the party's techs'
 * code runs each as an entry of its own ($C1:D9D7's table). Each takes and
 * gives back the carry the original's routine does. */
enum { EFFECT_KINDS = 0x2D };
bool effect_run_kind(EffectWork *w, uint8_t kind, bool carry);

/* $FD:AAD2, $FD:AB01: the acting enemy's tech or attack noted for its
 * targets: what marks it gives those it acts on, and its elements. */
void effect_note_enemy_action(GameState *g, ActionTable table, bool carry);

/* ---- The pieces the kinds share -------------------------------------------------------------- */

/* The acting combatant's and the target's stat blocks, by offset, as the
 * effect code reaches them (battle_effect_actor_block, battle_effect_target_block). */
uint8_t effect_actor_stat(const GameState *g, uint16_t offset);
uint8_t effect_target_stat(const GameState *g, uint16_t offset);
uint16_t effect_actor_stat_word(const GameState *g, uint16_t offset);
uint16_t effect_target_stat_word(const GameState *g, uint16_t offset);
void set_effect_target_stat(GameState *g, uint16_t offset, uint8_t v);

/* The combatant the effect is being worked out on (battle_effect_target). */
uint8_t effect_target(const GameState *g);

#endif
