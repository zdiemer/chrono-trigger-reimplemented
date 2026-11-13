/* The packer and unpacker of the cartridge's streams (frontend/lz.h) without
 * a ROM: made-up bytes of every kind pack and unpack to themselves, with
 * short runs and long, those whose odd items are all literals too; and
 * streams that aren't one are refused. Where the ROM is staged
 * (roms/ct-usa.sfc, from the source directory), every packed resource of
 * the catalogue packs again to its own bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "lz.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

static uint8_t image[LZ_MOST], packed[LZ_MOST + LZ_MOST / 8 + 16], unpacked[LZ_MOST];

/* `len` bytes packed and unpacked again: the same bytes, and the runs said. */
static void comes_back(size_t len, bool long_runs, const char *what)
{
    size_t n = lz_pack(image, len, long_runs, packed, sizeof packed), out_len = 0;
    bool runs = !long_runs;
    bool ok = n > 0 && lz_unpack(packed, n, unpacked, &out_len, &runs) && out_len == len && runs == long_runs &&
              !memcmp(unpacked, image, len);
    CHECK(ok);
    if (!ok)
        fprintf(stderr, "  %s (%zu bytes, %s runs) doesn't come back\n", what, len, long_runs ? "long" : "short");
}

static void made_up(void)
{
    static const size_t sizes[] = { 0, 1, 2, 3, 7, 8, 9, 31, 2048, 4095, 4096, 4097, 9000, LZ_MOST };
    for (int runs = 0; runs < 2; runs++) {
        for (size_t k = 0; k < sizeof sizes / sizeof *sizes; k++) {
            size_t len = sizes[k];
            memset(image, 0, len);
            comes_back(len, runs, "zeros");
            for (size_t i = 0; i < len; i++)   /* runs of every length and distance, and literals between */
                image[i] = (uint8_t)(i % 13 == 0 ? i * 7 : (i / 5) % 3 ? i % 29 : i >> 3);
            comes_back(len, runs, "patterned");
            uint32_t x = 12345;
            for (size_t i = 0; i < len; i++) {
                x = x * 1103515245u + 12345u;
                image[i] = (uint8_t)(x >> 24 & 0x0F);   /* few values: short runs everywhere */
            }
            comes_back(len, runs, "noise");
        }
    }

    /* Items that don't make whole control bytes, the odd ones all literals. */
    memcpy(image, "abcdefghij", 10);
    comes_back(10, false, "literals left over");

    /* Too little room, and streams that aren't one. */
    memset(image, 0, 100);
    CHECK(lz_pack(image, 100, false, packed, 4) == 0);
    size_t n = lz_pack(image, 100, false, packed, sizeof packed), out_len;
    bool runs;
    CHECK(n > 0 && lz_unpack(packed, n, unpacked, &out_len, &runs));
    CHECK(!lz_unpack(packed, n - 1, unpacked, &out_len, &runs));    /* cut short */
    static const uint8_t before_start[] = { 3, 0, 0x01, 0x05, 0x00, 0 };   /* a run 5 back, of nothing */
    CHECK(!lz_unpack(before_start, sizeof before_start, unpacked, &out_len, &runs));
    static const uint8_t past_end[] = { 0xFF, 0x00, 0 };
    CHECK(!lz_unpack(past_end, sizeof past_end, unpacked, &out_len, &runs));
}

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("lz_test: no ROM staged, the cartridge's streams not packed again\n");
        return;
    }
    uint8_t *file = malloc(ASSET_ROM_SIZE + 0x200);
    size_t len = file ? fread(file, 1, ASSET_ROM_SIZE + 0x200, f) : 0;
    fclose(f);
    size_t size;
    const uint8_t *rom = asset_rom_image(file, len, &size);
    AssetRom ctx;
    AssetStore store;
    asset_store_rom(&store, &ctx, rom, size);
    unsigned streams = 0, wrong = 0;
    for (uint16_t i = 0; i < asset_catalogue_count && size == ASSET_ROM_SIZE; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        if (strncmp(asset_codec_names[e->codec], "lz", 2))
            continue;
        uint32_t n = 0;
        const uint8_t *bytes = store.find(store.ctx, e->name, asset_type_names[e->type], &n);
        size_t out_len = 0;
        bool long_runs = false;
        size_t again = bytes && lz_unpack(bytes, n, image, &out_len, &long_runs)
                           ? lz_pack(image, out_len, long_runs, packed, sizeof packed)
                           : 0;
        streams++;
        if (again != n || memcmp(packed, bytes, n)) {
            if (!wrong++)
                fprintf(stderr, "  %s doesn't pack again to its bytes\n", e->name);
        }
    }
    CHECK(size == ASSET_ROM_SIZE && streams > 0 && wrong == 0);
    if (!wrong)
        printf("lz_test: the cartridge's %u packed streams pack again to their bytes\n", streams);
    free(file);
}

int main(void)
{
    made_up();
    the_cartridge();
    if (failures)
        fprintf(stderr, "lz_test: %d failures\n", failures);
    return failures != 0;
}
