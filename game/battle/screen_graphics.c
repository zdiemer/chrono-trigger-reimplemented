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

static void unpack(GameState *g, uint32_t from)
{
    set_decompress_source(g, from);
    set_decompress_dest(g, UNPACK_BUFFER);
    (void)unpack_stream_called(g, false);
}

// $D1:ECF3
/* An effect's tile blocks, each unpacked and sent to its place in VRAM and
 * waited for; its colors; and its tilemap graphics, unpacked and sent. */
bool battle_load_effect_graphics(GameState *g, Platform *pf, uint8_t effect)
{
    set_battle_effect_graphics_loading(g, (uint8_t)(battle_effect_graphics_loading(g) + 1));
    set_battle_effect_graphics_id(g, effect);
    for (unsigned block = 0; block < EFFECT_BLOCKS; block++) {
        uint8_t id = effect_graphics_block(g, effect, block);
        if (id == NO_GRAPHICS_BLOCK)
            continue;
        unpack(g, graphics_block_address(g, id));
        vid_send_vram_and_wait(pf, PF_CALL(0xD1ED49, 0xCD3ECE), (uint16_t)(FIRST_BLOCK_VRAM + block * BLOCK_VRAM_WORDS),
                               PF_GAME(UNPACK_BUFFER), BLOCK_BYTES);
    }
    for (unsigned i = 0; i < EFFECT_COLORS; i++)
        set_palette_base(g, EFFECT_FIRST_COLOR + i, effect_color(g, effect, i));
    unpack(g, effect_tilemap_graphics_address(g, effect));
    vid_send_vram_and_wait(pf, PF_CALL(0xD1EDC5, 0xCD3ECE), TILEMAP_GRAPHICS_VRAM, PF_GAME(UNPACK_BUFFER),
                           TILEMAP_GRAPHICS_BYTES);
    set_battle_effect_graphics_loading(g, false);
    return true;
}
