/* Field mode: its start and its frame (field/main_loop.c), and a location's
 * loading and keeping (field/location.c). */
#ifndef CT_GAME_FIELD_MAIN_LOOP_H
#define CT_GAME_FIELD_MAIN_LOOP_H

#include <stdbool.h>

#include "snes/system.h"
#include "platform/platform.h"
#include "state.h"

/* field/main_loop.c: the entries. The field's first start at power-on, its
 * entry from every other mode, one pass of its main loop, the frame's work
 * (the objects' turns and their sprites), the effects' refresh, a frame of
 * the leader's walk alone, and the start of the wait for the next frame. */
void field_first_start(Snes *s);
void field_enter(Snes *s);
void field_frame(Snes *s);
void field_frame_goes_on_with_the_requests(Snes *s);
void field_frame_goes_on_with_the_walk(Snes *s);
void field_frame_goes_on_with_confirm(Snes *s);
void field_frame_goes_on_with_the_dialog_window(Snes *s);
void field_frame_goes_on_with_the_screen_effect(Snes *s);
void field_frame_goes_on_with_the_script_layers(Snes *s);
void field_frame_goes_on_with_the_frame_work(Snes *s);
void field_frame_work(Snes *s);
void field_refresh_effects(Snes *s);
void field_walk_a_frame(Snes *s);
void field_finish_frame(Snes *s);
void field_wait_for_the_next_frame(Snes *s);
void field_main_loop_goes_round(Snes *s);
/* The field starting again on a fresh stack for a new location, and ending
 * for a scene: the one an event command asked for, or the field's close's. */
void field_start_again(Snes *s);
void field_start_the_scene(Snes *s);
void field_close_into_the_scene(Snes *s);
bool field_location_is_a_number(Snes *s);

/* The routines, for the ports that run them as C. */
enum {
    FIELD_FRAME_WORK_ENTRY = 0xC000BF,
    FIELD_REFRESH_EFFECTS_ENTRY = 0xC000DE,
    FIELD_WALK_A_FRAME_ENTRY = 0xC000EB,
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

void field_load_location(Snes *s);
void field_put_aside_for_a_menu(Snes *s);
void field_set_up_after_a_menu(Snes *s);
void field_start_location_song(Snes *s);
void field_load_tile_graphics(Snes *s);
void field_load_bg3_graphics(Snes *s);
void field_load_tile_looks(Snes *s);
void field_load_bg3_tile_looks(Snes *s);
bool field_party_can_be_put_aside(Snes *s);
/* The location's palette, the map's layers' state cleared, and its event
 * script unpacked with the built-in commands after it. */
void field_load_location_palette(GameState *g);
void field_clear_the_layers_state(GameState *g, Platform *pf);
void field_load_location_palette_entry(Snes *s);
void field_clear_the_layers_state_entry(Snes *s);
void field_load_the_script(Snes *s);
bool field_script_can_be_loaded(Snes *s);
bool field_can_be_set_up_again(Snes *s);
enum { FIELD_LOAD_LOCATION_ENTRY = 0xC000F4, FIELD_PUT_ASIDE_ENTRY = 0xC0011B, FIELD_SET_UP_AGAIN_ENTRY = 0xC001A5 };

#endif
