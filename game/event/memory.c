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

// $C0:6792
/* A variable learns the byte or the word at an address. */
static EventFlow peek(GameState *g, const EventCommand *c)
{
    ORIGINAL_EFFECT(set_event_source_pointer(g, c->address));
    uint16_t value = c->width == AS_BYTE ? wram_read8(g, c->address) : wram_read16(g, c->address);
    set_variable(g, c->variable, value, c->width);
    return next_command();
}

// $C0:67D7
/* Write a byte or a word at an address: a number, or a variable's value. */
static EventFlow poke(GameState *g, const EventCommand *c)
{
    ORIGINAL_EFFECT(set_event_target_pointer(g, c->address));
    if (c->width == AS_BYTE)
        wram_write8(g, c->address, (uint8_t)c->value);
    else
        wram_write16(g, c->address, c->value);
    return next_command();
}

EVENT_OPCODES(event_memory_opcodes,
    { 0x48, peek, .variable = VARIABLE(4), .address = OPERAND_ADDRESS(1), .width = BYTES, .length = 5,
      .routine = 0xC06792 },
    { 0x49, peek, .variable = VARIABLE(4), .address = OPERAND_ADDRESS(1), .width = WORDS, .length = 5,
      .routine = 0xC067C7 },
    { 0x4A, poke, .address = OPERAND_ADDRESS(1), .value = OPERAND(4), .width = BYTES, .length = 5,
      .routine = 0xC067D7 },
    { 0x4B, poke, .address = OPERAND_ADDRESS(1), .value = OPERAND_WORD(4), .width = WORDS, .length = 6,
      .routine = 0xC067FB },
    { 0x4C, poke, .address = OPERAND_ADDRESS(1), .value = VARIABLE_WORD(4), .width = BYTES, .length = 5,
      .routine = 0xC0680C },
    { 0x4D, poke, .address = OPERAND_ADDRESS(1), .value = VARIABLE_WORD(4), .width = WORDS, .length = 5,
      .routine = 0xC06829 },
    { 0x4E, .bridge = event_cmd_copy_data, .destination = OPERAND_WORD(1), .bank = OPERAND(3),
      .size = OPERAND_WORD(4), .length = 6, .routine = 0xC06846 });

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

enum { COPY_DATA_LENGTH = 6 };   /* the data follows the command */

static DataBlock data_block(const EventCommand *c, uint16_t at)
{
    uint32_t bank = c->bank == FIRST_BANK ? BANK(FIRST_BANK) : BANK(FIRST_BANK + 1);
    uint16_t length = (uint16_t)(c->size - 2);   /* the script counts the length word */
    return (DataBlock){ bank, c->destination, length, (uint16_t)(at + COPY_DATA_LENGTH) };
}

/* The original counts the copy in event_count, as its block move takes it:
 * one less than the bytes. */
static void keep_count(GameState *g, uint16_t length) { set_event_count(g, (uint16_t)(length - 1)); }

// $C0:6846
/* Copy the data that follows the command into work RAM (a table of positions
 * for a cutscene, a patch to some state), and go on after it. The copy goes a
 * byte at a time with both addresses wrapping in their banks, as the
 * original's block move does, so a copy onto itself comes out the same. */
static EventStep copy_data(GameState *g, const EventCommand *c, uint16_t at)
{
    DataBlock b = data_block(c, at);
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
    EventCommand c = event_command_of(s, 0x4E);
    EventStep st = copy_data(state_of(s), &c, at);
    s->cpu.db = m_pull8(s);
    ORIGINAL_EFFECT(event_hand_back(s, &left));
    event_return(s, st);
}
