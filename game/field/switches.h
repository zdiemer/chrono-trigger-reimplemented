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

/* The two kinds of chest (field_chest_kind). */
enum { CHEST_OF_THE_FIRST_KIND = 0, CHEST_OF_THE_OTHER_KIND = 1 };

bool field_is_switch(const GameState *g, uint16_t step);
SwitchPress field_press_switch(GameState *g, uint16_t step);
void field_queue_switch_redraw(GameState *g, SwitchPress p, uint16_t step);
/* For what the original leaves (field/leaves.c): how many entries the
 * pressed switch's shape redraws, and how many rows a shape has. */
unsigned field_switch_entry_count(const GameState *g);
unsigned field_switch_rows(SwitchShape shape);
void field_redraw_changed_tiles(GameState *g, Platform *pf);
void field_remember_step(GameState *g, uint16_t step);

#endif
