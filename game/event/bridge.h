/* Where the event script language (event/script.h) meets the original's
 * machine: the registered entries of the engine and of the commands, the
 * guards, the translation to and from the original's registers, and what the
 * bridges -- the commands that still call out to the original's code -- share.
 * Nothing outside this file, event/bridge.c and the bridges takes a `Snes *`;
 * a build without the original's engine replaces them. The bridges call the
 * original through the field's bridge vocabulary (field/bridge.h), the one
 * the field's own bridges use.
 *
 * Every command handler has the same calling convention in the original:
 * entered through the table at $C0:5D6E with 8-bit A, 16-bit X and Y, Y the
 * command's offset; it returns X = where the script goes on and the carry set
 * to keep running the object's script this frame. It also leaves Y and V,
 * which the entries reproduce as scaffolding (event/leaves.h); A, N and Z are
 * dead, and declared so at every entry (game/registry.c). */
#ifndef CT_GAME_EVENT_BRIDGE_H
#define CT_GAME_EVENT_BRIDGE_H

#include "event/event.h"
#include "event/leaves.h"
#include "event/script.h"
#include "field/bridge.h"
#include "port/machine.h"
#include "port/vars_ram.h"

/* A command that calls out to code still the original's (the sound driver,
 * the menus' party routines, the field's state routines): its row has no
 * operation, and it runs on the machine, as its registered entry, from the
 * handler's calling convention. event/event.h lists them (EVENT_BRIDGES). */
typedef void (*EventBridge)(Snes *s);

/* The registered entry of an opcode's row, and its guard: the port runs the
 * command at Y by its row and returns as the original does. */
void event_command_entry(Snes *s, uint8_t opcode);
bool event_command_accepts(Snes *s, uint8_t opcode);

/* Call a bridge of this subsystem as the original's JSR at `site` calls the
 * handler at `routine`: with the return address pushed, so that it returns,
 * or stops, as the original's handler would; its time is the handler's. True
 * if it returned (false: it stopped, and the original has the machine). */
bool event_call_bridge(Snes *s, uint32_t site, uint32_t routine, EventBridge bridge);

/* The scaffolding record for a command, starting from the registers the
 * machine has, and the Y and V the command leaves from the record. */
EventLeaves event_leaves_of(const Snes *s);
void event_hand_back(Snes *s, const EventLeaves *left);

/* For the bridges: the command's offset (Y at entry); the command there,
 * decoded by `opcode`'s row (a bridge reads its operands by name, as the
 * operations do); and a return as the original makes it, with X and the
 * carry from the step and Y and V as the machine has them. */
static inline uint16_t event_command_offset(const Snes *s) { return s->cpu.y; }
EventCommand event_command_of(Snes *s, uint8_t opcode);
void event_return(Snes *s, EventStep st);

/* The menus' party routines ($C2:8004), from the JSL at `site`: the routine
 * in A's low byte, its arguments in X and Y as the caller has them, and the
 * carry the routine leaves (event/party.c). True if it came back as the
 * engine runs. */
bool event_call_party_routine(Snes *s, uint32_t site, uint8_t routine);

/* How a command runs on the machine: 8-bit A, 16-bit X and Y (`x8` for the
 * calls made with 8-bit index registers), on the field's page, with the data
 * bank it has. A bridge's calls must come back as it (field/bridge.h). */
static inline BridgeMachine event_machine(const Snes *s, bool x8) { return on_the_field_page(s, x8); }

#endif
