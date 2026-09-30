/* Battle: the animation scripts' commands for colors -- the flash a
 * combatant takes when it is hit or casts, its blinking, the palette
 * animations' slots and frames, and the rotations of the flash's colors and
 * of the background's.
 *
 * A flash puts the combatants on the flash palette (sprite palette 4) and
 * loads it with one of the flash's color sets; restoring gives them their
 * own palettes back. Under an enemy's rule that cycles the background's
 * colors (battle_enemy_rule, battle_background_cycle), the flash also paints
 * the cycled background colors with its set, and restoring puts the
 * palette's own colors back there. The rotations themselves run with the
 * frame (battle/frame.c). On channels 0-3 these commands change the channel's
 * combatant; on every other channel, an effect's included, they change the
 * other targets (battle/script_language.h). */
#include "battle/script_language.h"

#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "original_effects.h"
#include "vars.h"

/* The combatants a color command changes. */
static ScriptChannel colored(ScriptChannel ch)
{
    if (ch.channel >= OTHER_TARGETS_CHANNEL)
        ch.kind = SCRIPT_OTHER_TARGETS;
    return ch;
}

/* ---- Flashing ---------------------------------------------------------------------------- */

/* The flash palette, sprite palette 4, as a combatant's palette byte holds
 * it (with the attribute bits); its colors 1-12 are the set's, and 13-15
 * white, every bit set. The background colors the rule cycles run from the
 * one it names to the last background color. */
enum { FLASH_PALETTE = 0x08, FLASH_FIRST_COLOR = 128 + 4 * 16 + 1, FLASH_SET_COLORS = 12, FLASH_WHITES = 3,
       WHITE = 0xFFFF, BACKGROUND_COLORS = 128, COLOR_BYTES = 2 };

static bool rule_binds(const GameState *g, uint8_t combatant)
{
    return battle_enemy_rule(g) != 0 && battle_rule_applies(g, combatant) != 0;
}

/* The background colors the rule cycles, painted with the flash's set over
 * and over, to the last background color. */
static void paint_cycled_background(GameState *g, uint16_t from)
{
    uint8_t left = FLASH_SET_COLORS, color = 0;
    for (unsigned at = battle_background_cycle(g);; at++) {
        set_palette_working(g, at, battle_flash_color(g, from, color++));
        if (at + 1 == BACKGROUND_COLORS)
            break;
        if (--left == 0) {
            left = FLASH_SET_COLORS;
            color = 0;
        }
    }
    ORIGINAL_EFFECT(script_leave_word(g, SCRIPT_CELL_LEFT, left));
}

// $C1:5DDC
/* The combatants go to the flash palette, which takes the set's colors;
 * where the rule binds one of them (on channels 0-3 an enemy), the cycled
 * background colors take them too. The channel goes on at once, past
 * `length` (0 runs the command again). The set, times eight, stays in the
 * first work cells. */
static ScriptStep flash(GameState *g, ScriptChannel ch, uint8_t set, uint8_t length)
{
    ScriptChannel who = colored(ch);
    uint8_t bound = 0, combatant;
    for (unsigned k = 0; script_each_combatant(g, who, k, &combatant); k++) {
        if ((who.kind == SCRIPT_OTHER_TARGETS || combatant >= BATTLE_PARTY_SIZE) && rule_binds(g, combatant))
            bound++;
        set_battle_object_palette(g, combatant, FLASH_PALETTE);
    }
    script_leave_word(g, SCRIPT_CELL_OPERAND, (uint16_t)(set * 8));
    uint16_t from = (uint16_t)(set * FLASH_SET_COLORS * COLOR_BYTES);
    ORIGINAL_EFFECT((script_leave(g, SCRIPT_CELL_PART, bound), script_leave_word(g, SCRIPT_CELL_FROM, from)));
    for (unsigned color = 0; color < FLASH_SET_COLORS; color++)
        set_palette_working(g, FLASH_FIRST_COLOR + color, battle_flash_color(g, from, color));
    for (unsigned color = 0; color < FLASH_WHITES; color++)
        set_palette_working(g, FLASH_FIRST_COLOR + FLASH_SET_COLORS + color, WHITE);
    if (bound)
        paint_cycled_background(g, from);
    return script_next(length);
}

// $C1:5E8B
/* The combatants take their own palettes back; a channel's own enemy, under
 * a rule, also puts the palette's own colors back over the cycled background
 * colors (whether the rule binds it or not, unlike the flash). The channel's
 * frame ends there, past `length`. */
static ScriptStep restore(GameState *g, ScriptChannel ch, uint8_t length)
{
    ScriptChannel who = colored(ch);
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, who, k, &combatant); k++) {
        set_battle_object_palette(g, combatant, battle_object_palette_initial(g, combatant));
        if (who.kind == SCRIPT_ONE_COMBATANT && combatant >= BATTLE_PARTY_SIZE && battle_enemy_rule(g))
            for (unsigned color = battle_background_cycle(g); color < BACKGROUND_COLORS; color++)
                set_palette_working(g, color, palette_base(g, color));
    }
    return script_next_frame(length);
}

// $C1:5DD3
/* The combatants flash with the set the operand names. */
ScriptStep script_flash(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, 2));
    return flash(g, ch, script_operand(g, 1), 2);
}

// $C1:5E87
/* The combatants' own colors come back. */
ScriptStep script_flash_off(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, 1));
    return restore(g, ch, 1);
}

/* The blink's per-combatant cells: the frames to its next change, and
 * whether it shows the flash now. */
typedef enum { BLINK_COUNTDOWN_ROW = 2, BLINK_FLASHING_ROW = 3 } BlinkRow;

static uint8_t blink_cell(const GameState *g, BlinkRow row, uint8_t combatant)
{
    return battle_script_command_cells(g, row * BATTLE_COMBATANTS + combatant);
}
static void set_blink_cell(GameState *g, BlinkRow row, uint8_t combatant, uint8_t value)
{
    set_battle_script_command_cells(g, row * BATTLE_COMBATANTS + combatant, value);
}

/* Counts the combatant's blink down; when it is due, starts the count over
 * and says so, the blink turning over to the flash or back. */
static bool blink_due(GameState *g, uint8_t combatant, uint8_t period, bool *flashing)
{
    uint8_t left = blink_cell(g, BLINK_COUNTDOWN_ROW, combatant);
    if (left != 0) {
        set_blink_cell(g, BLINK_COUNTDOWN_ROW, combatant, (uint8_t)(left - 1));
        return false;
    }
    set_blink_cell(g, BLINK_COUNTDOWN_ROW, combatant, period);
    uint8_t now = blink_cell(g, BLINK_FLASHING_ROW, combatant) ^ 1;
    set_blink_cell(g, BLINK_FLASHING_ROW, combatant, now);
    *flashing = now != 0;
    return true;
}

// $C1:5F13
/* The combatants blink between the flash (the first operand's set) and
 * their own colors, turning over every so many frames (the second), while
 * the script's first variable is set; when it is clear their colors come back
 * and the channel goes on. On the other targets, the first whose blink is due
 * turns the whole list over, and those after it aren't counted this frame;
 * where the list stopped stays in its work cell. Turning to the flash runs the
 * command again in the same frame, which counts the new period's first frame
 * at once. An effect channel only waits for the variable. */
enum { BLINK_LENGTH = 3, AGAIN = 0 };

ScriptStep script_blink(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, BLINK_LENGTH));
    if (battle_script_variables(g, 0) == 0)
        return restore(g, ch, BLINK_LENGTH);
    uint8_t set = script_operand(g, 1);
    set_battle_work(g, SCRIPT_CELL_OPERAND, set);
    uint8_t period = script_operand(g, 2);
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_PERIOD, period));
    if (ch.kind == SCRIPT_EFFECT)
        return script_next_frame(BLINK_LENGTH);
    uint8_t combatant;
    unsigned k;
    for (k = 0; script_each_combatant(g, ch, k, &combatant); k++) {
        if (ch.kind == SCRIPT_OTHER_TARGETS)
            script_leave_word(g, SCRIPT_CELL_LIST_PLACE, (uint8_t)k);
        bool flashing;
        if (!blink_due(g, combatant, period, &flashing))
            continue;
        ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, AGAIN));
        return flashing ? flash(g, ch, set, AGAIN) : restore(g, ch, AGAIN);
    }
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        script_leave_word(g, SCRIPT_CELL_LIST_PLACE, (uint8_t)k);
    return script_stays();
}

/* ---- The rotations ---------------------------------------------------------------------- */

/* The rotation turns every so many frames, the first at once. */
enum { ROTATION_DUE_NOW = 1, ROTATION_ON = 1, ROTATION_CHECK_START = 3 };

// $C1:5EDF
/* The flash's colors rotate every so many frames (the operand). Under an
 * enemy's rule, so do the cycled background colors, if the rule binds one of
 * the flashed targets -- looked for from the fourth channel's subject on, as
 * the original starts the list. */
ScriptStep script_rotate_colors(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    set_battle_color_rotation_period(g, script_operand(g, 1));
    set_battle_color_rotation_count(g, ROTATION_DUE_NOW);
    set_battle_color_rotation_on(g, ROTATION_ON);
    if (battle_enemy_rule(g)) {
        uint8_t combatant;
        for (unsigned at = ROTATION_CHECK_START; script_listed(g, at, &combatant); at++) {
            if (battle_rule_applies(g, combatant) && battle_object_palette(g, combatant) == FLASH_PALETTE) {
                set_battle_background_cycle_on(g, (uint8_t)(battle_background_cycle_on(g) + 1));
                break;
            }
        }
    }
    return script_next_frame(2);
}

// $C1:58DC, $C1:58E4
/* The palette animations pause, or go on. */
ScriptStep script_palettes_paused(GameState *g, ScriptChannel ch, uint8_t on)
{
    (void)ch;
    set_battle_palette_pause_request(g, on);
    return script_next_frame(1);
}

/* A palette animation slot steps through its colour frames from the first
 * to the last, one every period frames; it starts before its first frame and
 * takes it at once. */
enum { BEFORE_THE_FIRST_COLOURS = 0xFF, DUE_NOW = 1, SLOT_ON = 1, SLOT_OFF = 0 };

// $C1:5D93
/* The palette animation in the slot the command names starts, with the
 * period and the frames the operands give. The channel goes on at once. */
ScriptStep script_palette_animation(GameState *g, ScriptChannel ch, uint8_t slot)
{
    (void)ch;
    set_battle_palette_anim_period(g, slot, script_operand(g, 1));
    set_battle_palette_anim_first(g, slot, script_operand(g, 2));
    set_battle_palette_anim_last(g, slot, script_operand(g, 3));
    set_battle_palette_anim_delay(g, slot, DUE_NOW);
    set_battle_palette_anim_on(g, slot, SLOT_ON);
    set_battle_palette_anim_frame(g, slot, BEFORE_THE_FIRST_COLOURS);
    return script_next(4);
}

// $C1:5DC1
/* The palette animation in the slot the command names stops. */
ScriptStep script_palette_animation_off(GameState *g, ScriptChannel ch, uint8_t slot)
{
    (void)ch;
    set_battle_palette_anim_on(g, slot, SLOT_OFF);
    return script_next(1);
}

// $C1:5F9A
/* The colour rotation and the background's colour cycle stop. */
ScriptStep script_colour_cycles_off(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    set_battle_color_rotation_on(g, 0);
    set_battle_background_cycle_on(g, 0);
    return script_next_frame(1);
}

// $C1:5D1D
/* One of the palette animations' frames shows at once: its color set goes
 * into the first sprite palette at its place. The set stays in the first work
 * cells. The channel goes on at once. */
enum { PLACE_QUARTER = 0x07, PLACE_SEVEN_COLORS = 0x80 };

static void leave_seven_set(GameState *g, uint8_t set)
{
    script_leave(g, SCRIPT_CELL_PART + 1, (uint8_t)((set * 4) >> 8));
    script_leave_word(g, SCRIPT_CELL_FROM, (uint16_t)(set * 6));
}

ScriptStep script_palette_frame(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    uint16_t at = (uint16_t)(script_operand(g, 1) * 2 + battle_palette_anim_frames(g));
    BattlePaletteFrame frame = battle_palette_frame(g, at);
    bool seven = (frame.place & PLACE_SEVEN_COLORS) != 0;
    battle_put_color_set(g, seven, (uint16_t)(frame.set * (seven ? 14 : 6)), frame.place & PLACE_QUARTER);
    script_leave_word(g, SCRIPT_CELL_OPERAND, (uint16_t)(frame.set * 2));
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_PART, 0));
    if (seven)
        ORIGINAL_EFFECT(leave_seven_set(g, frame.set));
    return script_next(2);
}
