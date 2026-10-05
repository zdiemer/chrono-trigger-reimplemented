/* The overworld's registered entries on the original's machine
 * (game/overworld/entries.c, and terrain.h's two lookups) and their guards. */
#ifndef CT_GAME_OVERWORLD_ENTRIES_H
#define CT_GAME_OVERWORLD_ENTRIES_H

#include "snes/system.h"

/* The objects' handlers, each entered with its slot in X. */
void overworld_lead_entry(Snes *s);
void overworld_follow_entry(Snes *s);
void overworld_watch_the_ground_entry(Snes *s);
void overworld_region_song_entry(Snes *s);
void overworld_sign_entry(Snes *s);
bool overworld_leader_runs(Snes *s);
bool overworld_follower_runs(Snes *s);
bool overworld_watcher_runs(Snes *s);
bool overworld_region_song_runs(Snes *s);
bool overworld_sign_runs(Snes *s);

/* The main loop, from its head and from the menus' return. */
void overworld_main_loop_entry(Snes *s);
void overworld_back_from_the_menus_entry(Snes *s);
bool overworld_main_loop_runs(Snes *s);

#endif
