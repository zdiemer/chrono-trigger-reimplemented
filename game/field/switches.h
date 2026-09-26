/* Field mode: the map's switches and chests (field/switches.c). */
#ifndef CT_GAME_FIELD_SWITCHES_H
#define CT_GAME_FIELD_SWITCHES_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"

/* A switch's shape: a tile and the one above it, two by two, two across by
 * three down (field_switch_shape). */
typedef enum { SWITCH_ONE_BY_TWO, SWITCH_TWO_BY_TWO, SWITCH_TWO_BY_THREE } SwitchShape;

/* A switch pressed: its shape, and which of its columns the leader stepped
 * into. */
typedef struct {
    bool pressed;
    SwitchShape shape;
    unsigned step_column;
} SwitchPress;

/* The two chests a map's tile set has (field_chest_kind), by the tile each
 * is closed: $FE, or $E0. */
typedef enum { CHEST_ON_TILE_FE = 0, CHEST_ON_TILE_E0 = 1 } ChestKind;

bool field_is_switch(const GameState *g, uint16_t step);
uint16_t field_switch_upper_tile(uint16_t step);
bool field_is_one_by_two_switch(const GameState *g, uint16_t step);
/* A byte of the map's tables by the original's index, row << 8 | column
 * (which runs on past a layer's 64 columns into the row's next table). */
uint8_t field_map_byte(const GameState *g, uint16_t at);
SwitchPress field_press_switch(GameState *g, uint16_t step);
void field_queue_switch_redraw(GameState *g, SwitchPress p, uint16_t step);
/* For what the original leaves (field/leaves.c): how many entries the
 * pressed switch's shape redraws, and how many rows a shape has. */
unsigned field_switch_entry_count(const GameState *g);
unsigned field_switch_rows(SwitchShape shape);
void field_redraw_changed_tiles(GameState *g, Platform *pf);
void field_remember_step(GameState *g, uint16_t step);

#endif
