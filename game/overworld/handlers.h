/* The overworld's objects as the game runs them (game/overworld/handlers.c):
 * what each of their handlers takes of the game's state -- their contracts
 * -- and the song of where the
 * party is, which a scene's script starts. */
#ifndef CT_GAME_OVERWORLD_HANDLERS_H
#define CT_GAME_OVERWORLD_HANDLERS_H

#include <stdbool.h>

#include "menu/object.h"
#include "overworld/music.h"
#include "platform/platform.h"
#include "state.h"

bool overworld_leader_takes(const GameState *g, MenuObject self);
bool overworld_follower_takes(const GameState *g, MenuObject self);
bool overworld_watcher_takes(const GameState *g, MenuObject self);
bool overworld_region_song_takes(const GameState *g, MenuObject self);
bool overworld_sign_takes(const GameState *g, MenuObject self);
bool overworld_vehicles_take(const GameState *g, MenuObject self);
bool overworld_epoch_takes(const GameState *g, MenuObject self);
bool overworld_epoch_second_sprite_takes(const GameState *g, MenuObject self);
bool overworld_dactyl_takes(const GameState *g, MenuObject self);
bool overworld_dactyl_second_sprite_takes(const GameState *g, MenuObject self);
bool overworld_following_dactyl_takes(const GameState *g, MenuObject self);
bool overworld_following_dactyl_second_sprite_takes(const GameState *g, MenuObject self);
bool overworld_epoch_scene_call_takes(const GameState *g, MenuObject self);
bool overworld_location_song_takes(const GameState *g, MenuObject self);

/* The song of where the party is started on the driver ($C2:2F0F). */
void overworld_start_the_location_song(GameState *g, Platform *pf);

#endif
