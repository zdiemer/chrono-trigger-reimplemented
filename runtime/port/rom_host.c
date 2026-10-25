/* The cartridge's reads for a build with no machine (ct_native): game/rom.h's
 * backing, beside state_host.c. Every read a named resource accessor makes is
 * served from the asset store the driver gave the game (game/data/assets.h,
 * the pack frontend/assets.h reads), so no ROM image is mapped here. Where the
 * store has no copy, an address a pointer in the data gives that is work RAM
 * or save RAM is answered by the host's memory as for any other address; a
 * cartridge byte no resource holds (counted by data/assets.c) has no answer
 * here, and the program goes no further, the address named where it stops. */
#include "core/program.h"
#include "port/state_host.h"
#include "rom.h"

uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served)
{
    if (served)
        return *served;
    at &= 0xFFFFFFu;
    if (!state_host_is_cartridge(at))
        return state_host_read8(g, at);
    /* the stop is the backing's own record, not the game's state */
    program_needs_the_machine((GameState *)g, at, "a cartridge byte in no resource of the catalogue");
    return 0;
}
