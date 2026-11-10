/* Data an event script selects for its memory-peek commands. Usually these
 * are character stats or settings in RAM; cartridge operands must be served
 * by the same resource store as the event language's other data. */
#ifndef CT_GAME_EVENT_MEMORY_DATA_H
#define CT_GAME_EVENT_MEMORY_DATA_H

#include "rom.h"
#include "data/assets.h"
#include "wram.h"

static inline uint8_t event_selected_data_byte(const GameState *g, uint32_t at)
{
    return asset_is_cartridge(at) ? rom_read8(g, at) : wram_read8(g, at);
}
static inline uint16_t event_selected_data_word(const GameState *g, uint32_t at)
{
    uint8_t low = event_selected_data_byte(g, at);
    return (uint16_t)(low | event_selected_data_byte(g, at + 1) << 8);
}

#endif
