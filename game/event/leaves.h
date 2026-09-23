/* What the event commands leave behind for the original's sake: scaffolding
 * (original_effects.h), written only through ORIGINAL_EFFECT and never read
 * by the commands' logic.
 *
 * Three things, each established by evidence (docs/RECOMP.md, "The event
 * commands' scaffolding"; ct_dev --event-reads over the movie library):
 *
 * - Y. The original's handlers leave Y where their parse of the operands
 *   stopped. After the map-setup run of object 0 ($C0:595C, still the
 *   original's) and after the openings the engine runs at map setup, the last
 *   command's Y reaches the menus' dispatcher ($C2:8C36), which stores its
 *   low byte at $04C0; nothing the library runs reads it there. The value is
 *   static per opcode, so it is a column of the opcode table (event/script.h,
 *   `parse`); an operation whose parse stops elsewhere on one path says so
 *   with leave_y.
 * - V. The additions in a command (a jump's, the arithmetic's, the calls'
 *   index) leave their overflow in V, which the field loop carries into code
 *   it leaves through JMLs: the liveness analysis can't show it dead, so the
 *   engine's own entries hand it back. Nothing the library runs reads it.
 * - The scratch cells, event_scratch ($D9-$E4 of the field page): the
 *   original keeps a command's operands and intermediate values there. Every
 *   read the library makes of one is by the command that wrote it (or by a
 *   routine of that command's own, now C), so they are written for shadow
 *   mode's comparison alone. The cells are named here by the order the
 *   original uses them in, and each file names what its commands keep.
 *
 * The record the command running leaves its Y and V in belongs to whoever
 * runs it: the engine for a turn, or a command's registered entry
 * (event/bridge.c), which set `event_leaves` around the call. */
#ifndef CT_GAME_EVENT_LEAVES_H
#define CT_GAME_EVENT_LEAVES_H

#include <stdbool.h>
#include <stdint.h>

#include "original_effects.h"
#include "state.h"
#include "vars.h"

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
