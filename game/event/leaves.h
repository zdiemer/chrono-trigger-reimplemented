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
 *   low byte at $04C0; nothing the library runs reads it there.
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
 * Y and V are the machine's, and follow from the command: event/leaves.c
 * works them out around the operation, from its parameters, what it did and
 * the state before and after, in a table parallel to the language's (the
 * cells a command's first operands go to are in the same table). Three
 * things the moves say themselves, where they know them and nothing after
 * them does: how a move set off (the step's start is the field's, and leaves
 * nothing in the state that says whether it stepped, turned or stayed),
 * whether it looked for objects in the way on the way there, and the
 * walking animation its lining up picked (a path the move took, which the
 * state after it shares with others). The record they go into is the
 * runner's (event/bridge.c), which the derivations set up around the
 * operation; an operation never reads it, nor the command's offset. A build
 * without the original's effects has none of this. */
#ifndef CT_GAME_EVENT_LEAVES_H
#define CT_GAME_EVENT_LEAVES_H

#include <stdbool.h>
#include <stdint.h>

#include "original_effects.h"
#include "state.h"
#include "vars.h"

#include "event/contact.h"
#include "event/script.h"
#include "field/scratch.h"

/* The Y and V a command leaves the original's code after it. */
typedef struct {
    uint16_t y;
    bool overflow;
} EventLeaves;

/* Run a command by its row as event_perform does, with what the original
 * leaves worked out around it into `left` (event/leaves.c). */
EventStep event_perform_leaving(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at, EventLeaves *left);

/* What an operation says itself, where only it knows (above): a move's
 * setting off from the command at the offset the runner has (walking or
 * sliding), the objects in the way looked for, and a walking animation's
 * overflow. */
void leave_set_off(GameState *g, FieldObject self, StepStart start, bool walking);
void leave_in_the_way_check(GameState *g, FieldObject self);
void leave_walk_overflow(const GameState *g, FieldObject self);

/* What the leader's talk and touch leave (event/contact.c), which event
 * $B0's derivation takes for the touch too: working out a place among the
 * calls' leaves the overflow clear, and the touch leaves Y the object where
 * it looked to push it. */
bool event_place_worked_out_overflow(bool worked_out, bool before);
uint16_t event_touch_y(const GameState *g, bool looked_to_push, uint16_t before);

/* What the confirm button leaves (event/contact.c, event/chests.c): the
 * overflow of the last sum on its way -- the search's measures, a talk's
 * place, a chest's lookup and its flag's test -- which the field's main loop
 * carries on. Recorded as it goes, and taken by the entry. */
void event_forget_overflow(void);
void event_leave_overflow(bool overflow);
bool event_overflow_left(bool before);

/* The original keeps a command's offset (or where it goes on) in
 * event_saved_position across its calls, and reads it back itself. */
static inline void event_keep_offset(GameState *g, uint16_t at) { set_event_saved_position(g, at); }

/* What the commands' files tell the derivations, looked at before the
 * command runs: whether a call reaches the target's places (event/calls.c),
 * whose index the original works out with additions that leave V clear;
 * and whether a question opens its window (event/dialog.c). */
typedef enum { CALL_AND_GO, CALL_WHEN_TAKEN, CALL_AND_WAIT } CallKind;
bool event_call_reaches_the_places(const GameState *g, CallKind kind, FieldObject self, FieldObject target, bool there,
                                   uint8_t level);
bool event_question_opens(const GameState *g, FieldObject self);

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

/* The screen test (field/object.h, tile_well_on_screen) keeps the screen's
 * edges, in the objects' tiles. */
static inline void keep_screen_edges(GameState *g)
{
    keep_byte(g, KEPT_SECOND, (uint8_t)(field_screen_left(g) >> 1));
    keep_byte(g, KEPT_THIRD, (uint8_t)(field_screen_top(g) >> 1));
}

#endif
