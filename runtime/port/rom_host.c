/* The cartridge's reads for a build with no machine (ct_native): game/rom.h's
 * backing, beside state_host.c. Every read a named resource accessor makes is
 * served from the asset store the driver gave the game (game/data/assets.h,
 * the pack frontend/assets.h reads), so no ROM image is mapped here. RAM a
 * pointer in the data names is the game's (wram_data_read8, which game/data/
 * assets.c sends those reads to); a cartridge byte no resource holds (counted
 * by data/assets.c) has no answer here, and the program goes no further, the
 * address named where it stops. */
#include "core/program.h"
#include "port/state_host.h"
#include "port/vars_ram.h"
#include "rom.h"

uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served)
{
    if (!vars_peeking())
        state_host_game_access(g, STATE_HOST_CARTRIDGE_CLOCKS);
    if (served)
        return *served;
    at &= 0xFFFFFFu;
    /* the stop is the backing's own record, not the game's state */
    program_fail((GameState *)g, at, "a cartridge byte in no resource of the catalogue");
    return 0;
}
