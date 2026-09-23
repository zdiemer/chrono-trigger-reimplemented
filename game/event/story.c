/* Scripts recording the story (event/variables.h): writing a byte or a word
 * of the story memory, moving the story's progress on, and raising or
 * lowering a single story flag -- a door unlocked, a character met.
 *
 * Reading the story is done where it is used: the variables take values
 * from it (event/variables.c), and the conditions test it (event/flow.c). */
#include "event/event_data.h"
#include "event/leaves.h"
#include "event/script.h"
#include "event/variables.h"

// $C0:69A6
/* Write the story: a number, or a variable's value, as a byte or a word. The
 * story's progress counter is its first byte. */
static EventFlow record(GameState *g, const EventCommand *c)
{
    if (c->width == AS_BYTE)
        set_story_byte(g, c->story_at, (uint8_t)c->value);
    else
        set_story_word(g, c->story_at, c->value);
    return next_command();
}

/* A story flag, as a script names it: the byte in the operand after it (in
 * the second page if bit 7 of this one is set) and the bit in bits 0-3. The
 * original looks the bit up in a table of eight masks, so bits 8-15 read on
 * into what follows it: the table of the masks with one bit clear. */
static StoryOffset flag_byte(uint8_t flag, uint8_t offset) { return story_offset_in(offset, flag); }

/* What the original keeps of a flag: how the script named it, and its mask. */
static void keep_flag(GameState *g, uint8_t flag, uint8_t mask)
{
    keep_byte(g, KEPT_SECOND, flag);
    keep_byte(g, KEPT_FIRST, mask);
}

// $C0:6B76
/* Raise a story flag, or lower it. */
static EventFlow mark_story_flag(GameState *g, const EventCommand *c)
{
    uint8_t bit = c->flag & 0x0F;
    uint8_t mask = c->raise ? bit_mask(g, bit) : bit_clear_mask(g, bit);
    ORIGINAL_EFFECT(keep_flag(g, c->flag, mask));
    StoryOffset at = flag_byte(c->flag, c->offset);
    uint8_t byte = story_byte(g, at);
    set_story_byte(g, at, c->raise ? (uint8_t)(byte | mask) : (uint8_t)(byte & mask));
    return next_command();
}

EVENT_OPCODES(event_story_opcodes,
    { 0x56, record, .story_at = OPERAND_WORD(2), .value = OPERAND(1), .width = BYTES, .length = 4,
      .routine = 0xC069A6 },
    { 0x58, record, .story_at = OPERAND_WORD(2), .value = VARIABLE_WORD(1), .width = BYTES, .length = 4,
      .routine = 0xC069C4 },
    { 0x59, record, .story_at = OPERAND_WORD(2), .value = VARIABLE_WORD(1), .width = WORDS, .length = 4,
      .routine = 0xC069EB },
    { 0x5A, record, .story_at = CONSTANT(0), .value = OPERAND(1), .width = BYTES, .length = 2, .routine = 0xC06A12 },
    { 0x65, mark_story_flag, .flag = OPERAND(1), .offset = OPERAND(2), .raise = CONSTANT(true), .length = 3,
      .routine = 0xC06B76 },
    { 0x66, mark_story_flag, .flag = OPERAND(1), .offset = OPERAND(2), .raise = CONSTANT(false), .length = 3,
      .routine = 0xC06BA4 });
