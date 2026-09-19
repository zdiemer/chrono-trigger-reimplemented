/* Field mode: the per-line effects and the tables they read. */
#ifndef CT_GAME_FIELD_LINE_EFFECTS_H
#define CT_GAME_FIELD_LINE_EFFECTS_H

#include <stdbool.h>

#include "snes/system.h"

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
bool field_line_effects_page_reaches_ram(Snes *s);
bool field_line_effects_can_build(Snes *s);

#endif
