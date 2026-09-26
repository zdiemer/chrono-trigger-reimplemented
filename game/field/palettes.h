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

/* Whether the object's palette slot and the sharers it links to are ones the
 * tables hold. */
bool object_palette_links_in_the_tables(const GameState *g, FieldObject obj);

void field_give_up_palette(Snes *s);
bool field_palette_can_be_given_up(Snes *s);

enum { FIELD_GIVE_UP_PALETTE_ENTRY = 0xC0734C };

#endif
