/* Where the battle's routines meet the original's registers: the guards every
 * entry runs under, and the call the entry wrappers and the bridges around
 * call-outs make to the original's code. This, the entry wrappers at the end
 * of each file and the bridges they name, is the machine-aware code in
 * game/battle/; a build without the original's engine replaces it.
 *
 * The ports name the battle's cells through game/vars.h, which knows them where
 * the battle keeps them; the original reaches them through its direct page
 * (the battle's, D = $0000, or the sprite drawers', $1D00) and through its data
 * bank ($7E, so that absolute addresses reach the battle's tables above
 * $2000), and a combatant through its battle slot or its stat block's offset
 * in an index register. So a port is the same routine only where the game
 * enters it the way it always does, and each entry's guard says which of these
 * it assumes. Any other call is the original's; a guard that turned away a
 * call shows as a drop in the entry's shadow count. */
#ifndef CT_GAME_BATTLE_BRIDGE_H
#define CT_GAME_BATTLE_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/battle.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "state.h"
#include "vars.h"

enum { BATTLE_PAGE = 0x0000, BATTLE_SPRITE_PAGE = 0x1D00, BATTLE_DATA_BANK = 0x7E };

/* A call's return is the instruction after it. */
enum { BATTLE_JSR_SIZE = 3, BATTLE_JSL_SIZE = 4 };

/* ---- Calling the original's code ------------------------------------------------------ */

/* Call the original's routine from the JSR (JSL) at `site`, declaring what it
 * doesn't read of the registers (checked by tools/dontcare_check.py, which
 * knows these two as it knows m_jsr_dc). The battle's code runs with 8-bit A,
 * 16-bit index registers and binary arithmetic, on its page and data bank; a
 * callee must hand those back for the port to go on. True if it did;
 * otherwise the port stops at the call's return, and the original carries on
 * from the callee's machine. */
bool battle_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);
bool battle_call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);

/* Stop at the return of the JSR at `site`, where the original carries on with
 * the machine as its callee left it: for a port that asks, after a call-out,
 * whether what the callee left is what it goes on with. */
void battle_stop_after_call(Snes *s, uint32_t site);

/* The battle's steps' routine (BattleStep, battle.h). */
enum { BATTLE_STEP = 0xC10003 };

/* After a call made some other way (a service's call-out), returning to
 * `returns_to` on the page and bank it was made on: the same test, and the
 * same stop. */
bool battle_back(Snes *s, uint32_t returns_to, uint16_t page, uint8_t bank);

/* Run a sibling port's registered entry as the original's JSR (JSL) at
 * `site` calls `routine`: the return address pushed as the call pushes it, so
 * that the entry's own call-outs meet the original's where it makes them, and
 * its time counted as the routine's own. True if it came back to the call's
 * return as battle_call's callee must, on the page it was called on and with
 * whatever data bank it chose (the frame work sets its own); otherwise it
 * stopped where the original carries on, or at the return. For a machine-shaped sibling, or a sibling
 * whose guard must be able to turn a call away: one whose work is C on the game's state is called as C. Where the entry's guard (or its
 * registered preconditions) turns the state away, the call is made as a real
 * call instead, declaring nothing don't-care, and the runtime runs what the
 * registry has there. */
bool battle_run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));
bool battle_run_long(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* 8-bit A and 16-bit index registers, whatever else: what the interrupt
 * handlers need a callee to come back with. */
bool battle_widths_m8_x16(const Snes *s);

/* ---- What the registers name -------------------------------------------------------------- */

/* An index register holding a stat block's offset (battle.h) names the
 * combatant. */
static inline unsigned battle_slot_of_stat_block(uint16_t offset) { return offset / BATTLE_STAT_BLOCK_BYTES; }
bool battle_names_stat_block(uint16_t offset);
bool battle_names_combatant(uint16_t slot);

/* ---- The guards (registry .guard) ------------------------------------------------------ */

/* On the battle's page with the data bank at $7E, as nearly all of the
 * battle's code runs. */
bool battle_on_page(Snes *s);
/* ... with A's high byte 0 as well, which the effect handlers index with. */
bool battle_on_page_a_high_clear(Snes *s);
/* On the battle's page, whatever the data bank. */
bool battle_page(Snes *s);
/* On the battle's page with a data bank that reaches the I/O registers. */
bool battle_on_page_io_bank(Snes *s);
/* A data bank that reaches the I/O registers. */
bool battle_data_bank_is_io(Snes *s);
/* On the sprite drawers' page with the data bank at $7E; with A's high byte
 * 0; and on any page, the bank $7E and A's high byte 0. */
bool battle_sprite_page(Snes *s);
bool battle_sprite_page_ready(Snes *s);
bool battle_sprite_bank_ready(Snes *s);

/* On the battle's page with the data bank at $7E, and X names a stat block;
 * and Y the combatant whose stat block X names. */
bool battle_on_page_stat_block_x(Snes *s);
bool battle_on_page_combatant_xy(Snes *s);

#endif
