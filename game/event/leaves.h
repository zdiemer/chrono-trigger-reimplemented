/* What the event commands leave behind for the original's sake: scaffolding
 * (original_effects.h), written only through ORIGINAL_EFFECT and never read
 * by the commands' logic.
 *
 * Three things, each established by evidence (docs/RECOMP.md, "Phase 3a: the
 * event language, and its scaffolding"; ct_dev --event-reads over the movie library):
 *
 * - Y. The original's handlers leave Y where their parse of the operands
 *   stopped. After the map-setup run of object 0 ($C0:595C, still the
 *   original's) and after the openings the engine runs at map setup, the last
 *   command's Y reaches the menus' dispatcher ($C2:8C36), which stores its
 *   low byte at $04C0; nothing the library runs reads it there. The value is
 *   static per opcode, so it is kept in a table of its own beside the
 *   language's (event/leaves.c, by opcode); an operation whose parse stops
 *   elsewhere on one path says so with leave_y or leave_parse_at.
 * - V. The additions in a command (a jump's, the arithmetic's, the calls'
 *   index) leave their overflow in V, which the field loop carries into code
 *   it leaves through JMLs: the liveness analysis can't show it dead, so the
 *   engine's own entries hand it back. Nothing the library runs reads it. An
 *   addition's V follows from its operands, so it is derived after the
 *   operation (event/leaves.c), not computed by it.
 * - The scratch cells, event_scratch ($D9-$E4 of the field page): the
 *   original keeps a command's operands and intermediate values there. Every
 *   read the library makes of one is by the command that wrote it (or by a
 *   routine of that command's own, now C), so they are written for shadow
 *   mode's comparison alone. The cells are named here by the order the
 *   original uses them in, and each file names what its commands keep; the
 *   cells a command's first operands go to are in the same table as Y.
 *
 * The record the command running leaves its Y and V in belongs to whoever
 * runs it: the engine for a turn, or a command's registered entry
 * (event/bridge.c), which set `event_leaves` around the call. A build without
 * the original's effects has none of this: no record, no table. */
#ifndef CT_GAME_EVENT_LEAVES_H
#define CT_GAME_EVENT_LEAVES_H

#include <stdbool.h>
#include <stdint.h>

#include "original_effects.h"
#include "state.h"
#include "vars.h"

#include "event/script.h"

typedef struct {
    uint16_t at;      /* the command's offset */
    uint16_t y;       /* the original's Y */
    bool overflow;    /* the original's V */
} EventLeaves;

extern EventLeaves *event_leaves;

/* The record the command about to run leaves in. */
static inline void leave_into(EventLeaves *record) { event_leaves = record; }

static inline void leave_y(uint16_t y) { event_leaves->y = y; }
/* The parse stopped at operand n instead of where the row says. */
static inline void leave_parse_at(unsigned n) { event_leaves->y = (uint16_t)(event_leaves->at + n); }
static inline void leave_overflow(bool v) { event_leaves->overflow = v; }

/* The original keeps a command's offset (plus n) as where it goes on, and
 * reads it back itself. */
static inline void keep_resume_point(GameState *g, unsigned n)
{
    set_event_saved_position(g, (uint16_t)(event_leaves->at + n));
}

/* The V flag an 8- or 16-bit addition a + b = sum leaves (a subtraction
 * adds the complement and a carry): signed overflow. */
static inline bool overflows8(uint8_t a, uint8_t b, uint8_t sum) { return ((a ^ sum) & (b ^ sum) & 0x80) != 0; }
static inline bool overflows16(uint16_t a, uint16_t b, uint16_t sum)
{
    return ((a ^ sum) & (b ^ sum) & 0x8000) != 0;
}

/* The V flag the original leaves after it computes a + b, or a - b. */
static inline void leave_overflow_of_adding16(uint16_t a, uint16_t b)
{
    leave_overflow(overflows16(a, b, (uint16_t)(a + b)));
}

static inline void leave_overflow_of_subtracting8(uint8_t a, uint8_t b)
{
    leave_overflow(overflows8(a, (uint8_t)~b, (uint8_t)(a - b)));
}

static inline void leave_overflow_of_subtracting16(uint16_t a, uint16_t b)
{
    leave_overflow(overflows16(a, (uint16_t)~b, (uint16_t)(a - b)));
}

/* ---- What each command leaves, by opcode (event/leaves.c) --------------------------------
 *
 * The part that is the same every time a command runs, which the engine and
 * the entries reproduce around the operation: a table parallel to the
 * language's, which the build without the original's effects leaves out. */

/* A value the original keeps in a scratch cell before the operation runs: one
 * of the operation's parameters, as a byte or a word, or where one of the
 * command's operands is. */
typedef enum { KEEP_NOTHING, KEEP_BYTE, KEEP_WORD, KEEP_OPERAND_OFFSET } KeptAs;

typedef struct {
    KeptAs as;
    EventParameter parameter;   /* KEEP_BYTE, KEEP_WORD */
    uint8_t operand;            /* KEEP_OPERAND_OFFSET */
} Kept;

/* The V flag the command leaves: as it found it, or the overflow of adding
 * the amount to the variable (or taking it away), in the command's width. */
typedef enum { V_AS_IT_WAS, V_OF_ADDING, V_OF_SUBTRACTING } OverflowLeft;

typedef struct {
    uint8_t parse;            /* the operand the original's parse stops at, going on: its Y */
    uint8_t parse_on_jump;    /* ... and taking its jump */
    bool shared_comparison;   /* made by the original's comparison routine, which notes where the jump
                               * byte is and runs with 8-bit index registers (Y keeps its low byte) */
    Kept first, second;       /* what it keeps in its first two scratch cells */
    OverflowLeft overflow;
} OriginalLeaves;

const OriginalLeaves *original_leaves(uint8_t opcode);

/* Around a command the language's table runs (event/engine.c): before the
 * operation, Y where the parse stops and what is kept in the scratch cells
 * (from the command's parameters; `jump` the row's jump operand); after it,
 * the V an addition leaves, which follows from the amount and the variable as
 * it now is; and on the command's jump, what the jump leaves. */
void original_leaves_before(GameState *g, uint8_t opcode, uint16_t at, uint8_t jump, const EventCommand *c);
void original_leaves_after(const GameState *g, uint8_t opcode, const EventCommand *c);
void original_leaves_on_jump(GameState *g, uint8_t opcode, uint16_t at, uint16_t from, uint8_t distance, bool back);

/* The scratch cells, as byte offsets into event_scratch: six words, which the
 * original fills in this order, a command's first value first. */
enum { KEPT_FIRST = 0, KEPT_SECOND = 2, KEPT_THIRD = 4, KEPT_FOURTH = 6, KEPT_FIFTH = 8, KEPT_SIXTH = 10 };

static inline void keep_byte(GameState *g, unsigned cell, uint8_t v) { set_event_scratch_byte(g, cell, v); }

static inline void keep_word(GameState *g, unsigned cell, uint16_t v)
{
    set_event_scratch_byte(g, cell, (uint8_t)v);
    set_event_scratch_byte(g, cell + 1, (uint8_t)(v >> 8));
}

/* The high byte of a kept word, on its own. */
static inline void keep_high_byte(GameState *g, unsigned cell, uint8_t v) { set_event_scratch_byte(g, cell + 1, v); }

#endif
