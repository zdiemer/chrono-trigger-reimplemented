/* The sprite-object mode's per-line effects (game/menu/line_effects.c).
 *
 * An object can bend the picture line by line: a window's edges, a layer's
 * scroll. Each of the mode's line-effect slots (1-5) reads a table of 224
 * values, one a line, through a small table of its own that the vblank handler
 * fills in each frame from the two addresses an object stages for it -- the
 * values for the screen's top half and for its bottom half. So an object that
 * draws into one table while the other is shown, and stages the one it drew,
 * changes the effect a whole frame at a time.
 *
 * The wave (menu/wave.c) scrolls the layer over the map from slot 1; the
 * irises draw a circle of window into slot 1 or 2; the scripts' own routines
 * start a slot on a window's edges or the first plane's vertical scroll; the
 * time dial (menu/epoch.c) turns its gate's Mode 7 matrix from two slots, and
 * the Day of Lavos's turning world (menu/turning_world.c) its map's from
 * four, slots 2-5. */
#ifndef CT_GAME_MENU_LINE_EFFECTS_H
#define CT_GAME_MENU_LINE_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/object.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* A slot's line values: one table of a line's value (a word) for each of the
 * screen's lines, which the vblank handler sends in two halves. */
enum { MENU_LINE_TABLE_LINES = 224, MENU_LINE_HALF_BYTES = MENU_LINE_TABLE_LINES / 2 * 2 };

/* Where a slot keeps its values in bank $7E: two tables for each of slots 1-5
 * (menu_slot1_line_values and the rest), which an object draws into on
 * alternate frames (0 and 1). The wave uses slot 1's first. */
uint16_t menu_line_values(uint8_t slot, unsigned which);

/* A slot's small table, set to two runs of 112 lines that read the values
 * staged for the screen's top half and bottom half; and where it is, which
 * the slot's channel reads. */
void menu_lay_out_line_slot(GameState *g, uint8_t slot);
uint32_t menu_line_slot_table(uint8_t slot);

/* Program line-effect channel `channel` on `target`, reading its values
 * through the small table at `table` in bank $7E (a slot's, or an object's
 * own); the channel is programmed at `site`, the original's. The time
 * dial's Mode 7 matrix has its four channels read two slots' tables between
 * them; the turning world's, a slot's each. */
void menu_program_line_channel(Platform *pf, uint32_t site, uint8_t channel, VidLineTarget target, uint32_t table);

/* Turn slots' effects on or off from the next frame (a bit a slot). */
void menu_turn_on_line_slots(GameState *g, uint8_t slots);
void menu_turn_off_line_slots(GameState *g, uint8_t slots);

/* Start slot `slot` on `target`: its small table set to two runs of 112
 * lines, the channel programmed (at `site`, the original's), and the effect
 * turned on from the next frame. */
void menu_start_line_effect(GameState *g, Platform *pf, uint32_t site, uint8_t slot, VidLineTarget target);

/* Stage the values the slot shows from the next frame: the table at `values`
 * in bank $7E, its top half then its bottom. */
void menu_show_line_values(GameState *g, uint8_t slot, uint16_t values);

/* The entries: the scripts' routines of bank C6. */
bool menu_line_effect_can_start(Snes *s);
bool menu_squashed_iris_can_draw(Snes *s);
bool menu_capsule_iris_can_draw(Snes *s);
void menu_object_advance_line_tables(Snes *s);
void menu_object_window1_lines(Snes *s);
void menu_object_window1_lines_2(Snes *s);
void menu_object_window1_lines_3(Snes *s);
void menu_object_window2_lines(Snes *s);
void menu_object_window2_lines_2(Snes *s);
void menu_object_bg1_scroll_lines(Snes *s);
void menu_object_start_curtain(Snes *s);
void menu_object_step_curtain(Snes *s);
bool menu_curtain_can_start(Snes *s);
bool menu_curtain_can_step(Snes *s);
void menu_object_squashed_iris_1(Snes *s);
void menu_object_squashed_iris_2(Snes *s);
void menu_object_capsule_iris_1(Snes *s);
void menu_object_capsule_iris_2(Snes *s);

#endif
