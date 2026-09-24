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
static EventFlow record(GameState *g, StoryOffset story_at, uint16_t value, Width width)
{
    if (width == AS_BYTE)
        set_story_byte(g, story_at, (uint8_t)value);
    else
        set_story_word(g, story_at, value);
    return next_command();
}
EVENT_OPERATION(record, story_at, value, width)

/* The guard's part: what it writes is in the story memory (event/variables.h). */
static bool records_in_the_story(const GameState *g, StoryOffset story_at, Width width)
{
    (void)g;
    return story_holds(story_at, width);
}
EVENT_CHECK(records_in_the_story, story_at, width)

/* What the original keeps of a flag: its mask (and the operand that names
 * it, by event/leaves.c's table). */
static void keep_mask(GameState *g, uint8_t mask) { keep_byte(g, KEPT_FIRST, mask); }

// $C0:6B76
/* Raise a story flag, or lower it: a bit of a byte on the story's first
 * page, or its second. The original looks the bit up in a table of eight
 * masks, so bits 8-15 read on into what follows it: the table of the masks
 * with one bit clear. */
static EventFlow mark_story_flag(GameState *g, uint8_t flag_bit, bool on_second_page, uint8_t offset, bool raise)
{
    uint8_t mask = raise ? bit_mask(g, flag_bit) : bit_clear_mask(g, flag_bit);
    ORIGINAL_EFFECT(keep_mask(g, mask));
    StoryOffset at = (StoryOffset)((on_second_page ? STORY_SECOND_PAGE : 0) | offset);
    uint8_t byte = story_byte(g, at);
    set_story_byte(g, at, raise ? (uint8_t)(byte | mask) : (uint8_t)(byte & mask));
    return next_command();
}
EVENT_OPERATION(mark_story_flag, flag_bit, on_second_page, offset, raise)

/* A story flag, as a script names it: the byte in the operand after it (on
 * the second page if bit 7 of this one is set) and the bit in bits 0-3. */
#define A_STORY_FLAG .flag_bit = OPERAND_BITS(1, 0, 0x0F), .on_second_page = OPERAND_BITS(1, 7, 0x01), .offset = OPERAND(2)

EVENT_OPCODES(event_story_opcodes,
    { 0x56, OPERATION(record), .story_at = OPERAND_WORD(2), .value = OPERAND(1), .width = BYTES, .length = 4,
      CHECK(records_in_the_story) },
    { 0x58, OPERATION(record), .story_at = OPERAND_WORD(2), .value = VARIABLE_WORD(1), .width = BYTES, .length = 4,
      CHECK(records_in_the_story) },
    { 0x59, OPERATION(record), .story_at = OPERAND_WORD(2), .value = VARIABLE_WORD(1), .width = WORDS, .length = 4,
      CHECK(records_in_the_story) },
    { 0x5A, OPERATION(record), .story_at = CONSTANT(0), .value = OPERAND(1), .width = BYTES, .length = 2 },
    { 0x65, OPERATION(mark_story_flag), A_STORY_FLAG, .raise = CONSTANT(true), .length = 3 },
    { 0x66, OPERATION(mark_story_flag), A_STORY_FLAG, .raise = CONSTANT(false), .length = 3 });
