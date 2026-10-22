#include "asset_report.h"

#include <stdint.h>
#include <stdlib.h>

#include "assets.h"
#include "data/assets.h"
#include "port/rom_reads.h"

static uint8_t *seen;

int asset_reads_start(void)
{
    seen = calloc(ROM_READS_MAP_BYTES, 1);
    if (!seen) {
        fprintf(stderr, "--asset-reads: out of memory\n");
        return -1;
    }
    rom_reads_record(seen);
    return 0;
}

static int was_read(uint32_t a) { return seen[a >> 3] >> (a & 7) & 1; }

int asset_reads_write(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return -1;
    }
    for (uint32_t a = 0; a < 0x1000000u; a++) {
        if (!was_read(a) || (a && was_read(a - 1)))
            continue;
        uint32_t b = a;
        while (b + 1 < 0x1000000u && was_read(b + 1))
            b++;
        fprintf(f, "%02X:%04X-%02X:%04X\n", (unsigned)(a >> 16), (unsigned)(a & 0xFFFF), (unsigned)(b >> 16),
                (unsigned)(b & 0xFFFF));
    }
    return fclose(f) == 0 ? 0 : -1;
}

unsigned asset_report(FILE *f)
{
    unsigned lines = 0;
    const AssetFault *out = asset_outside_reads(), *differs = rom_differing_reads();
    if (out->count) {
        fprintf(f, "asset outside the catalogue: $%02X:%04X x%lu (re/assets.tsv, tools/assets.py)\n",
                (unsigned)(out->first >> 16), (unsigned)(out->first & 0xFFFF), out->count);
        lines++;
    }
    if (differs->count) {
        fprintf(f, "asset differs from the cartridge: $%02X:%04X x%lu (the asset pack isn't the ROM's)\n",
                (unsigned)(differs->first >> 16), (unsigned)(differs->first & 0xFFFF), differs->count);
        lines++;
    }
    return lines;
}

static uint8_t *whole_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *data = NULL;
    size_t n = 0, cap = 0, got;
    do {
        if (n == cap) {
            cap = cap ? cap * 2 : (size_t)1 << 20;
            uint8_t *grown = realloc(data, cap);
            if (!grown) {
                free(data);
                fclose(f);
                return NULL;
            }
            data = grown;
        }
        got = fread(data + n, 1, cap - n, f);
        n += got;
    } while (got);
    fclose(f);
    *len = n;
    return data;
}

int asset_pack_use(const char *path, const uint8_t *rom, size_t size)
{
    /* Kept for the run: the store reads the pack in place. */
    static uint8_t *bytes;
    static AssetPack *pack;
    static AssetStore store;
    size_t len = 0;
    const char *why = "";
    bytes = whole_file(path, &len);
    pack = bytes ? asset_pack_open(bytes, len, &why) : NULL;
    if (!pack) {
        if (bytes)
            fprintf(stderr, "%s: %s; importing it again\n", path, why);
        free(bytes);
        bytes = asset_pack_import(rom, size, &len, &why);
        FILE *f = bytes ? fopen(path, "wb") : NULL;
        int written = f && fwrite(bytes, 1, len, f) == len;
        if (f && fclose(f) != 0)
            written = 0;
        if (!written) {
            fprintf(stderr, "%s: cannot import the asset pack: %s\n", path, bytes ? "cannot write it" : why);
            return -1;
        }
        pack = asset_pack_open(bytes, len, &why);
        if (!pack) {
            fprintf(stderr, "%s: %s\n", path, why);
            return -1;
        }
    }
    asset_pack_store(pack, &store);
    assets_use(&store);
    return 0;
}
