#include "natives.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/recomp_table.h"
#include "registry.h"
#include "trace.h"

int natives_select(NativeTable *t, const uint8_t *rom, size_t rom_size, NativeMode mode, bool recomp,
                   bool recomp_only)
{
    memset(t, 0, sizeof *t);
    if (recomp && recomp_native_count > 0 && ct_crc32(rom, rom_size) != recomp_rom_crc) {
        fprintf(stderr, "warning: generated blocks are for ROM CRC-32 %08X, not this one; not using them\n",
                (unsigned)recomp_rom_crc);
        recomp = false;
    }
    t->generated = recomp ? recomp_native_count : 0;
    t->ported = recomp_only && t->generated > 0 && mode == NATIVE_ON ? 0 : game_native_count;
    t->entries = malloc(sizeof *t->entries * (size_t)(t->ported + t->generated + 1));
    if (!t->entries)
        return -1;
    memcpy(t->entries, game_natives, sizeof *t->entries * (size_t)t->ported);
    if (t->generated)
        memcpy(t->entries + t->ported, recomp_natives, sizeof *t->entries * (size_t)t->generated);
    t->count = t->ported + t->generated;
    return 0;
}

void natives_free(NativeTable *t)
{
    free(t->entries);
    memset(t, 0, sizeof *t);
}

NativeRuntime *natives_restart(NativeRuntime *old, const NativeTable *t, NativeMode mode, Snes *snes)
{
    native_destroy(old);
    snes->intercept = NULL;
    snes->intercept_ctx = NULL;
    NativeRuntime *rt = native_create(t->entries, t->count, mode);
    native_set_index(rt, recomp_index, recomp_index_count);   /* used only with generated entries */
    native_attach(rt, snes);
    return rt;
}
