/* The party on the map: the leader, whom the player walks with the pad, and
 * the two members behind it, who walk in its footsteps (field/party_trail.c).
 *
 * The walk ($C0:881E, still the original's) works out each frame how the pad
 * walks the leader, how far the map lets it move and what the ground under
 * it does; the party's scripts then run event $B0 every frame, which moves
 * each of them a step (event/movement.c, behave): the leader as the walk
 * says, leaving a footprint on its trail, and each member along the trail.
 * The places of the party are those of event/party.h: 0 the leader, 1 and 2
 * the members. */
#ifndef CT_GAME_FIELD_PARTY_H
#define CT_GAME_FIELD_PARTY_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* What the ground under the leader does, as the walk finds it: a climb (a
 * ladder, a vine, a rope), where the party goes up and down at a walk and
 * shows its climbing animation; or a moving floor, which pulls the party
 * along. */
typedef uint8_t LeaderGround;
enum { GROUND_CLIMB = 0x04, GROUND_MOVING_FLOOR = 0x08 };

/* The party's places, the leader's first. */
enum { PARTY_LEADER = 0, PARTY_PLACES = FIELD_PARTY_OBJECTS_COUNT };

/* This frame's step of the party, which event $B0 takes for each of its
 * places: the leader moves as the walk says and leaves a footprint where it
 * moved; a member takes the next footprint of the trail, or waits for the one
 * ahead of it to draw away. Either shows its walk, its run, its climb or
 * stands, as the footprint says. */
void party_leader_steps(GameState *g, FieldObject leader);
void party_member_follows(GameState *g, FieldObject member, unsigned place);

/* Whether a member takes the next footprint this frame, or waits: it keeps
 * its distance from the one ahead of it. */
bool party_member_moves_on(const GameState *g, unsigned place);

/* The party regroups (a script moved the members about the leader, or
 * gathered them to it): the members pick the trail up at the leader's newest
 * footprint, with nothing of the way behind it to retrace. */
void party_trail_regroup(GameState *g);

/* Whether the trail's places are its footprints: the game's always are (the
 * guards'). */
bool party_trail_places_hold(const GameState *g);

/* What its entries leave the original's callers, worked out beside the
 * other field routines' (field/leaves.c): the overflow of the leader's step,
 * and of a member's, which follows from whether it moved on. */
bool field_leader_step_overflow(const GameState *g, bool before);
bool field_member_step_overflow(const GameState *g, unsigned place, bool moved_on);

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
