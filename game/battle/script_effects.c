/* Battle: the animation scripts' commands for the effects -- what an effect's
 * anchor follows, whether the effects are shown, and which combatant an
 * effect is drawn with, in front of it or behind.
 *
 * These act on the channel's effect (battle_script_enemy) whatever the
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
ScriptStep script_anchor_follows(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    if (ch.kind == SCRIPT_EFFECT) {
        uint8_t named = script_operand(g, 1);
        uint8_t source = named < FIRST_ANCHOR_OPERAND ? battle_script_subjects(g, named)
                                                      : (uint8_t)((named - FIRST_ANCHOR_OPERAND) | FOLLOWS_AN_ANCHOR);
        set_battle_anchor_sources(g, script_effect(g), source);
    }
    return script_next(2);
}

// $C1:56BB
/* The anchor the operand names stops following anything (unchecked against
 * the eight, as the original takes it). */
ScriptStep script_anchor_stays(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    uint8_t anchor = script_operand(g, 1);
    if (anchor < BATTLE_ANCHORS)
        set_battle_anchor_sources(g, anchor, FOLLOWS_NOTHING);
    else
        set_battle_anchor_sources_unchecked(g, anchor, FOLLOWS_NOTHING);
    return script_next(2);
}

/* ---- Showing the effects --------------------------------------------------------------- */

/* An effect shows while its count is not 0; showing counts it up, as the
 * original does, and hiding clears it. */
static void show_effect(GameState *g, uint8_t effect)
{
    set_battle_effect_shown(g, effect, (uint8_t)(battle_effect_shown(g, effect) + 1));
}

// $C1:5FA5, $C1:5FB3
/* Every effect is shown, or hidden. */
ScriptStep script_all_effects_shown(GameState *g, ScriptChannel ch, uint8_t on)
{
    (void)ch;
    for (uint8_t effect = 0; effect < BATTLE_EFFECTS; effect++) {
        if (on)
            show_effect(g, effect);
        else
            set_battle_effect_shown(g, effect, 0);
    }
    return script_next_frame(1);
}

// $C1:5FC1, $C1:5FD3
/* The channel's effect is shown, or hidden. */
ScriptStep script_effect_shown(GameState *g, ScriptChannel ch, uint8_t on)
{
    (void)ch;
    uint8_t effect = script_effect(g);
    if (on) {
        ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, 0));
        show_effect(g, effect);
    } else {
        set_battle_effect_shown(g, effect, 0);
    }
    return script_next_frame(1);
}

// $C1:6059, $C1:605F
/* The channel's effect is drawn with the combatant the operand names by its
 * channel, in front of its sprites or behind them, and its anchor's orbit
 * turns that record. The original finds the record in a byte, from the
 * combatant as it finds it: an operand naming no one reaches another
 * effect's row. */
enum { RECORD_EFFECT_BITS = 0x3F };

ScriptStep script_effect_drawn_with(GameState *g, ScriptChannel ch, uint8_t side)
{
    (void)ch;
    uint8_t effect = script_effect(g), named = script_operand(g, 1);
    uint8_t combatant = named < BATTLE_SCRIPT_SUBJECTS_COUNT ? battle_script_subjects(g, named) : battle_script_subjects_unchecked(g, named);
    set_battle_work(g, SCRIPT_CELL_OPERAND, combatant);
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, side));
    set_battle_anchor_facing_record(g, effect, combatant);
    uint8_t record = (uint8_t)(battle_multiply(g, effect, BATTLE_COMBATANTS) + combatant);
    uint8_t value = (uint8_t)((effect & RECORD_EFFECT_BITS) | side);
    if (record < BATTLE_OBJECT_EFFECT_SLOTS_COUNT)
        set_battle_object_effect_slots(g, record, value);
    else
        set_battle_object_effect_slots_unchecked(g, record, value);
    return script_next_frame(2);
}
