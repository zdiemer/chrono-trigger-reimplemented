/* Field mode: the sprite palette slots objects share (field/palettes.c). */
#ifndef CT_GAME_FIELD_PALETTES_H
#define CT_GAME_FIELD_PALETTES_H

#include <stdbool.h>

#include "field/object.h"
#include "snes/system.h"
#include "state.h"

/* The object gives up its palette slot: it leaves the slot's sharers, and a
 * slot no object shares any more is free for other colors. Nothing, while a
 * script has set the palettes itself. */
void object_give_up_palette(GameState *g, FieldObject obj);

/* A script gives the object one of the eight sprite palette slots for its
 * own and loads its colours there (event $33); from then on, until the next
 * map, objects keep out of the shared slots, the script having set the
 * palettes itself. */
void object_take_palette_slot(GameState *g, FieldObject obj, uint8_t slot);
/* An enemy takes one of the slots the enemies use, shared or free; false
 * where all of them are taken by other colours ($C0:72B4). */
bool object_take_enemy_palette_slot(GameState *g, FieldObject obj);
/* An object coming into view takes a slot for its colours, by what it is
 * (each party place its own, the enemies theirs, the people theirs), shared
 * with an object of the same colours or free; false where none is, where a
 * script has set the palettes itself, or for a disabled object ($C0:7170).
 * And whether the slots it would look at are ones the tables hold, and its
 * kind one the original's table of slots has (the guards'). */
bool object_take_palette(GameState *g, FieldObject obj);
/* Every slot free, as a map's screen is set up ($C0:7155). */
void palette_slots_all_free(GameState *g);
bool object_takes_a_palette_the_tables_hold(const GameState *g, FieldObject obj);

/* The object's twelve colours into its slot (above), where a script ends an
 * effect on them (event $88); and the first of its slot's colours, in the
 * game's 256. Whether the object keeps one of the eight slots, as the game
 * always does (the guards'). */
void object_load_colours(GameState *g, FieldObject obj);
/* The member at a place of the party moving up one takes its colours along:
 * the palette of the place `from` copied to the place before it. */
void party_palette_moves_up(GameState *g, unsigned from);
unsigned object_first_colour(const GameState *g, FieldObject obj);
bool object_palette_slot_is_one(const GameState *g, FieldObject obj);

/* Whether the object's palette slot and the sharers it links to are ones the
 * tables hold. */
bool object_palette_links_in_the_tables(const GameState *g, FieldObject obj);

void field_give_up_palette(Snes *s);
bool field_palette_can_be_given_up(Snes *s);
void field_take_enemy_palette_slot(Snes *s);
bool field_enemy_palette_slot_can_be_taken(Snes *s);
void field_take_palette(Snes *s);
bool field_palette_can_be_taken(Snes *s);

enum { FIELD_GIVE_UP_PALETTE_ENTRY = 0xC0734C, FIELD_TAKE_ENEMY_PALETTE_ENTRY = 0xC072B4,
       FIELD_TAKE_PALETTE_ENTRY = 0xC07170 };

#endif
