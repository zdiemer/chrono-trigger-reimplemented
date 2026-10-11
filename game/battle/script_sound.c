/* Battle: the animation scripts' sound and screen commands -- a sound effect
 * played from the middle or from where a place is, a weapon's swing or hit,
 * and a screen effect (a shake, or a palette preset's flash).
 *
 * The sound commands hand the sound driver a command through the audio
 * service (platform/audio.h), which on the development system runs the
 * driver's CPU half, the original's code at $C7:0004: they are bridges
 * (battle/scripts.c's table names them by command), which check that the
 * driver came back as the battle runs before the channel goes on. The effects play on one of the
 * driver's two pairs of effect voices, which the command names, panned to
 * where the sound comes from.
 *
 * A screen effect is five settings bytes, which the command gives (as many
 * of them as its first byte says, the rest kept from the last it set);
 * battle/screen_motion.c sets the effect. */
#include "battle/script_language.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/screen.h"
#include "battle/script_data.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "vars.h"

/* ---- Sound effects ------------------------------------------------------------------------- */

/* What the driver's command routine reads of the registers it is called
 * with: nothing (it takes the command from its buffer), and nothing reads X,
 * Y, A's low byte or the flags after it returns either (checked against
 * liveness by tools/dontcare_check.py). A's high byte, which the command's
 * move past itself adds with, it hands back as it found it. */
#define DRIVER_IGNORES (DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C)

/* Play an effect on the voices the command names, panned; true where the
 * driver came back to the command's call as the battle runs. */
static bool play(Snes *s, uint32_t call, uint8_t voices, uint8_t sound, uint8_t pan)
{
    au_command(m_platform(s), PF_CALL_DC(call, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = voices, .p1 = sound, .p2 = pan, .count = 2 });
    return battle_back(s, call + BATTLE_JSL_SIZE, BATTLE_PAGE, BATTLE_DATA_BANK);
}

// $C1:619E
/* A sound effect plays, from the middle. */
bool script_play_sound_bridge(Snes *s, const ScriptCommand *command)
{
    return play(s, 0xC161B5, command->voices, command->sound, AU_CENTRE);
}

// $C1:61C1
/* A sound effect plays from where a place is across the screen. */
bool script_play_sound_at_bridge(Snes *s, const ScriptCommand *command)
{
    uint8_t pan = script_find_place(state_of(s), command->place).x;
    return play(s, 0xC161DF, command->voices, command->sound, pan);
}

// $C1:61EB, $C1:61F5
/* The acting member's weapon swings, or hits: its sound plays from where the
 * first channel's subject stands across the screen. */
bool script_weapon_sound_bridge(Snes *s, const ScriptCommand *command)
{
    GameState *g = state_of(s);
    uint8_t sound = battle_weapon_sound(g, battle_acting_weapon(g), command->hit);
    uint8_t pan = combatant_x(g, battle_script_subjects(g, 0));
    return play(s, 0xC1620F, AU_START_EFFECT, sound, pan);
}

/* ---- The screen effect ---------------------------------------------------------------------- */

/* The command is the settings and its own byte, the first setting counting
 * them (battle/screen.h). The battle keeps the settings
 * (battle_screen_effect_settings): a command giving fewer than five takes the
 * rest from the last. The original counts the settings down in a work cell
 * and keeps the count in the next. */
enum { LEFT_COUNTED = 0x00, LEFT_COUNT = 0x01 };

// $C1:621B
/* A screen effect is set from the settings the command gives. */
static ScriptFlow screen_effect(GameState *g, uint8_t first)
{
    uint8_t count = screen_effect_settings_given(first);
    uint8_t settings[SCREEN_EFFECT_SETTINGS];
    for (uint8_t k = 0; k < SCREEN_EFFECT_SETTINGS; k++)
        settings[k] = battle_screen_effect_settings(g, k);
    for (uint8_t k = 0; k < count; k++) {
        uint8_t given = script_operand(g, 1u + k);
        set_battle_screen_effect_settings(g, k, given);
        if (k < SCREEN_EFFECT_SETTINGS)
            settings[k] = given;
    }
    ORIGINAL_EFFECT(script_leave(g, LEFT_COUNTED, 0));
    ORIGINAL_EFFECT(script_leave(g, LEFT_COUNT, count));
    battle_set_screen_effect_to(g, settings);
    script_go_past(g, (uint8_t)(count + 1));
    return SCRIPT_MOVED_NEXT_FRAME;
}
SCRIPT_OPERATION(screen_effect, settings)

/* ---- The table ------------------------------------------------------------------------- */

SCRIPT_OPCODES(script_sound_opcodes,
    { 0x78, .voices = CONSTANT(AU_START_EFFECT), .sound = OPERAND(1), .length = 2 },
    { 0x79, .voices = CONSTANT(AU_START_OTHER_EFFECT), .sound = OPERAND(1), .length = 2 },
    { 0x7A, .voices = CONSTANT(AU_START_EFFECT), .sound = OPERAND(1), .place = OPERAND(2), .length = 3,
      .needs = NEEDS_A_PLACE_APART_FROM_THE_SOUND },
    { 0x7B, .voices = CONSTANT(AU_START_OTHER_EFFECT), .sound = OPERAND(1), .place = OPERAND(2), .length = 3,
      .needs = NEEDS_A_PLACE_APART_FROM_THE_SOUND },
    { 0x7C, .hit = CONSTANT(false), .length = 1 },
    { 0x7D, .hit = CONSTANT(true), .length = 1 },
    { 0x80, OPERATION(screen_effect), .settings = OPERAND(1), .length = 1, .needs = NEEDS_A_SCREEN_EFFECT });
