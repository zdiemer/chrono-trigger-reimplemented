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

/* ---- In the air (vehicles.c) ------------------------------------------------------------- */

/* The Epoch (once it has its wings) and the leader's dactyl fly alike. Each
 * stands where the party left it; once the party is aboard it rises, a half
 * pixel a frame for VEHICLE_RISING_FRAMES, drawn over the party a few frames
 * in, and flies: the pad flies it a tile at a time, the map scrolling under
 * it, and each turn it makes turns the way the party will arrive facing.
 * Its buttons ask the main loop for the menus or the map view (and the
 * Epoch's start button for the time gate). The confirm button brings it
 * down where the ground under it is open, the other vehicle isn't parked
 * there and it isn't over the part of the map no vehicle comes down in; it
 * comes down a half pixel a frame for VEHICLE_COMING_DOWN_FRAMES from as
 * high as it flies, drawn under the party again near the ground. Where the
 * two differ it is said where they do: the Epoch needs open ground under
 * two more tiles to come down, plays its song in the air and keeps how long
 * its take-off or landing lasts in its slot; the dactyl can't fly over the
 * highest walls, is drawn in its rider's palette, and the others come down
 * with it. The handlers' steps for all this are the same in both
 * (EpochStep, DactylStep). */
enum { VEHICLE_RISES = 2, VEHICLE_FLIES = 3, VEHICLE_FLIES_A_TILE = 4, VEHICLE_COMES_DOWN = 5 };

enum {
    VEHICLE_RISING_FRAMES = 0x30,
    VEHICLE_OVER_THE_PARTY_AFTER = 0x08,
    VEHICLE_COMING_DOWN_FRAMES = 0x31,
    VEHICLE_UNDER_THE_PARTY_AFTER = 0x20,
    VEHICLE_HEIGHT_IN_THE_AIR = 0x18,
};

/* Its state's bits (overworld_vehicle_state): whether any of `bits` is set;
 * set or cleared. */
bool overworld_vehicle_is(const GameState *g, Vehicle v, uint8_t bits);
void overworld_mark_vehicle(GameState *g, Vehicle v, uint8_t bits, bool on);

/* Drawn under the party (standing) or over it (in the air), in a palette: a
 * dactyl in its rider's (overworld_member_palette), the Epoch in the
 * first. */
enum { EPOCH_PALETTE = 0 };
void overworld_draw_vehicle(GameState *g, MenuObject self, uint8_t palette, bool over_the_party);

/* Whether it is in the air, which its second sprite goes by. */
void overworld_set_vehicle_flying(GameState *g, Vehicle v, bool flying);

/* Its flying animation, the way it faces. */
void overworld_show_vehicle_flying(GameState *g, Vehicle v, MenuObject self, Facing way);

/* The ground under it at a spot, kept in its slot for its landing: the four
 * tiles round the spot, and for the Epoch the two below them. */
void overworld_note_the_ground_under(GameState *g, Vehicle v, MenuObject self, WorldSpot at);

/* The Epoch's song in the air (the Wings of Time). */
void overworld_play_the_song_of_flight(GameState *g);

/* Taking off (`way` -1, up) or coming down (1), a half pixel a frame,
 * counting the frames from the start; and stopping where it is, the count
 * started over. */
void overworld_vehicle_climb(GameState *g, Vehicle v, MenuObject self, int8_t way);
void overworld_vehicle_stop(GameState *g, MenuObject vehicle);

/* A frame rising (VEHICLE_RISES): false once it has risen, and the handler
 * then takes it into the air, where it flies at once. */
bool overworld_vehicle_rises(GameState *g, Vehicle v, MenuObject self);
void overworld_vehicle_takes_to_the_air(GameState *g, Vehicle v, MenuObject self);

/* A frame in the air on the pad's word (VEHICLE_FLIES), and a frame flying a
 * tile on (VEHICLE_FLIES_A_TILE). */
void overworld_vehicle_flies(GameState *g, Vehicle v, MenuObject self);
void overworld_vehicle_flies_a_tile(GameState *g, Vehicle v, MenuObject self);

/* A frame coming down (VEHICLE_COMES_DOWN); down, it stands with the party
 * out of it: the Epoch to be set down afresh, the dactyl waiting for the
 * party again. */
void overworld_vehicle_comes_down(GameState *g, Vehicle v, MenuObject self);

/* The Epoch's own answers to the pad in the air (epoch.c): its start button
 * opens the time gate; over the Black Omen its confirm button takes the
 * party in (true where it was over the Omen). */
void overworld_epoch_opens_the_time_gate(GameState *g, MenuObject epoch);
bool overworld_epoch_takes_the_party_into_the_omen(GameState *g);

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
    EPOCH_RISES = VEHICLE_RISES,
    EPOCH_FLIES = VEHICLE_FLIES,
    EPOCH_FLIES_A_TILE = VEHICLE_FLIES_A_TILE,
    EPOCH_COMES_DOWN = VEHICLE_COMES_DOWN,
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

/* ---- The dactyls (dactyl.c) ------------------------------------------------------------- */

/* The leader's dactyl's own state bit (overworld_vehicle_state): it is
 * coming down, and the others come down with it. */
enum { DACTYL_COMING_DOWN = 0x20 };

/* The dactyls live on one world map, the 65,000,000 B.C. one. */
enum { DACTYL_MAP = 0x01F3 };

/* The leader's dactyl, a frame: it stands, takes off with the party, flies
 * where the pad says and comes down. */
void overworld_fly_the_dactyl(GameState *g, MenuObject self);
typedef enum {
    DACTYL_SETS_DOWN = 0,
    DACTYL_WAITS_FOR_THE_PARTY = 1,
    DACTYL_RISES = VEHICLE_RISES,
    DACTYL_FLIES = VEHICLE_FLIES,
    DACTYL_FLIES_A_TILE = VEHICLE_FLIES_A_TILE,
    DACTYL_COMES_DOWN = VEHICLE_COMES_DOWN,
    DACTYL_STEPS = 6,
} DactylStep;

/* The second and third members' dactyls, a frame each: they fly behind the
 * leader's in a V (overworld_data.h's formation places). The slot's party
 * place says whose it is. */
void overworld_fly_a_following_dactyl(GameState *g, MenuObject self);
typedef enum {
    FOLLOWER_SETS_DOWN = 0,
    FOLLOWER_WAITS_FOR_THE_PARTY = 1,
    FOLLOWER_RISES = 2,
    FOLLOWER_HEADS_FOR_ITS_PLACE = 3,
    FOLLOWER_FLIES_TO_ITS_PLACE = 4,
    FOLLOWER_COMES_DOWN = 5,
    FOLLOWER_STEPS = 6,
} FollowingDactylStep;

/* Whether a following dactyl in flight is near enough its place in the V
 * for the heading's table, and, as the leader's comes down, near enough the
 * leader's to glide down to it (the guards': the game keeps them so). */
bool overworld_following_dactyl_keeps_near(const GameState *g, MenuObject self);

/* The second sprite each dactyl is drawn with, which follows it: above it
 * while it stands, below it in flight. The leader's goes by whether the
 * leader's flies; a following one's takes off, flies and comes down with
 * its own. */
void overworld_draw_the_dactyl_s_second_sprite(GameState *g, MenuObject self);
void overworld_draw_a_following_dactyl_s_second_sprite(GameState *g, MenuObject self);
enum { DACTYL_SECOND_SPRITE_STEPS = 2 };
typedef enum {
    FOLLOWERS_SPRITE_SETS_DOWN = 0,
    FOLLOWERS_SPRITE_WAITS_FOR_THE_PARTY = 1,
    FOLLOWERS_SPRITE_RISES = 2,
    FOLLOWERS_SPRITE_FOLLOWS = 3,
    FOLLOWERS_SPRITE_COMES_DOWN = 4,
    FOLLOWERS_SPRITE_STEPS = 5,
} FollowingSecondSpriteStep;
bool overworld_following_second_sprite_keeps_near(const GameState *g, MenuObject self);

#endif
