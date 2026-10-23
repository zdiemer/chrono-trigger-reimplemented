/* The asset pack: the game's resources imported once from the user's ROM, and
 * the stores a driver gives the game to read them from (game/data/assets.h).
 *
 * The import checks that the ROM is Chrono Trigger (USA) and copies out every
 * resource of the catalogue (re/assets.tsv), each under its name and type, in
 * its original bytes ("raw first", docs/PLAN.md Phase 4a). A pack is bytes:
 * where it is kept and what it is called is the driver's business, as for the
 * saves (frontend/files.c on the desktop, beside them), so nothing here opens
 * a file. Two stores serve the resources: one over a pack, one over a ROM image
 * in memory, which is what the import reads through. */
#ifndef CT_FRONTEND_ASSETS_H
#define CT_FRONTEND_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#include "data/assets.h"

/* The ROM the catalogue is of: Chrono Trigger (USA), headerless. */
enum { ASSET_ROM_SIZE = 0x400000 };
#define ASSET_ROM_CRC32 0x2D206BF7u

/* A ROM image without its copier header, if it has one. */
const uint8_t *asset_rom_image(const uint8_t *file, size_t len, size_t *size);

/* A store over a ROM image the caller keeps: each resource is the bytes where
 * the catalogue says the original keeps it. `rom` must outlive the store. */
typedef struct {
    const uint8_t *rom;
    size_t size;
} AssetRom;
void asset_store_rom(AssetStore *store, AssetRom *ctx, const uint8_t *rom, size_t size);

/* Make a pack of every resource of the catalogue a store serves, marked as
 * imported from the ROM with CRC-32 `rom_crc`: NULL (with *why) if the store
 * lacks one; otherwise malloc'd bytes, *len of them. */
uint8_t *asset_pack_build(const AssetStore *store, uint32_t rom_crc, size_t *len, const char **why);

/* Make a pack from a ROM image: NULL (with *why saying so) unless it is the
 * catalogue's ROM; otherwise malloc'd bytes, *len of them, for the caller to
 * keep and free. */
uint8_t *asset_pack_import(const uint8_t *rom, size_t size, size_t *len, const char **why);

/* A pack's bytes, read: NULL with *why saying what is wrong (not a pack, a
 * pack of another catalogue or format, cut short). The bytes must outlive the
 * pack and its store; asset_pack_close frees what open took. */
typedef struct AssetPack AssetPack;
AssetPack *asset_pack_open(const uint8_t *bytes, size_t len, const char **why);
void asset_pack_store(AssetPack *pack, AssetStore *store);
void asset_pack_close(AssetPack *pack);

#endif
