/* The overworld's vehicles (game/overworld/vehicles.c, epoch.c): the Epoch,
 * and the dactyl the party rides on the 65,000,000 B.C. map. */
#ifndef CT_GAME_OVERWORLD_VEHICLES_H
#define CT_GAME_OVERWORLD_VEHICLES_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/script.h"
#include "overworld/overworld.h"
#include "overworld/overworld_data.h"

/* Whether two things on the map are within reach of each other: each at its
 * spot, reaching as far as its record says (overworld_data.h). */
bool overworld_within_reach(GameState *g, WorldSpot first, ReachRecord first_reaches, WorldSpot second,
                            ReachRecord second_reaches);

/* Scaffolding: the first of the two, as the original hands it to the test
 * before it decides whether to make it. */
void overworld_leave_reach_from(GameState *g, WorldSpot first, ReachRecord reaches);

/* The vehicles' controller, a frame: the party boards a vehicle, rides it
 * and gets off it again (vehicles.c). */
void overworld_control_the_vehicles(GameState *g);

/* The Epoch on the world map, a frame (epoch.c). It runs its scenes' scripts
 * through `scripts`, as the walkers run theirs (overworld.h); false where the
 * script handed the frame over. */
typedef struct {
    ScriptEnd (*run)(void *ctx);
    void *ctx;
} EpochScripts;
bool overworld_fly_the_epoch(GameState *g, MenuObject self, const EpochScripts *scripts);

/* The Epoch's step through its handler, which the entry's wrapper reads. */
typedef enum {
    EPOCH_SETS_DOWN = 0,
    EPOCH_STANDS = 1,
    EPOCH_RISES = 2,
    EPOCH_FLIES = 3,
    EPOCH_FLIES_A_TILE = 4,
    EPOCH_COMES_DOWN = 5,
    EPOCH_AT_THE_TIME_GATE = 6,
    EPOCH_RUNS_A_SCENE = 7,
    EPOCH_WAITS_FOR_THE_PARTY_TO_GET_OFF = 8,
    EPOCH_STEPS = 9,
} EpochStep;
EpochStep overworld_epoch_step(const GameState *g, MenuObject epoch);

/* What the Epoch's scenes' scripts call: show it flying the way it faces,
 * and carry it a third of the way round the map, the way it flies across. */
enum { EPOCH_CARRIED_ROUND = MENU_MAP_WIDTH / 3 };
void overworld_show_the_epoch_flying(GameState *g, MenuObject epoch);
void overworld_carry_the_epoch_round(GameState *g, MenuObject epoch);

/* The second sprite the Epoch is drawn with, and the watch for the Black
 * Omen under it in flight: objects of their own. The second sprite's object
 * is done where the Epoch isn't on this map. */
bool overworld_draw_the_epoch_s_second_sprite(GameState *g, MenuObject self);
void overworld_watch_for_the_black_omen(GameState *g);

#endif
