/* The sprite table: 128 entries of a sprite's place, tile and attributes
 * (oam_buffer), and a high table of the sprites' ninth X bit and size, two
 * bits a sprite (oam_high_table), which every mode fills in its own way and
 * the vblank sends whole. Most modes write a sprite's entry by its number;
 * the scenes and the field keep a byte cursor into the table, as the
 * original's indexed stores do, and write through sprite_table_set_byte. */
#include "core/sprite_table.h"

enum { ENTRY_X, ENTRY_Y, ENTRY_TILE, ENTRY_ATTRIBUTES };

void sprite_table_set_byte(GameState *g, unsigned at, uint8_t v)
{
    if (at >= SPRITE_TABLE_ENTRIES_BYTES) {
        set_oam_high_table_byte(g, at - SPRITE_TABLE_ENTRIES_BYTES, v);
        return;
    }
    unsigned sprite = at / VID_OAM_ENTRY_BYTES;
    switch (at % VID_OAM_ENTRY_BYTES) {
    case ENTRY_X: set_oam_buffer_x(g, sprite, v); break;
    case ENTRY_Y: set_oam_buffer_y(g, sprite, v); break;
    case ENTRY_TILE: set_oam_buffer_tile(g, sprite, v); break;
    default: set_oam_buffer_attributes(g, sprite, v); break;
    }
}
