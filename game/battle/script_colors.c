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
 * frame (battle/frame.c). The flash and the restore change the channel's
 * combatant on channels 0-3, and the other targets on every other channel, an
 * effect's included; the blink counts its time on the channel's own
 * combatant on channels 5-7, as on 0-3 (battle/script_language.h).
 *
 * The original works in the battle's first work cells as it goes; what it
 * leaves there is scaffolding, written by the leaves helpers. */
#include "battle/script_language.h"

#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "original_effects.h"
#include "vars.h"

/* The combatants a flash or a restore changes. */
static ScriptChannel colored(ScriptChannel ch)
{
    if (ch.channel >= OTHER_TARGETS_CHANNEL)
        ch.kind = SCRIPT_OTHER_TARGETS;
    return ch;
}

/* ---- Flashing ---------------------------------------------------------------------------- */

/* The flash palette, sprite palette 4, as a combatant's palette byte holds
 * it (with the attribute bits); its colors 1-12 are the set's, and 13-15
 * white, every bit set. */
enum { FLASH_PALETTE = 0x08, FLASH_FIRST_COLOR = 128 + 4 * 16 + 1, FLASH_WHITES = 3, WHITE = 0xFFFF };

/* What a flash leaves in the work cells: its set's place in the original's
 * tables, the rule-bound combatants it counted, and the colors left of the
 * set where its painting of the background stopped. And the length the
 * commands that flash or restore move on by. */
enum { LEFT_SET_EIGHTS = 0x00, LEFT_BOUND = 0x02, LEFT_SET_PLACE = 0x04, LEFT_COLORS_LEFT = 0x06, LEFT_LENGTH = 0x0E };

static void leave_flash(GameState *g, uint8_t set, uint8_t bound)
{
    script_leave_word(g, LEFT_SET_EIGHTS, (uint16_t)(set * 8));
    script_leave(g, LEFT_BOUND, bound);
    script_leave_word(g, LEFT_SET_PLACE, (uint16_t)(set * BATTLE_FLASH_SET_COLORS * 2));
}

/* The original paints the set in runs, counting the colors left of each run
 * down, and stops at the last background color in the middle of one. */
static void leave_painting(GameState *g, unsigned start)
{
    unsigned painted = BACKGROUND_COLORS - start;
    script_leave_word(g, LEFT_COLORS_LEFT, (uint16_t)(BATTLE_FLASH_SET_COLORS - (painted - 1) % BATTLE_FLASH_SET_COLORS));
}

static bool rule_binds(const GameState *g, uint8_t combatant)
{
    return battle_enemy_rule(g) != 0 && battle_rule_applies(g, combatant) != 0;
}

/* The background colors the rule cycles, painted with the flash's set over
 * and over, to the last background color. */
static void paint_cycled_background(GameState *g, uint8_t set)
{
    unsigned start = battle_background_cycle(g);
    for (unsigned at = start; at < BACKGROUND_COLORS; at++)
        set_palette_working(g, at, battle_flash_color(g, set, (at - start) % BATTLE_FLASH_SET_COLORS));
    ORIGINAL_EFFECT(leave_painting(g, start));
}

// $C1:5DDC
/* The combatants go to the flash palette, which takes the set's colors;
 * where the rule binds one of them (on channels 0-3 an enemy), the cycled
 * background colors take them too. */
static void flash(GameState *g, ScriptChannel ch, uint8_t set)
{
    ScriptChannel who = colored(ch);
    uint8_t bound = 0, combatant;
    for (unsigned k = 0; script_each_combatant(g, who, k, &combatant); k++) {
        if ((who.kind == SCRIPT_OTHER_TARGETS || combatant >= BATTLE_PARTY_SIZE) && rule_binds(g, combatant))
            bound++;
        set_battle_object_palette(g, combatant, FLASH_PALETTE);
    }
    ORIGINAL_EFFECT(leave_flash(g, set, bound));
    for (unsigned color = 0; color < BATTLE_FLASH_SET_COLORS; color++)
        set_palette_working(g, FLASH_FIRST_COLOR + color, battle_flash_color(g, set, color));
    for (unsigned color = 0; color < FLASH_WHITES; color++)
        set_palette_working(g, FLASH_FIRST_COLOR + BATTLE_FLASH_SET_COLORS + color, WHITE);
    if (bound)
        paint_cycled_background(g, set);
}

// $C1:5E8B
/* The combatants take their own palettes back; a channel's own enemy, under
 * a rule, also puts the palette's own colors back over the cycled background
 * colors (whether the rule binds it or not, unlike the flash). */
static void restore(GameState *g, ScriptChannel ch)
{
    ScriptChannel who = colored(ch);
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, who, k, &combatant); k++) {
        set_battle_object_palette(g, combatant, battle_object_palette_initial(g, combatant));
        if (who.kind == SCRIPT_ONE_COMBATANT && combatant >= BATTLE_PARTY_SIZE && battle_enemy_rule(g))
            for (unsigned color = battle_background_cycle(g); color < BACKGROUND_COLORS; color++)
                set_palette_working(g, color, palette_base(g, color));
    }
}

// $C1:5DD3
/* The combatants flash with the set the operand names. The channel goes on
 * at once. */
static ScriptFlow flash_with(GameState *g, ScriptChannel channel, uint8_t length, uint8_t set)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    flash(g, channel, set);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(flash_with, channel, length, set)

// $C1:5E87
/* The combatants' own colors come back. */
static ScriptFlow flash_off(GameState *g, ScriptChannel channel, uint8_t length)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    restore(g, channel);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(flash_off, channel, length)

/* Counts the combatant's blink down; when it is due, starts the count over
 * and says so, the blink turning over to the flash or back. The original
 * turns its byte over by its low bit, so any other bits it holds stay, and
 * show the flash. */
enum { FLASHING = 1 };

static bool blink_due(GameState *g, uint8_t combatant, uint8_t period, bool *flashing)
{
    uint8_t left = battle_blink_countdown(g, combatant);
    if (left != 0) {
        set_battle_blink_countdown(g, combatant, (uint8_t)(left - 1));
        return false;
    }
    set_battle_blink_countdown(g, combatant, period);
    uint8_t now = battle_blink_flashing(g, combatant) ^ FLASHING;
    set_battle_blink_flashing(g, combatant, now);
    *flashing = now != 0;
    return true;
}

/* What the blink leaves: its set and period, the length it moves on by (0
 * once it has turned over), and on the other targets where in the list it
 * stopped. */
enum { LEFT_BLINK_SET = 0x00, LEFT_PLACE_IN_LIST = 0x10, LEFT_PERIOD = 0x12, TURNED_OVER = 0 };

static void leave_blink(GameState *g, uint8_t length, uint8_t set, uint8_t period)
{
    script_leave(g, LEFT_LENGTH, length);
    script_leave(g, LEFT_BLINK_SET, set);
    script_leave(g, LEFT_PERIOD, period);
}
static void leave_place_in_list(GameState *g, ScriptChannel ch, unsigned place)
{
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        script_leave_word(g, LEFT_PLACE_IN_LIST, (uint8_t)place);
}

// $C1:5F13
/* The combatants blink between the flash (the first operand's set) and
 * their own colors, turning over every so many frames (the second), while
 * the script's first variable is set; when it is clear their colors come back
 * and the channel goes on. On the other targets, the first whose blink is due
 * turns the whole list over, and those after it aren't counted this frame.
 * Turning to the flash runs the command again in the same frame, which counts
 * the new period's first frame at once. An effect channel only waits for the
 * variable. */
static ScriptFlow blink(GameState *g, ScriptChannel channel, uint8_t length, uint8_t set, uint8_t period)
{
    if (battle_script_variables(g, 0) == 0) {
        ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
        restore(g, channel);
        return SCRIPT_NEXT_FRAME;
    }
    ORIGINAL_EFFECT(leave_blink(g, length, set, period));
    if (channel.kind == SCRIPT_EFFECT)
        return SCRIPT_NEXT_FRAME;
    uint8_t combatant;
    unsigned k;
    for (k = 0; script_each_combatant(g, channel, k, &combatant); k++) {
        bool flashing;
        if (!blink_due(g, combatant, period, &flashing))
            continue;
        ORIGINAL_EFFECT((script_leave(g, LEFT_LENGTH, TURNED_OVER), leave_place_in_list(g, channel, k)));
        if (!flashing) {
            restore(g, channel);
            return SCRIPT_STAY;
        }
        flash(g, channel, set);
        return SCRIPT_AGAIN;
    }
    ORIGINAL_EFFECT(leave_place_in_list(g, channel, k));
    return SCRIPT_STAY;
}
SCRIPT_OPERATION(blink, channel, length, set, period)

/* ---- The rotations ---------------------------------------------------------------------- */

/* The rotation turns every so many frames, the first at once. */
enum { ROTATION_DUE_NOW = 1, ROTATION_ON = 1 };

// $C1:5EDF
/* The flash's colors rotate every so many frames (the operand). Under an
 * enemy's rule, so do the cycled background colors, if the rule binds one of
 * the flashed targets -- looked for from channel 3's subject on, as the
 * original starts the list. */
static ScriptFlow rotate_colors(GameState *g, uint8_t period)
{
    set_battle_color_rotation_period(g, period);
    set_battle_color_rotation_count(g, ROTATION_DUE_NOW);
    set_battle_color_rotation_on(g, ROTATION_ON);
    if (battle_enemy_rule(g)) {
        uint8_t combatant;
        for (unsigned at = ROTATION_LIST_START; script_listed(g, at, &combatant); at++) {
            if (battle_rule_applies(g, combatant) && battle_object_palette(g, combatant) == FLASH_PALETTE) {
                set_battle_background_cycle_on(g, (uint8_t)(battle_background_cycle_on(g) + 1));
                break;
            }
        }
    }
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(rotate_colors, period)

// $C1:5F9A
/* The color rotation and the background's color cycle stop. */
static ScriptFlow color_cycles_off(GameState *g)
{
    set_battle_color_rotation_on(g, 0);
    set_battle_background_cycle_on(g, 0);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION_TAKING_NOTHING(color_cycles_off)

/* ---- The palette animations ------------------------------------------------------------- */

// $C1:58DC, $C1:58E4
/* The palette animations pause, or go on. */
static ScriptFlow palettes_paused(GameState *g, bool on)
{
    set_battle_palette_pause_request(g, on);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(palettes_paused, on)

/* A palette animation slot steps through its color frames from the first
 * to the last, one every period frames; it starts before its first frame and
 * takes it at once. */
enum { BEFORE_THE_FIRST_COLORS = 0xFF, DUE_NOW = 1, SLOT_ON = 1, SLOT_OFF = 0 };

// $C1:5D93
/* The palette animation in the slot the command names starts, with the
 * period and the frames the operands give. The channel goes on at once. */
static ScriptFlow palette_animation(GameState *g, uint8_t slot, uint8_t period, uint8_t first, uint8_t last)
{
    set_battle_palette_anim_period(g, slot, period);
    set_battle_palette_anim_first(g, slot, first);
    set_battle_palette_anim_last(g, slot, last);
    set_battle_palette_anim_delay(g, slot, DUE_NOW);
    set_battle_palette_anim_on(g, slot, SLOT_ON);
    set_battle_palette_anim_frame(g, slot, BEFORE_THE_FIRST_COLORS);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(palette_animation, slot, period, first, last)

// $C1:5DC1
/* The palette animation in the slot the command names stops. */
static ScriptFlow palette_animation_off(GameState *g, uint8_t slot)
{
    set_battle_palette_anim_on(g, slot, SLOT_OFF);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(palette_animation_off, slot)

// $C1:5D1D
/* One of the palette animations' frames shows at once: its color set goes
 * into the first sprite palette at its place. The channel goes on at once. */
enum { PLACE_QUARTER = 0x07, PLACE_SEVEN_COLORS = 0x80, LEFT_FRAME_SET = 0x00, LEFT_FRAME_COUNT = 0x02,
       LEFT_SEVEN_QUARTERS = 0x03, LEFT_SEVEN_PLACE = 0x04 };

static void leave_palette_frame(GameState *g, uint8_t set, bool seven)
{
    script_leave_word(g, LEFT_FRAME_SET, (uint16_t)(set * 2));
    script_leave(g, LEFT_FRAME_COUNT, 0);
    if (seven) {
        script_leave(g, LEFT_SEVEN_QUARTERS, (uint8_t)((set * 4) >> 8));
        script_leave_word(g, LEFT_SEVEN_PLACE, (uint16_t)(set * 6));
    }
}

static ScriptFlow palette_frame(GameState *g, uint8_t frame)
{
    BattlePaletteFrame shown = battle_palette_frame(g, battle_palette_anim_frames(g), frame);
    bool seven = (shown.place & PLACE_SEVEN_COLORS) != 0;
    battle_put_color_set(g, seven, shown.set, shown.place & PLACE_QUARTER);
    ORIGINAL_EFFECT(leave_palette_frame(g, shown.set, seven));
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(palette_frame, frame)

/* ---- The table ------------------------------------------------------------------------- */

#define SLOT_ROWS(on_byte, off_byte, n)                                                                                    \
    { on_byte, OPERATION(palette_animation), .slot = CONSTANT(n), .period = OPERAND(1), .first = OPERAND(2),               \
      .last = OPERAND(3), .length = 4 },                                                                                   \
    { off_byte, OPERATION(palette_animation_off), .slot = CONSTANT(n), .length = 1 }

SCRIPT_OPCODES(script_color_opcodes,
    { 0x69, OPERATION(flash_with), .set = OPERAND(1), .length = 2, .needs = NEEDS_COLORS },
    { 0x6A, OPERATION(flash_off), .length = 1, .needs = NEEDS_COLORS },
    { 0x6B, OPERATION(rotate_colors), .period = OPERAND(1), .length = 2, .needs = NEEDS_COLORS },
    { 0x6C, OPERATION(blink), .set = OPERAND(1), .period = OPERAND(2), .length = 3, .needs = NEEDS_COLORS },
    { 0x6D, OPERATION(color_cycles_off), .length = 1 },
    { 0x2D, OPERATION(palettes_paused), .on = CONSTANT(true), .length = 1 },
    { 0x2E, OPERATION(palettes_paused), .on = CONSTANT(false), .length = 1 },
    { 0x60, OPERATION(palette_frame), .frame = OPERAND(1), .length = 2 },
    SLOT_ROWS(0x61, 0x65, 0), SLOT_ROWS(0x62, 0x66, 1), SLOT_ROWS(0x63, 0x67, 2), SLOT_ROWS(0x64, 0x68, 3));
