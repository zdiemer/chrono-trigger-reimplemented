#include "storage.h"
#include "assets.h"
#include "file_store.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t *files_asset_pack(const Files *f, const uint8_t *rom, size_t size, bool fresh, size_t *len)
{
    bool missing = true;
    uint8_t *pack = fresh ? NULL : files_read(f->assets, len, false, &missing);
    if (pack || !missing) {
        if (!pack)
            fprintf(stderr, "cannot read %s\n", f->assets);
        return pack;
    }
    const char *why = "";
    pack = asset_pack_import(rom, size, len, &why);
    if (!pack) {
        fprintf(stderr, "cannot import the game's resources: %s\n", why);
        return NULL;
    }
    if (file_store_replace(f->assets, pack, *len) != 0) {
        free(pack);
        return NULL;
    }
    fprintf(stderr, "imported %u resources (%lu bytes) from the ROM into %s\n", (unsigned)asset_catalogue_count,
            (unsigned long)*len, f->assets);
    return pack;
}
