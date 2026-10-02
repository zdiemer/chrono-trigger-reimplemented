/* The party on the map: the leader, whom the player walks with the pad, and
 * the two members behind it, who walk in its footsteps (field/party_trail.c).
 *
 * The walk (field/walk.c) works out each frame how the pad walks the leader,
 * how far the map lets it move and what the ground under it does; the
 * party's scripts then run event $B0 every frame, which moves each of them a
 * step (event/movement.c, step_with_the_party): the leader as the walk says,
 * leaving a footprint on its trail, and each member along the trail. The
 * party has three places, the leader's first; the scripts name a member by
 * place (event/party.h). */
#ifndef CT_GAME_FIELD_PARTY_H
#define CT_GAME_FIELD_PARTY_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* A place in the party: the leader's, and the two members' behind it. */
typedef uint8_t PartyPlace;
enum { PARTY_LEADER = 0, PARTY_FIRST_MEMBER = 1, PARTY_SECOND_MEMBER = 2, PARTY_PLACES = 3 };

/* The place of a member of the party (object_is_party_member). */
PartyPlace object_party_place(const GameState *g, FieldObject obj);

/* What the ground under the leader does, as the walk finds it: plain ground;
 * a climb (a ladder, a vine, a rope), where the party goes up and down at a
 * walk and shows its climbing animation; or a moving floor, which pulls the
 * party along. The walk and the footprints keep it as a byte of flags, which
 * only these see: the walk makes no other, and any other byte reads as the
 * original reads it (a climb where it says one, a moving floor otherwise). */
typedef enum { GROUND_PLAIN, GROUND_CLIMB, GROUND_MOVING_FLOOR } GroundKind;
typedef struct {
    uint8_t stored;
} LeaderGround;
enum { GROUND_CLIMB_BIT = 0x04, GROUND_MOVING_FLOOR_BIT = 0x08 };

static inline GroundKind ground_kind(LeaderGround ground)
{
    if (ground.stored == 0)
        return GROUND_PLAIN;
    return (ground.stored & GROUND_CLIMB_BIT) ? GROUND_CLIMB : GROUND_MOVING_FLOOR;
}
static inline LeaderGround leader_ground_of(bool climb, bool moving_floor)
{
    return (LeaderGround){ (uint8_t)((climb ? GROUND_CLIMB_BIT : 0) | (moving_floor ? GROUND_MOVING_FLOOR_BIT : 0)) };
}
LeaderGround leader_ground(const GameState *g);
void set_leader_ground(GameState *g, LeaderGround ground);

/* This frame's step of the party, which event $B0 takes for each of its
 * places: the leader moves as the walk says and leaves a footprint where it
 * moved; a member takes the next footprint of the trail, or waits for the one
 * ahead of it to draw away. Either shows its walk, its run, its climb or
 * stands, as the footprint says. */
void party_leader_steps(GameState *g, FieldObject leader);
void party_member_follows(GameState *g, FieldObject member, PartyPlace place);

/* Whether a member takes the next footprint this frame, or waits: it keeps
 * its distance from the one ahead of it. */
bool party_member_moves_on(const GameState *g, PartyPlace place);

/* The party regroups (a script moved the members about the leader, or
 * gathered them to it): the members pick the trail up at the leader's newest
 * footprint, with nothing of the way behind it to retrace. */
void party_trail_regroup(GameState *g);

/* A map's start: the trail starts afresh, the whole party at its first
 * footprint, with nothing behind it (field/walk.c). */
void party_trail_start(GameState *g);
/* A member put on the map takes the trail up with the rest of the party at
 * its first footprint (field/cast.c); the way behind stays as it was. */
void party_trail_rejoin(GameState *g);

/* The party counted as on the move: not standing about (field_idle_frames). */
void count_as_on_the_move(GameState *g);

/* The party as it is now taken as the one the map's characters were set up
 * for (field_party_as_loaded): the field's start, and a menu that changed it. */
static inline void party_taken_as_loaded(GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        set_field_party_as_loaded(g, place, party_members(g, place));
}

/* After a battle the members catch the trail up at the leader's newest
 * footprint, the way behind it as it was (event/party.c). */
void party_trail_catch_up(GameState *g);

/* A place of the party takes the map's layering at a map's start: its
 * object's halves' priorities, 0-3. The original lays an empty place out as
 * the slot past the object tables, which only its effects keep. */
void party_place_layered(GameState *g, PartyPlace place, uint8_t upper, uint8_t lower);

/* Whether the trail's places are its footprints: the game's always are (the
 * guards'). */
bool party_trail_places_hold(const GameState *g);

/* The registered entries (field/party_trail.c): the leader's step ($C0:9E29)
 * and each member's ($C0:A26B, $C0:A2CE), as event $B0's handler calls them,
 * with the current object the one stepping and 8-bit index registers. */
void field_leader_steps(Snes *s);
void field_first_member_follows(Snes *s);
void field_second_member_follows(Snes *s);
bool field_leader_can_step(Snes *s);
bool field_first_member_can_follow(Snes *s);
bool field_second_member_can_follow(Snes *s);

#endif
