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
#include "port/vars_ram.h"
#include "state.h"

enum { FIELD_PAGE = 0x0100, FIELD_LAYERS_PAGE = 0x1D00 };

/* The object an index register names: its slot, the register halved. */
static inline unsigned field_object_in_x(const Snes *s) { return s->cpu.x >> 1; }
static inline unsigned field_object_in_y(const Snes *s) { return s->cpu.y >> 1; }

/* The current object's slot, read as the entry's index registers read it:
 * its low byte with 8-bit X, the word with 16-bit X. */
unsigned field_current_object_slot(Snes *s);

/* Whether a doubled slot names an object the accessors reach as the original
 * does: even, and below $100 (the tables are $80 bytes, and the original's
 * 8-bit index can't go further). */
static inline bool field_names_object(uint16_t doubled) { return !(doubled & 1) && doubled < 0x100; }

/* After a call-out: true if the callee came back with the widths the port
 * goes on with (8-bit A; 8-bit X for `x8`) and D and the data bank as they
 * were. Otherwise the port stops at the call's return address and the
 * original carries on from the callee's machine. */
bool field_machine_back(const Snes *s, bool x8, uint16_t d, uint8_t db);

/* ---- The guards (registry .guard) ------------------------------------------------------ */

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
