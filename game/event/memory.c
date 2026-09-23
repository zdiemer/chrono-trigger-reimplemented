/* Scripts reaching past their own world into the game's memory: reading a
 * byte or a word at an address into a variable, writing one there, and
 * copying a block of data carried in the script into work RAM. The scripts
 * use these for what the language has no command for -- a character's stats,
 * a menu's setting, a table of positions for a cutscene.
 *
 * The addresses are the script's to choose, so what they reach is work RAM
 * located at run time, through game/wram.h. The original keeps the address
 * it last read through, and the one it last wrote through (the cells
 * event_source_pointer and event_target_pointer); nothing but the command
 * itself reads them back, so they are kept as scaffolding. */
#include "event/bridge.h"
#include "event/variables.h"

#include "wram.h"

typedef uint32_t Address;   /* bank << 16 | offset */

// $C0:6792
/* A variable learns the byte or the word at an address. */
static EventFlow peek(GameState *g, EventVariable v, Address at, Width w)
{
    ORIGINAL_EFFECT(set_event_source_pointer(g, at));
    set_variable(g, v, w == AS_BYTE ? wram_read8(g, at) : wram_read16(g, at), w);
    return next_command();
}

// $C0:67D7
/* Write a byte or a word at an address: a number, or a variable's value. */
static EventFlow poke(GameState *g, Address at, uint16_t value, Width w)
{
    ORIGINAL_EFFECT(set_event_target_pointer(g, at));
    if (w == AS_BYTE)
        wram_write8(g, at, (uint8_t)value);
    else
        wram_write16(g, at, value);
    return next_command();
}

static EventFlow run_peek(GameState *g, const EventArgs *a) { return peek(g, a->arg[0], a->arg[1], a->arg[2]); }
static EventFlow run_poke(GameState *g, const EventArgs *a) { return poke(g, a->arg[0], a->arg[1], a->arg[2]); }

#define BYTES CONSTANT(AS_BYTE)
#define WORDS CONSTANT(AS_WORD)

/* The original keeps the variable operand's offset in its scratch cell,
 * where it has one; its parse stops on the opcode. */
EVENT_OPCODES(event_memory_opcodes,
    { 0x48, run_peek, { VARIABLE(4), ADDRESS(1), BYTES }, 5, .routine = 0xC06792 },
    { 0x49, run_peek, { VARIABLE(4), ADDRESS(1), WORDS }, 5, .routine = 0xC067C7 },
    { 0x4A, run_poke, { ADDRESS(1), BYTE(4), BYTES }, 5, .routine = 0xC067D7 },
    { 0x4B, run_poke, { ADDRESS(1), WORD(4), WORDS }, 6, .routine = 0xC067FB },
    { 0x4C, run_poke, { ADDRESS(1), VARIABLE_WORD(4), BYTES }, 5, .routine = 0xC0680C },
    { 0x4D, run_poke, { ADDRESS(1), VARIABLE_WORD(4), WORDS }, 5, .routine = 0xC06829 },
    { 0x4E, .bridge = event_cmd_copy_data, .length = 6, .routine = 0xC06846 });

/* ---- Copying data out of the script --------------------------------------------------------- */

enum { SCRIPT_ADDRESS = 0x2001 };   /* where the script starts in bank $7F */
enum { FIRST_BANK = 0x7E };            /* of work RAM; a block names it, or it goes to the second */
#define BANK(b) ((uint32_t)(b) << 16)

/* A block of data carried in the script, and where it goes: an address in
 * bank $7E, or $7F for any other bank byte. */
typedef struct {
    uint32_t bank;
    uint16_t to;
    uint16_t length;    /* the data's bytes */
    uint16_t from;      /* where the data is in the script */
} DataBlock;

static DataBlock data_block_at(const GameState *g, uint16_t at)
{
    uint16_t to = script_word(g, (uint16_t)(at + 1));
    uint32_t bank = script_byte(g, (uint16_t)(at + 3)) == FIRST_BANK ? BANK(FIRST_BANK) : BANK(FIRST_BANK + 1);
    uint16_t length = (uint16_t)(script_word(g, (uint16_t)(at + 4)) - 2);   /* the script counts the length word */
    return (DataBlock){ bank, to, length, (uint16_t)(at + 6) };
}

/* The original counts the copy in event_count, as its block move takes it:
 * one less than the bytes. */
static void keep_count(GameState *g, uint16_t length) { set_event_count(g, (uint16_t)(length - 1)); }

// $C0:6846
/* Copy the data that follows the command into work RAM (a table of positions
 * for a cutscene, a patch to some state), and go on after it. The copy goes a
 * byte at a time with both addresses wrapping in their banks, as the
 * original's block move does, so a copy onto itself comes out the same. */
static EventStep copy_data(GameState *g, uint16_t at)
{
    DataBlock b = data_block_at(g, at);
    ORIGINAL_EFFECT(keep_count(g, b.length));
    uint16_t from = (uint16_t)(b.from + SCRIPT_ADDRESS), to = b.to, left = b.length;
    do {   /* as the block move counts: no length is the whole bank */
        wram_write8(g, b.bank | to, wram_read8(g, BANK(FIRST_BANK + 1) | from));
        from++, to++;
    } while (--left != 0);
    /* the block move leaves Y where the destination ended, and turning the
     * source's end back into an offset leaves V */
    ORIGINAL_EFFECT(leave_y(to));
    ORIGINAL_EFFECT(leave_overflow_of_subtracting16(from, SCRIPT_ADDRESS));
    return (EventStep){ (uint16_t)(from - SCRIPT_ADDRESS), true };
}

/* $4E's entry, a bridge for the stack: the original keeps the data bank on
 * the stack across its block move, which a copy onto the stack changes, and
 * so does the port. */
void event_cmd_copy_data(Snes *s)
{
    m_push8(s, s->cpu.db);
    uint16_t at = event_command_offset(s);
    EventLeaves left = event_leaves_of(s, at);
    ORIGINAL_EFFECT(leave_into(&left));
    EventStep st = copy_data(state_of(s), at);
    s->cpu.db = m_pull8(s);
    ORIGINAL_EFFECT(event_hand_back(s, &left));
    event_return(s, st);
}
