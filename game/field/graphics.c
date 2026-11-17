/* Field mode: building object sprite graphics when an animation frame changes.
 *
 * The frame builder (field/frames.c) assembles an object's sprite tiles, 32
 * bytes each, in bank $7F, from the object's graphics set (the ROM's banks
 * $D2-$D5, or graphics built in $7F). A tile is copied straight, or mirrored
 * left to right. These are the builder's two tile copies
 * (field_draw_frame_tile). */
#include "field/graphics.h"

#include "charge.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The graphics set's bank, as the straight copy reads it: bank $7F where the
 * builder made the graphics, else the ROM's bank of sprite graphics it names
 * (field_data.h). */
static uint8_t source_bank(uint8_t bank)
{
    return bank == FIELD_BUILD_BANK ? bank : field_sprite_graphics_rom_bank(bank);
}

// $C0:E687
/* Copy a tile straight. The tile is at the set's address plus its offset, in
 * 16 bits, in the bank the copy reads; the addresses carry into the next
 * bank past $FFFF, as the original's indexed accesses do. From the
 * cartridge it is read and written a tile at a time, each byte's time as
 * the original's word reads and writes. */
static void copy_sprite_tile(GameState *g, ObjectGraphics set, uint16_t tile, uint16_t to)
{
    uint16_t from = (uint16_t)(field_tile_offset(tile) + set.address);
    uint32_t source = (uint32_t)source_bank(set.bank) << 16;
    uint32_t into = ((uint32_t)FIELD_BUILD_BANK << 16) + to;
    if ((source >> 16) != FIELD_BUILD_BANK) {
        uint8_t bytes[FIELD_TILE_BYTES];
        field_sprite_graphics_read(g, source + from, bytes, FIELD_TILE_BYTES);
        wram_write_span(g, into, bytes, FIELD_TILE_BYTES);
        return;
    }
    /* graphics built in the same bank: in one go where the tile neither runs
     * out of the bank nor overlaps where it goes, a word at a time otherwise */
    bool apart = (uint16_t)(to - from) >= FIELD_TILE_BYTES && (uint16_t)(from - to) >= FIELD_TILE_BYTES;
    if (apart && from <= 0x10000u - FIELD_TILE_BYTES && to <= 0x10000u - FIELD_TILE_BYTES) {
        uint8_t bytes[FIELD_TILE_BYTES];
        wram_read_span(g, source + from, bytes, FIELD_TILE_BYTES);
        wram_write_span(g, into, bytes, FIELD_TILE_BYTES);
        return;
    }
    for (uint16_t k = 0; k < FIELD_TILE_BYTES; k += 2)
        wram_write16(g, into + k, wram_read16(g, source + k + from));
}

// $C0:E534
/* Copy a tile mirrored left to right: each of its 32 bytes, from the set's
 * address plus the tile's offset in its bank, bit-reversed, to `to` in the
 * build bank. */
static void copy_sprite_tile_flipped(GameState *g, ObjectGraphics set, uint16_t tile, uint16_t to)
{
    /* graphics built in the build bank that overlap where the tile goes: a
     * byte read and written at a time, as each write reaches the bytes still
     * to be read */
    uint16_t at = field_tile_offset(tile);
    uint32_t source = ((uint32_t)set.bank << 16 | set.address) + at;
    uint32_t into = (uint32_t)FIELD_BUILD_BANK << 16 | to;
    if (source + FIELD_TILE_BYTES > into && into + FIELD_TILE_BYTES > source) {
        uint32_t graphics = (uint32_t)set.bank << 16 | set.address;
        for (uint16_t k = 0; k < FIELD_TILE_BYTES; k++) {
            uint8_t b = field_mirrored_byte(g, field_sprite_graphics_byte(g, graphics + (uint16_t)(at + k)));
            wram_write8(g, ((uint32_t)FIELD_BUILD_BANK << 16) + (uint16_t)(to + k), b);
        }
        return;
    }
    uint8_t bytes[FIELD_TILE_BYTES];
    field_read_frame_tile(g, set, tile, bytes);
    if (to <= 0x10000u - FIELD_TILE_BYTES) {
        wram_write_span(g, (uint32_t)FIELD_BUILD_BANK << 16 | to, bytes, FIELD_TILE_BYTES);
        return;
    }
    /* its places wrap in the build bank */
    for (uint16_t k = 0; k < FIELD_TILE_BYTES; k++)
        wram_write8(g, (uint32_t)FIELD_BUILD_BANK << 16 | (uint16_t)(to + k), bytes[k]);
}

/* A frame's tile as the two copies would draw it, read into `out` instead:
 * for a wider view to show an object the field doesn't run
 * (field/margin_objects.c). */
void field_read_frame_tile(const GameState *g, ObjectGraphics set, uint16_t tile, uint8_t out[FIELD_TILE_BYTES])
{
    if (tile & FIELD_TILE_MIRRORED) {
        /* each byte's place is the set's address plus the tile's offset and
         * the byte's, that offset in 16 bits: it carries into the next bank
         * past $FFFF, but the offset itself wraps */
        uint16_t at = field_tile_offset(tile);
        uint32_t graphics = (uint32_t)set.bank << 16 | set.address;
        if (at <= 0x10000u - FIELD_TILE_BYTES)
            field_sprite_graphics_read(g, graphics + at, out, FIELD_TILE_BYTES);
        else
            for (uint16_t k = 0; k < FIELD_TILE_BYTES; k++)
                out[k] = field_sprite_graphics_byte(g, graphics + (uint16_t)(at + k));
        for (uint16_t k = 0; k < FIELD_TILE_BYTES; k++)
            out[k] = field_mirrored_byte(g, out[k]);
        return;
    }
    uint16_t from = (uint16_t)(field_tile_offset(tile) + set.address);
    uint32_t source = (uint32_t)source_bank(set.bank) << 16;
    if ((source >> 16) != FIELD_BUILD_BANK) {
        field_sprite_graphics_read(g, source + from, out, FIELD_TILE_BYTES);
        return;
    }
    for (uint16_t k = 0; k < FIELD_TILE_BYTES; k += 2) {
        uint16_t word = wram_read16(g, source + k + from);
        out[k] = (uint8_t)word;
        out[k + 1] = (uint8_t)(word >> 8);
    }
}

/* A frame's tile drawn in bank $7F, timed as the copy that draws it. */
void field_draw_frame_tile(GameState *g, ObjectGraphics set, uint16_t tile, uint16_t to)
{
    if (tile & FIELD_TILE_MIRRORED)
        TIMED_AS(g, 0xC0E534, copy_sprite_tile_flipped(g, set, tile, to));
    else
        TIMED_AS(g, 0xC0E687, copy_sprite_tile(g, set, tile, to));
}
