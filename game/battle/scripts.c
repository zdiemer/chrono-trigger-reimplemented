/* Battle: the animation script channels.
 *
 * What the battle shows of an action -- a combatant stepping forward, a
 * tech's effect, the numbers rising over its targets -- is played by scripts
 * in the ROM, on sixteen channels (battle/script_language.h says what each
 * animates). Each frame the battle runs every channel that is running
 * (battle_run_scripts): a channel either waits out the frames a command gave
 * it, or runs commands until one ends its frame. A command byte past the
 * language's last holds the channel there.
 *
 * The commands are the language's rows (battle/script_language.h, the
 * script_*.c files and battle/numbers.c hold them), one for every command
 * byte. A command whose row's operation can run in the state runs it, timed
 * as the original's handler (found through its dispatch table, battle/
 * script_data.h); the sound commands play their sound (battle/
 * script_sound.c). A command whose row's needs the state doesn't meet goes
 * no further. The channel's place in the script is battle_script_pointer,
 * which a command reads its operands through and moves on past them; a
 * command that lets the channel go on within the frame sets
 * battle_script_continue. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/effects.h"
#include "battle/entries.h"
#include "battle/program.h"
#include "battle/script_data.h"
#include "battle/script_language.h"
#include "charge.h"
#include "vars.h"

/* The last command byte the language has: a byte past it holds the channel. */
enum { LAST_COMMAND = 0xDA };

/* The original's handler for a command, which a refusal names and the
 * commands' timing goes by (battle/script_data.h). The runner's read of its
 * dispatch table is the runner's own time, which the channels' estimate was
 * calibrated without, so it is left out of it. */
static uint32_t handler_of(GameState *g, uint8_t command)
{
    uint32_t mark = 0;
    mark = charge_uncounted_begin(g);
    uint32_t handler = battle_script_handler(g, command);
    charge_uncounted_end(g, mark);
    return handler;
}

static uint32_t channel_place(const GameState *g, unsigned channel) { return battle_script_pointers(g, channel); }
static void set_channel_place(GameState *g, unsigned channel, uint32_t at) { set_battle_script_pointers(g, channel, at); }

/* ---- One channel ------------------------------------------------------------------------- */

/* A channel's next command, or none when it holds. */
enum { HOLDS = -1 };

static int next_command(GameState *g)
{
    uint8_t command = script_operand(g, 0);
    set_battle_script_command(g, command);
    return command <= LAST_COMMAND ? command : HOLDS;
}

/* Take up a channel: its place in its script is where its commands read. The
 * byte offset of its pointer is kept beside it, as the original's callers
 * give it. True if it has frames to wait still (one of them is counted off). */
static bool take_up_channel(GameState *g, unsigned channel)
{
    set_battle_script_offset(g, (uint8_t)(channel * 4));
    set_battle_script_pointer(g, channel_place(g, channel));
    set_battle_script_channel(g, (uint16_t)channel);
    uint8_t wait = battle_script_waits(g, channel);
    if (wait == 0)
        return false;
    set_battle_script_waits(g, channel, (uint8_t)(wait - 1));
    return true;
}

/* A command whose operation is C: run as the original's handler, whose time
 * it takes. */
static void run_operation(GameState *g, uint8_t command, const ScriptOpcode *row)
{
    TIMED_AS(g, handler_of(g, command), script_move_on(g, row, script_operate(g, row)));
}

/* How a command the channel comes to runs: its operation, its sound (the
 * sound commands), or, where the port doesn't run it, its handler (a
 * refusal); and, told after it, that the operation ran. */
typedef enum { RUN_THE_OPERATION, RUN_THE_SOUND, RUN_THE_HANDLER, THE_OPERATION_RAN } CommandRuns;

static CommandRuns how_it_runs(const GameState *g, uint8_t command, const ScriptOpcode *row)
{
    if (!row || !script_runs_here(g, row))
        return RUN_THE_HANDLER;
    return (command >= 0x78 && command <= 0x7D) ? RUN_THE_SOUND : RUN_THE_OPERATION;
}

/* A command run outside its operation: the sounds, played here; any other
 * command the port doesn't run goes no further. */
static bool run_elsewhere(GameState *g, Platform *pf, uint8_t command, const ScriptOpcode *row, CommandRuns runs)
{
    if (runs == THE_OPERATION_RAN)
        return true;
    if (runs != RUN_THE_SOUND)
        return battle_contract_broken(g, handler_of(g, command), "an animation script's command the port doesn't run");
    ScriptCommand decoded = script_decode(g, row);
    TIMED_AS(g, handler_of(g, command), script_play_the_sound(g, pf, command, &decoded));
    script_move_on(g, row, SCRIPT_NEXT);
    return true;
}

// $C1:4C9E
/* Run a channel for a frame: count off a frame of its wait, or run its
 * commands until one ends the frame (or it holds), and keep its place. */
static bool run_channel(GameState *g, Platform *pf, unsigned channel)
{
    if (take_up_channel(g, channel))
        return true;
    for (;;) {
        set_battle_script_continue(g, false);
        int command = next_command(g);
        if (command == HOLDS)
            break;
        const ScriptOpcode *row = script_opcode((uint8_t)command);
        CommandRuns runs = how_it_runs(g, (uint8_t)command, row);
        if (runs == RUN_THE_OPERATION) {
            run_operation(g, (uint8_t)command, row);
            runs = THE_OPERATION_RAN;
        }
        if (!run_elsewhere(g, pf, (uint8_t)command, row, runs))
            return false;
        if (!battle_script_continue(g))
            break;
    }
    set_channel_place(g, channel, battle_script_pointer(g));
    return true;
}

static bool channel_running(const GameState *g, unsigned channel) { return battle_script_running(g, channel) != 0; }

/* A channel's frame, a routine of its own. */
static bool run_a_channel(GameState *g, Platform *pf, unsigned channel)
{
    bool done = false;
    TIMED_AS(g, 0xC14C9E, done = run_channel(g, pf, channel));
    return done;
}

// $C1:4BBE
/* Run the channels for a frame: the battle's own, the other targets' while
 * the action has some, and, unless the numbers over the combatants are being
 * shown, the enemies', each told its enemy. */
bool battle_run_scripts(GameState *g, Platform *pf)
{
    for (unsigned channel = 0; channel < OTHER_TARGETS_CHANNEL; channel++)
        if (channel_running(g, channel) && !run_a_channel(g, pf, channel))
            return false;
    if (channel_running(g, OTHER_TARGETS_CHANNEL) && battle_action_other_targets(g) != 0
        && !run_a_channel(g, pf, OTHER_TARGETS_CHANNEL))
        return false;
    if (battle_number_step(g) != 0)
        return true;
    for (unsigned channel = FIRST_EFFECT_CHANNEL; channel < SCRIPT_CHANNELS; channel++) {
        if (!channel_running(g, channel))
            continue;
        set_battle_script_effect(g, (uint8_t)(channel - FIRST_EFFECT_CHANNEL));
        if (!run_a_channel(g, pf, channel))
            return false;
    }
    return true;
}

/* The commands whose handler goes on into another's once what it found makes
 * the command do what the other does: the end unless listed into the end's
 * where nobody is listed, the return at the top into the plain return's where
 * the point is at the top, the throw that waits first into the plain throw's
 * once its wait is over. Entered so, the command's own operation runs there
 * and does the same. */
enum {
    END = 0x00,
    RETURN_FROM_CALL = 0x1F,
    RETURN_AT_THE_TOP = 0x21,
    END_UNLESS_LISTED = 0x25,
    THROW_WAITING_FIRST = 0xD4,
    THROW = 0xD6,
};

/* The end's handler doesn't read the operand the port decides by, so it is
 * read where the game keeps scripts: the ROM's banks or work RAM's, within
 * the bank. */
enum { WORK_RAM_BANK = 0x7E, LAST_WORK_RAM_BANK = 0x7F, FIRST_ROM_BANK = 0xC0, BANK_END = 0xFFFF };
