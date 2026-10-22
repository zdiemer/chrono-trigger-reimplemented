#include "asset_report.h"

#include <stdint.h>
#include <stdlib.h>

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
    const AssetFault *out = asset_outside_reads();
    if (!out->count)
        return 0;
    fprintf(f, "asset outside the catalogue: $%02X:%04X x%lu (re/assets.tsv, tools/assets.py)\n",
            (unsigned)(out->first >> 16), (unsigned)(out->first & 0xFFFF), out->count);
    return 1;
}
