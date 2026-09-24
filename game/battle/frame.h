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
 *
 * Their logic takes the game's state and the platform; what reaches the
 * original's code (the battle's routines not ported yet) is in the bridges
 * and entry wrappers at the end of each file. */
#ifndef CT_GAME_BATTLE_FRAME_H
#define CT_GAME_BATTLE_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

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

bool battle_scroll_mode_filled_here(BattleScrollMode mode);
/* Fill the layers' per-line scroll tables for this frame, in one of the modes
 * filled here. */
void battle_scroll_layers(GameState *g, BattleScrollMode mode);
/* The frame work's scroll, in whichever mode the battle is in: the call to the
 * original's dispatch, as the frame work makes it, for the modes that are
 * still its. False where the call didn't come back as the battle runs. */
bool battle_scroll_the_layers(Snes *s);

/* This frame's screen shake, across and down, put on the layers it moves;
 * and the palette presets a screen effect asked for, started. */
void battle_shake_screen(GameState *g);

/* ---- uploads.c ----------------------------------------------------------------------------- */

/* The working palette, all 256 colors, to CGRAM. */
void battle_send_palette(Platform *pf);
/* The VRAM transfer a routine queued for the next frame, if one is. */
void battle_send_queued_transfer(GameState *g, Platform *pf);
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

/* ---- frame.c ------------------------------------------------------------------------------- */

/* Hide every sprite but the menu's cursors, before the objects draw. */
void battle_hide_sprites(GameState *g);
/* Turn the HP gradient's rows a phase, in step with palette animation. */
void battle_turn_the_gradient(GameState *g);

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
