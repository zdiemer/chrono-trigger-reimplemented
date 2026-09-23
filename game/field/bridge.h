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

#include "port/machine.h"
#include "field/object.h"
#include "port/vars_ram.h"
#include "state.h"

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

/* After a call-out: true if the callee came back with the widths the port
 * goes on with (8-bit A; 8-bit X for `x8`) and D and the data bank as they
 * were. Otherwise the port stops at the call's return address and the
 * original carries on from the callee's machine. */
bool field_machine_back(const Snes *s, bool x8, uint16_t d, uint8_t db);

/* The overflow flag the original's last addition or subtraction leaves, for
 * the entry wrappers whose callers read it: whether a + b (or a - b), taken
 * as signed 8- or 16-bit numbers, left the signed range. The game's logic
 * never looks at it; only the original's code after the call does. */
static inline bool field_add_overflows8(uint8_t a, uint8_t b)
{
    uint8_t sum = (uint8_t)(a + b);
    return ((~(a ^ b) & (a ^ sum)) & 0x80) != 0;
}
static inline bool field_subtract_overflows8(uint8_t a, uint8_t b)
{
    uint8_t difference = (uint8_t)(a - b);
    return (((a ^ b) & (a ^ difference)) & 0x80) != 0;
}
static inline bool field_add_with_carry_overflows16(uint16_t a, uint16_t b, bool carry)
{
    uint16_t sum = (uint16_t)(a + b + carry);
    return ((~(a ^ b) & (a ^ sum)) & 0x8000) != 0;
}
static inline bool field_add_overflows16(uint16_t a, uint16_t b) { return field_add_with_carry_overflows16(a, b, false); }
static inline bool field_subtract_overflows16(uint16_t a, uint16_t b)
{
    uint16_t difference = (uint16_t)(a - b);
    return (((a ^ b) & (a ^ difference)) & 0x8000) != 0;
}

/* What the guards of the object update's steps check, over the game's state,
 * so that the update can run those steps inline where they hold and call
 * their entries (and so the original) where they don't: the tick's delay
 * entry inside its bank and the frame queue's links objects or none; the
 * rows' links even. */
bool field_tick_is_exact(const GameState *g, unsigned obj);
bool field_rows_are_exact(const GameState *g, unsigned obj);

/* The object update's steps that are entries of their own, for the update to
 * run as C calls on the machine the entries would have (the current object
 * on the field's page): the move (field/motion.c), the view and the
 * window-only check (field/object_view.c). They leave the flags the entries
 * leave; the view step is a bridge, and false where it stopped. */
void field_move_current_object(Snes *s, bool party_member);
bool field_update_visibility(Snes *s);
bool field_view_changes(const GameState *g, unsigned obj);
void field_update_in_window(Snes *s);

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
