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

#include <stdbool.h>
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

/* ---- The battle message ------------------------------------------------------------------ */

/* The frames a battle message stays up once set, at each of the player's
 * eight message speeds ($CD:019D, a byte each, the fastest first). */
static inline uint8_t battle_message_frames_at_speed(const GameState *g, uint8_t speed)
{
    return rom_read8(g, 0xCD019Du + (speed & 0x07u));
}

/* The sound each of the battle's own messages plays as it opens ($D1:EE99, a
 * byte a message; BATTLE_MESSAGE_SILENT for none). */
enum { BATTLE_MESSAGE_SILENT = 0xFF };
static inline uint8_t battle_message_sound(const GameState *g, uint8_t message)
{
    return rom_read8(g, 0xD1EE99u + message);
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

/* ---- The battlefields ------------------------------------------------------------------- */

/* What a battlefield brings with it ($D1:6240, four bytes a battlefield): the
 * screen scripts its two channels run and the two sets of background colors
 * it shows, $FF for none. The index is a word, four times the battlefield. */
typedef struct {
    uint8_t scripts[2], color_sets[2];
} BattlefieldLook;

static inline BattlefieldLook battlefield_look(const GameState *g, uint16_t battlefield)
{
    uint32_t at = 0xD16240u + (uint16_t)(battlefield * 4u);
    return (BattlefieldLook){ { rom_read8(g, at), rom_read8(g, at + 1) }, { rom_read8(g, at + 2), rom_read8(g, at + 3) } };
}

/* How a battlefield's BG3 layer is laid out ($D1:A233, six bytes a
 * battlefield, by its number less the top bit, the offset a word): the strips
 * of map it is built from (the low six bits; bit 6 the tile set's high bit),
 * its tile set, the first strip, a byte nothing is found reading, and the rows
 * of tiles each strip has. */
typedef struct {
    uint8_t strips, tile_set;
    uint16_t first_strip;
    uint8_t kept, rows;
} BattlefieldLayout;

static inline BattlefieldLayout battlefield_layout(const GameState *g, uint16_t battlefield)
{
    uint32_t at = 0xD1A233u + (uint16_t)((uint16_t)((battlefield & 0x7FFFu) << 1) * 3u);
    return (BattlefieldLayout){ rom_read8(g, at), rom_read8(g, at + 1), rom_read16(g, at + 2), rom_read8(g, at + 4),
                                rom_read8(g, at + 5) };
}

/* Where each strip of the battlefields' maps is in bank $D1 ($D1:A575, a word
 * a strip), and a byte of a strip, wherever it is: its step (the first byte),
 * which of its places have a tile (two bytes a row), then a byte for each tile,
 * its 16x16 tile in the set's sheet and its flips. */
static inline uint16_t battlefield_strip_place(const GameState *g, uint16_t strip)
{
    return rom_read16(g, 0xD1A575u + (uint16_t)(strip * 2u));
}
static inline uint8_t battlefield_strip_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }
enum { BATTLEFIELD_STRIP_BANK = 0xD10000 };

/* Which tiles of the battlefield's sheet take the layer's other palette
 * ($CF:E200): a bit a 16x16 tile, a byte each row of eight, the tile set's
 * byte its first. */
static inline uint8_t battlefield_tile_palettes(const GameState *g, uint8_t at) { return rom_read8(g, 0xCFE200u + at); }

/* The bit of such a byte for each column of the sheet ($CE:F03E), and each of
 * a strip's sixteen columns' place in its row of the map, in bytes
 * ($CE:F046). */
static inline uint8_t battlefield_palette_bit_mask(const GameState *g, uint8_t column) { return rom_read8(g, 0xCEF03Eu + column); }
static inline uint8_t battlefield_column_place(const GameState *g, uint8_t column) { return rom_read8(g, 0xCEF046u + column); }

/* The battlefield's 8x8 tiles: a tile set's descriptors ($CF:8000, 64 bytes a
 * set, wrapping in the bank as the original's pointer does), each a tile's
 * graphics (in 16-byte units from $CF:0000, two bit planes) and whether it is
 * mirrored or flipped. */
static inline uint16_t battlefield_tile_descriptor(const GameState *g, uint16_t at) { return rom_read16(g, 0xCF0000u | at); }
static inline uint8_t battlefield_tile_graphics_byte(const GameState *g, uint32_t at) { return rom_read8(g, 0xCF0000u + at); }

/* Where each of the four tile sets of a battlefield's layer goes in VRAM
 * ($CE:EFF2, a word a set). */
static inline uint16_t battlefield_tile_set_vram(const GameState *g, uint8_t set) { return rom_read16(g, 0xCEEFF2u + set * 2u); }

/* Where each of the six slots of an effect's tile rows goes in VRAM ($CD:1308,
 * a word a slot; the index a byte of twice the slot). */
static inline uint16_t effect_tile_rows_vram(const GameState *g, uint8_t slot)
{
    return rom_read16(g, 0xCD1308u + (uint8_t)(slot * 2));
}

/* The fade that takes the old battlefield's layer away before a new one is
 * loaded ($CE:EFFA, its kind by whether the battle started with color math
 * on the backdrop; $CE:EFFE, eight bytes a fade, laid out as a palette
 * animation slot from its first color: the fades out, and after each the fade
 * back in). */
typedef struct {
    uint8_t first_color, color_count, step, delay, period, fade_mode, fade_level, fade_target;
} BattlefieldFade;

static inline uint8_t battlefield_fade_kind(const GameState *g, uint8_t backdrop_math)
{
    return rom_read8(g, 0xCEEFFAu + backdrop_math);
}
/* The fades by record: the plain battlefield's out, then back in; the HP
 * gradient's from the fifth record. */
enum { BATTLEFIELD_FADE_BYTES = 8, BATTLEFIELD_GRADIENT_FADES = 4 };
static inline BattlefieldFade battlefield_fade(const GameState *g, bool gradient, bool back_in)
{
    unsigned record = (gradient ? BATTLEFIELD_GRADIENT_FADES : 0) + (back_in ? 1 : 0);
    uint32_t from = 0xCEEFFEu + record * BATTLEFIELD_FADE_BYTES;
    return (BattlefieldFade){ rom_read8(g, from), rom_read8(g, from + 1), rom_read8(g, from + 2), rom_read8(g, from + 3),
                              rom_read8(g, from + 4), rom_read8(g, from + 5), rom_read8(g, from + 6), rom_read8(g, from + 7) };
}

#endif
