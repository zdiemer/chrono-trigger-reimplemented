/* Battle: the frame's display work, shared between its files.
 *
 * Every frame of a battle, the vblank handler (battle/nmi.c) runs the frame
 * work for the battle's mode: while the battle is set up, the display's setup;
 * during the fight, the frame's uploads, the window, the layers' scroll and
 * the screen shake, and then the battle's own per-frame work. The main loop,
 * for its part, runs the frame update once a frame, which animates colors and
 * moves and draws the battlefield's objects.
 *
 *   frame.c     the frame work and the frame update, and hiding the sprites
 *               before the objects draw
 *   scroll.c    the layers' per-line scroll, and the screen shake
 *   uploads.c   what the vblank sends to VRAM and the palette, and the
 *               requests the rest of the battle makes for it
 *   display.c   the display's setup, the window's per-line tables, loading the
 *               battle's graphics, and the vblank handler of the battle's end
 *   tiles.c     decoding the battlefield's tiles for upload
 *   battlefield_change.c  changing the battlefield's BG3 layer for another,
 *               and loading an effect's rows of the battlefield's tiles
 *   battlefield_restore.c  the screen the battle starts with, and putting
 *               the battlefield back once the effects are over
 *   beaten_enemies.c  the enemies an action beat leaving the fight: a fade
 *               to white and a dissolve, or a battlefield of their own
 *   closing.c   the window's opening and closing, and the display closed
 *               down as the battle ends
 *
 * Their logic takes the game's state and the platform; what reaches the
 * original's code (the battle's routines still generated from the ROM) is in
 * the bridges and entry wrappers at the end of each file. */
#ifndef CT_GAME_BATTLE_FRAME_H
#define CT_GAME_BATTLE_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/frame_data.h"
#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* ---- The window's arrangement and slide (frame.c) ------------------------------------------- */

/* The battle lays its lines out in one of a few arrangements (battle_alternate_
 * base: the window's, the battlefield's, those of the battle's end), which the
 * line handler applies when it is asked (battle_alternate_apply counts the
 * asks). Change the arrangement, or only have it applied anew (a message
 * shown or closed, a screen script's rebase). */
void battle_arrange_the_lines(GameState *g, uint8_t arrangement);
void battle_lay_the_lines_out_again(GameState *g);

/* The battle window's slide (battle_window_slide): still, about to slide in,
 * or sliding out; a slide takes the window's 23 lines a line a frame. */
enum { BATTLE_SLIDE_STILL = 0, BATTLE_SLIDE_STARTING_IN = 0xFF, BATTLE_SLIDE_OUT = 0x80 };
void battle_start_the_slide(GameState *g, uint8_t slide);
// $CD:0534
static inline bool battle_window_sliding(const GameState *g) { return battle_window_slide_frames(g) != 0; }

/* ---- scroll.c ------------------------------------------------------------------------------ */

/* How the layers are scrolled: steady, or BG3 where screen script channel 0
 * moves it; and two modes with a per-line offset on each layer, the second
 * running BG3's down twice as many lines, which are still the original's. */
typedef enum {
    BATTLE_SCROLL_STEADY,
    BATTLE_SCROLL_BG3_MOVING,
    BATTLE_SCROLL_LINE_OFFSETS,
    BATTLE_SCROLL_LINE_OFFSETS_LONG_BG3,
    BATTLE_SCROLL_MODES
} BattleScrollMode;

/* Slide where the battlefield's lines meet the window's by a line, in
 * (up) or out (down), in the per-line tables (battle/display.c). */
void battle_slide_the_split(GameState *g, bool out);

/* Keep the battle's own palette animations off the colors an enemy's rule
 * cycles (battle/frame.c). */
void battle_keep_animations_off_the_cycle(GameState *g);

/* Fill the layers' per-line scroll tables for this frame, in a mode. */
void battle_scroll_layers(GameState *g, BattleScrollMode mode);
/* The frame work's scroll, in whichever mode the battle is in (the call to the
 * original's dispatch where A's high byte, which it indexes with, isn't
 * clear). False where the call didn't come back as the battle runs. */
bool battle_scroll_the_layers(Snes *s);

/* This frame's screen shake, across and down, put on the layers it moves;
 * and the palette presets a screen effect asked for, started. */
void battle_shake_screen(GameState *g);

/* ---- uploads.c ----------------------------------------------------------------------------- */

/* The working palette, all 256 colors, to CGRAM. */
void battle_send_palette(Platform *pf);
/* The VRAM transfer a routine queued for the next frame, if one is. */
void battle_send_queued_transfer(GameState *g, Platform *pf);
void battle_send_vram_and_wait(Snes *s);
void battle_send_vram_and_wait_long(Snes *s);
/* A block of tiles a routine asked for, and half of a battle message's tiles
 * (or of the VRAM they go to, cleared). */
void battle_send_block_and_message_tiles(GameState *g, Platform *pf);
/* The rest of the vblank's uploads, from the frame work in the fight: the
 * sprites, the palette, the window's rows and frames, and the objects'
 * graphics. */
void battle_send_vblank_uploads(GameState *g, Platform *pf);
/* What those uploads read is inside the tables they read (the guards'
 * part: the graphics object is a combatant's). */
bool battle_vblank_uploads_in_range(const GameState *g);
/* And a queued transfer or a block of tiles isn't sent from over the scratch
 * cells or the stack the transfers use. */
bool battle_transfers_apart(const GameState *g, uint16_t stack);

/* ---- display.c ----------------------------------------------------------------------------- */

/* Point the window's per-line effect at this frame's table. */
void battle_show_window_table(GameState *g, Platform *pf);

/* A battlefield whose background has the HP gradient (battle_background_flags
 * bit 0) also ripples. */
enum { BATTLE_BACKGROUND_HAS_GRADIENT = 0x01 };
/* A battlefield the screen effects made (bit 4): it has an end of its own as
 * the battle is lost (closing.c), and the enemies a rule binds dissolve on it
 * as any other enemy does (beaten_enemies.c). */
enum { BATTLE_BACKGROUND_EFFECTS_BATTLEFIELD = 0x10 };

/* Lay the screen out for the per-line effects in one of the ROM's
 * arrangements (the battlefield alone, the battle window in view, a line of
 * the window's slide): each per-line table takes the arrangement's runs of
 * lines. */
void battle_arrange_line_effects(GameState *g, uint8_t arrangement);

/* The rippling background's frame: its lines' sways a step on every fourth
 * frame, or none while background rows 2-4 are moved. */
void battle_ripple_background(GameState *g);
/* What the arrangement's block moves leave of the registers, for the
 * original's code after it: A clear, and Y just past the window's second
 * table. */
void battle_leave_arrangement_registers(Snes *s);

/* ---- frame.c ------------------------------------------------------------------------------- */

/* Hide every sprite but the menu's cursors, before the objects draw. */
void battle_hide_sprites(GameState *g);
/* Turn the HP gradient's rows a phase, in step with palette animation. */
void battle_turn_the_gradient(GameState *g);

/* ---- tiles.c ------------------------------------------------------------------------------- */

/* Decode `tiles` tiles of a row set of the battlefield's tiles into the
 * decode buffer, from its start. */
void battle_decode_tiles(GameState *g, uint16_t row_set, uint16_t tiles);

/* ---- battlefield_change.c, battlefield_restore.c -------------------------------------------- */

/* Background row 1's last eight colors put back as the battle started them. */
void battle_restore_kept_colors(GameState *g);
/* Run a fade of `kind` (palette_animations' kind) through palette animation
 * slot 10's preset: set up as the slot will run it, and asked for. The old
 * battlefield's layer fades so, and a beaten enemy's colors
 * (beaten_enemies.c). */
void battle_fade_through_the_preset(GameState *g, uint8_t kind, BattlePaletteFade f);
/* Fade the battlefield's own layer out, as it leaves, or back in, as it is put
 * back. */
void battle_fade_battlefield_layer(GameState *g, bool back_in);
/* Load another battlefield and start its own screen scripts, marked running
 * from the next frame: a battle ending on the victory's battlefield
 * (closing.c), and an enemy leaving on its family's or its rule's
 * (beaten_enemies.c). The load is the call at `site`, the battlefield in X,
 * a word, as the caller's TAX leaves it. */
bool battle_load_and_run_battlefield(Snes *s, uint32_t site, uint16_t battlefield);
/* The layers' maps and scroll and the screen's settings below the split, as
 * the field hands them to the battle. */
void battle_take_the_start_settings(GameState *g);

/* ---- The guards (registry .guard) ------------------------------------------------------ */

/* On the battle's page with the data bank $7E, and the mode one the dispatch
 * has; what the frame work and the frame update read in range (frame.c); the
 * scroll mode one of the four (scroll.c); the vblank
 * uploads in range, and a block of tiles apart (uploads.c). */
bool battle_frame_dispatch_ready(Snes *s);
bool battle_frame_work_ready(Snes *s);
bool battle_frame_update_ready(Snes *s);
bool battle_scroll_mode_is_one(Snes *s);
bool battle_vblank_uploads_ready(Snes *s);
bool battle_block_upload_ready(Snes *s);
bool battle_queued_transfer_ready(Snes *s);
bool battle_vram_transfer_ready(Snes *s);
bool battle_tile_rows_ready(Snes *s);

#endif
