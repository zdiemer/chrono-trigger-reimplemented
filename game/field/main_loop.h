/* Field mode: its start and its frame (field/main_loop.c), and a location's
 * loading and keeping (field/location.c). */
#ifndef CT_GAME_FIELD_MAIN_LOOP_H
#define CT_GAME_FIELD_MAIN_LOOP_H

#include <stdbool.h>

#include <stdint.h>
#include "platform/platform.h"
#include "field/requests.h"
#include "state.h"

/* field/main_loop.c: the field's first start at power-on, its entry from
 * every other mode, one pass of its main loop, the frame's work (the
 * objects' turns and their sprites), the effects' refresh, a frame of the
 * leader's walk alone, and the wait for the next frame. */
/* The first start, with a host playing the sound driver: up to the field's
 * entry. */
void field_power_on(GameState *g, Platform *pf);
/* The field's entry, through the fade in; false where a step is one the port
 * doesn't take, the program going no further there. */
bool field_enter_the_location(GameState *g, Platform *pf);
/* The effects refreshed ($C0:00DE): the line effects' tables built, the palettes animated. */
bool field_effects_refreshed(GameState *g);
/* The field's close's scene ($C0:2643): where the program goes. */
void field_closed_into_the_scene(GameState *g);
/* A frame of the leader's walk alone ($C0:00EB): the walk, the effects
 * refreshed and the wait for the next frame; false where a step is one the
 * port doesn't take, the program going no further there. */
bool field_walked_a_frame(GameState *g, Platform *pf);

/* The frame done and the wait for the vblank handler to take it ($C0:EC60). */
void field_finish_the_frame(GameState *g, Platform *pf);
/* The frame's work ($C0:00BF): the objects' scripts and moves, the
 * line effects and the palettes for the next frame, the objects' new frames
 * drawn and their sprites put in the table. False where a step is one the
 * port doesn't take, the program going no further there. */
bool field_frame_work_done(GameState *g, Platform *pf);
/* A pass of the main loop ($C0:0097), through the wait for the next frame,
 * and where the field goes after it. */
FieldPass field_runs_a_pass(GameState *g, Platform *pf);
/* The field as the program runs it: entered, its main loop passes, entered
 * again for each new place; it returns where the program goes no further or
 * on to another mode. */
void field_runs(GameState *g, Platform *pf);
/* The field ended for scene `scene` (an event command's: below $80 the scene
 * engine's, from $80 the time gate's tunnels): where the program goes. */
void field_scene_starts(GameState *g, uint8_t scene);

/* The routines' timing keys (TIMED_AS). */
enum {
    FIELD_FRAME_WORK_ENTRY = 0xC000BF,
    FIELD_REFRESH_EFFECTS_ENTRY = 0xC000DE,
    FIELD_FRAME_WAIT = 0xC0EC60,
};

/* field/location.c: loading the location the party is in, and the field put
 * aside for a menu and set up again after it. The field's state moved aside
 * and back as one block, the party's place taken as the leader's, and all
 * that a menu's putting aside keeps, for the battle's (field/battle_start.c). */
void field_put_the_state_aside(GameState *g);
void field_bring_the_state_back(GameState *g);
void field_party_place_is_the_leaders(GameState *g);
/* The leader's tile and facing, the party's place (with no leader on the
 * map, the first slot's past the tables, as the original reads it); and
 * whether the leader's link is one to read it by: an object, or none. */
typedef struct {
    uint8_t x, y, facing;
} LeaderPlace;
LeaderPlace field_leaders_place(const GameState *g);
bool field_leader_readable(const GameState *g);
void field_put_aside(GameState *g);

/* The location the party is in loaded: false where its map's tile maps or
 * its script are none the port takes. */
bool field_load_the_location(GameState *g, Platform *pf);
/* The field set up again after a menu ($C0:01A5): false where a step is one
 * the port doesn't take, the program going no further there. */
bool field_set_up_again(GameState *g, Platform *pf);
/* Whether the party's places are ones the field keeps for a menu as the port
 * does: the leader an object or none, the members objects the tables hold or
 * none (checked before the field is put aside). */
bool field_party_can_be_kept(const GameState *g);
/* The tile set's looks loaded, and BG3's ($C0:09DD, $C0:0A14). */
void field_load_tile_looks(GameState *g);
void field_load_bg3_tile_looks(GameState *g);
/* The location's palette, the map's layers' state cleared, and its event
 * script unpacked with the built-in commands after it. */
void field_load_location_palette(GameState *g);
void field_clear_the_layers_state(GameState *g, Platform *pf);
/* The location's event script unpacked ($C0:56D4): false where it
 * doesn't fit, which the original checks. */
bool field_load_the_script(GameState *g);
enum { FIELD_LOAD_LOCATION_ENTRY = 0xC000F4, FIELD_PUT_ASIDE_ENTRY = 0xC0011B, FIELD_SET_UP_AGAIN_ENTRY = 0xC001A5 };

#endif
