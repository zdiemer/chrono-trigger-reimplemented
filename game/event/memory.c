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
#include "event/engine.h"
#include "event/variables.h"

#include "wram.h"

// $C0:6792
/* A variable learns the byte or the word at an address. */
static EventFlow peek(GameState *g, EventVariable variable, Address address, Width width)
{
    ORIGINAL_EFFECT(set_event_source_pointer(g, address));
    uint16_t value = width == AS_BYTE ? wram_read8(g, address) : wram_read16(g, address);
    set_variable(g, variable, value, width);
    return next_command();
}
EVENT_OPERATION(peek, variable, address, width)

// $C0:67D7
/* Write a byte or a word at an address: a number, or a variable's value. */
static EventFlow poke(GameState *g, Address address, uint16_t value, Width width)
{
    ORIGINAL_EFFECT(set_event_target_pointer(g, address));
    if (width == AS_BYTE)
        wram_write8(g, address, (uint8_t)value);
    else
        wram_write16(g, address, value);
    return next_command();
}
EVENT_OPERATION(poke, address, value, width)

/* ---- Copying data out of the script --------------------------------------------------------- */

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

static DataBlock data_block(uint16_t destination, uint8_t bank, uint16_t size, uint16_t at)
{
    uint32_t to_bank = bank == FIRST_BANK ? BANK(FIRST_BANK) : BANK(FIRST_BANK + 1);
    uint16_t length = (uint16_t)(size - 2);   /* the script counts the length word */
    return (DataBlock){ to_bank, destination, length, (uint16_t)(at + COPY_DATA_LENGTH) };
}

/* The original counts the copy in event_count, as its block move takes it:
 * one less than the bytes. */
static void keep_count(GameState *g, uint16_t length) { set_event_count(g, (uint16_t)(length - 1)); }

/* For the original's code and shadow mode: the block move leaves Y where the
 * destination ended, and turning the source's end back into a script offset
 * leaves V. Both follow from the block. */
enum { SCRIPT_ADDRESS = 0x2001 };   /* where the script starts in its bank */

static void leave_copy_ends(EventLeaves *left, DataBlock b)
{
    uint16_t from_end = (uint16_t)(b.from + b.length);
    left->y = (uint16_t)(b.to + b.length);
    left->overflow = field_subtract_overflows16((uint16_t)(from_end + SCRIPT_ADDRESS), SCRIPT_ADDRESS);
}

// $C0:6846
/* Copy the data that follows the command into work RAM (a table of positions
 * for a cutscene, a patch to some state), and go on after it. The copy goes a
 * byte at a time with the destination wrapping in its bank, as the original's
 * block move does, so a copy onto itself comes out the same. */
static EventStep copy_data(GameState *g, uint16_t destination, uint8_t bank, uint16_t size, uint16_t at)
{
    DataBlock b = data_block(destination, bank, size, at);
    ORIGINAL_EFFECT(keep_count(g, b.length));
    uint16_t from = b.from, to = b.to;
    for (uint16_t left = b.length; left != 0; left--) {
        wram_write8(g, b.bank | to, script_byte(g, from));
        from++, to++;
    }
    return (EventStep){ from, true };
}

bool event_copy_data_as_c(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    EventCommand c = event_decode(g, pf, event_opcode(0x4E), at);
    *step = copy_data(g, c.destination, c.bank, c.size, at);
    return true;
}

/* $4E's entry, a bridge for the stack: the original keeps the data bank on
 * the stack across its block move, which a copy onto the stack changes, and
 * so does the port. */
void event_cmd_copy_data(Snes *s)
{
    m_push8(s, s->cpu.db);
    uint16_t at = event_command_offset(s);
    EventLeaves left = event_leaves_of(s);
    EventCommand c = event_command_of(s, 0x4E);
    EventStep st = copy_data(state_of(s), c.destination, c.bank, c.size, at);
    ORIGINAL_EFFECT(leave_copy_ends(&left, data_block(c.destination, c.bank, c.size, at)));
    s->cpu.db = m_pull8(s);
    ORIGINAL_EFFECT(event_hand_back(s, &left));
    event_return(s, st);
}

/* The guard's part: the data lies in the script, and there is some (the
 * original's block move takes no length as the whole bank, and past the
 * script's end reads on round the bank; no script does either). */
static bool data_in_the_script(const GameState *g, uint16_t size, uint16_t at)
{
    (void)g;
    uint16_t length = (uint16_t)(size - 2);
    return length != 0 && (uint32_t)at + COPY_DATA_LENGTH + length <= EVENT_SCRIPT_BYTES;
}
EVENT_CHECK(data_in_the_script, size, at)

EVENT_OPCODES(event_memory_opcodes,
    { 0x48, OPERATION(peek), .variable = VARIABLE(4), .address = OPERAND_ADDRESS(1), .width = BYTES, .length = 5 },
    { 0x49, OPERATION(peek), .variable = VARIABLE(4), .address = OPERAND_ADDRESS(1), .width = WORDS, .length = 5 },
    { 0x4A, OPERATION(poke), .address = OPERAND_ADDRESS(1), .value = OPERAND(4), .width = BYTES, .length = 5 },
    { 0x4B, OPERATION(poke), .address = OPERAND_ADDRESS(1), .value = OPERAND_WORD(4), .width = WORDS, .length = 6 },
    { 0x4C, OPERATION(poke), .address = OPERAND_ADDRESS(1), .value = VARIABLE_WORD(4), .width = BYTES, .length = 5 },
    { 0x4D, OPERATION(poke), .address = OPERAND_ADDRESS(1), .value = VARIABLE_WORD(4), .width = WORDS, .length = 5 },
    { 0x4E, .destination = OPERAND_WORD(1), .bank = OPERAND(3), .size = OPERAND_WORD(4), .length = 6,
      CHECK(data_in_the_script) });
