/* The overworld's objects, each a frame's work: the leader (walk.c), the
 * followers (followers.c), the watcher of the ground under the leader
 * (places.c), the region's music (music.c) and the place's sign (sign.c).
 * Each is an object's handler in the sprite-object mode, run once a frame and
 * never done; the registered entries that run them on the original's machine
 * are entries.c's. */
#ifndef CT_GAME_OVERWORLD_FRAMES_H
#define CT_GAME_OVERWORLD_FRAMES_H

#include <stdbool.h>

#include "overworld/overworld.h"

/* The leader's steps on foot: setting off as the overworld starts (or after
 * the travel or an event changes), standing, and walking a tile; and as it
 * boards a vehicle. */
enum { LEADER_SETS_OFF = 0, LEADER_STANDS = 1, LEADER_WALKS = 2, LEADER_STEPS_ON_FOOT = 3 };
enum {
    LEADER_HEADS_FOR_THE_VEHICLE = 0,
    LEADER_WALKS_DOWN_TO_IT = 1,
    LEADER_WALKS_ACROSS_TO_IT = 2,
    LEADER_IS_ABOARD = 3,
    LEADER_STEPS_BOARDING = 4,
};

/* A follower's steps on foot: setting off, deciding its way after the
 * leader, and walking a leg across or down; and as it boards a vehicle. */
enum {
    FOLLOWER_SETS_OFF = 0,
    FOLLOWER_DECIDES = 1,
    FOLLOWER_WALKS_ACROSS = 2,
    FOLLOWER_WALKS_DOWN = 3,
    FOLLOWER_STEPS_ON_FOOT = 4,
};
enum {
    FOLLOWER_HEADS_FOR_THE_VEHICLE = 0,
    FOLLOWER_GOES_ACROSS_TO_IT = 1,
    FOLLOWER_GOES_DOWN_TO_IT = 2,
    FOLLOWER_IS_ABOARD = 3,
    FOLLOWER_STEPS_BOARDING = 4,
};

/* The walkers run an event's scripts through `scripts`; each says whether
 * it finished its frame (false: its script handed the frame over). */
bool overworld_lead(GameState *g, MenuObject self, const WalkerScripts *scripts);
bool overworld_follow(GameState *g, MenuObject self, const WalkerScripts *scripts);
void overworld_watch_the_ground(GameState *g, MenuObject self);
void overworld_play_the_region_song(GameState *g, MenuObject self);
void overworld_show_the_sign(GameState *g, MenuObject self);

#endif
