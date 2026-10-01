/* Battle: the animation scripts' commands for the effects -- what an effect's
 * anchor follows, whether the effects are shown, and which combatant an
 * effect is drawn with, in front of it or behind.
 *
 * These act on the channel's effect (battle_script_effect) whatever the
 * channel, all but the anchor's following, which only an effect channel
 * changes (battle/script_language.h). */
#include "battle/script_language.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/effects.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Anchors --------------------------------------------------------------------------- */

/* What an anchor follows: a combatant by its battle slot, another anchor
 * (FOLLOWS_AN_ANCHOR and its number), or nothing. The scripts name a channel's
 * subject by its channel, 0-8, and an anchor by its number after those. */
enum { FOLLOWS_AN_ANCHOR = 0x40, FOLLOWS_NOTHING = 0xFF, FIRST_ANCHOR_OPERAND = 9 };

// $C1:5694
/* The channel's anchor follows what the operand names. Only an effect
 * channel has an anchor; the channel goes on at once either way. */
static ScriptFlow anchor_follows(GameState *g, ScriptChannel channel, uint8_t named)
{
    if (channel.kind == SCRIPT_EFFECT) {
        uint8_t source = named < FIRST_ANCHOR_OPERAND ? battle_script_subjects(g, named)
                                                      : (uint8_t)((named - FIRST_ANCHOR_OPERAND) | FOLLOWS_AN_ANCHOR);
        set_battle_anchor_sources(g, script_effect(g), source);
    }
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(anchor_follows, channel, named)

// $C1:56BB
/* The anchor the operand names stops following anything (unchecked against
 * the eight, as the original takes it). */
static ScriptFlow anchor_stays(GameState *g, uint8_t anchor)
{
    if (anchor < BATTLE_ANCHORS)
        set_battle_anchor_sources(g, anchor, FOLLOWS_NOTHING);
    else
        set_battle_anchor_sources_unchecked(g, anchor, FOLLOWS_NOTHING);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(anchor_stays, anchor)

/* ---- Showing the effects --------------------------------------------------------------- */

/* An effect shows while its count is not 0; showing counts it up, as the
 * original does, and hiding clears it. */
static void show_effect(GameState *g, uint8_t effect)
{
    set_battle_effect_shown(g, effect, (uint8_t)(battle_effect_shown(g, effect) + 1));
}

// $C1:5FA5, $C1:5FB3
/* Every effect is shown, or hidden. */
static ScriptFlow all_effects_shown(GameState *g, bool on)
{
    for (uint8_t effect = 0; effect < BATTLE_EFFECTS; effect++) {
        if (on)
            show_effect(g, effect);
        else
            set_battle_effect_shown(g, effect, 0);
    }
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(all_effects_shown, on)

// $C1:5FC1, $C1:5FD3
/* The channel's effect is shown, or hidden. Showing shares its body with the
 * wait that shows an effect, which the original tells apart by a work cell. */
enum { LEFT_SHOWING_FOR_A_WAIT = 0x0E };

static ScriptFlow effect_shown(GameState *g, bool on)
{
    uint8_t effect = script_effect(g);
    if (on) {
        ORIGINAL_EFFECT(script_leave(g, LEFT_SHOWING_FOR_A_WAIT, false));
        show_effect(g, effect);
    } else {
        set_battle_effect_shown(g, effect, 0);
    }
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(effect_shown, on)

// $C1:6059, $C1:605F
/* The channel's effect is drawn with the combatant the operand names by its
 * channel, in front of its sprites or behind them, and its anchor's orbit
 * turns that record. The original finds the record in a byte, from the
 * combatant as it finds it: an operand naming no one reaches another
 * effect's row. */
enum { RECORD_EFFECT_BITS = 0x3F, LEFT_COMBATANT = 0x00, LEFT_SIDE = 0x0E };

static void leave_drawn_with(GameState *g, uint8_t combatant, EffectSide side)
{
    script_leave(g, LEFT_COMBATANT, combatant);
    script_leave(g, LEFT_SIDE, side);
}

static ScriptFlow effect_drawn_with(GameState *g, uint8_t named, EffectSide side)
{
    uint8_t effect = script_effect(g);
    uint8_t combatant = named < BATTLE_SCRIPT_SUBJECTS_COUNT ? battle_script_subjects(g, named) : battle_script_subjects_unchecked(g, named);
    ORIGINAL_EFFECT(leave_drawn_with(g, combatant, side));
    set_battle_anchor_facing_record(g, effect, combatant);
    uint8_t record = (uint8_t)(battle_multiply(g, effect, BATTLE_COMBATANTS) + combatant);
    uint8_t value = (uint8_t)((effect & RECORD_EFFECT_BITS) | side);
    if (record < BATTLE_OBJECT_EFFECT_SLOTS_COUNT)
        set_battle_object_effect_slots(g, record, value);
    else
        set_battle_object_effect_slots_unchecked(g, record, value);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(effect_drawn_with, named, side)

/* ---- The table ------------------------------------------------------------------------- */

SCRIPT_OPCODES(script_effect_opcodes,
    { 0x1C, OPERATION(anchor_follows), .named = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x1D, OPERATION(anchor_stays), .anchor = OPERAND(1), .length = 2 },
    { 0x6E, OPERATION(all_effects_shown), .on = CONSTANT(true), .length = 1 },
    { 0x6F, OPERATION(all_effects_shown), .on = CONSTANT(false), .length = 1 },
    { 0x70, OPERATION(effect_shown), .on = CONSTANT(true), .length = 1, .needs = NEEDS_EFFECT },
    { 0x71, OPERATION(effect_shown), .on = CONSTANT(false), .length = 1, .needs = NEEDS_EFFECT },
    { 0x73, OPERATION(effect_drawn_with), .named = OPERAND(1), .side = CONSTANT(DRAWN_IN_FRONT), .length = 2, .needs = NEEDS_EFFECT },
    { 0x74, OPERATION(effect_drawn_with), .named = OPERAND(1), .side = CONSTANT(DRAWN_BEHIND), .length = 2, .needs = NEEDS_EFFECT });
