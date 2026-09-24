/* Scenes: the set pieces the game runs as programs of their own.
 *
 * The title screen with its pendulum and its logo, the attract sequence, the
 * jet bike race and the endings each run as a scene program: the field or the
 * game over starts one (scene/boot.c), which decompresses the program into
 * work RAM and runs it there. A program is a main loop, the frame's work its
 * NMI does (scene/frame.c), and the sequence scripts two channels run each
 * frame (scene/script.c), which move things, call the program's routines and
 * start and stop its tasks: routines the frame's work calls every frame, from
 * one of two lists.
 *
 * Where to look:
 *   boot.c        starting a scene program, and the copies of its resources
 *   frame.c       the frame: the NMI, pause, the song, the task lists
 *   script.c      the sequence scripts
 *   screen.c      the program's screen: its setup, and what it sends each frame
 *   title.c       the title screen: the pendulum, the logo, the ripple
 *   race.c        the jet bike race's display, and its engine's pitch
 *   race_matrix.c the race's generated matrix code
 *   sound.c       the program's messages to the sound driver
 *   sprites.c, circle.c, scaler.c, math.c
 *                 what the scenes share: sprites, the iris, the bitmap
 *                 scaler, the arithmetic
 *   guards.c      what the ports assume of the machine they're entered on
 *
 * The endings have no routines of their own here: they are the program's
 * scripts, and they use the iris, the scaler and the arithmetic. */
#ifndef CT_GAME_SCENE_H
#define CT_GAME_SCENE_H

#include <stdbool.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* scene_flags, which whoever starts a scene sets (the boot takes them in A). */
enum {
    /* The scene is the title as the game comes back to it (after a reset or a
     * game over, never from the field), which runs with the line interrupt:
     * the boot starts it, the frame's work leaves interrupts enabled so it can
     * fire, and the program's clear of its work RAM keeps what lies below $0920. */
    SCENE_FLAG_TITLE_WITH_LINE_IRQ = 0x80,
    /* The frame's work doesn't send the sound driver song changes. */
    SCENE_FLAG_NO_SONGS = 0x20,
};

/* ---- The scene program's memory and transfers ------------------------------------------------ */

/* The two banks of work RAM. The program runs in the first, and puts large
 * buffers (the scaler's bitmaps, some irises' tables) in the second. */
enum { SCENE_LOWER_WORK_RAM = 0x7E, SCENE_UPPER_WORK_RAM = 0x7F };

/* The tilemap the scene builds for vblank (scene_tilemap_buffer), where the
 * uploads read it: low RAM, through bank 0. */
enum { SCENE_TILEMAP_BUFFER = 0x000C00 };

/* A word of the program the loaders fill a map's unused space with: the tile a
 * layer shows where nothing is drawn. */
enum { SCENE_BLANK_TILE = 0x7E350B };

/* The program's transfers: its DMA channel, and how a transfer moves its bytes
 * (the SNES-only escape and the per-call steps take them; docs/TODO.md). */
enum {
    SCENE_DMA_CHANNEL = 7,
    SCENE_DMA_ONE_REGISTER = 0x00,     /* a byte at a time into one register */
    SCENE_DMA_WORDS = 0x01,            /* into a register pair, a word at a time */
    SCENE_DMA_FIXED_SOURCE = 0x08,     /* the same source byte over and over: a fill */
    SCENE_DMA_FROM_REGISTER = 0x80,    /* out of the register into memory */
};

/* How VRAM writes step: a word at a time, stepping after the high byte (whole
 * words), or after the low byte (the low bytes alone, as the Mode 7 plane's
 * name table is written). */
#define SCENE_VRAM_WHOLE_WORDS VID_VRAM_WORD_PORT
#define SCENE_VRAM_LOW_BYTES (&(const VidVramPort){ .step = 1, .after_high = false })

/* A task: a routine of the program (its address), which the frame's work
 * calls every frame while it is on one of the two lists. 0 is an empty slot. */
typedef uint16_t SceneTask;
enum { NO_TASK = 0 };

typedef enum {
    SCENE_VBLANK_TASKS,   /* run first, while the frame's work has the screen blanked: the uploads */
    SCENE_FRAME_TASKS,    /* run after the controller is read, from a copy the frame's work makes */
} SceneTaskList;

/* scene/frame.c: put a task on a list (in its first empty slot), or take one
 * off; false if there is no such slot, where the original searches on past the
 * list through memory (no script does that). */
bool scene_add_task(GameState *g, SceneTaskList list, SceneTask task);
bool scene_remove_task(GameState *g, SceneTaskList list, SceneTask task);
/* Whether a list holds the task (NO_TASK: whether it has an empty slot). */
bool scene_task_listed(const GameState *g, SceneTaskList list, SceneTask task);

/* A sequence channel: one of the two the program runs a script on
 * (scene_channels), whose page its routines run on when a script calls them. */
typedef unsigned SceneChannel;

/* scene/script.c: a channel's registers, 0-15. */
uint16_t scene_channel_register(const GameState *g, SceneChannel ch, unsigned n);
void set_scene_channel_register(GameState *g, SceneChannel ch, unsigned n, uint16_t v);

/* scene/sound.c: how far the music has got. The sound driver shows it on a
 * port, which two reads that agree say is steady (not being changed as it is
 * read); its progress is a level from 0 to 15 that the song sets as it plays. */
typedef struct {
    uint8_t first, second;   /* the two reads */
    bool steady;
} MusicStatus;
MusicStatus scene_music_status(Platform *pf);
uint8_t scene_music_progress(MusicStatus music);
bool scene_music_reached(MusicStatus music, uint8_t level);

/* scene/circle.c: the iris, as a C call for the menus' sprite objects. */
void scene_circle_draw(GameState *g, uint8_t mode);

/* ---- Entries, by file ------------------------------------------------------------------------ */

/* scene/guards.c */
bool scene_on_scene_page(Snes *s);
bool scene_on_scene_page_low_bank(Snes *s);
bool scene_on_scene_page_io_bank(Snes *s);
bool scene_on_program_page(Snes *s);
bool scene_on_program_page_io_bank(Snes *s);
bool scene_on_channel_page(Snes *s);
bool scene_channel_at(uint16_t d, SceneChannel *ch);
SceneProgramPage scene_program_page_at_entry(const Snes *s);

/* scene/boot.c */
void scene_boot(Snes *s);
bool scene_boot_bank_reaches_io(Snes *s);
void scene_boot_nmi(Snes *s);
void scene_boot_line_irq(Snes *s);
void scene_boot_line_irq_wait_hblank_end_loop(Snes *s);
void scene_boot_line_irq_wait_hblank_loop(Snes *s);
void scene_boot_line_irq_blank(Snes *s);
void scene_copy_resource(Snes *s);
void scene_copy_resource_to_upper_ram(Snes *s);

/* scene/frame.c */
void scene_nmi(Snes *s);
void scene_nmi_while_working(Snes *s);
void scene_line_irq_ack(Snes *s);
void scene_frame_work(Snes *s);
bool scene_pause_is_a_switch(Snes *s);
bool scene_frame_tasks_present(Snes *s);
bool scene_vblank_tasks_present(Snes *s);
void scene_run_frame_tasks(Snes *s);
void scene_run_vblank_tasks(Snes *s);

/* scene/script.c */
void scene_run_sequence(Snes *s);
void scene_script_wait_for_sound(Snes *s);
bool scene_sequence_bank_is_scripts(Snes *s);
bool scene_sequence_waits_on_channel(Snes *s);

/* scene/screen.c (the program in RAM; each guarded by a hash of its code) */
bool scene_program_restart_present(Snes *s);
void scene_program_restart(Snes *s);
bool scene_clear_work_ram_present(Snes *s);
void scene_clear_work_ram(Snes *s);
bool scene_init_video_present(Snes *s);
void scene_init_video(Snes *s);
bool scene_upload_sprites_present(Snes *s);
void scene_upload_sprites(Snes *s);
bool scene_clear_sprite_table_present(Snes *s);
void scene_clear_sprite_table(Snes *s);
bool scene_upload_palette_present(Snes *s);
void scene_upload_palette(Snes *s);
bool scene_transfer_present(Snes *s);
void scene_transfer(Snes *s);
bool scene_setup_line_effects_present(Snes *s);
void scene_setup_line_effects(Snes *s);

/* scene/title.c (the program in RAM) */
bool scene_title_loader_present(Snes *s);
void scene_title_load_sprite_graphics(Snes *s);
void scene_title_load_layer_tiles(Snes *s);
void scene_title_load_plane_graphics(Snes *s);
void scene_title_load_bg1_map(Snes *s);
void scene_title_load_bg2_map(Snes *s);
void scene_title_fill_bg1_map(Snes *s);
bool scene_title_set_up_pendulum_present(Snes *s);
void scene_title_set_up_pendulum(Snes *s);
bool scene_title_load_plane_map_present(Snes *s);
void scene_title_load_plane_map(Snes *s);
bool scene_title_place_pendulum_present(Snes *s);
void scene_title_place_pendulum(Snes *s);
bool scene_title_move_logo_present(Snes *s);
void scene_title_move_logo(Snes *s);
bool scene_title_start_ripple_present(Snes *s);
void scene_title_start_ripple(Snes *s);
bool scene_title_expand_plane_tile_present(Snes *s);
void scene_title_expand_plane_tile(Snes *s);
bool scene_title_ripple_columns_present(Snes *s);
void scene_title_ripple_columns(Snes *s);
bool scene_title_upload_ripple_present(Snes *s);
void scene_title_upload_ripple(Snes *s);

/* scene/race.c (the program in RAM) */
bool scene_race_clear_buffers_present(Snes *s);
void scene_race_clear_buffers(Snes *s);
bool scene_race_setup_maps_present(Snes *s);
void scene_race_setup_maps(Snes *s);
bool scene_race_fill_map_present(Snes *s);
void scene_race_fill_map(Snes *s);
bool scene_race_loader_present(Snes *s);
void scene_race_load_plane_graphics(Snes *s);
void scene_race_load_plane_map(Snes *s);
void scene_race_load_layer_tiles(Snes *s);
void scene_race_load_more_layer_tiles(Snes *s);
void scene_race_load_sprite_graphics(Snes *s);
void scene_race_load_bg1_map(Snes *s);
void scene_race_load_bg3_map(Snes *s);
void scene_race_load_bg2_map(Snes *s);
bool scene_race_move_camera_present(Snes *s);
void scene_race_move_camera(Snes *s);
bool scene_race_upload_column_present(Snes *s);
void scene_race_upload_column_rom(Snes *s);
void scene_race_upload_column_ram(Snes *s);
bool scene_race_upload_tilemap_present(Snes *s);
void scene_race_upload_tilemap(Snes *s);
bool scene_race_upload_strip_present(Snes *s);
void scene_race_upload_strip(Snes *s);
bool scene_race_upload_plane_present(Snes *s);
void scene_race_upload_plane(Snes *s);
bool scene_race_wait_for_tilemap_upload_present(Snes *s);
void scene_race_wait_for_tilemap_upload_loop(Snes *s);
bool scene_race_engine_present(Snes *s);
void scene_race_set_engine_pitch(Snes *s);

/* scene/race_matrix.c */
void scene_write_matrix_code(Snes *s);

/* scene/sound.c (the program in RAM) */
bool scene_wait_for_song_marker_present(Snes *s);
void scene_wait_for_song_marker_loop(Snes *s);
bool scene_send_sound_command_present(Snes *s);
void scene_send_sound_command(Snes *s);
void scene_send_sound_command_again(Snes *s);
void scene_wait_sound_command_echo_loop(Snes *s);

/* scene/sprites.c */
bool scene_sprite_list_fits(Snes *s);
bool scene_clipped_sprite_list_fits(Snes *s);
bool scene_sprite_fits(Snes *s);
void scene_load_sprite_list(Snes *s);
void scene_load_sprite_list_clipped(Snes *s);
void scene_add_sprite(Snes *s);
void scene_add_large_sprite(Snes *s);

/* scene/circle.c */
bool scene_circle_table_is_clear(Snes *s);
void scene_draw_circle(Snes *s);

/* scene/scaler.c */
bool scene_scaler_in_its_bank(Snes *s);
void scene_scale_bitmap_row(Snes *s);
void scene_scale_bitmap(Snes *s);

/* scene/math.c */
bool scene_divide_present(Snes *s);
void scene_divide(Snes *s);
bool scene_divide_8_8_present(Snes *s);
void scene_divide_8_8(Snes *s);
bool scene_multiply_present(Snes *s);
void scene_multiply(Snes *s);
void scene_multiply_signed(Snes *s);
void scene_find_divisor(Snes *s);
bool scene_find_divisor_on_divider_page(Snes *s);

#endif
