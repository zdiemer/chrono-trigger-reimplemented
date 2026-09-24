/* Battle: what the screen effects read from the ROM.
 *
 * Every read the screen effects make of cartridge data goes through here, by
 * what the data is, so a build that loads these tables from somewhere else
 * changes this file and nothing that uses it. The accessors take the game's
 * state (game/rom.h), not the machine. Where the original indexes a table
 * with a byte-wide offset that wraps, the accessor takes the value the offset
 * is made from and says how it wraps. */
#ifndef CT_GAME_BATTLE_SCREEN_DATA_H
#define CT_GAME_BATTLE_SCREEN_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "port/machine.h"
#include "port/vars_ram.h"
#include "rom.h"
#include "state.h"

/* ---- The screen scripts ---------------------------------------------------------------- */

/* A byte of a screen script, at its 24-bit address. */
static inline uint8_t battle_screen_script_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* ---- The HP gradient ---------------------------------------------------------------------- */

/* The HP gradient's colors: 14 a set, one set per HP level, at a byte offset
 * per level ($D1:E9EF, a word a level, into the colors at $D0:FBE2). The
 * level's index is a byte of twice the level. */
enum { GRADIENT_SET_COLORS = 14 };
static inline uint16_t gradient_set_color(const GameState *g, uint8_t level, unsigned color)
{
    uint16_t set = rom_read16(g, 0xD1E9EFu + (uint8_t)(level * 2));
    return rom_read16(g, 0xD0FBE2u + (uint16_t)(set + 2u * color));
}

/* The first of all the sets' colors, which the background's first gradient
 * row is looked for. */
static inline uint16_t gradient_first_color(const GameState *g) { return rom_read16(g, 0xD0FBE2u); }

/* How far each of the gradient's three palette rows is turned, by the
 * gradient's phase: three tables of 14 at $D1:EA21. */
enum { GRADIENT_ROWS = 3, GRADIENT_PHASES = 14 };
static inline uint8_t gradient_row_turn(const GameState *g, unsigned row, uint8_t phase)
{
    return rom_read8(g, 0xD1EA21u + GRADIENT_PHASES * row + phase);
}

/* ---- Palette rows ------------------------------------------------------------------------- */

/* The colors background rows 2-4 start the battle with ($D0:FD00, 48). */
static inline uint16_t background_row_start_color(const GameState *g, unsigned color)
{
    return rom_read16(g, 0xD0FD00u + 2u * color);
}

/* ---- An effect's graphics ----------------------------------------------------------------- */

/* The compressed graphics blocks an effect loads: four ids an effect, a byte
 * each ($D1:ECDB; an id of $FF is none). The list's index is a byte of four
 * times the effect. */
enum { EFFECT_BLOCKS = 4, NO_GRAPHICS_BLOCK = 0xFF };
static inline uint8_t effect_graphics_block(const GameState *g, uint8_t effect, unsigned block)
{
    return rom_read8(g, 0xD1ECDBu + (uint8_t)(effect * EFFECT_BLOCKS) + block);
}

/* Where a compressed graphics block is: a long pointer per id ($F6:2220). */
static inline uint32_t graphics_block_address(const GameState *g, uint8_t id)
{
    uint32_t at = 0xF62220u + 3u * id;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

/* An effect's 48 colors: a byte offset per effect ($D1:ECCF, a word each, its
 * index a byte of twice the effect) into the colors at $D0:FA00. */
enum { EFFECT_COLORS = 48 };
static inline uint16_t effect_color(const GameState *g, uint8_t effect, unsigned color)
{
    uint16_t set = rom_read16(g, 0xD1ECCFu + (uint8_t)(effect * 2));
    return rom_read16(g, 0xD0FA00u + (uint16_t)(set + 2u * color));
}

/* The compressed graphics an effect puts in the tilemap area: a long pointer
 * per effect ($D0:E000, its index a byte of three times the effect). */
static inline uint32_t effect_tilemap_graphics_address(const GameState *g, uint8_t effect)
{
    uint32_t at = 0xD0E000u + (uint8_t)(effect * 3);
    return rom_read8(g, at) | (uint32_t)rom_read8(g, at + 1) << 8 | (uint32_t)rom_read8(g, at + 2) << 16;
}

/* ---- Motion ------------------------------------------------------------------------------- */

/* The battle's sine table ($CE:F48E): 256 steps a turn, the magnitude 0-255
 * of each; the second half of the turn is the negative one. */
static inline uint8_t battle_sine_magnitude(const GameState *g, uint8_t angle) { return rom_read8(g, 0xCEF48Eu + angle); }

/* The random bytes the battle picks from ($C0:FE00), 256 of them. */
static inline uint8_t battle_random_table(const GameState *g, uint8_t pick) { return rom_read8(g, 0xC0FE00u + pick); }

/* The screen shake's settings: how many frames between new magnitudes and
 * between amplitude steps (eight choices each), and which layers each axis
 * moves (four). */
static inline uint8_t shake_period_choice(const GameState *g, unsigned choice) { return rom_read8(g, 0xD1F69Cu + choice); }
static inline uint8_t shake_rate_choice(const GameState *g, unsigned choice) { return rom_read8(g, 0xD1F6A4u + choice); }
static inline uint8_t shake_layers_across(const GameState *g, unsigned choice) { return rom_read8(g, 0xD1F6ACu + choice); }
static inline uint8_t shake_layers_down(const GameState *g, unsigned choice) { return rom_read8(g, 0xD1F6B0u + choice); }

/* ---- Palette presets ---------------------------------------------------------------------- */

/* A palette preset: a pair of palette animation slots' fields from the first
 * color on, eight bytes a slot, sixteen presets ($CE:F0E6). */
enum { PRESET_SLOT_FIELDS = 8 };
static inline uint8_t palette_preset_field(const GameState *g, unsigned preset, unsigned slot, unsigned field)
{
    return rom_read8(g, 0xCEF0E6u + 16u * preset + PRESET_SLOT_FIELDS * slot + field);
}

/* The two slots' delay and period for each of eight paces ($CE:F126), and
 * their step for each ($CE:F146). */
static inline uint8_t palette_preset_timing(const GameState *g, unsigned pace, unsigned slot, bool period)
{
    return rom_read8(g, 0xCEF126u + 4u * pace + 2u * slot + period);
}
static inline uint8_t palette_preset_step(const GameState *g, unsigned pace, unsigned slot)
{
    return rom_read8(g, 0xCEF146u + 2u * pace + slot);
}

/* ---- Debugging ---------------------------------------------------------------------------- */

/* Whether the battle's debug frame advance is on: a byte of the ROM ($CD:3FFF),
 * 0 in the released game. battle/nmi.c asks with the machine until its port
 * takes the state. */
static inline bool battle_debug_frame_advance(const GameState *g) { return rom_read8(g, 0xCD3FFFu) != 0; }
static inline bool battle_frame_advance_enabled(Snes *s) { return battle_debug_frame_advance(state_of(s)); }

#endif
