/* How a script finds its way: jumps, the conditions it branches on, and
 * waiting.
 *
 * A condition goes on after itself when it holds and otherwise jumps ahead by
 * its last operand; a jump goes forward or back by its own. The engine does
 * the jumping (event/engine.c); the operations here only say whether a
 * condition holds, or how long to wait. The conditions on the story, the
 * party, dialog and the controller are with those, in their own files. */
#include "event/bridge.h"
#include "event/leaves.h"
#include "event/variables.h"

// $C0:62B5
/* Jump: to another part of the script, forward or back by the distance in
 * the command (the row says which). */
static EventFlow jump(GameState *g)
{
    (void)g;
    return take_jump();
}
EVENT_OPERATION_TAKING_NOTHING(jump)

/* ---- Comparing values -------------------------------------------------------------------- */

/* How two values compare: the scripts' operators, shared by every comparison. */
typedef enum {
    EQUAL, NOT_EQUAL, GREATER, LESS, AT_LEAST, AT_MOST, SHARE_A_BIT, EITHER_NONZERO,
    BYTE_OPERATORS = 8,   /* the word comparisons have all but the last */
    WORD_OPERATORS = 7,
} Comparison;

/* The operator byte: the original turns it into its table's index with an
 * 8-bit doubling, which drops bit 7, so a script's operator means the same
 * with or without it. */
static Comparison operator_in(uint8_t operator_byte) { return (Comparison)(operator_byte & 0x7F); }

static bool holds(Comparison how, uint16_t left, uint16_t right)
{
    switch (how) {
    case EQUAL: return left == right;
    case NOT_EQUAL: return left != right;
    case GREATER: return left > right;
    case LESS: return left < right;
    case AT_LEAST: return left >= right;
    case AT_MOST: return left <= right;
    case SHARE_A_BIT: return (left & right) != 0;
    default: return (left | right) != 0;
    }
}

// $C0:62DE
/* If a variable compares with a number or another variable. */
static EventFlow if_compares(GameState *g, uint16_t left, uint16_t right, uint8_t comparison)
{
    (void)g;
    return unless_jump(holds(operator_in(comparison), left, right));
}
EVENT_OPERATION(if_compares, left, right, comparison)

/* What the original keeps of a story comparison: the byte compared, the
 * value it is compared with and the operator. */
static void keep_story_comparison(GameState *g, uint8_t byte, uint8_t value, uint8_t how)
{
    keep_byte(g, KEPT_FIRST, byte);
    keep_byte(g, KEPT_SECOND, value);
    keep_byte(g, KEPT_THIRD, how);
}

// $C0:63E6
/* If a byte of the story -- an event seen, a chest opened -- compares with a
 * value. The operator's top bit says the byte is on the story's second
 * page. */
static EventFlow if_story(GameState *g, uint8_t offset, uint16_t value, uint8_t comparison)
{
    uint8_t byte = story_byte(g, story_offset_in(offset, comparison));
    ORIGINAL_EFFECT(keep_story_comparison(g, byte, (uint8_t)value, operator_in(comparison)));
    return unless_jump(holds(operator_in(comparison), byte, value));
}
EVENT_OPERATION(if_story, offset, value, comparison)

// $C0:6442
/* If the story hasn't yet reached a point: its progress counter is below it. */
static EventFlow if_story_before(GameState *g, uint8_t progress, uint8_t point)
{
    (void)g;
    return unless_jump(progress < point);
}
EVENT_OPERATION(if_story_before, progress, point)

/* ---- Where an object is ------------------------------------------------------------------- */

// $C0:662C
/* If an object is in view (the field decides that as it draws). */
static EventFlow if_in_view(GameState *g, FieldObject object)
{
    return unless_jump(object_view(g, object) != OUT_OF_VIEW);
}
EVENT_OPERATION(if_in_view, object)

// $C0:6655
/* If an object stands well on screen, clear of its edges: where a cutscene
 * can start without the camera moving. */
static EventFlow if_well_on_screen(GameState *g, FieldObject object)
{
    ORIGINAL_EFFECT(keep_screen_edges(g));
    return unless_jump(tile_well_on_screen(g, object_tile(g, object)));
}
EVENT_OPERATION(if_well_on_screen, object)

/* ---- Waiting ------------------------------------------------------------------------------ */

// $C0:2F96
/* Wait a number of frames: the object's turn ends here each frame until they
 * have passed. A count already past the wait, left by a longer one, starts it
 * again at once. */
static EventFlow wait_frames(GameState *g, FieldObject self, uint8_t frames)
{
    uint8_t waited = event_object_wait_frames(g, self);
    if (waited < frames) {
        set_event_object_wait_frames(g, self, (uint8_t)(waited + 1));
        return stay();
    }
    set_event_object_wait_frames(g, self, 0);
    return waited == frames ? next_command() : run_again();
}
EVENT_OPERATION(wait_frames, self, frames)

// $C0:353F
/* Give up the rest of this frame's turn; the script goes on next frame. */
static EventFlow wait_for_next_frame(GameState *g)
{
    (void)g;
    return end_turn();
}
EVENT_OPERATION_TAKING_NOTHING(wait_for_next_frame)

// $C0:3543
/* Stop here for good: the object's script runs no further. */
static EventFlow stop_here(GameState *g)
{
    (void)g;
    return stay();
}
EVENT_OPERATION_TAKING_NOTHING(stop_here)

// $C0:2FFD
/* Let the object behave as its state says for one step (the command after
 * it, event_cmd_behave in movement.c), then go on. The original calls that
 * command's handler and moves its result past its own opcode. */
void event_cmd_behave_once(Snes *s)
{
    if (!event_call_bridge(s, 0xC02FFD, 0xC03002, event_cmd_behave))
        return;
    s->cpu.x++;
    m_rts(s);
}

/* The comparisons' operator must be one of their table's (the guard's part):
 * the byte comparisons' eight, the word comparisons' seven. */
static bool compares_bytes(const GameState *g, uint8_t comparison)
{
    (void)g;
    return operator_in(comparison) < BYTE_OPERATORS;
}
EVENT_CHECK(compares_bytes, comparison)

static bool compares_words(const GameState *g, uint8_t comparison)
{
    (void)g;
    return operator_in(comparison) < WORD_OPERATORS;
}
EVENT_CHECK(compares_words, comparison)

/* A condition's jump is its last operand. */
EVENT_OPCODES(event_flow_opcodes,
    { 0x10, OPERATION(jump), .length = 2, .jump = 1 },
    { 0x11, OPERATION(jump), .length = 2, .jump = 1, .back = true },

    { 0x12, OPERATION(if_compares), .left = VARIABLE_BYTE(1), .right = OPERAND(2), .comparison = OPERAND(3),
      .length = 5, .jump = 4, CHECK(compares_bytes) },
    { 0x13, OPERATION(if_compares), .left = VARIABLE_WORD(1), .right = OPERAND_WORD(2), .comparison = OPERAND(4),
      .length = 6, .jump = 5, CHECK(compares_words) },
    { 0x14, OPERATION(if_compares), .left = VARIABLE_BYTE(1), .right = VARIABLE_BYTE(2), .comparison = OPERAND(3),
      .length = 5, .jump = 4, CHECK(compares_bytes) },
    { 0x15, OPERATION(if_compares), .left = VARIABLE_WORD(1), .right = VARIABLE_WORD(2), .comparison = OPERAND(3),
      .length = 5, .jump = 4, CHECK(compares_words) },
    { 0x16, OPERATION(if_story), .offset = OPERAND(1), .value = OPERAND(2), .comparison = OPERAND(3), .length = 5,
      .jump = 4, CHECK(compares_bytes) },
    { 0x18, OPERATION(if_story_before), .progress = STORY_PROGRESS, .point = OPERAND(1), .length = 3, .jump = 2 },

    { 0x27, OPERATION(if_in_view), .object = OBJECT(1), .length = 3, .jump = 2 },
    { 0x28, OPERATION(if_well_on_screen), .object = OBJECT(1), .length = 3, .jump = 2 },

    { 0xAD, OPERATION(wait_frames), .frames = OPERAND(1), .length = 2 },
    { 0xB9, OPERATION(wait_frames), .frames = CONSTANT(4), .length = 1 },
    { 0xBA, OPERATION(wait_frames), .frames = CONSTANT(8), .length = 1 },
    { 0xBC, OPERATION(wait_frames), .frames = CONSTANT(16), .length = 1 },
    { 0xBD, OPERATION(wait_frames), .frames = CONSTANT(32), .length = 1 },
    { 0xAF, .length = 1 },
    { 0xB1, OPERATION(wait_for_next_frame), .length = 1 },
    { 0xB2, OPERATION(stop_here), .length = 1 });
