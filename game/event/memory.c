#include "event/script.h"
#include "event/script.h"
/* Scripts reaching past their own world into the game's memory: reading a
 * byte or a word at an address into a variable, writing one there, and
 * copying a block of data carried in the script into work RAM. The scripts
 * use these for what the language has no command for -- a character's stats,
 * a menu's setting, a table of positions for a cutscene.
 *
 * The addresses are the script's to choose. Reads use named data accessors
 * that also serve cartridge resources; writes reach work RAM through wram.h. The original keeps the address
 * it last read through, and the one it last wrote through (the cells
 * event_source_pointer and event_target_pointer); nothing but the command
 * itself reads them back, so the port doesn't keep them. */
#include "charge.h"
#include "event/engine.h"
#include "event/memory_data.h"
#include "event/variables.h"

#include "wram.h"

// $C0:6792
/* A variable learns the byte or the word at an address. */
static EventFlow peek(GameState *g, Platform *pf, EventVariable variable, Address address, Width width)
{
    charge_accesses(g, 3); /* source address selection */
    uint16_t value = width == AS_BYTE ? event_selected_data_byte(g, pf, address) : event_selected_data_word(g, pf, address);
    set_variable(g, variable, value, width);
    return next_command();
}
EVENT_OPERATION(peek, platform, variable, address, width)

// $C0:67D7
/* Write a byte or a word at an address: a number, or a variable's value. */
static EventFlow poke(GameState *g, Address address, uint16_t value, Width width)
{
    charge_accesses(g, 3); /* destination address selection */
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

/* Copy the data that follows the command into work RAM (a table of positions
 * for a cutscene, a patch to some state), and go on after it. The copy goes a
 * byte at a time with the destination wrapping in its bank, as the original's
 * block move does, so a copy onto itself comes out the same. */
static EventStep copy_data(GameState *g, uint16_t destination, uint8_t bank, uint16_t size, uint16_t at)
{
    DataBlock b = data_block(destination, bank, size, at);
    uint16_t from = b.from, to = b.to;
    for (uint16_t left = b.length; left != 0; left--) {
        wram_write8(g, b.bank | to, script_byte(g, from));
        from++, to++;
    }
    return (EventStep){ from, true };
}

bool event_copy_data(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    EventCommand c = event_decode(g, pf, event_opcode(0x4E), at);
    *step = copy_data(g, c.destination, c.bank, c.size, at);
    return true;
}

/* The command's length: its six bytes and the data after them. */
static uint16_t measure_copy_data(const uint8_t *command, uint16_t available)
{
    return available < COPY_DATA_LENGTH ? 0 : (uint16_t)(COPY_DATA_LENGTH + (uint16_t)(command[4] | command[5] << 8) - 2);
}

/* What the port checks: the data lies in the script, and there is some (the
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
    { 0x4E, .destination = OPERAND_WORD(1), .bank = OPERAND(3), .size = OPERAND_WORD(4), .length = COPY_DATA_LENGTH,
      CHECK(data_in_the_script), MEASURED(measure_copy_data) });
