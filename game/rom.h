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

/* Where a reader goes on through the data from an address (an unpacker's
 * stream, a script's commands): the run of the store's bytes from `at` that
 * one resource holds, in the same bank, found once. A read in it is
 * rom_read8's, the same byte and the same time, with nothing to look up; a
 * read outside it is rom_read8. */
typedef struct {
    uint32_t at, length;
    const uint8_t *bytes;
} RomRun;
RomRun rom_run_from(uint32_t at);
uint8_t rom_run_read8(const GameState *g, const RomRun *run, uint32_t at);
/* rom_run_read8's `length` bytes from `at` where the run holds them all,
 * taking none of their reads' time: for a reader that goes on through a
 * stream and charges the reads it made this way together (charge.h,
 * charge_rom_accesses). NULL where the run doesn't, and the reader reads by
 * rom_run_read8. */
static inline const uint8_t *rom_run_at(const RomRun *run, uint32_t at, uint32_t length)
{
    uint32_t i = at - run->at;
    return i < run->length && length <= run->length - i ? run->bytes + i : NULL;
}

/* `length` of the cartridge's bytes from `at` on, read straight on into the
 * next bank as a long pointer's reads go (where rom_data, a transfer, wraps
 * in its bank), where one resource holds them all: in place, taking none of
 * their reads' time. NULL where none does, and the reader reads by rom_read8. */
const uint8_t *rom_bytes_on(uint32_t at, uint32_t length);

/* The packed stream starting at `at` as the resources' source keeps it
 * unpacked already (data/assets.h, AssetUnpacked), or NULL where it keeps
 * none: no time taken, the reader charges what unpacking it would have. */
const AssetUnpacked *rom_unpacked(uint32_t at);
static inline const uint8_t *rom_unpacked_bytes(const AssetUnpacked *u) { return (const uint8_t *)(u + 1); }

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
