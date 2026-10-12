/* The registered entries of the commands (event/event.h), and their guards,
 * for the callers that reach a handler by its address -- the original's own
 * loop that starts a character's script, its opening up to its first zero
 * byte ($C0:597D), and
 * ct_fuzz, which exercises each alone. An operation's entry runs the command
 * by its row, as the engine does; a bridge is its own entry. Either way the
 * guard is the row's. */
#include "event/bridge.h"

#define EVENT_DEFINE_ENTRY(name, opcode)                                          \
    void name(Snes *s) { event_command_entry(s, opcode); }                      \
    bool name##_accepts(Snes *s) { return event_command_accepts(s, opcode); }
EVENT_ENTRIES(EVENT_DEFINE_ENTRY)

#define EVENT_DEFINE_GUARD(name, opcode) \
    bool name##_accepts(Snes *s) { return event_command_accepts(s, opcode); }
EVENT_BRIDGES(EVENT_DEFINE_GUARD)

/* The screen command that calls the menus' service, which reads the
 * active party as characters (menu/party.h). */
bool event_screen_party_routine_accepts(Snes *s)
{
    if (!event_command_accepts(s, 0xFF))
        return false;
    vars_peek(true);
    bool ok = party_record_shows_characters(state_of(s));
    vars_peek(false);
    return ok;
}
