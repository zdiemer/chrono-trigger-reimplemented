/* The cartridge's reads for a build with no machine (ct_native): game/rom.h's
 * backing, beside state_host.c. Every read a named resource accessor makes is
 * served from the asset store the driver gave the game (game/data/assets.h,
 * the pack frontend/assets.h reads), so no ROM image is mapped here. Where the
 * store has no copy -- an address a pointer in the data gives that is work
 * RAM, or a cartridge byte no resource holds (counted by data/assets.c) --
 * the host's memory answers as for any other address (state_host_read8, which
 * counts what it doesn't hold). */
#include "port/state_host.h"
#include "rom.h"

uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served)
{
    return served ? *served : state_host_read8(g, at & 0xFFFFFFu);
}
