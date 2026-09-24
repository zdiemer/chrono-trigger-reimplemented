/* The party, as the scripts see it: up to three members, each a character
 * walking the map as one of its objects.
 *
 * Scripts name a member by their place in the party (0 the leader), doubled as
 * they write it. A place can be empty; the commands that address a member
 * check first, or need someone there (event/engine.c). */
#ifndef CT_GAME_EVENT_PARTY_H
#define CT_GAME_EVENT_PARTY_H

#include <stdbool.h>
#include <stdint.h>

#include "event/script.h"

typedef uint8_t PartyPlace;   /* 0 the leader, 1 and 2 the others */

/* The party's places, and the character byte of an empty one (the party's
 * record of who is in it keeps the character with its top bit set). */
enum { PARTY_PLACES = FIELD_PARTY_OBJECTS_COUNT, NO_CHARACTER = 0x80 };

/* Whether a place of the party is taken, and the object its member walks
 * the map as. An empty place has none: the commands that allow one decode
 * `there` false and don't look at the object, and the guards leave the ones
 * that don't to the original (event/engine.c). */
static inline bool place_taken(const GameState *g, PartyPlace place)
{
    return field_link_is_object(party_member_link(g, place));
}

static inline FieldObject member_object(const GameState *g, PartyPlace place)
{
    FieldLink link = party_member_link(g, place);
    return field_link_is_object(link) ? field_linked_object(link) : 0;
}

/* The character leading the party. */
static inline uint8_t party_leader(const GameState *g) { return party_members(g, 0); }

/* Someone leaves the party: their place is empty, on the map and in the
 * party's record. */
static inline void empty_place(GameState *g, PartyPlace place)
{
    party_place_left_empty(g, place);
    set_party_members(g, place, NO_CHARACTER);
}

#endif
