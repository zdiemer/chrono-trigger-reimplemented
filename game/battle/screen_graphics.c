/* Battle: loading an effect's graphics.
 *
 * Before a spell or an attack's effect plays, its graphics are loaded: up to
 * four compressed blocks of tiles into VRAM $2000-$3FFF, one after the other,
 * the effect's 48 colors into background rows 2-4 of the palette as loaded,
 * and one more block into the tilemap area at $6800.
 * Each block is unpacked into a buffer in work RAM and sent to VRAM by the
 * frame work, which takes frames; the battle's frame wait doesn't start
 * another effect's load meanwhile.
 *
 * Unpacking and sending are still the original's code, so the loader is a
 * bridge: the machine appears at those two calls and at the entry. */
#include "battle/screen.h"

#include "battle/bridge.h"
#include "original_effects.h"

enum {
    FIRST_BLOCK_VRAM = 0x2000,
    BLOCK_VRAM_WORDS = 0x0800,
    BLOCK_BYTES = 2 * BLOCK_VRAM_WORDS,
    TILEMAP_GRAPHICS_VRAM = 0x6800,
    TILEMAP_GRAPHICS_BYTES = 0x0800,
    EFFECT_FIRST_COLOR = 2 * 16,
};

/* The blocks are unpacked here, then sent. */
#define UNPACK_BUFFER 0x7ED000u

/* ---- The load, in the game's terms ---------------------------------------------------------- */

static void start_loading(GameState *g, uint8_t effect)
{
    set_battle_effect_graphics_loading(g, (uint8_t)(battle_effect_graphics_loading(g) + 1));
    set_battle_effect_graphics_id(g, effect);
}

static void finish_loading(GameState *g) { set_battle_effect_graphics_loading(g, false); }

static uint16_t block_vram(unsigned block) { return (uint16_t)(FIRST_BLOCK_VRAM + block * BLOCK_VRAM_WORDS); }

static void load_effect_colors(GameState *g, uint8_t effect)
{
    for (unsigned i = 0; i < EFFECT_COLORS; i++)
        set_palette_base(g, EFFECT_FIRST_COLOR + i, effect_color(g, effect, i));
}

/* The original keeps its place in the effect's list in three cells, which
 * nothing else reads: the list's offset, the VRAM address and the block. They
 * hold these at each of its calls. */
static void leave_place(GameState *g, uint8_t effect, unsigned block)
{
    set_battle_effect_graphics_list(g, (uint16_t)((uint8_t)(effect * EFFECT_BLOCKS) + block));
    set_battle_effect_graphics_vram(g, block_vram(block));
    set_battle_effect_graphics_block(g, (uint8_t)block);
}

/* ---- The two calls to the original's code --------------------------------------------------- */

/* The carry and overflow the original's index arithmetic leaves before each
 * unpacking (three bytes an entry: twice the index plus it), which the
 * decompressor and the transfer hand back to the loader's caller: for a
 * block id a 16-bit sum, which neither carries nor overflows; for the effect
 * a byte-wide one. */
static void leave_index_flags(Snes *s, uint8_t twice, uint8_t index)
{
    uint8_t sum = (uint8_t)(twice + index);
    m_set_flag(s, CPU_FLAG_C, twice + index > 0xFF);
    m_set_flag(s, CPU_FLAG_V, (~(twice ^ index) & (twice ^ sum) & 0x80) != 0);
}

/* Unpack a compressed block into the buffer: the decompressor ($C3:0002)
 * takes where from and where to in its cells, and none of the registers but
 * the carry and overflow, which it hands back as they came (the endings'
 * movies show the loader's callers reading the carry). */
static bool unpack(Snes *s, uint32_t site, uint32_t from)
{
    GameState *g = state_of(s);
    set_decompress_source(g, from);
    set_decompress_dest(g, UNPACK_BUFFER);
    return battle_call_long(s, site, 0xC30002, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_DB | DC_D);
}

/* Send the buffer to VRAM and wait for it to go: the frame work's transfer
 * ($CD:3ECE) takes the buffer's bank in A and its address in X, the VRAM
 * address in Y and the size in the page's cells $53-$54. */
static bool send_buffer(Snes *s, uint32_t site, uint16_t vram, uint16_t bytes)
{
    GameState *g = state_of(s);
    set_battle_scratch(g, 0x53 - SCREEN_CELLS, (uint8_t)bytes);
    set_battle_scratch(g, 0x54 - SCREEN_CELLS, (uint8_t)(bytes >> 8));
    s->cpu.y = vram;
    s->cpu.x = (uint16_t)UNPACK_BUFFER;
    m_set_a8(s, (uint8_t)(UNPACK_BUFFER >> 16));
    return battle_call_long(s, site, 0xCD3ECE, DC_N | DC_Z);
}

// $D1:ECF3
/* Load the effect in A's graphics and colors. Where a call doesn't come back
 * as the battle's code runs, the original goes on from it. */
void battle_load_effect_graphics(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t effect = (uint8_t)s->cpu.a;
    start_loading(g, effect);
    for (unsigned block = 0; block < EFFECT_BLOCKS; block++) {
        uint8_t id = effect_graphics_block(g, effect, block);
        if (id == NO_GRAPHICS_BLOCK)
            continue;
        ORIGINAL_EFFECT(leave_place(g, effect, block));
        ORIGINAL_EFFECT(screen_leave_word(g, 0x53, id));
        ORIGINAL_EFFECT(leave_index_flags(s, 0, 0));
        if (!unpack(s, 0xD1ED38, graphics_block_address(g, id)))
            return;
        if (!send_buffer(s, 0xD1ED49, block_vram(block), BLOCK_BYTES))
            return;
    }
    ORIGINAL_EFFECT(leave_place(g, effect, EFFECT_BLOCKS));
    load_effect_colors(g, effect);
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x53, effect));
    ORIGINAL_EFFECT(leave_index_flags(s, (uint8_t)(effect << 1), effect));
    if (!unpack(s, 0xD1EDB4, effect_tilemap_graphics_address(g, effect)))
        return;
    if (!send_buffer(s, 0xD1EDC5, TILEMAP_GRAPHICS_VRAM, TILEMAP_GRAPHICS_BYTES))
        return;
    finish_loading(g);
    m_rtl(s);
}
