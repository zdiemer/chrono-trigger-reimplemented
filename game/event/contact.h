/* The leader meeting the map's objects (event/contact.c): talking to the
 * object before it, and walking into one, which touches it and may push it. */
#ifndef CT_GAME_EVENT_CONTACT_H
#define CT_GAME_EVENT_CONTACT_H

#include <stdbool.h>

#include "field/object.h"
#include "snes/system.h"
#include "state.h"

/* Whether the leader is up against an object (its walk ran into one this
 * frame), and walking into it: it is touched, and pushed where it can be. */
bool leader_has_contact(const GameState *g);
void touch_what_the_leader_met(GameState *g);

/* The player talks to the object before the leader. */
void talk_to_the_leaders_contact(GameState *g);

/* What walking into an object does, before it is done (the touch and event
 * $B0's derivations of what the original leaves): whether the object takes
 * calls at all, whether it takes the touch now, whether the leader then
 * looks to push it (no push is under way, and the object doesn't run at the
 * most urgent level), and whether it is pushed, to which tile. */
typedef struct {
    bool answers;
    bool touched;
    bool looks_to_push;
    bool pushed;
    MapTile to;
} Touch;
Touch leaders_touch(const GameState *g);

/* The guards' part: the object the leader is up against is one the tables
 * hold, at a level the places hold, and a push would look at a tile of the
 * map by a facing of the leader's four. */
bool event_touch_is_ours(const GameState *g);

/* The registered entries, and their guards: the talk ($C0:5AC5, from the
 * field's confirm button) and the touch ($C0:3154, from event $B0). */
void event_talk_to_contact(Snes *s);
void event_touch_contact(Snes *s);
bool event_contact_is_one(Snes *s);
bool event_touch_can_run(Snes *s);

#endif
