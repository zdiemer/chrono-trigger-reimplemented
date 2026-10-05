/* What the overworld reads from the ROM: the walkers' animations, the places'
 * names and the sign's sprites.
 *
 * Every such read goes through an accessor here, named after what the data is
 * for, so a build that loads the data from somewhere other than the ROM image
 * changes this file and nothing that uses it. */
#ifndef CT_GAME_OVERWORLD_DATA_H
#define CT_GAME_OVERWORLD_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The walkers' animations, as sprite scripts by number (menu/sprites.h): for
 * each way a walker faces, two walking ones ($C2:397F, two bytes a facing,
 * which a walker's gait picks between) and a standing one ($C2:39AA, a byte a
 * facing). Magus walks and stands with his own, a fixed count further on. */
static inline uint8_t overworld_walking_animation(const GameState *g, uint8_t facing, uint8_t gait)
{
    return rom_read8(g, 0xC2397Fu + (uint8_t)(facing * 2u + gait));
}
static inline uint8_t overworld_standing_animation(const GameState *g, uint8_t facing)
{
    return rom_read8(g, 0xC239AAu + facing);
}
enum { OVERWORLD_MAGUS = 6, OVERWORLD_MAGUS_ANIMATIONS_ON = 12 };

/* The idle animation each of the party plays once after standing long
 * enough, the leader's first. */
enum { OVERWORLD_FIRST_IDLE_ANIMATION = 0xA2 };

/* The places' names, the strings the sign sets (a table of strings in bank
 * C6, as the typesetter reads one: menu_text). */
enum { OVERWORLD_PLACE_NAMES = 0xC6F400 };

/* The sign's sprite script as the ROM keeps it ($C2:57C2), which the sign
 * copies to work RAM to show: a command showing the sprite group after it,
 * held, and the group: its count of sprites, then six of them side by side
 * over the tiles the place's name is set in. */
enum { OVERWORLD_SIGN_SPRITES_BYTES = 0x1D };
static inline uint8_t overworld_sign_sprites_byte(const GameState *g, unsigned at)
{
    return rom_read8(g, 0xC257C2u + at);
}

#endif
