/* Battle: decoding the battlefield's tiles for upload.
 *
 * The battlefield's graphics are kept in the ROM as rows of 32 tiles, each
 * named by a descriptor: where its graphics start, whether it has a third bit
 * plane, and whether it is mirrored left to right or flipped top to bottom.
 * A tile's graphics are two bit planes, or three; decoded, it becomes a
 * four-plane tile whose upper two planes give each of its drawn pixels the
 * palette group its row's palette word gives its pair of tiles (a third plane
 * of its own adds to that), so that one two-plane tile can be drawn in four
 * sets of colors. The decoded row goes into the decode buffer
 * (battle_decoded_tiles), which the battle's setup then uploads (its load step
 * at $CD:1331). */
#include "battle/animation_data.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "original_effects.h"
#include "vars.h"

/* A descriptor: the tile's graphics start at 8 bytes a number; it has only
 * the two lower bit planes; it is mirrored; it is flipped. */
enum { TILE_GRAPHICS = 0x1FFF, TWO_PLANES = 0x2000, MIRRORED = 0x4000, FLIPPED = 0x8000 };
enum { GRAPHICS_UNIT = 8, TILE_ROWS = 8, TILE_BYTES = 32, HALF_TILE_BYTES = 16, DESCRIPTOR_BYTES = 2 };

/* A row set's descriptors are 64 bytes apart, and its palette word a word
 * each. */
enum { ROW_SET_BYTES = 64, TILES_PER_PALETTE_WORD = 16, TILES_PER_PALETTE = 2, PALETTE_BITS = 2, PALETTE_GROUP = 0x3 };

/* The palette group of a tile of the row: two bits of the row's palette word
 * for every two tiles, a byte of it for every eight, the word serving each
 * sixteen of the row. */
enum { TILES_PER_PALETTE_BYTE = 8 };
static uint8_t tile_palette_byte(uint16_t palettes, unsigned tile)
{
    return (uint8_t)(palettes >> tile % TILES_PER_PALETTE_WORD / TILES_PER_PALETTE_BYTE * 8);
}
static uint8_t tile_palette(uint16_t palettes, unsigned tile)
{
    unsigned shift = tile % TILES_PER_PALETTE_BYTE / TILES_PER_PALETTE * PALETTE_BITS;
    return (uint8_t)(tile_palette_byte(palettes, tile) >> shift & PALETTE_GROUP);
}

/* A row of a decoded tile, as its two bit planes are kept: the lower two in
 * the tile's first half, the upper two in its second, a word a row. */
static uint16_t tile_word(const GameState *g, uint16_t at)
{
    return (uint16_t)(battle_decoded_tiles(g, at) | battle_decoded_tiles(g, at + 1u) << 8);
}
static void set_tile_word(GameState *g, uint16_t at, uint16_t v)
{
    set_battle_decoded_tiles(g, at, (uint8_t)v);
    set_battle_decoded_tiles(g, at + 1u, (uint8_t)(v >> 8));
}

static uint16_t swap_bytes(uint16_t v) { return (uint16_t)(v >> 8 | v << 8); }

/* The upper planes of a row: its drawn pixels (either lower plane set) in
 * both, kept in the planes the palette group's mask lets through. A third
 * plane of the tile's own sets its pixels in the lower of the two, and lets
 * the group's upper plane through the lower plane's mask, as the original's
 * byte swap has it. */
static uint16_t upper_planes(uint16_t lower, uint16_t mask) { return (uint16_t)((lower | swap_bytes(lower)) & mask); }
static uint16_t with_third_plane(uint16_t upper, uint8_t third, uint16_t mask)
{
    return (uint16_t)(swap_bytes((uint16_t)((upper | third) & mask)) | third);
}

/* What the original leaves of the last tile it decoded: its descriptor, the
 * palette byte and mask it used, and the last row of a third plane. */
enum { LEFT_ROW_SET = 0x12, LEFT_PALETTE_BYTE = 0x18, LEFT_MASK = 0x1A, LEFT_DESCRIPTOR = 0x20 };
static void leave_word(GameState *g, unsigned at, uint16_t v)
{
    set_battle_scratch(g, at, (uint8_t)v);
    set_battle_scratch(g, at + 1, (uint8_t)(v >> 8));
}
static void leave_tile(GameState *g, uint16_t descriptor, uint8_t palette_byte, uint16_t mask)
{
    leave_word(g, LEFT_DESCRIPTOR, descriptor);
    set_battle_scratch(g, LEFT_PALETTE_BYTE, palette_byte);
    leave_word(g, LEFT_MASK, mask);
}
static void leave_third_plane(GameState *g, uint8_t last_row) { leave_word(g, LEFT_ROW_SET, last_row); }

// $CD:1509
/* Decode a tile into the decode buffer at `at`, its upper planes masked for
 * its palette group. */
static void decode_tile(GameState *g, uint16_t descriptor, uint16_t mask, uint16_t at)
{
    uint32_t graphics = (uint16_t)((descriptor & TILE_GRAPHICS) * GRAPHICS_UNIT);
    for (unsigned row = 0; row < TILE_ROWS; row++) {
        uint16_t lower = battle_field_graphics_word(g, graphics + row * 2u);
        set_tile_word(g, (uint16_t)(at + row * 2), lower);
        set_tile_word(g, (uint16_t)(at + HALF_TILE_BYTES + row * 2), upper_planes(lower, mask));
    }
    if (descriptor & TWO_PLANES)
        return;
    uint8_t third = 0;
    for (unsigned row = 0; row < TILE_ROWS; row++) {
        third = (uint8_t)battle_field_graphics_word(g, graphics + HALF_TILE_BYTES + row);
        uint16_t upper_at = (uint16_t)(at + HALF_TILE_BYTES + row * 2);
        set_tile_word(g, upper_at, with_third_plane(tile_word(g, upper_at), third, mask));
    }
    ORIGINAL_EFFECT(leave_third_plane(g, third));
}

/* The original swaps rows through a cell; it is left the last row it moved. */
enum { LEFT_SWAPPED = 0x24 };

// $CD:13FE
/* Flip a tile top to bottom: each half's rows in the other order. */
static void flip_tile(GameState *g, uint16_t at)
{
    for (unsigned half = 0; half < TILE_BYTES; half += HALF_TILE_BYTES) {
        uint16_t kept = 0;
        for (unsigned row = 0; row < TILE_ROWS / 2; row++) {
            uint16_t top = (uint16_t)(at + half + row * 2), bottom = (uint16_t)(at + half + (TILE_ROWS - 1 - row) * 2);
            kept = tile_word(g, top);
            set_tile_word(g, top, tile_word(g, bottom));
            set_tile_word(g, bottom, kept);
        }
        ORIGINAL_EFFECT(leave_word(g, LEFT_SWAPPED, kept));
    }
}

/* The original counts the mirrored rows down in a cell. */
enum { LEFT_ROWS_TO_MIRROR = 0x1C };

// $CD:144A
/* Mirror a tile left to right: every byte of its planes. */
static void mirror_tile(GameState *g, uint16_t at)
{
    for (unsigned b = 0; b < TILE_BYTES; b++) {
        uint16_t byte = (uint16_t)(at + b);
        set_battle_decoded_tiles(g, byte, battle_mirrored_byte(g, battle_decoded_tiles(g, byte)));
    }
    ORIGINAL_EFFECT(set_battle_scratch(g, LEFT_ROWS_TO_MIRROR, 0));
}

/* What the original leaves of the row: where its descriptors ended (the
 * pointer's bank twice over), the row set twice over, the tiles decoded, and
 * none left to count; and the row's palette word, twice. */
enum { LEFT_DESCRIPTORS = 0x0E, LEFT_TILES_DONE = 0x16, LEFT_TILES_LEFT = 0x22 };
enum { DESCRIPTOR_BANK = 0xCF, DESCRIPTOR_TABLE = 0xA000 };
enum { LEFT_DESCRIPTOR_BANK = 0x10, LEFT_SECOND_BANK = 0x14 };
static void leave_row(GameState *g, uint16_t row_set, uint16_t palettes)
{
    set_battle_scratch(g, LEFT_DESCRIPTOR_BANK, DESCRIPTOR_BANK);
    set_battle_scratch(g, LEFT_SECOND_BANK, DESCRIPTOR_BANK);
    leave_word(g, LEFT_ROW_SET, (uint16_t)(row_set * 2));
    set_battle_tile_row_palettes(g, 0, palettes);
    set_battle_tile_row_palettes(g, 1, palettes);
}
static void leave_row_end(GameState *g, uint16_t descriptors, uint16_t tiles)
{
    leave_word(g, LEFT_DESCRIPTORS, (uint16_t)(DESCRIPTOR_TABLE + descriptors));
    leave_word(g, LEFT_TILES_DONE, tiles);
    leave_word(g, LEFT_TILES_LEFT, 0);
}

// $CD:1368
/* Decode `tiles` tiles of a row set into the decode buffer from its start. */
static void decode_tile_row(GameState *g, uint16_t row_set, uint16_t tiles)
{
    uint16_t descriptors = (uint16_t)(row_set * ROW_SET_BYTES);
    uint16_t palettes = battle_tile_row_palette_word(g, (uint16_t)(row_set * 2));
    ORIGINAL_EFFECT(leave_row(g, row_set, palettes));
    uint16_t at = 0;
    for (uint16_t tile = 0; tile < tiles; tile++) {
        uint16_t descriptor = battle_tile_descriptor(g, descriptors);
        uint8_t palette = tile_palette(palettes, tile);
        uint16_t mask = battle_palette_plane_mask(g, palette);
        decode_tile(g, descriptor, mask, at);
        if (descriptor & FLIPPED)
            flip_tile(g, at);
        if (descriptor & MIRRORED)
            mirror_tile(g, at);
        ORIGINAL_EFFECT(leave_tile(g, descriptor, tile_palette_byte(palettes, tile), mask));
        descriptors = (uint16_t)(descriptors + DESCRIPTOR_BYTES);
        at = (uint16_t)(at + TILE_BYTES);
    }
    ORIGINAL_EFFECT(leave_row_end(g, descriptors, tiles));
}

/* ---- The entry ------------------------------------------------------------------------- */

/* The row set comes in X, and the count of tiles in its cell; the setup
 * always decodes a whole row. It leaves A clear. */
enum { TILE_COUNT = 0x22 };

void battle_decode_tile_row(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t tiles = (uint16_t)(battle_scratch(g, TILE_COUNT) | battle_scratch(g, TILE_COUNT + 1) << 8);
    decode_tile_row(g, s->cpu.x, tiles);
    s->cpu.a = 0;
    m_rts(s);
}

/* A whole row or less: the decode buffer holds three rows' tiles. */
enum { MOST_TILES = 3072 / TILE_BYTES };
bool battle_decodes_tile_row(Snes *s)
{
    vars_peek(true);
    GameState *g = state_of(s);
    uint16_t tiles = (uint16_t)(battle_scratch(g, TILE_COUNT) | battle_scratch(g, TILE_COUNT + 1) << 8);
    vars_peek(false);
    return battle_on_page(s) && tiles != 0 && tiles <= MOST_TILES;
}
