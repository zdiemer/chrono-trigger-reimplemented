/* Where the event script protocol (event/script.h) meets the original's
 * registers: the one translation every command handler's registered entry
 * shares, the guards, and the call-outs a few handlers make into code that is
 * still the original's. This, and the bridges around call-outs in the handler
 * files, is the only machine-aware code in game/event/; a build without the
 * original's engine replaces it.
 *
 * Every handler has the same calling convention in the original: entered
 * through the table at $C0:5D6E with 8-bit A, 16-bit X and Y, Y the command's
 * offset; it returns X = where the script goes on and the carry set to keep
 * running the object's script this frame. What else it leaves that the
 * engine's callers may read is in EventStep (the parse position in Y, the
 * overflow in V); A, N and Z are dead, and declared so at every entry
 * (game/registry.c). */
#ifndef CT_GAME_EVENT_BRIDGE_H
#define CT_GAME_EVENT_BRIDGE_H

#include "event/event.h"
#include "event/script.h"
#include "port/machine.h"
#include "port/vars_ram.h"

typedef EventStep (*EventHandler)(GameState *g, const EventCommand *c);

/* The registered entry of a handler: read the command's offset from Y, run
 * the handler, and return as the original does. */
void event_command_run(Snes *s, EventHandler handler);
#define EVENT_COMMAND(entry, handler) \
    void entry(Snes *s) { event_command_run(s, handler); }

/* The same for a handler that also reads the platform (the sound driver's status). */
typedef EventStep (*EventPlatformHandler)(GameState *g, Platform *pf, const EventCommand *c);
void event_command_run_platform(Snes *s, EventPlatformHandler handler);
#define EVENT_PLATFORM_COMMAND(entry, handler) \
    void entry(Snes *s) { event_command_run_platform(s, handler); }

/* The command whose offset is in Y, and the return the step says: X, Y, the
 * carry and V, and an RTS. For the bridges. */
EventCommand event_command_at(const Snes *s);
void event_return(Snes *s, EventStep st);

/* What a handler called through the table leaves: X, the carry, Y and V. */
EventStep event_step_left(const Snes *s);

/* After a call-out: true if the callee came back with the machine as the
 * command runs: 8-bit A, 16-bit X (`x8` for the calls made with 8-bit X), D
 * on the field page and the data bank $00 or $80, where the port's accessors
 * reach what the original's code does. If not, the port stops at the call's
 * return address (each file's `back`) and the original carries on. */
bool event_machine_back(const Snes *s, bool x8);

/* Whether an object offset (a slot doubled) names an object: even, and in
 * the tables the port's accessors reach as the original does. */
bool event_names_object(uint16_t doubled);

/* A registry guard: D on the field page, the data bank $00 or $80, the
 * running object one, and `holds` (or NULL) of the game's state with the
 * command's offset (Y). The state is looked at without the machine's time
 * (port/vars_ram.h, vars_peek). */
typedef bool (*EventStateCheck)(const GameState *g, uint16_t at);
bool event_guard(Snes *s, EventStateCheck holds);

/* Set A (all 16 bits), X and Y for a call-out. */
void event_set_registers(Snes *s, uint16_t a, uint16_t x, uint16_t y);

#endif
