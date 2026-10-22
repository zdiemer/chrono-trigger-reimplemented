/* The game's data, as resources: the catalogue of what the accessors read
 * from the cartridge (re/assets.tsv, docs/PLATFORM.md "The assets, as built").
 *
 * Every byte a port reads from the ROM is read by a named accessor (the
 * subsystems' *_data.h headers, game/rom.h), and every such byte lies in a
 * resource of the catalogue: a table, a stream, a string, a script, each with
 * its name, its type (MD2's, docs/MATCHING.md), where the original keeps it
 * and the codec that decodes it. The catalogue holds no data, only those
 * facts; tools/assets.py writes it from the typed regions the matching build
 * extracts, and game/data/asset_catalogue.c from it. */
#ifndef CT_GAME_DATA_ASSETS_H
#define CT_GAME_DATA_ASSETS_H

#include <stdint.h>

/* What a resource is, as MD2 types it, and the codec that decodes it: the
 * names re/assets.tsv spells them with, in the order of asset_type_names and
 * asset_codec_names. */
typedef struct {
    uint32_t at;          /* where the original keeps it: its first byte's 24-bit ROM address */
    uint32_t length;      /* its bytes */
    const char *name;
    uint8_t type, codec;  /* indices into asset_type_names, asset_codec_names */
} AssetEntry;

extern const AssetEntry asset_catalogue[];
extern const uint16_t asset_catalogue_count;
extern const char *const asset_type_names[];
extern const char *const asset_codec_names[];
/* A CRC-32 of the catalogue's rows: a pack made from another catalogue holds
 * other resources, or the same under other names. */
extern const uint32_t asset_catalogue_version;

/* Where the game's resources come from: the driver supplies a store, as it
 * keeps the saves (frontend/assets.h makes one over an asset pack, or over a
 * ROM image). The game asks it for a resource by name and type; `find` gives
 * the resource's bytes and sets *length, or gives NULL when it hasn't it. */
typedef struct {
    const uint8_t *(*find)(void *ctx, const char *name, const char *type, uint32_t *length);
    void *ctx;
} AssetStore;

/* Read the resources from `store` from now on: the driver's to call, before
 * the game runs or between frames. NULL reads them from the cartridge the
 * machine maps, as there was no store before. The store must outlive its use;
 * each resource is asked for once, and what the store answers kept. */
void assets_use(const AssetStore *store);

/* What a store answered for each resource of the catalogue (generated beside
 * it, a slot a row). */
typedef struct {
    const uint8_t *bytes;   /* NULL: the store hasn't it */
    uint8_t asked;
} AssetServed;
extern AssetServed asset_served[];

typedef uint16_t AssetId;
enum { ASSET_NONE = 0xFFFF };

/* The resource holding the cartridge byte at `at`, or ASSET_NONE. `at` may
 * be any of the addresses the cartridge answers at (its banks $40-$7D, or the
 * upper half of banks $00-$3F and $80-$BF, mirror $C0-$FF). */
AssetId asset_containing(uint32_t at);

/* Cartridge reads that land in no resource of the catalogue: how many, and
 * the first one's address (an address that isn't the cartridge's, work RAM a
 * pointer in the data names, isn't counted). ct_dev prints an `asset outside the catalogue`
 * line when there are any, and tools/suite.py fails the run that prints one. */
typedef struct {
    unsigned long count;
    uint32_t first;
} AssetFault;
const AssetFault *asset_outside_reads(void);
void asset_note_outside(uint32_t at);

#endif
