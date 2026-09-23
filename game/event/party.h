/* The party, as the scripts see it: up to three members, each a character
 * walking the map as one of its objects.
 *
 * Scripts name a member by their place in the party (0 the leader), doubled as
 * they write it. A place can be empty; the commands that address a member
 * either check first or take whatever the place holds. */
#ifndef CT_GAME_EVENT_PARTY_H
#define CT_GAME_EVENT_PARTY_H

#include <stdbool.h>
#include <stdint.h>

#include "event/script.h"

typedef uint8_t PartyPlace;   /* 0 the leader, 1 and 2 the others */

enum { PARTY_PLACES = FIELD_PARTY_OBJECTS_COUNT, NOBODY = 0x80 };

/* The object a place in the party walks the map as; for an empty place this
 * is the marker's slot, which the commands that don't check take as it is. */
static inline EventObject member_object(const GameState *g, PartyPlace place)
{
    return slot_of(field_party_objects(g, place));
}

static inline bool place_taken(const GameState *g, PartyPlace place)
{
    return !(field_party_objects(g, place) & NOBODY);
}

/* The character leading the party. */
static inline uint8_t party_leader(const GameState *g) { return party_members(g, 0); }

/* Someone leaves the party: their place is empty, on the map and in the party. */
static inline void empty_place(GameState *g, PartyPlace place)
{
    set_field_party_objects(g, place, NOBODY);
    set_party_members(g, place, NOBODY);
}

#endif
