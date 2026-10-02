/* Who a map's objects are: the cast its scripts give them (field/cast.c).
 *
 * A map's objects are blank slots until their scripts' openings say who each
 * is: one of the playable characters, one of the people and creatures, or an
 * enemy. Casting gives the object its role on the field, the character it
 * is, and a sprite -- the record that says how it looks (field_data.h) put
 * on it, its graphics found or unpacked. The event commands that cast are
 * event/characters.c's. */
#ifndef CT_GAME_FIELD_CAST_H
#define CT_GAME_FIELD_CAST_H

#include <stdbool.h>
#include <stdint.h>

#include "field/field_data.h"
#include "field/object.h"
#include "platform/platform.h"
#include "state.h"

/* The playable characters, by number: Crono, Marle, Lucca, Robo, Frog, Ayla
 * and Magus. Their sprite records are the field's first seven, and the
 * people's and creatures' follow. */
enum { PLAYABLE_CHARACTERS = 7, FIRST_NPC_RECORD = PLAYABLE_CHARACTERS };

/* A playable character takes the object: in the party, as the member in its
 * place, standing where the location puts the party (the members on the
 * leader); out of it, set aside for the scripts to bring on. */
void object_cast_as_character(GameState *g, FieldObject obj, uint8_t character);

/* A playable character takes the object as one of the map's own, apart from
 * the party (one who hasn't joined it yet, or has left it). */
void object_cast_as_character_apart(GameState *g, FieldObject obj, uint8_t character);

/* Where a person's, a creature's or an enemy's graphics come from once it is
 * cast: the ROM, where a few enemies keep theirs; the graphics an object of
 * the map with the same set has already unpacked into bank $7F; or the set
 * still to unpack there, after those already unpacked. */
typedef enum { GRAPHICS_IN_THE_ROM, GRAPHICS_SHARED, GRAPHICS_TO_UNPACK } GraphicsSource;
typedef struct {
    GraphicsSource source;
    FieldSpriteRecord record;   /* the sprite record cast, whose graphics set it is */
} CastGraphics;

/* One of the people and creatures takes the object, by its number among
 * them. The first three loaded note where their graphics are for a battle on
 * the map; once its graphics are there, it counts as loaded. */
CastGraphics object_cast_as_npc(GameState *g, FieldObject obj, uint8_t npc);
void npc_loaded(GameState *g);

/* An enemy takes the object; then, its graphics there, it takes its place on
 * the screen: the block of sprite tiles its script names (`slot`'s low seven
 * bits, less one), whether the battle tells it apart (the top bit), and a
 * figure of 24 sprites is drawn whole at once. */
CastGraphics object_cast_as_enemy(GameState *g, FieldObject obj, uint8_t enemy);
void enemy_takes_its_place(GameState *g, Platform *pf, FieldObject obj, uint8_t slot);

/* A set to unpack: where it goes (after the sets already unpacked) and where
 * it comes from, for the unpacker; and once it has, the end of what is
 * unpacked moved on past it. False where it ran past the bank's end, which
 * stops the game. */
void ask_to_unpack(GameState *g, uint8_t set);
bool graphics_unpacked(GameState *g);

/* What casting needs of the state, the guards': the record tables where the
 * game points them; an entry of the shared graphics for the set (one that
 * has it, or a free one: with none the original stops the game); the block
 * an enemy's script names, among the blocks; and a figure of 24 sprites one
 * the port draws as the original does. */
bool cast_tables_in_the_rom(const GameState *g);
bool cast_graphics_entry_for(const GameState *g, uint8_t set);
bool cast_enemy_fits(const GameState *g, uint8_t enemy, uint8_t slot);

#endif
