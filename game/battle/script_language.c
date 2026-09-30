/* Battle: the animation script language's shared pieces
 * (battle/script_language.h). */
#include "battle/script_language.h"

#include "rom.h"
#include "vars.h"
#include "wram.h"

ScriptChannel script_channel_running(const GameState *g, uint8_t command)
{
    uint8_t channel = (uint8_t)battle_script_channel(g);
    ScriptChannelKind kind = channel >= FIRST_EFFECT_CHANNEL    ? SCRIPT_EFFECT
                             : channel == OTHER_TARGETS_CHANNEL ? SCRIPT_OTHER_TARGETS
                                                                : SCRIPT_ONE_COMBATANT;
    return (ScriptChannel){ channel, kind, command };
}

uint8_t script_combatant(const GameState *g, ScriptChannel ch) { return battle_script_subjects(g, ch.channel); }

uint8_t script_effect(const GameState *g) { return battle_script_enemy(g); }

bool script_listed(const GameState *g, unsigned at, uint8_t *combatant)
{
    uint8_t subject = at < BATTLE_SCRIPT_SUBJECTS_COUNT ? battle_script_subjects(g, at) : battle_script_subjects_unchecked(g, at);
    if (subject & LIST_ENDS)
        return false;
    *combatant = subject;
    return true;
}

bool script_combatant_from(const GameState *g, ScriptChannel ch, unsigned first, unsigned k, uint8_t *combatant)
{
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        return script_listed(g, first + k, combatant);
    if (ch.kind != SCRIPT_ONE_COMBATANT || k != 0)
        return false;
    *combatant = script_combatant(g, ch);
    return true;
}

void script_leave(GameState *g, unsigned cell, uint8_t value) { set_battle_work(g, cell, value); }
void script_leave_word(GameState *g, unsigned cell, uint16_t value)
{
    set_battle_work(g, cell, (uint8_t)value);
    set_battle_work(g, cell + 1, (uint8_t)(value >> 8));
}

/* A script's bytes, where its pointer puts them: the ROM, or work RAM. */
static uint8_t script_byte(const GameState *g, uint32_t at)
{
    uint8_t bank = (uint8_t)(at >> 16);
    return (bank == 0x7E || bank == 0x7F) ? wram_read8(g, at) : rom_read8(g, at);
}

uint8_t script_operand(const GameState *g, unsigned k) { return script_byte(g, (battle_script_pointer(g) + k) & 0xFFFFFF); }

/* The original adds to the pointer's offset and then its bank as a word with
 * the byte after it, so an offset carried out of its bank carries into the
 * bank (and past the last bank, into that byte). */
void script_advance(GameState *g, uint8_t length)
{
    uint32_t at = battle_script_pointer(g);
    uint32_t offset = (at & 0xFFFF) + length;
    if (offset <= 0xFFFF) {
        set_battle_script_pointer(g, (at & 0xFF0000) | offset);
        return;
    }
    uint16_t bank = (uint16_t)(((at >> 16) | battle_script_pointer_pad(g) << 8) + 1);
    set_battle_script_pointer(g, (uint32_t)(bank & 0xFF) << 16 | (offset & 0xFFFF));
    set_battle_script_pointer_pad(g, (uint8_t)(bank >> 8));
}

/* The original counts the channel's going on up, which its runner clears
 * before each command. */
void script_take_step(GameState *g, ScriptStep step)
{
    if (step.go_on)
        set_battle_script_continue(g, (uint8_t)(battle_script_continue(g) + 1));
    script_advance(g, step.length);
}
