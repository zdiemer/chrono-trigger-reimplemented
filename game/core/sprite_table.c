/* The sprite table: 128 entries of a sprite's place, tile and attributes
 * (oam_buffer), and a high table of the sprites' ninth X bit and size, two
 * bits a sprite (oam_high_table), which every mode fills in its own way and
 * the vblank sends whole. Most modes write a sprite's entry by its number;
 * the scenes and the field keep a byte cursor into the table, as the
 * original's indexed stores do, and write through sprite_table_set_byte. */
#include "core/sprite_table.h"

#include "charge.h"

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

void sprite_table_place(GameState *g, unsigned sprite, int16_t x, uint8_t y)
{
    sprite_places(g)->sprite[sprite % OAM_BUFFER_COUNT] = (VidSpritePlace){ x, y, VID_PLACE_AT, 0 };
}

/* The entry is read outside the game's time: the original never reads it. */
void sprite_table_move_as_it_stands(GameState *g, unsigned sprite, int16_t moves)
{
    sprite %= OAM_BUFFER_COUNT;
    uint32_t mark = charge_uncounted_begin(g);
    uint8_t high = (uint8_t)(oam_high_table_byte(g, sprite / VID_OAM_SPRITES_A_HIGH_BYTE) >>
                             (VID_OAM_HIGH_BITS * (sprite % VID_OAM_SPRITES_A_HIGH_BYTE)));
    int16_t x = vid_oam_x(oam_buffer_x(g, sprite), high);
    uint8_t y = oam_buffer_y(g, sprite);
    charge_uncounted_end(g, mark);
    sprite_places(g)->sprite[sprite] = (VidSpritePlace){ x, y, VID_PLACE_AT, moves };
}

void sprite_table_forget_places(GameState *g)
{
    for (unsigned sprite = 0; sprite < OAM_BUFFER_COUNT; sprite++)
        sprite_places(g)->sprite[sprite] = (VidSpritePlace){ 0, 0, VID_PLACE_NONE, 0 };
}

void sprite_table_hide(GameState *g, unsigned sprite, int16_t x, uint8_t y)
{
    sprite_places(g)->sprite[sprite % OAM_BUFFER_COUNT] = (VidSpritePlace){ x, y, VID_PLACE_HIDDEN, 0 };
}

const VidSpritePlace *sprite_table_places(GameState *g) { return sprite_places(g)->sprite; }

/* The entry is read outside the game's time: the original never reads it. */
void sprite_table_hide_as_it_stands(GameState *g, unsigned sprite)
{
    sprite %= OAM_BUFFER_COUNT;
    uint32_t mark = charge_uncounted_begin(g);
    uint8_t high = (uint8_t)(oam_high_table_byte(g, sprite / VID_OAM_SPRITES_A_HIGH_BYTE) >>
                             (VID_OAM_HIGH_BITS * (sprite % VID_OAM_SPRITES_A_HIGH_BYTE)));
    int16_t x = vid_oam_x(oam_buffer_x(g, sprite), high);
    uint8_t y = oam_buffer_y(g, sprite);
    charge_uncounted_end(g, mark);
    sprite_table_hide(g, sprite, x, y);
}
