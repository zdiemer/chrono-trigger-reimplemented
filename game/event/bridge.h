/* Where the event script language (event/script.h) meets the original's
 * machine: the registered entries of the commands, the guards, the
 * translation to and from the original's registers, and what the bridges --
 * the commands that still call out to the original's code -- share. Nothing
 * outside this file, the engine's turn loop (event/engine.c) and the bridges
 * takes a `Snes *`; a build without the original's engine replaces them.
 *
 * Every handler has the same calling convention in the original: entered
 * through the table at $C0:5D6E with 8-bit A, 16-bit X and Y, Y the command's
 * offset; it returns X = where the script goes on and the carry set to keep
 * running the object's script this frame. It also leaves Y and V, which the
 * entries reproduce as scaffolding (event/leaves.h); A, N and Z are dead, and
 * declared so at every entry (game/registry.c). */
#ifndef CT_GAME_EVENT_BRIDGE_H
#define CT_GAME_EVENT_BRIDGE_H

#include "event/event.h"
#include "event/leaves.h"
#include "event/script.h"
#include "port/machine.h"
#include "port/vars_ram.h"

/* The registered entry of the opcode's handler, and its guard: the port runs
 * the command at Y by its row and returns as the original does. */
void event_command_entry(Snes *s, uint8_t opcode);
bool event_command_accepts(Snes *s, uint8_t opcode);

/* Whether the port runs the command at `at` from the engine (the machine as
 * the engine keeps it; event_runs_here without the machine's time). */
bool event_takes(Snes *s, const EventOpcode *row, uint16_t at);

/* The scaffolding record for a command at `at`, starting from the registers
 * the machine has, and the registers it leaves when the command is done: X
 * and the carry from the step, Y and V from the record. */
EventLeaves event_leaves_of(const Snes *s, uint16_t at);
void event_hand_back(Snes *s, const EventLeaves *left, EventStep st);

/* For the bridges: the command's offset (Y at entry), and a return as the
 * original makes it, with X and the carry from the step and Y and V as the
 * machine has them. */
static inline uint16_t event_command_offset(const Snes *s) { return s->cpu.y; }
void event_return(Snes *s, EventStep st);

/* Y as the original leaves it, for a bridge whose command leaves it other
 * than as its callee did (scaffolding: event/leaves.h). */
static inline void event_leave_y(Snes *s, uint16_t y) { s->cpu.y = y; }

/* After a call-out: true if the callee came back with the machine as the
 * command runs: 8-bit A, 16-bit X (`x8` for the calls made with 8-bit X), D
 * on the field page and the data bank $00 or $80, where the port's accessors
 * reach what the original's code does. If not, the port stops at the call's
 * return address (each file's `back`) and the original carries on. */
bool event_machine_back(const Snes *s, bool x8);

/* A bridge's guard: D on the field page, the data bank $00 or $80, the
 * running object one, the command inside the script buffer, and `holds` (or
 * NULL) of the game's state with the command's offset (Y). */
bool event_guard(Snes *s, EventCheck holds);

/* Set A (all 16 bits), X and Y for a call-out. */
void event_set_registers(Snes *s, uint16_t a, uint16_t x, uint16_t y);

#endif
