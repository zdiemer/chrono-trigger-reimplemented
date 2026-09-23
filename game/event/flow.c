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
static EventFlow jump(GameState *g, const EventArgs *a)
{
    (void)g, (void)a;
    return take_jump();
}

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
static Comparison comparison(uint8_t operator_byte) { return (Comparison)(operator_byte & 0x7F); }

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
static EventFlow if_compares(GameState *g, uint16_t left, uint16_t right, uint8_t operator_byte)
{
    (void)g;
    return unless_jump(holds(comparison(operator_byte), left, right));
}


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
 * value. */
static EventFlow if_story(GameState *g, uint8_t offset, uint8_t value, uint8_t operator_byte)
{
    uint8_t byte = story_byte(g, story_offset_in(offset, operator_byte));
    ORIGINAL_EFFECT(keep_story_comparison(g, byte, value, comparison(operator_byte)));
    return unless_jump(holds(comparison(operator_byte), byte, value));
}

// $C0:6442
/* If the story hasn't yet reached a point: its progress counter is below it. */
static EventFlow if_story_before(GameState *g, uint8_t progress, uint8_t point)
{
    (void)g;
    return unless_jump(progress < point);
}

/* ---- Where an object is ------------------------------------------------------------------- */

// $C0:662C
/* If an object is in view (the field decides that as it draws). */
static EventFlow if_in_view(GameState *g, EventObject object)
{
    return unless_jump(field_object_in_view(g, object) != 0);
}

/* How far in from the screen's left and top edges an object must stand, in
 * tiles, to be well on screen: past the first two, short of the last two or
 * three. The field keeps the edges in 8-pixel tiles, and objects in 16. */
enum { MARGIN = 2, ACROSS = 14, DOWN = 13 };

static bool well_inside(uint8_t tile, uint8_t edge, uint8_t extent)
{
    uint8_t in = (uint8_t)(tile - edge);
    return in >= MARGIN && in < extent;
}

/* What the original keeps of the screen's edges, in 16-pixel tiles. */
static void keep_screen_edges(GameState *g, uint8_t left, uint8_t top)
{
    keep_byte(g, KEPT_SECOND, left);
    keep_byte(g, KEPT_THIRD, top);
}

// $C0:6655
/* If an object stands well on screen, clear of its edges: where a cutscene
 * can start without the camera moving. */
static EventFlow if_well_on_screen(GameState *g, EventObject object)
{
    uint8_t left = (uint8_t)(field_screen_left(g) >> 1), top = (uint8_t)(field_screen_top(g) >> 1);
    ORIGINAL_EFFECT(keep_screen_edges(g, left, top));
    uint8_t x = field_object_x_tile(g, object), y = field_object_y_tile(g, object);
    bool inside = well_inside(x, left, ACROSS) && well_inside(y, top, DOWN);
    if (inside)
        ORIGINAL_EFFECT(leave_overflow_of_subtracting8(y, top));
    return unless_jump(inside);
}

/* ---- Waiting ------------------------------------------------------------------------------ */

// $C0:2F96
/* Wait a number of frames: the object's turn ends here each frame until they
 * have passed. A count already past the wait, left by a longer one, starts it
 * again at once. */
static EventFlow wait_frames(GameState *g, EventObject self, uint8_t frames)
{
    uint8_t waited = event_object_wait_frames(g, self);
    if (waited < frames) {
        set_event_object_wait_frames(g, self, (uint8_t)(waited + 1));
        return stay();
    }
    set_event_object_wait_frames(g, self, 0);
    return waited == frames ? next_command() : run_again();
}

// $C0:353F
/* Give up the rest of this frame's turn; the script goes on next frame. */
static EventFlow wait_for_next_frame(GameState *g, const EventArgs *a)
{
    (void)g, (void)a;
    return end_turn();
}

// $C0:3543
/* Stop here for good: the object's script runs no further. */
static EventFlow stop_here(GameState *g, const EventArgs *a)
{
    (void)g, (void)a;
    return stay();
}

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

static EventFlow run_if_compares(GameState *g, const EventArgs *a)
{
    return if_compares(g, a->arg[0], a->arg[1], a->arg[2]);
}
static EventFlow run_if_story(GameState *g, const EventArgs *a) { return if_story(g, a->arg[0], a->arg[1], a->arg[2]); }
static EventFlow run_if_story_before(GameState *g, const EventArgs *a)
{
    return if_story_before(g, a->arg[0], a->arg[1]);
}
static EventFlow run_if_in_view(GameState *g, const EventArgs *a) { return if_in_view(g, a->arg[0]); }
static EventFlow run_if_well_on_screen(GameState *g, const EventArgs *a) { return if_well_on_screen(g, a->arg[0]); }
static EventFlow run_wait_frames(GameState *g, const EventArgs *a) { return wait_frames(g, a->self, a->arg[0]); }

/* The comparisons' operator must be one of their table's (the guard's part):
 * the byte comparisons' eight, the word comparisons' seven. */
static bool byte_operator(uint8_t op) { return comparison(op) < BYTE_OPERATORS; }
static bool word_operator(uint8_t op) { return comparison(op) < WORD_OPERATORS; }
static bool operand3_compares_bytes(const GameState *g, uint16_t at)
{
    return byte_operator(script_byte(g, (uint16_t)(at + 3)));
}
static bool operand3_compares_words(const GameState *g, uint16_t at)
{
    return word_operator(script_byte(g, (uint16_t)(at + 3)));
}
static bool operand4_compares_words(const GameState *g, uint16_t at)
{
    return word_operator(script_byte(g, (uint16_t)(at + 4)));
}

/* Columns past the operands: the jump (a condition's is its last operand),
 * and what the original leaves (event/leaves.h): its parse position, the
 * compared values it keeps, and for $12-$16 the comparison routine they
 * share. */
EVENT_OPCODES(event_flow_opcodes,
    { 0x10, jump, { { 0 } }, 2, 1, .routine = 0xC062B5 },
    { 0x11, jump, { { 0 } }, 2, 1, .back = true, .routine = 0xC062CB },

    { 0x12, run_if_compares, { VARIABLE_BYTE(1), BYTE(2), BYTE(3) }, 5, 4, .accepts = operand3_compares_bytes,
      .routine = 0xC062DE },
    { 0x13, run_if_compares, { VARIABLE_WORD(1), WORD(2), BYTE(4) }, 6, 5, .accepts = operand4_compares_words,
      .routine = 0xC06313 },
    { 0x14, run_if_compares, { VARIABLE_BYTE(1), VARIABLE_BYTE(2), BYTE(3) }, 5, 4,
      .accepts = operand3_compares_bytes, .routine = 0xC06361 },
    { 0x15, run_if_compares, { VARIABLE_WORD(1), VARIABLE_WORD(2), BYTE(3) }, 5, 4,
      .accepts = operand3_compares_words, .routine = 0xC063A4 },
    { 0x16, run_if_story, { BYTE(1), BYTE(2), BYTE(3) }, 5, 4, .accepts = operand3_compares_bytes,
      .routine = 0xC063E6 },
    { 0x18, run_if_story_before, { STORY_PROGRESS, BYTE(1) }, 3, 2, .routine = 0xC06442 },

    { 0x27, run_if_in_view, { OBJECT(1) }, 3, 2, .routine = 0xC0662C },
    { 0x28, run_if_well_on_screen, { OBJECT(1) }, 3, 2, .routine = 0xC06655 },

    { 0xAD, run_wait_frames, { BYTE(1) }, 2, .routine = 0xC02F96 },
    { 0xB9, run_wait_frames, { CONSTANT(4) }, 1, .routine = 0xC02FC6 },
    { 0xBA, run_wait_frames, { CONSTANT(8) }, 1, .routine = 0xC02FCA },
    { 0xBC, run_wait_frames, { CONSTANT(16) }, 1, .routine = 0xC02FD2 },
    { 0xBD, run_wait_frames, { CONSTANT(32) }, 1, .routine = 0xC02FCE },
    { 0xAF, .bridge = event_cmd_behave_once, .length = 1, .routine = 0xC02FFD },
    { 0xB1, wait_for_next_frame, { { 0 } }, 1, .routine = 0xC0353F },
    { 0xB2, stop_here, { { 0 } }, 1, .routine = 0xC03543 });
