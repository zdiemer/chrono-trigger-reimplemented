/* Battle: what the frame's display work reads from the ROM.
 *
 * Every cartridge read the frame work, the uploads and the display setup make
 * goes through here, by what the data is (game/rom.h); so do the places in
 * the cartridge the uploads send from, which a build that loads its graphics
 * from files would point somewhere else. Where the original indexes a table
 * with a byte-wide offset, the accessor takes what the offset is made from and
 * says how it wraps. */
#ifndef CT_GAME_BATTLE_FRAME_DATA_H
#define CT_GAME_BATTLE_FRAME_DATA_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

/* ---- The queued VRAM uploads ------------------------------------------------------------- */

/* Four uploads the vblank makes in turn while the combatants animate
 * ($CC:F957): each two transfers from bank $7E, the first always and the
 * second where its size isn't 0. The sources, VRAM addresses and sizes are
 * tables of four words each, the second transfers' after the firsts'. The
 * index is a byte of twice the slot, so a slot past the four reads on into
 * what follows, as the original's does. */
typedef struct {
    uint16_t source, vram, size;
} BattleQueuedTransfer;

enum { BATTLE_QUEUED_SLOTS = 4 };

static inline BattleQueuedTransfer battle_queued_transfer(const GameState *g, uint8_t slot, unsigned second)
{
    uint16_t at = (uint16_t)((uint8_t)(slot * 2) + second * 2 * BATTLE_QUEUED_SLOTS);
    return (BattleQueuedTransfer){ rom_read16(g, 0xCCF957u + at), rom_read16(g, 0xCCF967u + at),
                                   rom_read16(g, 0xCCF977u + at) };
}

/* ---- Blocks of rows ---------------------------------------------------------------------- */

/* Where each of the four blocks a routine can ask for goes in VRAM ($CD:05FF,
 * a word a block). The block is remembered, and the table indexed, by a byte
 * of twice its number: a block past the four reads the code that follows. */
static inline uint16_t battle_block_vram(const GameState *g, uint8_t block_offset)
{
    return rom_read16(g, 0xCD05FFu + block_offset);
}

/* ---- The animated color ------------------------------------------------------------------ */

/* The color the cursor's palette entry cycles through, one a phase ($D1:5980).
 * The index is the phase doubled, whose high byte is the caller's. */
static inline uint16_t battle_animated_color(const GameState *g, uint16_t phase_offset)
{
    return rom_read16(g, 0xD15980u + phase_offset);
}

/* ---- The line effects' arrangements -------------------------------------------------------- */

/* The ways the screen is laid out for the per-line effects, 32 of them
 * ($CE:F24E, a word each, its index a byte of twice the arrangement): each the
 * byte offset of its ten lists of runs of lines among the lists' places at
 * $CE:F58E (a word each, the place in bank $CE of the list's 30 bytes). The
 * first nine are the nine per-line tables' (the seven effects' and the
 * window's two); the tenth stands in for the BG2 scroll's on a battlefield
 * whose background ripples. */
enum { LINE_ARRANGEMENT_LISTS = 10, LINE_ARRANGEMENT_LIST_BYTES = 30, RIPPLING_BG2_SCROLL_LIST = 9 };

static inline uint8_t line_arrangement_byte(const GameState *g, uint8_t arrangement, unsigned list, unsigned i)
{
    uint16_t lists = rom_read16(g, 0xCEF24Eu + (uint8_t)(arrangement * 2));
    uint16_t at = rom_read16(g, 0xCEF58Eu + (uint16_t)(lists + 2u * list));
    return rom_read8(g, 0xCE0000u + (uint16_t)(at + i));
}

/* ---- The background's ripple -------------------------------------------------------------- */

/* The sway of a rippling background ($CE:E08E): 128 signed words, two
 * periods of a wave a column or two either way, which the ripple shows 64 at
 * a time. */
enum { RIPPLE_SWAYS = 128 };

static inline uint16_t background_ripple_sway(const GameState *g, unsigned i) { return rom_read16(g, 0xCEE08Eu + 2u * i); }

/* ---- What the uploads send from the ROM -------------------------------------------------- */

/* A zero byte, which tiles are cleared from ($CD:06B2). */
enum { BATTLE_ZERO_BYTE = 0xCD06B2 };

/* The graphics a battle loads from the ROM as it is set up
 * (battle_load_graphics): the first tiles of the layer the battle's messages
 * are drawn on, and five 64-byte blocks of the window layer's tiles (what
 * each block shows isn't mapped yet: docs/TODO.md). */
enum {
    BATTLE_MESSAGE_LAYER_TILES = 0xFF8C60,
    BATTLE_WINDOW_TILE_BLOCK_1 = 0xD15E82,
    BATTLE_WINDOW_TILE_BLOCK_2 = 0xD15EC2,
    BATTLE_WINDOW_TILE_BLOCK_3 = 0xD15F02,
    BATTLE_WINDOW_TILE_BLOCK_4 = 0xFF9CF0,
    BATTLE_WINDOW_TILE_BLOCK_5 = 0xFF9D30,
};

#endif
