/* Contracts of the machine forms the still-gated field calls as C. */
#include "event/bridge.h"

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
