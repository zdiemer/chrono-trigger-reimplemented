/* Battle: load an effect's tile blocks, palette and tilemap graphics.
 * The frame tail owns the request. Loading stays marked while each unpacked
 * block is sent, so a frame wait cannot start a second load over its buffer. */
#include "battle/screen.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "charge.h"
#include "data/decompress.h"
#include "platform/video.h"

enum {
    FIRST_BLOCK_VRAM = 0x2000, BLOCK_VRAM_WORDS = 0x0800,
    BLOCK_BYTES = 2 * BLOCK_VRAM_WORDS,
    TILEMAP_GRAPHICS_VRAM = 0x6800, TILEMAP_GRAPHICS_BYTES = 0x0800,
    EFFECT_FIRST_COLOR = 2 * 16,
};
#define UNPACK_BUFFER 0x7ED000u

typedef bool (*SendGraphics)(void *ctx, bool tilemap, uint16_t vram, uint16_t bytes);

static void unpack(GameState *g, uint32_t from)
{
    set_decompress_source(g, from);
    set_decompress_dest(g, UNPACK_BUFFER);
    (void)unpack_stream_called(g, false);
}

// $D1:ECF3
static bool load_effect_graphics_with(GameState *g, uint8_t effect, SendGraphics send, void *ctx)
{
    set_battle_effect_graphics_loading(g, (uint8_t)(battle_effect_graphics_loading(g) + 1));
    set_battle_effect_graphics_id(g, effect);
    for (unsigned block = 0; block < EFFECT_BLOCKS; block++) {
        uint8_t id = effect_graphics_block(g, effect, block);
        if (id == NO_GRAPHICS_BLOCK)
            continue;
        charge_machine_accesses(g, 7);
        unpack(g, graphics_block_address(g, id));
        if (!send(ctx, false, (uint16_t)(FIRST_BLOCK_VRAM + block * BLOCK_VRAM_WORDS), BLOCK_BYTES))
            return false;
    }
    charge_machine_accesses(g, 5);
    for (unsigned i = 0; i < EFFECT_COLORS; i++)
        set_palette_base(g, EFFECT_FIRST_COLOR + i, effect_color(g, effect, i));
    charge_machine_accesses(g, 1);
    unpack(g, effect_tilemap_graphics_address(g, effect));
    if (!send(ctx, true, TILEMAP_GRAPHICS_VRAM, TILEMAP_GRAPHICS_BYTES))
        return false;
    set_battle_effect_graphics_loading(g, false);
    return true;
}

static bool send_graphics(void *ctx, bool tilemap, uint16_t vram, uint16_t bytes)
{
    if (tilemap)
        vid_send_vram_and_wait(ctx, PF_CALL_DC(0xD1EDC5, 0xCD3ECE, DC_N | DC_Z), vram, PF_GAME(UNPACK_BUFFER), bytes);
    else
        vid_send_vram_and_wait(ctx, PF_CALL_DC(0xD1ED49, 0xCD3ECE, DC_N | DC_Z), vram, PF_GAME(UNPACK_BUFFER), bytes);
    return true;
}

bool battle_load_effect_graphics(GameState *g, Platform *pf, uint8_t effect)
{
    return load_effect_graphics_with(g, effect, send_graphics, pf);
}

static bool send_graphics_on_the_machine(void *ctx, bool tilemap, uint16_t vram, uint16_t bytes)
{
    Snes *s = ctx;
    screen_set_cell_word(state_of(s), 0x53, bytes);
    s->cpu.y = vram;
    s->cpu.x = (uint16_t)UNPACK_BUFFER;
    m_set_a8(s, (uint8_t)(UNPACK_BUFFER >> 16));
    if (tilemap)
        return battle_run_long_called(s, 0xD1EDC5, 0xCD3ECE, DC_N | DC_Z, battle_send_vram_and_wait_long_entry);
    return battle_run_long_called(s, 0xD1ED49, 0xCD3ECE, DC_N | DC_Z, battle_send_vram_and_wait_long_entry);
}

void battle_load_effect_graphics_entry(Snes *s)
{
    if (load_effect_graphics_with(state_of(s), (uint8_t)s->cpu.a, send_graphics_on_the_machine, s))
        m_rtl(s);
}
