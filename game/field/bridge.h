/* Where the field's routines meet the original's registers: the guards every
 * entry runs under, and the pieces the entry wrappers and the bridges around
 * call-outs share. This, the entry wrappers at the end of each file and the
 * bridges this header's users name, is the machine-aware code in game/field/;
 * a build without the original's engine replaces it.
 *
 * The ports name the field's cells through game/vars.h, which knows them where
 * the field keeps them; the original reaches them through its direct page
 * (the field's, D = $0100, or its layers', $1D00), through the data bank (low
 * RAM below $2000, the I/O registers, and the tables in the upper half of bank
 * $C0), and objects through their slot doubled in an index register. So a port
 * is the same routine only where the game enters it the way it always does,
 * and each entry's guard says which of these it assumes. Any other call is the
 * original's; a guard that turned away a call shows as a drop in the entry's
 * shadow count, and a D no page names as a `page refused` line. */
#ifndef CT_GAME_FIELD_BRIDGE_H
#define CT_GAME_FIELD_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "state.h"
#include "vars.h"

enum { FIELD_PAGE = 0x0100, FIELD_LAYERS_PAGE = 0x1D00 };

/* The object X names: its slot, the register halved. */
static inline unsigned field_object_in_x(const Snes *s) { return s->cpu.x >> 1; }

/* The current object's slot, read as the entry's index registers read it:
 * its low byte with 8-bit X, the word with 16-bit X. */
unsigned field_current_object_slot(Snes *s);

/* Whether a doubled slot names an object the accessors reach as the original
 * does: even, and inside the tables ($80 bytes, 64 objects). Past them the
 * original's index reads the tables' neighbours. */
static inline bool field_names_object(uint16_t doubled) { return !(doubled & 1) && doubled < 2 * FIELD_OBJECTS; }

/* ---- Calling the original's code ------------------------------------------------------ */

/* How a bridge -- a port around a call to code still the original's, in
 * game/field/ or game/event/ -- runs when it makes the call: 8-bit A, its
 * index registers' width, its direct page and its data bank. The callee must
 * hand these back for the port to go on; otherwise the port stops at the
 * call's return, and the original carries on from the callee's machine. */
typedef struct {
    bool x8;
    uint16_t page;
    uint8_t bank;
} BridgeMachine;

static inline BridgeMachine bridge_machine_now(const Snes *s)
{
    return (BridgeMachine){ m_x8(s), m_direct_page(s), s->cpu.db };
}

/* The field's own bridges run on its page, with the data bank they have. */
static inline BridgeMachine on_the_field_page(const Snes *s, bool x8)
{
    return (BridgeMachine){ x8, FIELD_PAGE, s->cpu.db };
}

/* A call's return is the instruction after it. */
enum { JSR_SIZE = 3, JSL_SIZE = 4 };

/* Call the original's routine from the JSR (JSL) at `site`, declaring what it
 * doesn't read of the registers (checked by tools/dontcare_check.py, which
 * knows these two as it knows m_jsr_dc). True if it came back as `m`. */
bool bridge_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, BridgeMachine m);
bool bridge_call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, BridgeMachine m);

/* After a call made some other way (a service's call-out, a sibling bridge
 * called as the original's JSR calls it), returning to `returns_to`: the
 * same test, and the same stop. */
bool bridge_back(Snes *s, uint32_t returns_to, BridgeMachine m);


/* What the guards of the object update's steps check, over the game's state,
 * so that the update can run those steps inline where they hold and call
 * their entries (and so the original) where they don't: the tick's delay
 * entry inside its bank and the frame queue's links objects or none; the
 * rows' links even. */
bool field_tick_is_exact(const GameState *g, unsigned obj);
bool field_rows_are_exact(const GameState *g, unsigned obj);

/* The rows' steps as C, timed as the routines they are, where the rows' links
 * are ones the entries' guards take; false where the caller must call the
 * entry instead (field/object_update.c, the object update and the view). */
bool field_rows_step_as_c(Snes *s, FieldObject obj, bool join);
enum { FIELD_LEAVE_ROW_ENTRY = 0xC0A98A, FIELD_JOIN_ROW_ENTRY = 0xC0A9CD };

/* The object update's steps that are entries of their own, for the update to
 * run as C calls on the machine the entries would have (the current object
 * on the field's page): the move (field/motion.c), the view and the
 * window-only check (field/object_view.c). They leave the flags the entries
 * leave; the view step is a bridge, and false where it stopped. */
void field_move_object_step(Snes *s, FieldObject obj, bool party_member);
bool field_update_visibility(Snes *s, FieldObject obj);
bool field_view_changes(const GameState *g, unsigned obj);
void field_update_in_window(Snes *s, FieldObject obj);

/* The object the original's routines work on (field_current_object, a word
 * whose low byte is the object's link), which the field sets before it calls
 * one of them. */
static inline void object_make_current(GameState *g, FieldObject obj)
{
    set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | field_link_byte(field_link_to(obj))));
}

/* ---- The guards (registry .guard) ------------------------------------------------------ */

/* The data bank reaches low RAM, on any page. */
bool field_bank_low_ram(Snes *s);

/* On the field's page, and the data bank reaches... */
bool field_on_page(Snes *s);
bool field_on_page_io_bank(Snes *s);         /* the I/O registers */
bool field_on_page_low_bank(Snes *s);        /* low RAM */
bool field_on_page_bank_zero(Snes *s);       /* low RAM, the I/O registers and bank $C0's tables */

/* ... and the object named in X (8-bit), or the current object, is one. The
 * current object is a word, read as the width of X the entry runs with. */
bool field_object_x_low_bank(Snes *s);
bool field_current_object_low_bank(Snes *s);
bool field_current_object_bank_zero(Snes *s);
bool field_current_object_io_bank(Snes *s);

/* On the layers' page. */
bool field_on_layers_page(Snes *s);
bool field_on_layers_page_low_bank(Snes *s);
bool field_on_layers_page_io_bank(Snes *s);

#endif
