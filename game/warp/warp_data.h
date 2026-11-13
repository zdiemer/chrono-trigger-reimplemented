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

/* The circle's radius on each of the 32 frames the tunnel takes to open (and
 * to close), from a dot to a little past the screen's half height. */
enum { WARP_CIRCLE_FRAMES = 32 };

/* The flickering warp's brightness, a step of its pattern each: dark, then up
 * to full, held. `past` is how many times 256 bytes past the pattern the
 * original reads, which its first frame does (warp/warp.c, flicker): 0 but
 * there, and taken as the original's index takes it, into the next bank. */
enum { WARP_FLICKER_STEPS = 32 };

/* The light at the tunnel's end is 16 large sprites, each an entry as the
 * sprite table holds it: X, Y, tile, attributes (which quarter of the light it
 * shows, flipped). */
enum { WARP_LIGHT_SPRITES = 16 };
typedef struct {
    uint8_t x, y, tile, attributes;
} WarpSprite;

/* The tunnel's colours, Mode 7's first 64: a red ramp and a blue ramp, up and
 * down again across them. */
enum { WARP_TUNNEL_COLOURS = 64 };

/* The light's colours: every sprite palette the same 16 shades of blue. */
enum { WARP_LIGHT_SHADES = 16 };

/* How far the pulsing colours (16-47 of the tunnel's) swing, each. */
enum { WARP_PULSING_COLOURS = 32 };

/* The console's sine: a quarter turn is 64, and the value is signed, 127 at
 * the top. The warp reads it as the scenes do, from bank $C0's table. */
static inline int8_t warp_sine(const GameState *g, uint8_t angle)
{
    return (int8_t)asset_read8(g, ASSET_DIRECTION_FACTORS, angle);
}

static inline uint8_t warp_circle_radius_on(const GameState *g, uint8_t frame)
{
    return asset_read8(g, ASSET_WARP_CIRCLE_RADIUS_ON, frame % WARP_CIRCLE_FRAMES);
}

static inline uint8_t warp_flicker_brightness(const GameState *g, uint8_t step, uint8_t past)
{
    return asset_read8(g, ASSET_WARP_CIRCLE_RADIUS_ON, 0x5Fu + (uint16_t)(past << 8 | step % WARP_FLICKER_STEPS));
}

static inline WarpSprite warp_light_sprite(const GameState *g, unsigned i)
{
    uint32_t at = 0x1Fu + 4u * (i % WARP_LIGHT_SPRITES);
    return (WarpSprite){ asset_read8(g, ASSET_WARP_CIRCLE_RADIUS_ON, at),
                         asset_read8(g, ASSET_WARP_CIRCLE_RADIUS_ON, at + 1),
                         asset_read8(g, ASSET_WARP_CIRCLE_RADIUS_ON, at + 2),
                         asset_read8(g, ASSET_WARP_CIRCLE_RADIUS_ON, at + 3) };
}

static inline uint8_t warp_tunnel_red(const GameState *g, unsigned colour)
{
    return (uint8_t)asset_read16(g, ASSET_WARP_TUNNEL_RED, 2u * (colour % WARP_TUNNEL_COLOURS));
}

static inline uint8_t warp_tunnel_blue(const GameState *g, unsigned colour)
{
    return (uint8_t)asset_read16(g, ASSET_WARP_TUNNEL_RED, 0x80u + 2u * (colour % WARP_TUNNEL_COLOURS));
}

static inline uint8_t warp_light_blue(const GameState *g, unsigned shade)
{
    return (uint8_t)asset_read16(g, ASSET_WARP_TUNNEL_RED, 0x100u + 2u * (shade % WARP_LIGHT_SHADES));
}

static inline uint8_t warp_pulse_reach(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_WARP_PULSE_REACH, 2u * (i % WARP_PULSING_COLOURS));
}

#endif
