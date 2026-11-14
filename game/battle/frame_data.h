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

/* The sound each of the battle's own messages plays as it opens ($D1:EE99, a
 * byte a message; BATTLE_MESSAGE_SILENT for none). Message 13, nothing to
 * steal, reads into the following screen routine in the original.
 * The resource includes that live sound selector as a data view. */
enum { BATTLE_MESSAGE_SILENT = 0xFF };

/* ---- The line effects' arrangements -------------------------------------------------------- */

/* The ways the screen is laid out for the per-line effects, 32 of them
 * ($CE:F24E, a word each, its index a byte of twice the arrangement): each the
 * byte offset of its ten lists of runs of lines among the lists' places at
 * $CE:F58E (a word each, the place in bank $CE of the list's 30 bytes). The
 * first nine are the nine per-line tables' (the seven effects' and the
 * window's two); the tenth stands in for the BG2 scroll's on a battlefield
 * whose background ripples. */
enum { LINE_ARRANGEMENT_LISTS = 10, LINE_ARRANGEMENT_LIST_BYTES = 30, RIPPLING_BG2_SCROLL_LIST = 9 };

/* ---- What the uploads send from the ROM -------------------------------------------------- */

/* ---- The battlefields ------------------------------------------------------------------- */

/* What a battlefield brings with it ($D1:6240, four bytes a battlefield): the
 * screen scripts its two channels run and the two sets of background colors
 * it shows, $FF for none. The index is a word, four times the battlefield. */
typedef struct {
    uint8_t scripts[2], color_sets[2];
} BattlefieldLook;

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

/* A fade the battle runs through palette animation slot 10's preset, laid
 * out as a palette animation slot from its first color: which colors, how
 * fast, and from what level to what (battle_fade_through_the_preset). */
typedef struct {
    uint8_t first_color, color_count, step, delay, period, fade_mode, fade_level, fade_target;
} BattlePaletteFade;

/* The fades by record: the plain battlefield's out, then back in; the HP
 * gradient's from the fifth record. */
enum { BATTLEFIELD_FADE_BYTES = 8, BATTLEFIELD_GRADIENT_FADES = 4 };

static inline BattleQueuedTransfer battle_queued_transfer(const GameState *g, uint8_t slot, unsigned second)
{
    uint16_t at = (uint16_t)((uint8_t)(slot * 2) + second * 2 * BATTLE_QUEUED_SLOTS);
    return (BattleQueuedTransfer){ asset_read16(g, ASSET_BATTLE_QUEUED_TRANSFER, at),
                                   asset_read16(g, ASSET_BATTLE_QUEUED_TRANSFER_F967, at),
                                   asset_read16(g, ASSET_BATTLE_QUEUED_TRANSFER_F977, at) };
}

/* ---- The battle message ------------------------------------------------------------------ */

/* The frames a battle message stays up once set, at each of the player's
 * eight message speeds ($CD:019D, a byte each, the fastest first). */
static inline uint8_t battle_message_frames_at_speed(const GameState *g, uint8_t speed)
{
    return asset_read8(g, ASSET_BATTLE_MESSAGE_FRAMES_AT_SPEED, (speed & 0x07u));
}

static inline uint8_t battle_message_sound(const GameState *g, uint8_t message)
{
    return asset_read8(g, ASSET_BATTLE_MESSAGE_SOUND, message);
}

/* ---- Blocks of rows ---------------------------------------------------------------------- */

/* Where each of the four blocks a routine can ask for goes in VRAM ($CD:05FF,
 * a word a block). The block is remembered, and the table indexed, by a byte
 * of twice its number: a block past the four reads the code that follows. */
static inline uint16_t battle_block_vram(const GameState *g, uint8_t block_offset)
{
    return asset_read16(g, ASSET_BATTLE_BLOCK_VRAM, block_offset);
}

/* ---- The animated color ------------------------------------------------------------------ */

/* The color the cursor's palette entry cycles through, one a phase ($D1:5980).
 * The index is the phase doubled, whose high byte is the caller's. */
static inline uint16_t battle_animated_color(const GameState *g, uint16_t phase_offset)
{
    return asset_read16(g, ASSET_BATTLE_ANIMATED_COLOR, phase_offset);
}

static inline uint8_t line_arrangement_byte(const GameState *g, uint8_t arrangement, unsigned list, unsigned i)
{
    uint16_t lists = asset_read16(g, ASSET_LINE_ARRANGEMENT_BYTE, (uint8_t)(arrangement * 2));
    uint16_t at = asset_read16(g, ASSET_LINE_ARRANGEMENT_BYTE_F58E, (uint16_t)(lists + 2u * list));
    return asset_read8_at_place(g, ASSET_LINE_ARRANGEMENT_BYTE_F58E, (uint16_t)(at + i));
}

static inline uint16_t background_ripple_sway(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_BACKGROUND_RIPPLE_SWAY, 2u * i);
}

static inline BattlefieldLook battlefield_look(const GameState *g, uint16_t battlefield)
{
    uint32_t at = (uint16_t)(battlefield * 4u);
    return (BattlefieldLook){
        { asset_read8(g, ASSET_BATTLEFIELD_LOOK, at), asset_read8(g, ASSET_BATTLEFIELD_LOOK, at + 1) },
        { asset_read8(g, ASSET_BATTLEFIELD_LOOK, at + 2), asset_read8(g, ASSET_BATTLEFIELD_LOOK, at + 3) }
    };
}

static inline BattlefieldLayout battlefield_layout(const GameState *g, uint16_t battlefield)
{
    uint32_t at = (uint16_t)((uint16_t)((battlefield & 0x7FFFu) << 1) * 3u);
    return (BattlefieldLayout){ asset_read8(g, ASSET_BATTLEFIELD_LAYOUT, at),
                                asset_read8(g, ASSET_BATTLEFIELD_LAYOUT, at + 1),
                                asset_read16(g, ASSET_BATTLEFIELD_LAYOUT, at + 2),
                                asset_read8(g, ASSET_BATTLEFIELD_LAYOUT, at + 4),
                                asset_read8(g, ASSET_BATTLEFIELD_LAYOUT, at + 5) };
}

/* Where each strip of the battlefields' maps is in bank $D1 ($D1:A575, a word
 * a strip), and a byte of a strip, wherever it is: its step (the first byte),
 * which of its places have a tile (two bytes a row), then a byte for each tile,
 * its 16x16 tile in the set's sheet and its flips. */
static inline uint16_t battlefield_strip_place(const GameState *g, uint16_t strip)
{
    return asset_read16(g, ASSET_BATTLEFIELD_STRIP_PLACE, (uint16_t)(strip * 2u));
}

/* A byte of the strips, at a place in their bank the strip's place gives. */
static inline uint8_t battlefield_strip_byte(const GameState *g, uint16_t place)
{
    return asset_read8_at_place(g, ASSET_BATTLEFIELD_STRIPS, place);
}

/* The strips' bank, which the map's builder leaves in a cell. */
static inline uint8_t battlefield_strips_bank(void) { return (uint8_t)(asset_address(ASSET_BATTLEFIELD_STRIPS) >> 16); }

/* Which tiles of the battlefield's sheet take the layer's other palette
 * ($CF:E200): a bit a 16x16 tile, a byte each row of eight, the tile set's
 * byte its first. */
static inline uint8_t battlefield_tile_palettes(const GameState *g, uint8_t at)
{
    return asset_read8(g, ASSET_BATTLEFIELD_TILE_PALETTES, at);
}

/* The bit of such a byte for each column of the sheet ($CE:F03E), and each of
 * a strip's sixteen columns' place in its row of the map, in bytes
 * ($CE:F046). */
static inline uint8_t battlefield_palette_bit_mask(const GameState *g, uint8_t column)
{
    return asset_read8(g, ASSET_BATTLEFIELD_PALETTE_BIT_MASK, column);
}

static inline uint8_t battlefield_column_place(const GameState *g, uint8_t column)
{
    return asset_read8(g, ASSET_BATTLEFIELD_COLUMN_PLACE, column);
}

/* The battlefield's 8x8 tiles: a tile set's descriptors ($CF:8000, 64 bytes a
 * set, wrapping in the bank as the original's pointer does), each a tile's
 * graphics (in 16-byte units from $CF:0000, two bit planes) and whether it is
 * mirrored or flipped. */
static inline uint16_t battlefield_tile_descriptor(const GameState *g, uint16_t at)
{
    return asset_read16_at_place(g, ASSET_BATTLEFIELD_TILE_SET_DESCRIPTORS, at);
}

static inline uint8_t battlefield_tile_graphics_byte(const GameState *g, uint32_t at)
{
    return asset_read8(g, ASSET_BATTLEFIELD_TILES, at);
}

/* Where each of the four tile sets of a battlefield's layer goes in VRAM
 * ($CE:EFF2, a word a set). */
static inline uint16_t battlefield_tile_set_vram(const GameState *g, uint8_t set)
{
    return asset_read16(g, ASSET_BATTLEFIELD_TILE_SET_VRAM, set * 2u);
}

/* Where each of the six slots of an effect's tile rows goes in VRAM ($CD:1308,
 * a word a slot; the index a byte of twice the slot). */
static inline uint16_t effect_tile_rows_vram(const GameState *g, uint8_t slot)
{
    return asset_read16(g, ASSET_BATTLE_PALETTE_PLANE_MASK, 0x8u + (uint8_t)(slot * 2));
}

static inline BattlePaletteFade battle_palette_fade_at(const GameState *g, AssetId fades, uint32_t at)
{
    uint8_t b[BATTLEFIELD_FADE_BYTES];
    for (unsigned i = 0; i < BATTLEFIELD_FADE_BYTES; i++)
        b[i] = asset_read8(g, fades, at + i);
    return (BattlePaletteFade){ b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7] };
}

/* A beaten enemy's colors fade to white as it dissolves ($CE:EFE1: the
 * animation's kind, then the fade). */
static inline uint8_t battle_dissolve_fade_kind(const GameState *g)
{
    return asset_read8(g, ASSET_BATTLE_DISSOLVE_FADE_KIND, 0);
}

static inline BattlePaletteFade battle_dissolve_fade(const GameState *g) { return battle_palette_fade_at(g, ASSET_BATTLE_DISSOLVE_FADE, 0); }

/* The fade that takes the old battlefield's layer away before a new one is
 * loaded ($CE:EFFA, its kind by whether the battle started with color math
 * on the backdrop; $CE:EFFE, eight bytes a fade: the fades out, and after
 * each the fade back in). */

static inline uint8_t battlefield_fade_kind(const GameState *g, uint8_t backdrop_math)
{
    return asset_read8(g, ASSET_BATTLEFIELD_FADE_KIND, backdrop_math);
}

static inline BattlePaletteFade battlefield_fade(const GameState *g, bool gradient, bool back_in)
{
    unsigned record = (gradient ? BATTLEFIELD_GRADIENT_FADES : 0) + (back_in ? 1 : 0);
    return battle_palette_fade_at(g, ASSET_BATTLEFIELD_FADES, record * BATTLEFIELD_FADE_BYTES);
}

/* Tiles selected by battle loads and queued uploads; built tiles may live in RAM. */
static inline PfData battle_upload_data(uint32_t source, uint32_t bytes) { return rom_data(source, bytes); }

static inline PfData battle_described_upload_data(uint32_t source, uint32_t bytes, uint8_t mode)
{
    return rom_data_mode(source, bytes, mode);
}

#endif
