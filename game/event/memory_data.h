/* Data an event script selects for its memory-peek commands. Usually these
 * are character stats or settings in RAM; cartridge operands must be served
 * by the same resource store as the event language's other data. */
#ifndef CT_GAME_EVENT_MEMORY_DATA_H
#define CT_GAME_EVENT_MEMORY_DATA_H

#include "rom.h"
#include "data/assets.h"
#include "wram.h"
#include "platform/script_memory.h"

static inline uint8_t event_selected_data_at(const GameState *g, Platform *pf, PfOrigin origin, uint32_t at)
{
    uint8_t status;
    if (pf_script_status(pf, origin, at, &status))
        return status;
    return asset_is_cartridge(at) ? rom_read8(g, at) : wram_read8(g, at);
}

static inline uint8_t event_selected_data_byte(const GameState *g, Platform *pf, uint32_t at)
{
    return event_selected_data_at(g, pf, PF_AT(0xC06797), at);
}

static inline uint16_t event_selected_data_word(const GameState *g, Platform *pf, uint32_t at)
{
    uint8_t low = event_selected_data_at(g, pf, PF_AT(0xC067CA), at);
    return (uint16_t)(low | event_selected_data_at(g, pf, PF_AT(0xC067CA), at + 1) << 8);
}

#endif
