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

/* The two chests a map's tile set has (field_chest_kind), by where each is
 * in the set's last 32 tiles, the ones the field changes (switches, chests):
 * the chest closed on the next-to-last tile ($FE), and the one on the first
 * of them ($E0); open, each is the tile after. */
typedef enum { CHEST_AT_THE_SET_END = 0, CHEST_FIRST_OF_THE_CHANGING_TILES = 1 } ChestKind;

bool field_is_switch(const GameState *g, uint16_t step);
bool field_switch_inside_the_map(const GameState *g, uint16_t step);
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

/* The chest the leader opened (field_opened_chest_tile), from the main
 * loop's requests: its kind forgotten as the opening starts, and the chest
 * shown open. */
void field_chest_opening_starts(GameState *g);
bool field_open_chest(GameState *g);

#endif
