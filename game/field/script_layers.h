/* Field mode: the work on the layers scripts leave the field's main loop
 * (field/script_layers.c): BG2's halves cleared, the credits written into BG3
 * a line a frame, and the chest's message closed once the chest is off the
 * screen. */
#ifndef CT_GAME_FIELD_SCRIPT_LAYERS_H
#define CT_GAME_FIELD_SCRIPT_LAYERS_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* What scripts ask (game/event/screen.c): the layer work, by its
 * field_mode_flags bit (FIELD_CLEAR_BG2_LEFT, FIELD_CLEAR_BG2_RIGHT,
 * FIELD_WRITING_CREDITS); which of the credits' texts BG3 shows next; and
 * where in BG3's tile map its lines start. */
void script_asks_for_layer_work(GameState *g, uint8_t work);
uint16_t credits_text_start(const GameState *g, uint8_t text);
void credits_start_at(GameState *g, uint8_t column, uint8_t row);

/* The main loop's turn at it ($C0:274D), and its guard. */
void field_do_script_layer_work(Snes *s);
bool field_script_layer_work_can_run(Snes *s);

#endif
