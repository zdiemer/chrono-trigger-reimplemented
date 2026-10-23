/* The asset pack and the game's reads through a store, without a ROM or a
 * machine: a pack made from a store that serves made-up bytes for every
 * resource of the catalogue reads back the same, a broken pack is refused, and
 * the accessors' reads (game/rom.h) come from the store, a resource found by
 * any of the addresses the cartridge answers at. This file is also the
 * backing a build without a machine gives game/rom.h: the store's byte, or 0
 * where it holds none. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "rom.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served)
{
    (void)g, (void)at;
    return served ? *served : 0;
}

/* A resource's made-up byte: its row and the offset, mixed. */
static uint8_t made_up(uint16_t row, uint32_t offset) { return (uint8_t)(row * 7 + offset * 13 + 1); }

static uint8_t *made_up_bytes;
static uint32_t made_up_at[0x10000];

static const uint8_t *made_up_find(void *ctx, const char *name, const char *type, uint32_t *length)
{
    (void)ctx;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        if (!strcmp(e->name, name) && !strcmp(asset_type_names[e->type], type)) {
            *length = e->length;
            return made_up_bytes + made_up_at[i];
        }
    }
    return NULL;
}

int main(void)
{
    size_t total = 0;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        made_up_at[i] = (uint32_t)total;
        total += asset_catalogue[i].length;
    }
    made_up_bytes = malloc(total);
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        for (uint32_t k = 0; k < asset_catalogue[i].length; k++)
            made_up_bytes[made_up_at[i] + k] = made_up(i, k);
    AssetStore made = { made_up_find, NULL };

    size_t len = 0;
    const char *why = "";
    uint8_t *bytes = asset_pack_build(&made, ASSET_ROM_CRC32, &len, &why);
    CHECK(bytes != NULL);
    if (!bytes)
        return 1;
    AssetPack *pack = asset_pack_open(bytes, len, &why);
    CHECK(pack != NULL);
    AssetStore store;
    asset_pack_store(pack, &store);
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        uint32_t n = 0;
        const uint8_t *got = store.find(store.ctx, e->name, asset_type_names[e->type], &n);
        CHECK(got && n == e->length && !memcmp(got, made_up_bytes + made_up_at[i], n));
        CHECK(!store.find(store.ctx, e->name, "not-a-type", &n));
    }

    /* The game's reads, through the store. */
    assets_use(&store);
    for (uint16_t i = 0; i < asset_catalogue_count; i += 97) {
        const AssetEntry *e = &asset_catalogue[i];
        uint32_t last = e->at + e->length - 1;
        CHECK(rom_read8(NULL, e->at) == made_up(i, 0));
        CHECK(rom_read8(NULL, last) == made_up(i, e->length - 1));
        if ((e->at >> 16) < 0xFE)                                        /* banks $40-$7D */
            CHECK(rom_read8(NULL, e->at - 0x800000u) == made_up(i, 0));
        if ((e->at & 0xFFFF) >= 0x8000 && e->length > 1)                 /* the upper half of $80-$BF */
            CHECK(rom_read16(NULL, e->at - 0x400000u) == (made_up(i, 0) | made_up(i, 1) << 8));
    }
    /* A cartridge byte in no resource is counted; work RAM isn't the cartridge's. */
    unsigned long outside = asset_outside_reads()->count;
    CHECK(asset_catalogue[0].at > 0xC00000u);
    CHECK(rom_read8(NULL, 0xC00000u) == 0);
    CHECK(asset_outside_reads()->count == outside + 1);
    CHECK(rom_read8(NULL, 0x7E0000u) == 0);
    CHECK(asset_outside_reads()->count == outside + 1);
    assets_use(NULL);

    /* What a pack mustn't be. */
    CHECK(!asset_pack_open(bytes, 7, &why));
    CHECK(!asset_pack_open(bytes, len - 1, &why) || asset_catalogue[asset_catalogue_count - 1].length == 0);
    bytes[12] ^= 1;   /* another catalogue's */
    CHECK(!asset_pack_open(bytes, len, &why));
    bytes[12] ^= 1;
    bytes[0] = 'X';
    CHECK(!asset_pack_open(bytes, len, &why));

    /* The ROM's store serves each resource where the catalogue puts it, and
     * the import takes only the catalogue's ROM. */
    uint8_t *rom = calloc(ASSET_ROM_SIZE, 1);
    for (size_t k = 0; k < ASSET_ROM_SIZE; k++)
        rom[k] = (uint8_t)(k ^ k >> 8 ^ k >> 16);
    AssetRom ctx;
    AssetStore from_rom;
    asset_store_rom(&from_rom, &ctx, rom, ASSET_ROM_SIZE);
    const AssetEntry *e = &asset_catalogue[asset_catalogue_count / 2];
    uint32_t n = 0;
    const uint8_t *got = from_rom.find(from_rom.ctx, e->name, asset_type_names[e->type], &n);
    CHECK(got == rom + (e->at - 0xC00000u) && n == e->length);
    CHECK(!asset_pack_import(rom, ASSET_ROM_SIZE, &len, &why));

    asset_pack_close(pack);
    free(bytes);
    free(rom);
    free(made_up_bytes);
    if (failures)
        fprintf(stderr, "assets_test: %d failures\n", failures);
    else
        printf("assets_test: %u resources, a pack of them read back, the reads through it, OK\n",
               (unsigned)asset_catalogue_count);
    return failures != 0;
}
