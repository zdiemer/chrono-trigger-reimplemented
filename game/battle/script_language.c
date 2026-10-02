/* Battle: the animation script language's shared pieces, and the running of
 * a command by its row (battle/script_language.h). */
#include "battle/script_language.h"

#include "battle/battle.h"
#include "battle/effects.h"
#include "rom.h"
#include "vars.h"
#include "wram.h"

/* ---- What a channel animates ----------------------------------------------------------- */

ScriptChannel script_channel_running(const GameState *g)
{
    uint8_t channel = (uint8_t)battle_script_channel(g);
    ScriptChannelKind kind = channel >= FIRST_EFFECT_CHANNEL    ? SCRIPT_EFFECT
                             : channel == OTHER_TARGETS_CHANNEL ? SCRIPT_OTHER_TARGETS
                                                                : SCRIPT_ONE_COMBATANT;
    return (ScriptChannel){ channel, kind };
}

uint8_t script_combatant(const GameState *g, ScriptChannel ch) { return battle_script_subjects(g, ch.channel); }

uint8_t script_effect(const GameState *g) { return battle_script_effect(g); }

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
uint8_t script_left(const GameState *g, unsigned cell) { return battle_work(g, cell); }

/* ---- A script's place ------------------------------------------------------------------ */

ScriptPlace script_place(const GameState *g)
{
    return (ScriptPlace){ battle_script_pointer(g), battle_script_pointer_pad(g) };
}

void set_script_place(GameState *g, ScriptPlace place)
{
    set_battle_script_pointer(g, place.at);
    set_battle_script_pointer_pad(g, place.beyond);
}

ScriptPlace script_place_plus(ScriptPlace place, uint8_t length)
{
    uint32_t offset = (place.at & 0xFFFF) + length;
    if (offset <= 0xFFFF)
        return (ScriptPlace){ (place.at & 0xFF0000) | offset, place.beyond };
    uint16_t bank = (uint16_t)(((place.at >> 16) | place.beyond << 8) + 1);
    return (ScriptPlace){ (uint32_t)(bank & 0xFF) << 16 | (offset & 0xFFFF), (uint8_t)(bank >> 8) };
}

/* A script's bytes, where its pointer puts them: the ROM, or work RAM. */
static uint8_t script_byte(const GameState *g, uint32_t at)
{
    uint8_t bank = (uint8_t)(at >> 16);
    return (bank == 0x7E || bank == 0x7F) ? wram_read8(g, at) : rom_read8(g, at);
}

uint8_t script_operand(const GameState *g, unsigned k) { return script_byte(g, (battle_script_pointer(g) + k) & 0xFFFFFF); }

/* Low RAM, the direct page's among it, is bank $7E's start and mirrored in
 * the first quarter of each half of the banks. */
enum { WORK_RAM_BANK = 0x7E, HALF_OF_THE_BANKS = 0x7F, LOW_RAM_MIRRORS_END = 0x40, DIRECT_PAGE_END = 0x100 };

bool script_over_the_direct_page(const GameState *g)
{
    uint32_t at = battle_script_pointer(g);
    uint8_t bank = (uint8_t)(at >> 16);
    return (at & 0xFFFF) < DIRECT_PAGE_END && (bank == WORK_RAM_BANK || (bank & HALF_OF_THE_BANKS) < LOW_RAM_MIRRORS_END);
}

/* ---- What a command in C needs -------------------------------------------------------- */

/* A list of combatants from `first`, ended in reach (and with one, if asked). */
enum { LIST_MOST = 16 };

static bool list_of_combatants(const GameState *g, unsigned first, bool needs_one)
{
    for (unsigned at = first; at < first + LIST_MOST; at++) {
        uint8_t combatant;
        if (!script_listed(g, at, &combatant))
            return !needs_one || at > first;
        if (combatant >= BATTLE_COMBATANTS)
            return false;
    }
    return false;
}

/* The original reads the channel's effect as a word. */
static bool effect_in_range(const GameState *g)
{
    return battle_script_effect(g) < BATTLE_EFFECTS && battle_script_effect_high(g) == 0;
}

/* A combatant's battle slot for a combatant channel; on the other targets'
 * channels a list with a first target, from the list's own start and from the
 * shadows', each a combatant; on an effect channel, one of the effects. */
static bool subjects_in_range(const GameState *g)
{
    ScriptChannel ch = script_channel_running(g);
    if (ch.kind == SCRIPT_EFFECT)
        return effect_in_range(g);
    if (ch.kind == SCRIPT_ONE_COMBATANT && script_combatant(g, ch) >= BATTLE_COMBATANTS)
        return false;
    if (ch.channel < OTHER_TARGETS_CHANNEL)
        return true;
    return list_of_combatants(g, OTHER_TARGETS_START, true) && list_of_combatants(g, SHADOW_LIST_START, false);
}

/* The colors change the other targets on every channel from the fourth on (an
 * effect channel's included), look for a flashed enemy from the rotation's
 * start, and copy the cycled background colors from the one the rule names to
 * the last background color. */
static bool colors_in_range(const GameState *g)
{
    return subjects_in_range(g) && list_of_combatants(g, ROTATION_LIST_START, false)
           && list_of_combatants(g, OTHER_TARGETS_START, false) && battle_background_cycle(g) < BACKGROUND_COLORS;
}

/* The screen effect's settings: how many of them the command gives (its
 * first operand's low bits; the original copies 256 for none), and which
 * effect (its high bits), one of the six the original's table has. */
enum { SETTINGS_GIVEN = 0x0F, SCREEN_EFFECTS = 6 };

static bool a_screen_effect(const GameState *g)
{
    uint8_t settings = script_operand(g, 1);
    return (settings & SETTINGS_GIVEN) != 0 && settings >> 4 < SCREEN_EFFECTS;
}

static bool needs_met(const GameState *g, const ScriptOpcode *row)
{
    switch (row->needs) {
    case NEEDS_SUBJECTS: return subjects_in_range(g);
    case NEEDS_A_COMBATANT_CHANNEL: return subjects_in_range(g) && script_channel_running(g).kind != SCRIPT_EFFECT;
    case NEEDS_ONE_MOVER: return subjects_in_range(g) && script_channel_running(g).kind != SCRIPT_OTHER_TARGETS;
    case NEEDS_EFFECT: return effect_in_range(g);
    case NEEDS_COLORS: return colors_in_range(g);
    case NEEDS_A_SCREEN_EFFECT: return a_screen_effect(g);
    case NEEDS_SUBJECTS_AND_A_QUIET_PLACE:
        return subjects_in_range(g) && !script_place_works_in_cells(script_decode(g, row).place);
    case NEEDS_SUBJECTS_AND_THE_USUAL_LIFT: return subjects_in_range(g) && script_point_jump_lift_as_usual(g);
    default: return true;
    }
}

/* A place or a facing the command names is one of the original's. */
static bool names_known_places(const GameState *g, const ScriptOpcode *row)
{
    if (row->place.kind == ARG_NONE && row->toward.kind == ARG_NONE && row->facing.kind == ARG_NONE)
        return true;
    ScriptCommand command = script_decode(g, row);
    bool aims_at_a_place = row->aim.kind != ARG_NONE && command.aim == AIM_AT_A_PLACE;
    if ((aims_at_a_place || row->place.kind != ARG_NONE) && !script_place_known(g, command.place))
        return false;
    if (row->toward.kind != ARG_NONE && !script_place_known(g, command.toward))
        return false;
    return row->facing.kind == ARG_NONE || command.facing <= LAST_FACING;
}

bool script_runs_here(const GameState *g, const ScriptOpcode *row)
{
    if (script_over_the_direct_page(g))
        return false;
    return needs_met(g, row) && names_known_places(g, row);
}

/* ---- Running a command ----------------------------------------------------------------- */

static const ScriptOpcodes *const every_file[] = {
    &script_flow_opcodes,
    &script_pose_opcodes,
    &script_effect_opcodes,
    &script_color_opcodes,
    &script_place_opcodes,
    &script_move_opcodes,
    &script_walk_opcodes,
    &script_sound_opcodes,
    &script_number_opcodes,
};

enum { COMMANDS = 0x100 };

const ScriptOpcode *script_opcode(uint8_t command)
{
    static const ScriptOpcode *by_command[COMMANDS];
    static bool indexed;
    if (!indexed) {
        for (unsigned f = 0; f < sizeof every_file / sizeof every_file[0]; f++)
            for (unsigned r = 0; r < every_file[f]->count; r++)
                by_command[every_file[f]->rows[r].command] = &every_file[f]->rows[r];
        indexed = true;
    }
    return by_command[command];
}

static uint8_t decode(const GameState *g, ScriptArg arg)
{
    return arg.kind == ARG_OPERAND ? script_operand(g, arg.operand_or_constant) : arg.operand_or_constant;
}

ScriptCommand script_decode(const GameState *g, const ScriptOpcode *row)
{
    ScriptCommand command = { .channel = script_channel_running(g), .length = row->length };
#define SCRIPT_PARAMETER_DECODE(type, name) command.name = (type)decode(g, row->name);
    SCRIPT_PARAMETERS(SCRIPT_PARAMETER_DECODE)
#undef SCRIPT_PARAMETER_DECODE
    return command;
}

/* The original counts the channel's going on up, which its runner clears
 * before each command. */
static void go_on_this_frame(GameState *g) { set_battle_script_continue(g, (uint8_t)(battle_script_continue(g) + 1)); }

void script_go_past(GameState *g, uint8_t length) { set_script_place(g, script_place_plus(script_place(g), length)); }

void script_move_on(GameState *g, const ScriptOpcode *row, ScriptFlow flow)
{
    switch (flow) {
    case SCRIPT_NEXT:
        go_on_this_frame(g);
        script_go_past(g, row->length);
        break;
    case SCRIPT_NEXT_FRAME: script_go_past(g, row->length); break;
    case SCRIPT_AGAIN:
    case SCRIPT_MOVED: go_on_this_frame(g); break;
    case SCRIPT_STAY:
    case SCRIPT_MOVED_NEXT_FRAME: break;
    }
}

ScriptFlow script_operate(GameState *g, const ScriptOpcode *row)
{
    ScriptCommand command = script_decode(g, row);
    return row->run(g, &command);
}
