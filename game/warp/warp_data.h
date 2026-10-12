/* What the time warp reads from the ROM: the sine it shapes its plane and its
 * light with, the ramps its palette is made of, the circle's sizes as it opens
 * and closes, the light's sprites and the flickering warp's brightness.
 *
 * Every read goes through an accessor here, named for what the data is for, so
 * a build that loads the data from somewhere other than the ROM image changes
 * this file and nothing that uses it. */
#ifndef CT_GAME_WARP_DATA_H
#define CT_GAME_WARP_DATA_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The console's sine: a quarter turn is 64, and the value is signed, 127 at
 * the top. The warp reads it as the scenes do, from bank $C0's table. */
static inline int8_t warp_sine(const GameState *g, uint8_t angle) { return (int8_t)rom_read8(g, 0xC0F800u + angle); }

/* The circle's radius on each of the 32 frames the tunnel takes to open (and
 * to close), from a dot to a little past the screen's half height. */
enum { WARP_CIRCLE_FRAMES = 32 };
static inline uint8_t warp_circle_radius_on(const GameState *g, uint8_t frame)
{
    return rom_read8(g, 0xFDDD96u + frame % WARP_CIRCLE_FRAMES);
}

/* The flickering warp's brightness, a step of its pattern each: dark, then up
 * to full, held. `past` is how many times 256 bytes past the pattern the
 * original reads, which its first frame does (warp/warp.c, flicker): 0 but
 * there, and taken as the original's index takes it, into the next bank. */
enum { WARP_FLICKER_STEPS = 32 };
static inline uint8_t warp_flicker_brightness(const GameState *g, uint8_t step, uint8_t past)
{
    return rom_read8(g, 0xFDDDF5u + (uint16_t)(past << 8 | step % WARP_FLICKER_STEPS));
}

/* The light at the tunnel's end is 16 large sprites, each an entry as the
 * sprite table holds it: X, Y, tile, attributes (which quarter of the light it
 * shows, flipped). */
enum { WARP_LIGHT_SPRITES = 16 };
typedef struct {
    uint8_t x, y, tile, attributes;
} WarpSprite;
static inline WarpSprite warp_light_sprite(const GameState *g, unsigned i)
{
    uint32_t at = 0xFDDDB5u + 4u * (i % WARP_LIGHT_SPRITES);
    return (WarpSprite){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2), rom_read8(g, at + 3) };
}

/* The tunnel's colours, Mode 7's first 64: a red ramp and a blue ramp, up and
 * down again across them. */
enum { WARP_TUNNEL_COLOURS = 64 };
static inline uint8_t warp_tunnel_red(const GameState *g, unsigned colour)
{
    return (uint8_t)rom_read16(g, 0xFDF001u + 2u * (colour % WARP_TUNNEL_COLOURS));
}
static inline uint8_t warp_tunnel_blue(const GameState *g, unsigned colour)
{
    return (uint8_t)rom_read16(g, 0xFDF081u + 2u * (colour % WARP_TUNNEL_COLOURS));
}

/* The light's colours: every sprite palette the same 16 shades of blue. */
enum { WARP_LIGHT_SHADES = 16 };
static inline uint8_t warp_light_blue(const GameState *g, unsigned shade)
{
    return (uint8_t)rom_read16(g, 0xFDF101u + 2u * (shade % WARP_LIGHT_SHADES));
}

/* How far the pulsing colours (16-47 of the tunnel's) swing, each. */
enum { WARP_PULSING_COLOURS = 32 };
static inline uint8_t warp_pulse_reach(const GameState *g, unsigned i)
{
    return rom_read8(g, 0xFDF181u + 2u * (i % WARP_PULSING_COLOURS));
}

#endif
