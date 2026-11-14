/* RAM a pointer in the cartridge's data names, read for game/data/assets.c
 * (wram.h, wram_data_read8): the host's copy of the game's RAM, at the time a
 * cartridge read takes, but for a guard's look (vars_peek). */
#include "port/state_host.h"
#include "port/vars_ram.h"
#include "wram.h"

uint8_t wram_data_read8(const GameState *g, uint32_t at)
{
    if (!vars_peeking())
        state_host_game_access(g, STATE_HOST_CARTRIDGE_CLOCKS);
    return state_host_read8(g, at);
}
