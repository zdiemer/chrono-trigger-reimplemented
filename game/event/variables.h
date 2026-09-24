/* The scripts' own memory: their variables, and the story flags.
 *
 * Variables are the scripts' working storage for the map they run on: 256
 * words (event_variables), which a script names by number and reads and
 * writes as a byte (the low one; the high byte stays) or as a whole word.
 *
 * The story memory (event_memory, at $7F:0000) is what the game remembers of
 * the story as it goes: which events have happened, which chests are open,
 * who has joined. Scripts read and write it by the byte, the word or the
 * single flag, at an offset of their choosing; the second of its first two
 * pages is reached by a flag in the comparison's operator. */
#ifndef CT_GAME_EVENT_VARIABLES_H
#define CT_GAME_EVENT_VARIABLES_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"
#include "vars.h"

typedef uint8_t EventVariable;   /* a variable's number */

static inline uint8_t variable_byte(const GameState *g, EventVariable v) { return (uint8_t)event_variables(g, v); }
static inline uint16_t variable_word(const GameState *g, EventVariable v) { return event_variables(g, v); }
static inline void set_variable_word(GameState *g, EventVariable v, uint16_t value) { set_event_variables(g, v, value); }

static inline void set_variable_byte(GameState *g, EventVariable v, uint8_t value)
{
    set_event_variables(g, v, (uint16_t)((event_variables(g, v) & 0xFF00) | value));
}

/* Most variable commands come in a byte and a word form. */
typedef enum { AS_BYTE, AS_WORD } Width;

static inline uint16_t variable(const GameState *g, EventVariable v, Width w)
{
    return w == AS_BYTE ? variable_byte(g, v) : variable_word(g, v);
}

static inline void set_variable(GameState *g, EventVariable v, uint16_t value, Width w)
{
    if (w == AS_BYTE)
        set_variable_byte(g, v, (uint8_t)value);
    else
        set_variable_word(g, v, value);
}

/* ---- The story ------------------------------------------------------------------------- */

typedef uint16_t StoryOffset;   /* where in the story memory */

enum { STORY_SECOND_PAGE = 0x0100 };

/* A story byte a command names by its offset in a page: a flag with the
 * offset (bit 7 of a flag or an operator byte) says it is on the second. */
enum { ON_THE_SECOND_PAGE = 0x80 };

static inline StoryOffset story_offset_in(uint8_t offset, uint8_t marked)
{
    return (StoryOffset)((marked & ON_THE_SECOND_PAGE ? STORY_SECOND_PAGE : 0) | offset);
}

/* Whether a byte or a word at `at` is inside the story memory. The commands
 * that name a story byte by a 16-bit offset could name any in the bank, and
 * past the memory are the engine's other tables; no script names one there
 * (every offset the game's scripts give is below $1EE), so those are the
 * original's to run. */
static inline bool story_holds(StoryOffset at, Width width)
{
    return (unsigned)at + (width == AS_WORD ? 2u : 1u) <= EVENT_MEMORY_BYTES;
}

static inline uint8_t story_byte(const GameState *g, StoryOffset at) { return event_memory_byte(g, at); }
static inline void set_story_byte(GameState *g, StoryOffset at, uint8_t v) { set_event_memory_byte(g, at, v); }

static inline uint16_t story_word(const GameState *g, StoryOffset at)
{
    return (uint16_t)(event_memory_byte(g, at) | event_memory_byte(g, (unsigned)at + 1) << 8);
}

static inline void set_story_word(GameState *g, StoryOffset at, uint16_t v)
{
    set_event_memory_byte(g, at, (uint8_t)v);
    set_event_memory_byte(g, (unsigned)at + 1, (uint8_t)(v >> 8));
}

/* The story's progress counter: the first byte of the story memory, which the
 * scripts compare against and set as the story moves on. */
static inline uint8_t story_progress(const GameState *g) { return story_byte(g, 0); }
static inline void set_story_progress(GameState *g, uint8_t v) { set_story_byte(g, 0, v); }

#endif
