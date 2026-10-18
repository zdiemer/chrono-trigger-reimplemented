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

/* The battle's waits on the display, as the timing service's (platform/
 * timing.h) from the JSR at `site` to the battle's wait `routine`: for the
 * next frame (the battle's frame wait, $C1:007E, battle/wait.c, and the
 * others there), for a number of frames ($CD:3E3B), and for the picture to
 * reach `line` (the wait at $CD:3B82). Each comes back as battle_call's
 * callee must. */
bool battle_wait_frame(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care);
bool battle_wait_frames(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint8_t frames);
bool battle_wait_line(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint8_t line);

/* Stop at the return of the JSR at `site`, where the original carries on with
 * the machine as its callee left it: for a port that asks, after a call-out,
 * whether what the callee left is what it goes on with. */
void battle_stop_after_call(Snes *s, uint32_t site);

/* The battle's steps' routine (BattleStep, battle.h). */
enum { BATTLE_STEP = 0xC10003 };

/* A port's ask for a step, as the original's JSR at `site` makes it: the step
 * run as C, through the steps' table, where the runtime would run the
 * dispatch's hand port (its guard taking the call); otherwise the original's
 * dispatch called, with `dont_care` what it doesn't
 * read of the asker's registers (checked as a call-out's declaration by
 * tools/dontcare_check.py; battle/steps.c). True if it came back to the call's return. */
bool battle_take_step(Snes *s, uint32_t site, BattleStep step, uint16_t dont_care);

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

/* A sibling's work run as C where the original's JSR at `site` calls it,
 * with the call's return pushed meanwhile, so that the work's own call-outs
 * are made under it as the original's are (its time is the caller's to tag,
 * TIMED_AS); and the return taken off again after. */
void battle_push_the_return(Snes *s, uint32_t site);
void battle_pull_the_return(Snes *s);

/* battle_run and battle_run_long where the call reads less than every
 * register: the don't-care set of the real call made where the entry's guard
 * turns the state away (and the set a fuzz run stopping at the call compares
 * by). */
bool battle_run_dc(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));
bool battle_run_long_dc(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));

/* battle_run_long for a sibling that comes back on a page and data bank of
 * its own, which the caller puts right after the call (the field's music
 * after a battle, field/sounds.c): true if it came back to the call's return
 * with the battle's widths, whatever its page. Where the entry's guard turns
 * the state away, the real call made instead goes on only on the page it was
 * made on, as battle_run's does. */
bool battle_run_long_to_any_page(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *));

/* 8-bit A and 16-bit index registers, whatever else: what the interrupt
 * handlers need a callee to come back with; and with binary arithmetic, what
 * most of the battle's entries need to be entered with. */
bool battle_widths_m8_x16(const Snes *s);
bool battle_binary_m8_x16(const Snes *s);

/* ---- What the original keeps across a call or a wait ------------------------------------- */

/* What the original has in the machine at one of its calls or waits beyond
 * the call's own arguments, which shadow mode compares there and its code
 * after the call reads: the bytes it keeps on the stack under the call's
 * return (a count, a value it uses again after), first pushed first; and the
 * carry its last test left, which the waits pass on to their callers. A
 * bridge says what its original keeps at each call, from what it is doing
 * there; these set the machine up so and take it down again after. */
typedef enum { BATTLE_CARRY_AS_IT_IS, BATTLE_CARRY_CLEAR, BATTLE_CARRY_SET } BattleCarry;
typedef struct {
    uint8_t kept[5];
    uint8_t kept_bytes;
    BattleCarry carry;
} BattleFrame;

static inline BattleCarry battle_carry_of(bool carry) { return carry ? BATTLE_CARRY_SET : BATTLE_CARRY_CLEAR; }
/* Nothing kept: only the carry, if that. */
static inline BattleFrame battle_carrying(BattleCarry carry) { return (BattleFrame){ .carry = carry }; }
/* A word kept, as a 16-bit push keeps it. */
static inline BattleFrame battle_keeping_word(uint16_t word, BattleCarry carry)
{
    return (BattleFrame){ .kept = { (uint8_t)(word >> 8), (uint8_t)word }, .kept_bytes = 2, .carry = carry };
}

void battle_keep(Snes *s, BattleFrame f);
void battle_stop_keeping(Snes *s, BattleFrame f);

/* The frame of a routine that keeps its caller's accumulator (its low byte)
 * and index registers across a call, as the battle's steps do ($C1:0003);
 * and those registers given back from it after. */
BattleFrame battle_keeping_the_registers(const Snes *s, BattleCarry carry);
void battle_give_the_registers_back(Snes *s, BattleFrame f);

/* battle_run, battle_run_dc, battle_run_long_dc and battle_wait_frame,
 * battle_wait_frames with the frame set up for the call and taken down after it; where the call
 * stops, the original carries on with what it keeps still on the stack. */
bool battle_run_under(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *), BattleFrame f);
bool battle_run_dc_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                         BattleFrame f);
bool battle_run_long_dc_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                              BattleFrame f);
bool battle_wait_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, BattleFrame f);
bool battle_wait_frames_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint8_t frames,
                              BattleFrame f);

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
/* ... or work RAM's, where the palette's send misses the hardware
 * (battle/uploads.c). */
bool battle_data_bank_is_io_or_work_ram(Snes *s);
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
