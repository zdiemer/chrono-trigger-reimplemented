/* What each command leaves for the original's sake (event/leaves.h), by
 * opcode: a table parallel to the language's, of what isn't the language's.
 * The build without the original's effects has none of it. */
#include "event/leaves.h"
#include "event/variables.h"

#if ORIGINAL_EFFECTS

#define PARSE(n) .parse = (n), .parse_on_jump = (n)
#define KEEP(how, parameter) { KEEP_##how, PARAMETER_##parameter, 0 }
#define KEEP_OPERAND_AT(n) { KEEP_OPERAND_OFFSET, 0, (n) }

static const OriginalLeaves by_opcode[256] = {
    /* animation */
    [0xAA] = { .parse = 1 },
    /* calls between objects */
    [0x02] = { .parse = 3 },
    [0x05] = { .parse = 3 },
    [0x0A] = { .parse = 1 },
    [0x0B] = { .parse = 1 },
    [0x0C] = { .parse = 1 },
    /* dialog */
    [0x19] = { .parse = 1 },
    [0x1A] = { PARSE(1) },
    [0x1C] = { .parse = 2 },
    /* facing */
    [0x1E] = { .parse = 1 },
    [0x1F] = { .parse = 1 },
    [0x23] = { .parse = 3 },
    [0x24] = { .parse = 3 },
    [0x25] = { .parse = 1 },
    [0x26] = { .parse = 1 },
    [0xA6] = { .parse = 1 },
    [0xA7] = { .parse = 1 },
    /* jumps and conditions */
    [0x10] = { PARSE(1) },
    [0x11] = { PARSE(1) },
    [0x12] = { PARSE(2), .shared_comparison = true, .first = KEEP(BYTE, left), .second = KEEP(BYTE, right) },
    [0x13] = { PARSE(2), .shared_comparison = true, .first = KEEP(WORD, left), .second = KEEP(WORD, right) },
    [0x14] = { PARSE(3), .shared_comparison = true, .first = KEEP(BYTE, left), .second = KEEP(BYTE, right) },
    [0x15] = { PARSE(3), .shared_comparison = true, .first = KEEP(WORD, left), .second = KEEP(WORD, right) },
    [0x16] = { PARSE(1), .shared_comparison = true },
    [0x18] = { PARSE(1) },
    [0x27] = { PARSE(1) },
    [0x28] = { PARSE(1) },
    /* the controller: its parse stops on the jump when the button is down, before it when not */
    [0x2D] = { .parse = 2, .parse_on_jump = 1 },
    [0x30] = { .parse = 2, .parse_on_jump = 1 },
    [0x31] = { .parse = 2, .parse_on_jump = 1 },
    [0x34] = { .parse = 2, .parse_on_jump = 1 },
    [0x35] = { .parse = 2, .parse_on_jump = 1 },
    [0x36] = { .parse = 2, .parse_on_jump = 1 },
    [0x37] = { .parse = 2, .parse_on_jump = 1 },
    [0x38] = { .parse = 2, .parse_on_jump = 1 },
    [0x39] = { .parse = 2, .parse_on_jump = 1 },
    [0x3B] = { .parse = 2, .parse_on_jump = 1 },
    [0x3C] = { .parse = 2, .parse_on_jump = 1 },
    [0x3F] = { .parse = 2, .parse_on_jump = 1 },
    [0x40] = { .parse = 2, .parse_on_jump = 1 },
    [0x41] = { .parse = 2, .parse_on_jump = 1 },
    [0x42] = { .parse = 2, .parse_on_jump = 1 },
    [0x43] = { .parse = 2, .parse_on_jump = 1 },
    [0x44] = { .parse = 2, .parse_on_jump = 1 },
    /* places */
    [0xDC] = { .first = KEEP(WORD, map_and_how) },
    [0xDD] = { .first = KEEP(WORD, map_and_how) },
    [0xDE] = { .first = KEEP(WORD, map_and_how) },
    /* memory by address */
    [0x48] = { .first = KEEP_OPERAND_AT(4) },
    [0x49] = { .first = KEEP_OPERAND_AT(4) },
    [0x4C] = { .first = KEEP_OPERAND_AT(4) },
    [0x4D] = { .first = KEEP_OPERAND_AT(4) },
    /* object properties */
    [0x0D] = { .parse = 1 },
    [0x0E] = { .parse = 1 },
    [0x21] = { .parse = 4 },
    [0x22] = { .parse = 4 },
    [0x7C] = { .parse = 1 },
    [0x7D] = { .parse = 1 },
    [0x84] = { .parse = 2 },
    [0x87] = { .parse = 1 },
    [0x89] = { .parse = 1 },
    [0x8A] = { .parse = 1 },
    [0x8B] = { .parse = 2 },
    [0x8C] = { .parse = 2 },
    [0x8D] = { .parse = 3 },
    [0x8E] = { .parse = 1 },
    /* the screen */
    [0x47] = { .parse = 2 },
    [0xE3] = { .parse = 2 },
    [0xE6] = { .first = KEEP(BYTE, layers), .second = KEEP(WORD, drift) },
    [0xF0] = { .first = KEEP(BYTE, fade_to) },
    /* the story */
    [0x56] = { .parse = 4, .first = KEEP(BYTE, value) },
    [0x58] = { .parse = 4, .first = KEEP(WORD, value) },
    [0x59] = { .parse = 4, .first = KEEP(WORD, value) },
    [0x5A] = { .parse = 1 },
    [0x65] = { .parse = 3 },
    [0x66] = { .parse = 3 },
    /* variables, and the party leader */
    [0x20] = { .parse = 2 },
    [0x4F] = { .parse = 1, .first = KEEP(BYTE, value) },
    [0x50] = { .parse = 1, .first = KEEP(WORD, value) },
    [0x51] = { .parse = 2, .first = KEEP(WORD, value) },
    [0x52] = { .parse = 2, .first = KEEP(WORD, value) },
    [0x53] = { .parse = 3, .first = KEEP(WORD, value) },
    [0x54] = { .parse = 3, .first = KEEP(WORD, value) },
    [0x55] = { .parse = 1 },
    [0x5B] = { .parse = 3, .first = KEEP(BYTE, amount), .overflow = V_OF_ADDING },
    [0x5D] = { .parse = 3, .first = KEEP(WORD, amount), .overflow = V_OF_ADDING },
    [0x5E] = { .parse = 3, .first = KEEP(WORD, amount), .overflow = V_OF_ADDING },
    [0x5F] = { .parse = 3, .first = KEEP(BYTE, amount), .overflow = V_OF_SUBTRACTING },
    [0x60] = { .parse = 4, .first = KEEP(WORD, amount), .overflow = V_OF_SUBTRACTING },
    [0x61] = { .parse = 3, .first = KEEP(WORD, amount), .overflow = V_OF_SUBTRACTING },
    [0x63] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x64] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x67] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x69] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x6B] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x6F] = { .parse = 3 },
    [0x71] = { .parse = 2 },
    [0x72] = { .parse = 2 },
    [0x73] = { .parse = 2 },
    [0x75] = { .parse = 2 },
    [0x76] = { .parse = 2 },
    [0x77] = { .parse = 2 },
    [0x7F] = { .parse = 2 },
};

const OriginalLeaves *original_leaves(uint8_t opcode) { return &by_opcode[opcode]; }

static uint16_t parse_stop(const OriginalLeaves *l, uint16_t at, uint8_t operand)
{
    uint16_t y = (uint16_t)(at + operand);
    return l->shared_comparison ? (uint16_t)(y & 0x00FF) : y;
}

static void keep(GameState *g, unsigned cell, Kept k, const EventCommand *c, uint16_t at)
{
    switch (k.as) {
    case KEEP_BYTE: keep_byte(g, cell, (uint8_t)event_parameter(c, k.parameter)); break;
    case KEEP_WORD: keep_word(g, cell, (uint16_t)event_parameter(c, k.parameter)); break;
    case KEEP_OPERAND_OFFSET: keep_word(g, cell, (uint16_t)(at + k.operand)); break;
    default: break;
    }
}

void original_leaves_before(GameState *g, uint8_t opcode, uint16_t at, uint8_t jump, const EventCommand *c)
{
    const OriginalLeaves *l = original_leaves(opcode);
    event_leaves->at = at;
    event_leaves->y = parse_stop(l, at, l->parse);
    keep(g, KEPT_FIRST, l->first, c, at);
    keep(g, KEPT_SECOND, l->second, c, at);
    /* the comparisons' shared routine notes where the jump byte is, and
     * reads it back itself */
    if (l->shared_comparison)
        set_event_condition_resume(g, (uint16_t)(at + jump));
}

void original_leaves_after(const GameState *g, uint8_t opcode, const EventCommand *c)
{
    const OriginalLeaves *l = original_leaves(opcode);
    if (l->overflow == V_AS_IT_WAS)
        return;
    Width w = c->width;
    uint16_t amount = c->amount, now = variable(g, c->variable, w);
    bool subtracting = l->overflow == V_OF_SUBTRACTING;
    uint16_t before = subtracting ? (uint16_t)(now + amount) : (uint16_t)(now - amount);
    uint16_t added = subtracting ? (uint16_t)~amount : amount;
    leave_overflow(w == AS_BYTE ? overflows8((uint8_t)before, (uint8_t)added, (uint8_t)now)
                                : overflows16(before, added, now));
}

void original_leaves_on_jump(GameState *g, uint8_t opcode, uint16_t at, uint16_t from, uint8_t distance, bool back)
{
    const OriginalLeaves *l = original_leaves(opcode);
    uint16_t to = back ? (uint16_t)(from - distance) : (uint16_t)(from + distance);
    keep_word(g, KEPT_FIRST, distance);
    leave_overflow(back ? overflows16(from, (uint16_t)~distance, to) : overflows16(from, distance, to));
    leave_y(parse_stop(l, at, l->parse_on_jump));
}

#else
typedef int no_original_leaves;
#endif
