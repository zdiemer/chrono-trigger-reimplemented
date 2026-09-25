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
#include "snes/system.h"

/* The character byte of an empty place (the party's record of who is in it
 * keeps the character with its top bit set); the places are field/party.h's. */
enum { NO_CHARACTER = 0x80 };

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

/* The party's own scripts, which every map load puts after the map's own
 * ($C0:5CEC; tools/disasm/eventscript.py reads them): a member walks up to
 * the leader and, once the gathering count says all are there, hands the
 * party back to the player; a member walks to its place in a formation (one
 * script a place of the party), faces its way and counts itself in the
 * formation count; an object being pushed walks to its tile. The command that
 * starts one tells it where to go, and a formation's which way to face. */
typedef enum {
    GATHERING_SCRIPT,
    FORMATION_SCRIPT_LEADER,
    FORMATION_SCRIPT_FIRST_MEMBER,
    FORMATION_SCRIPT_SECOND_MEMBER,
    PUSH_SCRIPT,
} PartyScript;
uint16_t party_script_start(PartyScript script);
void party_script_go_to(GameState *g, PartyScript script, MapTile tile);
void party_script_face(GameState *g, PartyScript script, Facing facing);
static inline PartyScript formation_script(PartyPlace place)
{
    return (PartyScript)(FORMATION_SCRIPT_LEADER + place);
}

/* A formation's place gives the way to face there in the top bits of its
 * column and its row. */
Facing formation_facing(bool column_bit, bool row_bit);
void formation_facing_bits(Facing facing, bool *column_bit, bool *row_bit);

/* The counts the party's scripts keep, in two of the map's variables. */
enum { FORMATION_COUNT = 0xFE, GATHERING_COUNT = 0xFF };

/* After a battle the party comes back together, each member going back to
 * the leader ($C0:034B, $C0:5B1F). */
void party_back_together(GameState *g);
void member_back_to_the_leader(GameState *g, FieldObject member);
void event_party_back_together(Snes *s);
bool event_party_back_can_run(Snes *s);
void event_member_back_to_the_leader(Snes *s);
bool event_member_can_go_back(Snes *s);

/* Someone leaves the party: their place is empty, on the map and in the
 * party's record. */
static inline void empty_place(GameState *g, PartyPlace place)
{
    party_place_left_empty(g, place);
    set_party_members(g, place, NO_CHARACTER);
}

#endif
