/* Field mode: the per-line effects and the tables they read. */
#ifndef CT_GAME_FIELD_LINE_EFFECTS_H
#define CT_GAME_FIELD_LINE_EFFECTS_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
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
void field_rebuild_the_line_effects(Snes *s);
bool field_line_effects_can_be_rebuilt(Snes *s);

/* As a screen is set up: the line effects' values, their tables built, and
 * them set up for the first frame ($C0:0B28's three calls); and the tables
 * built for the next frame ($FD:C2C1's work). */
void field_line_effects_ready(GameState *g, Platform *pf);
void field_line_effects_build(GameState *g);

void field_setup_line_effects(Snes *s);
void field_init_line_effect_values(Snes *s);
void field_build_line_effect_tables(Snes *s);
/* The builders for each window position and table set ($FD:C2EB-$FD:D27E). */
void field_build_no_window_0(Snes *s);
void field_build_no_window_1(Snes *s);
void field_build_top_window_0(Snes *s);
void field_build_top_window_1(Snes *s);
void field_build_bottom_window_0(Snes *s);
void field_build_bottom_window_1(Snes *s);
/* The builders' pieces, each an entry of its own ($FD:C439-$FD:D52D). */
void field_set_window2_table_0(Snes *s);
void field_set_window2_table_1(Snes *s);
void field_set_bg2_table_0(Snes *s);
void field_set_bg2_table_1(Snes *s);
void field_send_scroll_0(Snes *s);
void field_send_scroll_1(Snes *s);
void field_start_wave_table(Snes *s);
void field_put_wave_part_0(Snes *s);
void field_put_wave_part_1(Snes *s);
void field_skip_wave_lines(Snes *s);
void field_fill_dialog_shading(Snes *s);

bool field_line_effects_page_is_field(Snes *s);
bool field_line_effects_bank_is_7f(Snes *s);
bool field_line_effects_page_and_bank(Snes *s);
bool field_line_effects_scroll_after_choice(Snes *s);
bool field_line_effects_page_reaches_ram(Snes *s);
bool field_line_effects_can_build(Snes *s);

#endif
