/* The party, as the scripts see it: up to three members, each a character
 * walking the map as one of its objects.
 *
 * Scripts name a member by their place in the party (0 the leader), doubled as
 * they write it. A place can be empty -- a party of one or two, or none at all
 * in an ending -- and scripts written for a full party address the member who
 * isn't there all the same: the moves up to them and following them find
 * nobody to walk to, and asking where they stand learns nothing (Someone,
 * below). */
#ifndef CT_GAME_EVENT_PARTY_H
#define CT_GAME_EVENT_PARTY_H

#include <stdbool.h>
#include <stdint.h>

#include "event/script.h"

/* The party's places, and the character byte of an empty one (the party's
 * record of who is in it keeps the character with its top bit set). */
enum { PARTY_PLACES = FIELD_PARTY_OBJECTS_COUNT, NO_CHARACTER = 0x80 };

/* Whether a place of the party is taken, and the object its member walks
 * the map as. An empty place has none: the commands that allow one decode
 * `there` false and don't look at the object, or ask for the member as
 * Someone (below); the guards leave the rest to the original (event/engine.c),
 * and the game gives them no empty place. */
static inline bool place_taken(const GameState *g, PartyPlace place)
{
    return field_link_is_object(party_member_link(g, place));
}

static inline FieldObject member_object(const GameState *g, PartyPlace place)
{
    FieldLink link = party_member_link(g, place);
    return field_link_is_object(link) ? field_linked_object(link) : 0;
}

/* Someone a command walks up to or asks after: an object, or a member of the
 * party by their place. Whether there is anyone (an empty place has nobody),
 * whether they are disabled (on the map but out of it, and nobody to walk up
 * to), where they stand, and the object. */
typedef struct {
    bool there;
    bool disabled;
    FieldPosition at;
    FieldObject object;
} Someone;

Someone someone_of_object(const GameState *g, FieldObject object);

/* The member in a place of the party, as `asking` looks for them (the
 * object whose script asks). */
Someone member_in_place(const GameState *g, PartyPlace place, FieldObject asking);

/* Whether the port can say what the original finds in a place: the game's
 * places hold a member's object or the marker for none, and a fuzzed state
 * other bytes, which the guard leaves to the original (event/party.c). */
bool member_place_readable(const GameState *g, PartyPlace place);

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
