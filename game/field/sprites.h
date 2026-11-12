/* Field mode: building the OAM buffer from the objects' sprites. */
#ifndef CT_GAME_FIELD_SPRITES_H
#define CT_GAME_FIELD_SPRITES_H

#include <stdbool.h>

#include "field/object.h"
#include <stdint.h>

/* Where the sprite table is in low RAM (field_sprite_table), which the
 * vblank sends and whose ranges' cursors the building keeps as addresses. */
enum { FIELD_SPRITE_TABLE_ADDRESS = 0x0700 };

/* field/sprites.c */
void sprites_hide_all_next_frame(GameState *g);
void sprites_hide_the_first(GameState *g, unsigned count);
/* The same as C, where every draw is one the port makes (false otherwise,
 * with nothing built). */
bool field_sprite_table_built(GameState *g);
/* A finite row list whose objects' sprite records fit in their tables. */
/* A map's start frees every frame area ($C0:E935). */
void sprites_free_every_frame_area(GameState *g);
enum { FIELD_FREE_EVERY_SPRITE_AREA_ENTRY = 0xC0E935 };
/* As a map's objects start: each one's sprites to be built afresh, and every
 * frame area free ($C0:B192). */
void sprites_all_to_build_afresh(GameState *g);
enum { FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY = 0xC0B192 };

/* For the frame builder (field/frames.c): the claim of the tile areas a new
 * frame is drawn into, a sprite of the frame's data the records are rebuilt
 * from, and whether the object's records lie inside their tables. */
typedef struct {
    int8_t x, y;
    uint8_t tile, attributes;
} FrameSprite;
bool sprites_claim_frame_areas(GameState *g, FieldObject obj, SpriteLayout layout);
/* An object leaving view lets go of the frame areas it holds ($C0:EA42). */
void sprites_release_frame_areas(GameState *g, FieldObject obj);
enum { FIELD_RELEASE_FRAME_AREAS_ENTRY = 0xC0EA42 };
void sprites_set_frame_sprite(GameState *g, FieldObject obj, unsigned n, FrameSprite sprite);
void sprites_set_record_sprite(GameState *g, FieldObject obj, unsigned n, FrameSprite sprite);
bool sprites_records_in_the_tables(const GameState *g, FieldObject obj, SpriteLayout layout);
/* The same for records at `records`, wherever an object's are. */
bool sprites_records_fit(uint16_t records, SpriteLayout layout);

#endif
