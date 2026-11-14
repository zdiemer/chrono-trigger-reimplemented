/* The cartridge's data: what the named resource accessors in the subsystems'
 * *_data.h headers read through, and nothing else.
 *
 * A port never reads ROM by address (CLAUDE.md, "ROM data is loaded, not
 * addressed"): it calls an accessor named for the resource. The accessor
 * reads its resource by name (asset_read8: the catalogue's rows by name,
 * data/asset_names.h), a place in it, from the store the driver supplied
 * (data/assets.h), the asset pack in ct_native; so a pack may keep a
 * resource anywhere, in any order. What the data's own pointers name -- a
 * stream a table points to, a script's text -- is still found by the
 * address the original keeps it at (rom_read8): that address is a key,
 * game/data/assets.c finding the resource of the catalogue it lies in. No
 * port knows which store answers. */
#ifndef CT_GAME_ROM_H
#define CT_GAME_ROM_H

#include <stdint.h>

#include "data/asset_names.h"
#include "data/assets.h"
#include "platform/platform.h"
#include "state.h"

/* A byte or a little-endian word of a named resource (ASSET_...), `offset`
 * bytes into it. An offset past its end reads on at the address after it, as
 * the original's indexing does, into whatever the catalogue holds there. */
uint8_t asset_read8(const GameState *g, AssetId resource, uint32_t offset);
uint16_t asset_read16(const GameState *g, AssetId resource, uint32_t offset);

/* The same where the data gives a 16-bit place in the resource's bank (a
 * table of places, a record's link): the place is the original's, so it lies
 * as far into the resource as it is past the resource's own place. A place
 * outside the resource reads that place of its bank, as the original does. */
uint8_t asset_read8_at_place(const GameState *g, AssetId resource, uint16_t place);
uint16_t asset_read16_at_place(const GameState *g, AssetId resource, uint16_t place);

/* A byte where a 16-bit place the data gives leads in the bank a named
 * resource is in, which may be any of that bank's resources (a bank of tables
 * whose records link to one another by place): read by the address, a key
 * the catalogue's rows are looked up by. */
uint8_t rom_read8_in_bank_of(const GameState *g, AssetId resource, uint16_t place);
uint16_t rom_read16_in_bank_of(const GameState *g, AssetId resource, uint16_t place);

/* The address of a 16-bit place the data gives, in the bank a named
 * resource is in: what the game hands on where it keeps a long pointer to
 * what the place leads to (the stream the unpacker reads, a script it runs). */
uint32_t asset_place_address(AssetId resource, uint16_t place);

/* Where the original keeps a resource: what the game holds where it keeps a
 * pointer to one (a table of strings it chose, a script it runs), and what a
 * pointer the data holds is told apart by. */
uint32_t asset_address(AssetId resource);

/* A byte or a word where the data's own pointers lead, by the address: the
 * cartridge's, or the game's RAM where the pointer names it (wram.h,
 * wram_data_read8). */
uint8_t rom_read8(const GameState *g, uint32_t at);
uint16_t rom_read16(const GameState *g, uint32_t at);

/* A named graphics accessor's transfer, with no game-time charge. Validates
 * every cartridge byte, including a transfer that wraps inside its bank.
 * RAM sources retain their backend address. */
PfData rom_data(uint32_t at, uint32_t length);
/* A script-selected transfer may repeat one byte or walk backwards. */
PfData rom_data_mode(uint32_t at, uint32_t length, uint8_t mode);

/* The backing's side of each of those reads of the cartridge, with the time
 * it takes. `served` is the store's copy of the byte, which is what the read
 * gives; NULL for a cartridge byte no resource holds, which stops the program
 * there. */
uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served);

#endif
