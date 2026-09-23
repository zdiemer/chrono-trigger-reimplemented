/* The registered entries of the commands the language's table runs
 * (event/event.h): each runs its command by its row, as the engine does, for
 * the callers that reach a handler by its address -- the original's own
 * loops over object 0's function at map setup ($C0:595C, $C0:597D), and
 * ct_fuzz, which exercises each alone. */
#include "event/bridge.h"

#define EVENT_DEFINE_ENTRY(name, opcode)                                          \
    void name(Snes *s) { event_command_entry(s, opcode); }                      \
    bool name##_accepts(Snes *s) { return event_command_accepts(s, opcode); }
EVENT_ENTRIES(EVENT_DEFINE_ENTRY)
