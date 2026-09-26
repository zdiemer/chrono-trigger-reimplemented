/* Field mode: building object sprite graphics when an animation frame changes.
 *
 * The frame builder (field/frames.c) assembles an object's sprite tiles, 32
 * bytes each, in bank $7F, from the object's graphics set (the ROM's banks
 * $D2-$D5, or graphics built in $7F). A tile is copied straight, or mirrored
 * left to right. These are the builder's two tile copies; each is also an
 * entry of its own, which the original's code calls with the set in
 * field_sprite_graphics and the place in field_sprite_tile_offset (the
 * straight copy) or the work RAM port's cursor (the mirrored one). */
#include "field/graphics.h"

#include "charge.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* A tile's offset in its graphics set. */
static uint16_t tile_offset(uint16_t tile) { return (uint16_t)((tile & FIELD_TILE_NUMBER) * FIELD_TILE_BYTES); }

/* The graphics set's bank, as the straight copy reads it: bank $7F where the
 * builder made the graphics, else the ROM's bank of sprite graphics it names
 * (field_data.h). */
static uint8_t source_bank(uint8_t bank)
{
    return bank == FIELD_BUILD_BANK ? bank : field_sprite_graphics_rom_bank(bank);
}

// $C0:E687
/* Copy a tile straight, a word at a time. The tile is at the set's address
 * plus its offset, in 16 bits, in the bank the copy reads; the addresses
 * carry into the next bank past $FFFF, as the original's indexed accesses
 * do. */
static void copy_sprite_tile(GameState *g, ObjectGraphics set, uint16_t tile, uint16_t to)
{
    uint16_t from = (uint16_t)(tile_offset(tile) + set.address);
    uint32_t source = (uint32_t)source_bank(set.bank) << 16;
    for (uint16_t k = 0; k < FIELD_TILE_BYTES; k += 2) {
        uint32_t at = source + k + from;
        uint16_t word = (source >> 16) == FIELD_BUILD_BANK ? wram_read16(g, at) : field_sprite_graphics_word(g, at);
        wram_write16(g, ((uint32_t)FIELD_BUILD_BANK << 16) + k + to, word);
    }
}

// $C0:E534
/* Copy a tile mirrored left to right: each of its 32 bytes, from the set's
 * address plus the tile's offset in its bank, bit-reversed, to `to` in work
 * RAM (17 bits: the place wraps round work RAM as the port the original
 * writes through does). */
enum { WORK_RAM = 0x7E0000, WORK_RAM_BITS = 0x1FFFF, SECOND_BANK = 0x10000 };
static void copy_sprite_tile_flipped(GameState *g, ObjectGraphics set, uint16_t tile, uint32_t to)
{
    uint16_t at = tile_offset(tile);
    uint32_t graphics = (uint32_t)set.bank << 16 | set.address;
    for (uint16_t k = 0; k < FIELD_TILE_BYTES; k++) {
        uint8_t b = field_mirrored_byte(g, field_sprite_graphics_byte(g, graphics + (uint16_t)(at + k)));
        wram_write8(g, WORK_RAM + ((to + k) & WORK_RAM_BITS), b);
    }
}

/* A frame's tile drawn in bank $7F, timed as the copy that draws it. */
void field_draw_frame_tile(GameState *g, ObjectGraphics set, uint16_t tile, uint16_t to)
{
    if (tile & FIELD_TILE_MIRRORED)
        TIMED_AS(g, 0xC0E534, copy_sprite_tile_flipped(g, set, tile, SECOND_BANK + to));
    else
        TIMED_AS(g, 0xC0E687, copy_sprite_tile(g, set, tile, to));
}

/* ---- For the original's code and shadow mode (field/scratch.h) -------------------------- */

/* The tile copies save the caller's Y (and give it back from there). */
static void leave_saved_y(GameState *g, uint16_t y) { original_cell16(g, FIELD_SAVED_Y_CELL, y); }

/* ---- Entries: the frame builder's calls, on the field's page ------------------------------ */

/* The set the original's caller left in field_sprite_graphics. */
static ObjectGraphics graphics_left(const GameState *g)
{
    return (ObjectGraphics){ field_sprite_graphics_bank(g), field_sprite_graphics_address(g) };
}

/* 16-bit A, the tile. Y comes back as it came, from the cell the copy saved
 * it in (the guards keep the copies' writes off the cell). The straight copy
 * draws at field_sprite_tile_offset, and leaves the port's cursor past the
 * tile in the bank it is in, A that cursor, and the overflow of its sum; the
 * mirrored one draws at the port's cursor, which it leaves past the tile. */
static void tile_copy_entry(Snes *s, bool mirrored)
{
    GameState *g = state_of(s);
    uint16_t tile = s->cpu.a, y = s->cpu.y;
    ORIGINAL_EFFECT(leave_saved_y(g, y));
    if (mirrored) {
        uint32_t port = s->bus.wram_port_addr & WORK_RAM_BITS;
        copy_sprite_tile_flipped(g, graphics_left(g), tile, port);
        uint32_t after = (port + FIELD_TILE_BYTES) & WORK_RAM_BITS;
        hw_port_seek(g, (uint16_t)after, (uint8_t)(after >> 16));
    } else {
        uint16_t to = field_sprite_tile_offset(g);
        copy_sprite_tile(g, graphics_left(g), tile, to);
        uint16_t cursor = (uint16_t)(to + FIELD_TILE_BYTES);
        hw_port_seek_address(g, cursor);
        m_set_flag(s, CPU_FLAG_V, field_add_overflows16(to, FIELD_TILE_BYTES));
        tile = cursor;
    }
    s->cpu.a = tile;
    s->cpu.y = y;
    m_rts(s);
}

void field_copy_sprite_tile(Snes *s) { tile_copy_entry(s, false); }
void field_copy_sprite_tile_flipped(Snes *s) { tile_copy_entry(s, true); }

/* The mirrored copy's guard: on the field's page with the data bank $00, the
 * tile's 32 bytes in banks where every address answers (elsewhere the copy
 * would read what the bus last held; the set's address carries into the next
 * bank, the last bank's into bank $00), and the port's 32 bytes clear of the
 * field page's cells the copy reads back -- the saved Y, and the set's own
 * pointer -- which it would otherwise write over first. The frame builder
 * draws into bank $7F. */
enum { PORT_SPACE = 0x20000, SAVED_Y_AT = 0x00100 + FIELD_SAVED_Y_CELL, SET_AT = 0x001CD, SET_BYTES = 3 };

bool field_tile_copy_keeps_saved_y(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    ObjectGraphics set = graphics_left(state_of(s));
    vars_peek(false);
    uint32_t first = ((uint32_t)set.bank << 16 | set.address) + tile_offset(s->cpu.a);
    uint32_t last = first + FIELD_TILE_BYTES - 1;
    if (!field_bank_always_answers((uint8_t)(first >> 16)) || !field_bank_always_answers((uint8_t)(last >> 16)))
        return false;
    uint32_t port = s->bus.wram_port_addr;
    for (uint32_t k = 0; k < FIELD_TILE_BYTES; k++) {
        uint32_t at = (port + k) % PORT_SPACE;
        if (at == SAVED_Y_AT || at == SAVED_Y_AT + 1 || (at >= SET_AT && at < SET_AT + SET_BYTES))
            return false;
    }
    return true;
}
