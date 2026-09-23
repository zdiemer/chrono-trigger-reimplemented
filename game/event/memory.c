/* Event commands: memory -- the event memory at $7F:0000 (the story flags),
 * anything at all through a 24-bit pointer in the script, and blocks of data
 * the script carries, copied into work RAM.
 *
 * The pointers are the script's to choose, so what they reach is work RAM
 * the game locates at run time, through game/wram.h: a pointer can land on
 * any cell, the commands' own included, so the commands keep the original's
 * order of reads and writes. */
#include "event/bridge.h"
#include "event/event_data.h"

#include "wram.h"

/* Keep the 24-bit pointer at operands 1-3 a byte at a time, as the original
 * does: an operand past the end of bank $7F is read through the mirror of low
 * RAM, where it can be the byte just kept. */
static uint32_t keep_pointer(GameState *g, const EventCommand *c, uint32_t (*get)(const GameState *),
                             void (*set)(GameState *, uint32_t))
{
    for (unsigned n = 0; n < 3; n++) {
        uint8_t byte = operand(g, c, 1 + n);
        set(g, (get(g) & ~(0xFFu << (8 * n))) | (uint32_t)byte << (8 * n));
    }
    return get(g);
}

// $C0:67A2
/* $48 and $49 read through the pointer they keep in event_source_pointer;
 * the scratch cell keeps the variable operand's offset. */
static uint32_t read_through(GameState *g, const EventCommand *c, uint8_t *v)
{
    keep_pointer(g, c, event_source_pointer, set_event_source_pointer);
    *v = variable_operand(g, c, 4);
    keep16(g, SCRATCH_D9, (uint16_t)(c->at + 4));
    return event_source_pointer(g);
}

// $C0:6792
/* $48: variable (operand 4) = the byte at the pointer in operands 1-3. */
static EventStep var_from_pointer(GameState *g, const EventCommand *c)
{
    uint8_t v;
    uint32_t at = read_through(g, c, &v);
    set_var8(g, v, wram_read8(g, at));
    return next_command(c, 5, 0);
}

// $C0:67C7
/* $49: 16-bit variable (operand 4) = the word at the pointer in operands 1-3. */
static EventStep var_from_pointer16(GameState *g, const EventCommand *c)
{
    uint8_t v;
    uint32_t at = read_through(g, c, &v);
    set_var16(g, v, wram_read16(g, at));
    return next_command(c, 5, 0);
}

// $C0:67E3
/* $4A-$4D write through the pointer they keep in event_target_pointer. */
static uint32_t write_through(GameState *g, const EventCommand *c)
{
    return keep_pointer(g, c, event_target_pointer, set_event_target_pointer);
}

// $C0:67D7
/* $4A: store operand 4 at the pointer in operands 1-3. */
static EventStep poke(GameState *g, const EventCommand *c)
{
    uint32_t at = write_through(g, c);
    wram_write8(g, at, operand(g, c, 4));
    return next_command(c, 5, 0);
}

// $C0:67FB
/* $4B: store operands 4-5 at the pointer in operands 1-3. */
static EventStep poke16(GameState *g, const EventCommand *c)
{
    uint32_t at = write_through(g, c);
    wram_write16(g, at, operand_word(g, c, 4));
    return next_command(c, 6, 0);
}

// $C0:680C
/* $4C: store variable (operand 4) at the pointer in operands 1-3. */
static EventStep poke_variable(GameState *g, const EventCommand *c)
{
    uint32_t at = write_through(g, c);
    uint8_t v = variable_operand(g, c, 4);
    keep16(g, SCRATCH_D9, (uint16_t)(c->at + 4));
    wram_write8(g, at, var8(g, v));
    return next_command(c, 5, 0);
}

// $C0:6829
/* $4D: store 16-bit variable (operand 4) at the pointer in operands 1-3. */
static EventStep poke_variable16(GameState *g, const EventCommand *c)
{
    uint32_t at = write_through(g, c);
    uint8_t v = variable_operand(g, c, 4);
    keep16(g, SCRATCH_D9, (uint16_t)(c->at + 4));
    wram_write16(g, at, var16(g, v));
    return next_command(c, 5, 0);
}

enum { SCRIPT_ADDRESS = 0x2001 };   /* where the script starts in bank $7F */

// $C0:6846
/* $4E: copy the data that follows into work RAM: operands 1-2 the address,
 * operand 3 its bank ($7E; anything else is $7F), operands 4-5 the data's
 * length plus 2, then the data. Goes on after it. The copy is the block move
 * the original makes, a byte at a time with both addresses wrapping in their
 * banks, so a copy onto itself goes as the original's does; it leaves the
 * count in event_count, the parse position where the destination ended,
 * and the overflow of turning the source's end back into an offset. */
static EventStep copy_data(GameState *g, const EventCommand *c)
{
    uint16_t to = operand_word(g, c, 1);
    uint32_t bank = operand(g, c, 3) == 0x7E ? 0x7E0000u : 0x7F0000u;
    uint16_t count = (uint16_t)(operand_word(g, c, 4) - 3);
    set_event_count(g, count);
    uint16_t from = (uint16_t)(c->at + 6 + SCRIPT_ADDRESS);
    do {
        wram_write8(g, bank | to, wram_read8(g, 0x7F0000u | from));
        from++, to++;
    } while (count-- != 0);
    uint16_t next = (uint16_t)(from - SCRIPT_ADDRESS);
    EventStep st = step_at(next, true, to);
    return with_overflow(st, overflows16(from, (uint16_t)~SCRIPT_ADDRESS, next));
}

// $C0:693D
/* $53: variable (operand 3) = the byte of event memory at operands 1-2 (the
 * scratch cell keeps the word there). */
static EventStep var_from_memory(GameState *g, const EventCommand *c)
{
    keep16(g, SCRATCH_D9, memory16(g, operand_word(g, c, 1)));
    set_var8(g, variable_operand(g, c, 3), scratch8(g, SCRATCH_D9));
    return next_command(c, 4, 3);
}

// $C0:6964
/* $54: 16-bit variable (operand 3) = the word of event memory at operands 1-2. */
static EventStep var_from_memory16(GameState *g, const EventCommand *c)
{
    keep16(g, SCRATCH_D9, memory16(g, operand_word(g, c, 1)));
    set_var16(g, variable_operand(g, c, 3), scratch16(g, SCRATCH_D9));
    return next_command(c, 4, 3);
}

// $C0:698B
/* $55: variable (operand 1) = the first byte of event memory. */
static EventStep var_from_memory0(GameState *g, const EventCommand *c)
{
    set_var8(g, variable_operand(g, c, 1), memory8(g, 0));
    return next_command(c, 2, 1);
}

// $C0:69A6
/* $56: store operand 1 in event memory at operands 2-3. */
static EventStep memory_set(GameState *g, const EventCommand *c)
{
    keep8(g, SCRATCH_D9, operand(g, c, 1));
    set_memory8(g, operand_word(g, c, 2), scratch8(g, SCRATCH_D9));
    return next_command(c, 4, 4);
}

// $C0:69C4
/* $58: store variable (operand 1) in event memory at operands 2-3. */
static EventStep memory_set_from_var(GameState *g, const EventCommand *c)
{
    keep16(g, SCRATCH_D9, var16(g, variable_operand(g, c, 1)));
    set_memory8(g, operand_word(g, c, 2), scratch8(g, SCRATCH_D9));
    return next_command(c, 4, 4);
}

// $C0:69EB
/* $59: store 16-bit variable (operand 1) in event memory at operands 2-3. */
static EventStep memory_set_from_var16(GameState *g, const EventCommand *c)
{
    keep16(g, SCRATCH_D9, var16(g, variable_operand(g, c, 1)));
    set_memory16(g, operand_word(g, c, 2), scratch16(g, SCRATCH_D9));
    return next_command(c, 4, 4);
}

// $C0:6A12
/* $5A: store operand 1 in the first byte of event memory. */
static EventStep memory0_set(GameState *g, const EventCommand *c)
{
    set_memory8(g, 0, operand(g, c, 1));
    return next_command(c, 2, 1);
}

/* The byte of event memory a bit command names: operand 2 in the first
 * page, or the second when bit 7 of operand 1 is set; bits 0-3 of operand 1
 * pick the bit. The operand and the mask are kept in the scratch cells. */
static uint16_t flag_byte(GameState *g, const EventCommand *c, uint8_t mask)
{
    uint8_t which = operand(g, c, 1);
    keep8(g, SCRATCH_DB, which);
    keep8(g, SCRATCH_D9, mask);
    return (uint16_t)((which & 0x80) << 1 | operand(g, c, 2));
}

// $C0:6B76
/* $65: set a bit of event memory (a story flag). */
static EventStep memory_set_bit(GameState *g, const EventCommand *c)
{
    uint8_t mask = bit_mask(g, operand(g, c, 1) & 0x0F);
    uint16_t at = flag_byte(g, c, mask);
    set_memory8(g, at, (uint8_t)(memory8(g, at) | mask));
    return next_command(c, 3, 3);
}

// $C0:6BA4
/* $66: clear a bit of event memory. */
static EventStep memory_clear_bit(GameState *g, const EventCommand *c)
{
    uint8_t mask = bit_clear_mask(g, operand(g, c, 1) & 0x0F);
    uint16_t at = flag_byte(g, c, mask);
    set_memory8(g, at, (uint8_t)(memory8(g, at) & mask));
    return next_command(c, 3, 3);
}

EVENT_COMMAND(event_cmd_var_from_pointer, var_from_pointer)
EVENT_COMMAND(event_cmd_var_from_pointer16, var_from_pointer16)
EVENT_COMMAND(event_cmd_poke, poke)
EVENT_COMMAND(event_cmd_poke16, poke16)
EVENT_COMMAND(event_cmd_poke_variable, poke_variable)
EVENT_COMMAND(event_cmd_poke_variable16, poke_variable16)
EVENT_COMMAND(event_cmd_var_from_memory, var_from_memory)
EVENT_COMMAND(event_cmd_var_from_memory16, var_from_memory16)
EVENT_COMMAND(event_cmd_var_from_memory0, var_from_memory0)
EVENT_COMMAND(event_cmd_memory_set, memory_set)
EVENT_COMMAND(event_cmd_memory_set_from_var, memory_set_from_var)
EVENT_COMMAND(event_cmd_memory_set_from_var16, memory_set_from_var16)
EVENT_COMMAND(event_cmd_memory0_set, memory0_set)
EVENT_COMMAND(event_cmd_memory_set_bit, memory_set_bit)
EVENT_COMMAND(event_cmd_memory_clear_bit, memory_clear_bit)

/* $4E's entry: the original keeps the data bank on the stack across its block
 * move, which a copy onto the stack changes, and so does the port. */
void event_cmd_copy_data(Snes *s)
{
    m_push8(s, s->cpu.db);
    EventCommand c = event_command_at(s);
    EventStep st = copy_data(state_of(s), &c);
    s->cpu.db = m_pull8(s);
    event_return(s, st);
}
