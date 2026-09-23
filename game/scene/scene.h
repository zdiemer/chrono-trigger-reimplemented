/* Scenes the game decompresses to $7E:3000 and runs from RAM (the attract
 * sequence and title screen, the jet bike race), and the bank C3 routines those
 * programs call. The attract sequence's wait loops are in game/attract/. */
#ifndef CT_GAME_SCENE_H
#define CT_GAME_SCENE_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* scene/scratch.c: the math scratch cells ($F0-$FF) of either page the
 * helpers are called on, the scene's own or a sequence channel's
 * (SceneMathPage, game/vars.h), as values of the widths the helpers use, and
 * what the helpers keep in them (re/ram.toml, scene_math_scratch). */
enum {
    SCRATCH_A = 0x0,          /* first operand word */
    SCRATCH_B = 0x2,          /* second operand word */
    SCRATCH_PRODUCT = 0x4,    /* 32-bit product, or divisor */
    SCRATCH_QUOTIENT = 0x8,   /* 32-bit quotient */
    SCRATCH_REMAINDER = 0xC,  /* 32-bit remainder */
};
uint8_t scene_scratch8(const GameState *g, SceneMathPage page, unsigned at);
void set_scene_scratch8(GameState *g, SceneMathPage page, unsigned at, uint8_t v);
uint16_t scene_scratch16(const GameState *g, SceneMathPage page, unsigned at);
void set_scene_scratch16(GameState *g, SceneMathPage page, unsigned at, uint16_t v);
uint32_t scene_scratch32(const GameState *g, SceneMathPage page, unsigned at);
void set_scene_scratch32(GameState *g, SceneMathPage page, unsigned at, uint32_t v);

/* scene/guards.c */
bool scene_on_scene_page(Snes *s);
bool scene_on_scene_page_low_bank(Snes *s);
bool scene_on_scene_page_io_bank(Snes *s);
bool scene_on_math_page(Snes *s);
bool scene_on_math_page_io_bank(Snes *s);

/* scene/sprites.c */
void scene_load_sprite_list(Snes *s);
void scene_load_sprite_list_clipped(Snes *s);
void scene_add_sprite(Snes *s);
void scene_add_large_sprite(Snes *s);

/* scene/bitmap.c */
void scene_scale_bitmap_row(Snes *s);
void scene_scale_bitmap(Snes *s);

/* scene/sequence.c */
void scene_run_sequence(Snes *s);
void scene_script_wait_sound_port2(Snes *s);
bool scene_sequence_bank_is_scripts(Snes *s);
bool scene_sequence_waits_on_channel(Snes *s);

/* scene/circle.c */
void scene_draw_circle(Snes *s);
void scene_circle_draw(Snes *s, uint8_t mode);

/* scene/matrix_code.c */
void scene_write_matrix_code(Snes *s);

/* scene/math.c */
void scene_multiply_signed(Snes *s);
void scene_find_divisor(Snes *s);
bool scene_find_divisor_on_divider_page(Snes *s);

/* scene/nmi.c (scene_frame_work is in scene/display.h) */
void scene_nmi(Snes *s);
void scene_nmi_while_working(Snes *s);
void scene_line_irq_ack(Snes *s);
void scene_boot_nmi(Snes *s);
void scene_boot_line_irq(Snes *s);
void scene_boot_line_irq_wait_hblank_end_loop(Snes *s);
void scene_boot_line_irq_wait_hblank_loop(Snes *s);
void scene_boot_line_irq_blank(Snes *s);

/* scene/boot.c */
void scene_boot(Snes *s);
bool scene_boot_bank_reaches_io(Snes *s);
void scene_copy_data_to_7e(Snes *s);
void scene_copy_data_to_7f(Snes *s);

/* scene/program.c (RAM code; guarded) */
bool scene_divide_present(Snes *s);
void scene_divide(Snes *s);
bool scene_divide_8_8_present(Snes *s);
void scene_divide_8_8(Snes *s);
bool scene_multiply_present(Snes *s);
void scene_multiply(Snes *s);
bool scene_shift_c00_words_present(Snes *s);
void scene_shift_c00_words(Snes *s);
bool scene_run_tasks_950_present(Snes *s);
void scene_run_tasks_950(Snes *s);
bool scene_run_tasks_940_present(Snes *s);
void scene_run_tasks_940(Snes *s);
bool scene_wait_apu_port3_present(Snes *s);
void scene_wait_apu_port3_loop(Snes *s);
bool scene_wait_39_clear_present(Snes *s);
void scene_wait_39_clear_loop(Snes *s);

#endif
