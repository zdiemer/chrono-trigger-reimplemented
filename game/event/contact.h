/* The leader meeting the map's objects (event/contact.c): talking to the
 * object before it, and walking into one, which touches it and may push it. */
#ifndef CT_GAME_EVENT_CONTACT_H
#define CT_GAME_EVENT_CONTACT_H

#include <stdbool.h>

#include "event/chests.h"
#include "field/object.h"
#include "snes/system.h"
#include "state.h"

/* Whether the leader is up against an object (its walk ran into one this
 * frame), and walking into it: it is touched, and pushed where it can be. */
bool leader_has_contact(const GameState *g);
void touch_what_the_leader_met(GameState *g);

/* The player talks to the object before the leader. */
void talk_to_the_leaders_contact(GameState *g);

/* The player presses the confirm button on the field: the leader talks to
 * the object before it and opens the chest before it, whose gift (if any)
 * the party's inventory is to take, and the field then announce. */
ChestGift press_confirm(GameState *g);

/* What walking into an object does, worked out before it is done: whether
 * the object takes the touch now, and whether it is pushed, to which tile. */
typedef struct {
    bool touched;
    bool pushed;
    MapTile to;
} Touch;
Touch leaders_touch(const GameState *g);

/* Whether the leader looks to push the object at all, having touched it or
 * not (`touched`): it takes calls, no push is under way, and it doesn't run
 * at the most urgent level. */
bool leader_looks_to_push(const GameState *g, bool touched);

/* The guards' part: the object the leader is up against is one the tables
 * hold, at a level the places hold, and a push would look at a tile of the
 * map by a facing of the leader's four. */
bool event_touch_is_ours(const GameState *g);

/* The registered entries, and their guards: the confirm button ($C0:1AAC,
 * from the field's main loop), the talk ($C0:5AC5, from the confirm button)
 * and the touch ($C0:3154, from event $B0). */
void field_press_confirm(Snes *s);
/* The confirm button as C: talk to what the leader faces, or open a chest. */
void field_confirm_pressed(GameState *g);
void event_talk_to_contact(Snes *s);
void event_touch_contact(Snes *s);
bool field_confirm_can_run(Snes *s);
bool event_contact_is_one(Snes *s);
bool event_touch_can_run(Snes *s);

#endif
