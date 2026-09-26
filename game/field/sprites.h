/* Field mode: building the OAM buffer from the objects' sprites. */
#ifndef CT_GAME_FIELD_SPRITES_H
#define CT_GAME_FIELD_SPRITES_H

#include <stdbool.h>

#include "field/object.h"
#include "snes/system.h"

/* Where the sprite table is in low RAM (field_sprite_table), which the
 * vblank sends and whose ranges' cursors the building keeps as addresses. */
enum { FIELD_SPRITE_TABLE_ADDRESS = 0x0700 };

/* field/sprites.c */
void sprites_hide_all_next_frame(GameState *g);
void field_build_sprite_table(Snes *s);
void field_draw_object_sprites(Snes *s);
void field_update_object_sprites(Snes *s);
void field_position_sprites(Snes *s);
void field_load_and_position_sprites(Snes *s);
void field_place_8_sprites(Snes *s);
void field_rebuild_8_sprites(Snes *s);
void field_place_12_sprites(Snes *s);
void field_rebuild_12_sprites(Snes *s);
void field_free_4_sprite_areas(Snes *s);
void field_free_8_sprite_areas(Snes *s);
void field_free_12_sprite_areas(Snes *s);
void field_free_every_sprite_area(Snes *s);
void field_claim_4_sprite_areas(Snes *s);
void field_claim_8_sprite_areas(Snes *s);
void field_claim_12_sprite_areas(Snes *s);
bool field_sprites_draw_in_table(Snes *s);
bool field_sprites_update_in_tables(Snes *s);
bool field_sprites_place_object(Snes *s);
bool field_sprites_place_8(Snes *s);
bool field_sprites_place_12(Snes *s);

/* For the frame builder (field/frames.c): the claim of the tile areas a new
 * frame is drawn into, a sprite of the frame's data the records are rebuilt
 * from, and whether the object's records lie inside their tables. */
typedef struct {
    int8_t x, y;
    uint8_t tile, attributes;
} FrameSprite;
bool sprites_claim_frame_areas(GameState *g, FieldObject obj, SpriteLayout layout);
void sprites_set_frame_sprite(GameState *g, FieldObject obj, unsigned n, FrameSprite sprite);
void sprites_set_record_sprite(GameState *g, FieldObject obj, unsigned n, FrameSprite sprite);
bool sprites_records_in_the_tables(const GameState *g, FieldObject obj, SpriteLayout layout);

#endif
