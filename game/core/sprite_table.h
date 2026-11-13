/* The sprite table every mode fills and the vblank sends (core/sprite_table.c). */
#ifndef CT_GAME_CORE_SPRITE_TABLE_H
#define CT_GAME_CORE_SPRITE_TABLE_H

#include <stdint.h>

#include "platform/video.h"
#include "state.h"
#include "vars.h"

/* The table's bytes in the order the video hardware reads them: the
 * entries (oam_buffer: X, Y, tile and attributes each), then the high
 * table's two bits a sprite (oam_high_table). */
enum {
    SPRITE_TABLE_ENTRIES_BYTES = OAM_BUFFER_COUNT * VID_OAM_ENTRY_BYTES,
    SPRITE_TABLE_BYTES = SPRITE_TABLE_ENTRIES_BYTES + OAM_HIGH_TABLE_BYTES,
};

/* A byte of the table set by its place in that order, for the modes that
 * keep their cursor into it as a byte offset rather than a sprite: the
 * scenes' (which can leave it between entries) and the field's ranges. */
void sprite_table_set_byte(GameState *g, unsigned at, uint8_t v);

/* Where the game put each sprite, beside the table, which the vblank sends
 * with it (VidSpritePlace): the table holds X in nine bits, and hides a sprite
 * by putting it where the original's screen shows nothing, both of which a
 * wider view (vid_view) would show wrongly. A builder that knows where its
 * sprite stands or that it hides it says so here, after writing the entry; the
 * place holds while the entry keeps that X and Y. It is the program's own
 * state, beside the game's memory: nothing the game decides reads it, and it
 * takes none of the original's time. */
typedef struct {
    VidSpritePlace sprite[OAM_BUFFER_COUNT];
} SpritePlaces;
/* Module storage supplied by the state owner, independent of mapped RAM. */
SpritePlaces *sprite_places(GameState *g);

/* The sprite whose entry now holds X `x` (its nine bits) and Y `y` stands at
 * the whole X `x`. */
void sprite_table_place(GameState *g, unsigned sprite, int16_t x, uint8_t y);
/* The sprite whose entry now holds X `x` and Y `y` is out of sight; and the
 * same for the entry as it now stands, for a builder that hides a sprite by
 * its high bits alone. */
void sprite_table_hide(GameState *g, unsigned sprite, int16_t x, uint8_t y);
void sprite_table_hide_as_it_stands(GameState *g, unsigned sprite);
/* The places, for the send (vid_send_oam). */
const VidSpritePlace *sprite_table_places(GameState *g);

#endif
