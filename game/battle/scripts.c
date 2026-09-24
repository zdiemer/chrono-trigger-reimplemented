/* Battle: the animation script channels.
 *
 * What the battle shows of an action -- a combatant stepping forward, a
 * tech's effect, the numbers rising over its targets -- is played by scripts
 * in the ROM, on sixteen channels: channels 0-4 the battle's own (channel 4
 * animates an action's other targets, and runs only while it has some), and
 * channels 8-15 one for each enemy. Each frame the battle runs every channel
 * that is running (battle_run_scripts): a channel either waits out the frames
 * a command gave it, or runs commands until one ends its frame. A command
 * byte past the language's last holds the channel there.
 *
 * The commands are still the original's code, in bank C1: a channel calls each
 * through the language's table in the ROM, the only dispatch there is until
 * they are ported (docs/TODO.md), with the channel's place in the script in
 * battle_script_pointer, which the command reads its operands through and moves
 * on past them; a command that lets the channel go on within the frame sets
 * battle_script_continue. The number display's commands are ported
 * (battle/numbers.c) and the table reaches them there. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "rom.h"
#include "vars.h"
#include "wram.h"

enum { SCRIPT_CHANNELS = 16, FIRST_ENEMY_CHANNEL = 8, OTHER_TARGETS_CHANNEL = 4, LAST_COMMAND = 0xDA };

/* A script's bytes, where its pointer puts them: the ROM, or work RAM. */
static uint8_t script_byte(const GameState *g, uint32_t at)
{
    uint8_t bank = (uint8_t)(at >> 16);
    return (bank == 0x7E || bank == 0x7F) ? wram_read8(g, at) : rom_read8(g, at);
}

static uint32_t channel_place(const GameState *g, unsigned channel) { return battle_script_pointers(g, channel); }
static void set_channel_place(GameState *g, unsigned channel, uint32_t at) { set_battle_script_pointers(g, channel, at); }

/* ---- One channel ------------------------------------------------------------------------- */

/* A channel's next command, or none when it holds. */
enum { HOLDS = -1 };

static int next_command(GameState *g)
{
    uint8_t command = script_byte(g, battle_script_pointer(g));
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

/* The commands' table in the ROM (bank C1), where each command's handler is. */
static uint16_t command_handler(const GameState *g, uint8_t command)
{
    return rom_read16(g, 0xC17A6Bu + (uint16_t)(command * 2));
}

// $C1:4C9E
/* Run a channel for a frame: count off a frame of its wait, or run its
 * commands until one ends the frame (or it holds), and keep its place. */
static bool run_channel(Snes *s, unsigned channel)
{
    GameState *g = state_of(s);
    if (take_up_channel(g, channel))
        return true;
    for (;;) {
        set_battle_script_continue(g, false);
        int command = next_command(g);
        if (command == HOLDS)
            break;
        s->cpu.x = (uint16_t)(command * 2);
        s->cpu.y = 0;
        s->cpu.a = 0;
        if (!battle_call(s, 0xC14CD4, 0xC10000u | command_handler(g, (uint8_t)command), DC_AL | DC_N | DC_Z | DC_C))
            return false;
        if (!battle_script_continue(g))
            break;
    }
    set_channel_place(g, channel, battle_script_pointer(g));
    return true;
}

/* ---- The channels, each frame ------------------------------------------------------------- */

/* Each channel is run from a call of its own in the original ($C1:4C9E is
 * called from a site a channel): the channel's routine is the entry above,
 * and it calls the commands, which is why the frame's run calls it through
 * the machine rather than as C (docs/PORTING.md, "Call-outs"). */
static const uint16_t channel_calls[SCRIPT_CHANNELS] = {
    0x4BC8, 0x4BD5, 0x4BE2, 0x4BEF, 0x4C04, 0, 0, 0, 0x4C1C, 0x4C2E, 0x4C40, 0x4C52, 0x4C64, 0x4C76, 0x4C88, 0x4C9A,
};

static bool call_channel(Snes *s, unsigned channel)
{
    s->cpu.y = (uint16_t)channel;
    m_set_a8(s, (uint8_t)(channel * 4));
    return battle_call(s, 0xC10000u | channel_calls[channel], 0xC14C9E, DC_N | DC_Z | DC_C);
}

static bool channel_running(const GameState *g, unsigned channel) { return battle_script_running(g, channel) != 0; }

// $C1:4BBE
/* Run the channels for a frame: the battle's own, the other targets' while
 * the action has some, and, unless the numbers over the combatants are being
 * shown, the enemies', each told its enemy. */
static bool run_scripts(Snes *s)
{
    GameState *g = state_of(s);
    for (unsigned channel = 0; channel < OTHER_TARGETS_CHANNEL; channel++)
        if (channel_running(g, channel) && !call_channel(s, channel))
            return false;
    if (channel_running(g, OTHER_TARGETS_CHANNEL) && battle_action_other_targets(g) != 0
        && !call_channel(s, OTHER_TARGETS_CHANNEL))
        return false;
    if (battle_number_step(g) != 0)
        return true;
    for (unsigned channel = FIRST_ENEMY_CHANNEL; channel < SCRIPT_CHANNELS; channel++) {
        if (!channel_running(g, channel))
            continue;
        set_battle_script_enemy(g, (uint8_t)(channel - FIRST_ENEMY_CHANNEL));
        if (!call_channel(s, channel))
            return false;
    }
    return true;
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The channel in Y, its pointer's offset in A (four times it). */
void battle_run_script_channel(Snes *s)
{
    if (run_channel(s, s->cpu.y))
        m_rts(s);
}

void battle_run_scripts(Snes *s)
{
    if (run_scripts(s))
        m_rts(s);
}

/* The channel the original names in Y, and its pointer's offset in A, which
 * it indexes with as a word (A's high byte 0, as its callers leave it). */
bool battle_script_channel_in_y(Snes *s)
{
    return battle_on_page(s) && s->cpu.y < SCRIPT_CHANNELS && s->cpu.a == (uint16_t)(s->cpu.y * 4);
}
