/* Field mode: the per-line effects and the tables they read. */
#ifndef CT_GAME_FIELD_LINE_EFFECTS_H
#define CT_GAME_FIELD_LINE_EFFECTS_H

#include <stdbool.h>

#include "platform/platform.h"
#include <stdint.h>
#include "state.h"

/* Program the field's eight line effects from the table set the frame's
 * builder finished (field/nmi.c, every vblank); returns the set. */
uint8_t field_set_up_line_effects(GameState *g, Platform *pf);

/* Both sets built afresh as the field's setup builds them and programmed
 * ($C0:0AFF, through $C0:0005); returns the effects the field runs. Where
 * the builders can build (the dialog window's place one of theirs). */
uint8_t field_rebuild_line_effects(GameState *g, Platform *pf);
bool field_line_effects_can_be_built(const GameState *g);
enum { FIELD_REBUILD_THE_LINE_EFFECTS_ENTRY = 0xC00005 };

/* As a screen is set up: the line effects' values, their tables built, and
 * them set up for the first frame ($C0:0B28's three calls); and the tables
 * built for the next frame ($FD:C2C1's work). */
void field_line_effects_ready(GameState *g, Platform *pf);
void field_line_effects_build(GameState *g);

/* The builders for each window position and table set ($FD:C2EB-$FD:D27E). */
#endif
