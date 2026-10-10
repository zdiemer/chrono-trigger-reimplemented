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

/* The vehicles' handlers (vehicles.h): the controller, the Epoch, its second
 * sprite and its watch for the Black Omen; and the test of whether two things
 * on the map are within reach of each other, which the original's code calls
 * too. */
void overworld_vehicles_entry(Snes *s);
void overworld_epoch_entry(Snes *s);
void overworld_epoch_second_sprite_entry(Snes *s);
void overworld_black_omen_watch_entry(Snes *s);
void overworld_within_reach_entry(Snes *s);
void overworld_show_the_epoch_flying_entry(Snes *s);
void overworld_carry_the_epoch_round_entry(Snes *s);
bool overworld_epoch_scene_call_runs(Snes *s);
bool overworld_vehicles_run(Snes *s);
bool overworld_epoch_runs(Snes *s);
bool overworld_epoch_second_sprite_runs(Snes *s);
bool overworld_black_omen_watch_runs(Snes *s);
bool overworld_reach_test_runs(Snes *s);

/* The main loop, from its head and from the menus' return. */
void overworld_main_loop_entry(Snes *s);
void overworld_back_from_the_menus_entry(Snes *s);
bool overworld_main_loop_runs(Snes *s);

#endif
