/* The event script protocol: what a command reads, and what it tells the
 * interpreter to do next.
 *
 * Every map object runs a script in the buffer at $7F:2001 (event_script),
 * one command at a time. A command is a byte and its operands; its handler
 * reads them, acts on the object whose script is running or on the script's
 * variables, and says where the script goes on and whether the object's turn
 * goes on this frame. That is the whole protocol, and every handler in
 * game/event/ is a function over it:
 *
 *     EventStep handler(GameState *g, const EventCommand *c);
 *
 * EventCommand says where the command is; the operands are read by their
 * position in it (`operand(g, c, 1)` is the byte after the command byte), and
 * objects, party members and variables named by an operand through the
 * helpers below. EventStep is the result: where the script goes on (`next`)
 * and whether the turn does (`go_on`).
 *
 * The step carries two more things, because the original's engine hands them
 * on to its callers and the liveness analysis can't show that nobody reads
 * them: `cursor`, where the command's parse of its operands stopped (the
 * original's Y), which the map-setup run of object 0 ($C0:595C) passes on to
 * the menus' dispatcher at $C2:8C36, which keeps it at $04C0; and `overflow`,
 * the V flag an addition in the command leaves, which the field loop carries
 * into code it leaves through JMLs. Neither is part of what a command means:
 * a build without the original's engine drops both. The translation to and
 * from the original's registers is in game/event/bridge.c.
 *
 * Offsets in the script are 16-bit and wrap, as the original's index
 * registers do; a word read at the last offset reads its second byte past it. */
#ifndef CT_GAME_EVENT_SCRIPT_H
#define CT_GAME_EVENT_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"
#include "vars.h"

/* ---- The script -------------------------------------------------------------------- */

static inline uint8_t script_byte(const GameState *g, uint16_t at) { return event_script_byte(g, at); }

static inline uint16_t script_word(const GameState *g, uint16_t at)
{
    return (uint16_t)(event_script_byte(g, at) | event_script_byte(g, (unsigned)at + 1) << 8);
}

/* A command, as its handler reads it. */
typedef struct {
    uint16_t at;   /* the offset of the command byte */
} EventCommand;

/* The byte `n` bytes after the command byte: operand n. */
static inline uint8_t operand(const GameState *g, const EventCommand *c, unsigned n)
{
    return script_byte(g, (uint16_t)(c->at + n));
}

static inline uint16_t operand_word(const GameState *g, const EventCommand *c, unsigned n)
{
    return script_word(g, (uint16_t)(c->at + n));
}

/* ---- What happens next ------------------------------------------------------------- */

enum { OVERFLOW_KEPT = -1 };

typedef struct {
    uint16_t next;     /* the offset the script goes on at */
    bool go_on;        /* the object's turn goes on: run `next` this frame (the original's carry) */
    uint16_t cursor;   /* where the command's parse stopped (the original's Y; see above) */
    int8_t overflow;   /* the V flag an addition leaves, or OVERFLOW_KEPT (see above) */
} EventStep;

static inline EventStep step_at(uint16_t next, bool go_on, uint16_t cursor)
{
    return (EventStep){ next, go_on, cursor, OVERFLOW_KEPT };
}

/* Go on with the command `length` bytes on, this frame. The parse stopped at
 * operand `parsed` (0: the command byte). */
static inline EventStep next_command(const EventCommand *c, uint16_t length, uint16_t parsed)
{
    return step_at((uint16_t)(c->at + length), true, (uint16_t)(c->at + parsed));
}

/* End the object's turn; its script resumes `length` bytes on next frame. */
static inline EventStep end_turn(const EventCommand *c, uint16_t length, uint16_t parsed)
{
    return step_at((uint16_t)(c->at + length), false, (uint16_t)(c->at + parsed));
}

/* End the object's turn on this command, which runs again next frame. */
static inline EventStep wait_here(const EventCommand *c, uint16_t parsed)
{
    return end_turn(c, 0, parsed);
}

/* The signed overflow of a 16-bit addition or subtraction, and of an 8-bit one. */
static inline bool overflows16(uint16_t a, uint16_t b, uint16_t result)
{
    return ((a ^ result) & (b ^ result) & 0x8000) != 0;
}

static inline bool overflows8(uint8_t a, uint8_t b, uint8_t result)
{
    return ((a ^ result) & (b ^ result) & 0x80) != 0;
}

static inline EventStep with_overflow(EventStep st, bool overflow)
{
    st.overflow = overflow ? 1 : 0;
    return st;
}

/* ---- The running object, other objects, the party ---------------------------------- */

/* Scripts and the engine name an object by its slot doubled, the offset of its
 * cells in the object tables; the tables' accessors take the slot. The guards
 * (bridge.c) accept only even ones, which are all the game makes. */
static inline unsigned slot_of(uint16_t doubled) { return doubled >> 1; }

/* The object whose script is running. */
static inline unsigned current_object(const GameState *g) { return slot_of(field_current_object(g)); }

/* The object operand n names. */
static inline unsigned object_operand(const GameState *g, const EventCommand *c, unsigned n)
{
    return slot_of(operand(g, c, n));
}

/* The object of party member operand n (doubled, as the scripts write it).
 * Most commands take it as it is, whatever it holds; the calls first check
 * there is such a member (the entry is negative when there isn't). */
static inline unsigned member_object(const GameState *g, const EventCommand *c, unsigned n)
{
    return slot_of(field_party_objects(g, slot_of(operand(g, c, n))));
}

static inline bool party_has_member(const GameState *g, const EventCommand *c, unsigned n)
{
    return !(field_party_objects(g, slot_of(operand(g, c, n))) & 0x80);
}

/* ---- Variables and memory ------------------------------------------------------------ */

/* The variable operand n names, and its value as a byte or a word. The 8-bit
 * commands read and write the low byte alone. */
static inline uint8_t variable_operand(const GameState *g, const EventCommand *c, unsigned n)
{
    return operand(g, c, n);
}

static inline uint8_t var8(const GameState *g, uint8_t v) { return (uint8_t)event_variables(g, v); }
static inline uint16_t var16(const GameState *g, uint8_t v) { return event_variables(g, v); }
static inline void set_var16(GameState *g, uint8_t v, uint16_t value) { set_event_variables(g, v, value); }

static inline void set_var8(GameState *g, uint8_t v, uint8_t value)
{
    set_event_variables(g, v, (uint16_t)((event_variables(g, v) & 0xFF00) | value));
}

/* The event memory at $7F:0000 (the story flags), by a 16-bit offset. */
static inline uint8_t memory8(const GameState *g, uint16_t at) { return event_memory_byte(g, at); }
static inline void set_memory8(GameState *g, uint16_t at, uint8_t v) { set_event_memory_byte(g, at, v); }

static inline uint16_t memory16(const GameState *g, uint16_t at)
{
    return (uint16_t)(event_memory_byte(g, at) | event_memory_byte(g, (unsigned)at + 1) << 8);
}

static inline void set_memory16(GameState *g, uint16_t at, uint16_t v)
{
    set_event_memory_byte(g, at, (uint8_t)v);
    set_event_memory_byte(g, (unsigned)at + 1, (uint8_t)(v >> 8));
}

/* ---- The commands' scratch cells ------------------------------------------------------
 *
 * $D9-$E4 of the field page (event_scratch): each command keeps its operands
 * and intermediate values there, in words at these offsets, and other code
 * may read them, so the handlers still write what the original writes. */
enum { SCRATCH_D9 = 0, SCRATCH_DA = 1, SCRATCH_DB = 2, SCRATCH_DC = 3, SCRATCH_DD = 4, SCRATCH_DF = 6,
       SCRATCH_E1 = 8, SCRATCH_E3 = 10 };

static inline uint8_t scratch8(const GameState *g, unsigned at) { return event_scratch_byte(g, at); }
static inline void keep8(GameState *g, unsigned at, uint8_t v) { set_event_scratch_byte(g, at, v); }

static inline uint16_t scratch16(const GameState *g, unsigned at)
{
    return (uint16_t)(event_scratch_byte(g, at) | event_scratch_byte(g, at + 1) << 8);
}

static inline void keep16(GameState *g, unsigned at, uint16_t v)
{
    set_event_scratch_byte(g, at, (uint8_t)v);
    set_event_scratch_byte(g, at + 1, (uint8_t)(v >> 8));
}

/* ---- Jumps ---------------------------------------------------------------------------- */

/* Go on this frame `distance` bytes past (or before) the operand at `from`,
 * the jump byte: every jump in the language is relative to it. The distance
 * is kept in the scratch cell, and the addition leaves its overflow. */
static inline EventStep jump_from(GameState *g, const EventCommand *c, uint16_t from, uint8_t distance, bool back,
                                  uint16_t parsed)
{
    uint16_t base = (uint16_t)(c->at + from);
    uint16_t target = back ? (uint16_t)(base - distance) : (uint16_t)(base + distance);
    keep16(g, SCRATCH_D9, distance);
    EventStep st = step_at(target, true, (uint16_t)(c->at + parsed));
    return with_overflow(st, back ? overflows16(base, (uint16_t)~distance, target) : overflows16(base, distance, target));
}

/* A condition: if it holds, go on with the command `length` bytes on;
 * otherwise jump forward by the distance at operand `length - 1`, the last. */
static inline EventStep branch_unless(GameState *g, const EventCommand *c, bool holds, uint16_t length,
                                      uint16_t parsed)
{
    if (holds)
        return next_command(c, length, parsed);
    uint16_t from = (uint16_t)(length - 1);
    return jump_from(g, c, from, operand(g, c, from), false, parsed);
}

#endif
