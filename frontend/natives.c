#include "natives.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/generated_table.h"
#include "registry.h"
#include "trace.h"

static bool has_prefix(const char *name, const char *const *prefixes, int n)
{
    for (int k = 0; k < n; k++)
        if (strncmp(name, prefixes[k], strlen(prefixes[k])) == 0)
            return true;
    return false;
}

int natives_select(NativeTable *t, const uint8_t *rom, size_t rom_size, const NativeChoice *c)
{
    memset(t, 0, sizeof *t);
    bool use_generated = c->generated;
    if (use_generated && generated_native_count > 0 && ct_crc32(rom, rom_size) != generated_rom_crc) {
        fprintf(stderr, "warning: generated blocks are for ROM CRC-32 %08X, not this one; not using them\n",
                (unsigned)generated_rom_crc);
        use_generated = false;
    }
    int generated = use_generated ? generated_native_count : 0;
    bool ports = !c->generated_only ||
                 (c->only == NATIVES_ONLY_TIMED && (c->mode != NATIVE_ON || generated == 0));
    t->entries = malloc(sizeof *t->entries * (size_t)(game_native_count + generated + 1));
    t->index = malloc(sizeof *t->index * (size_t)((use_generated ? generated_index_count : 0) + 1));
    if (!t->entries || !t->index) {
        natives_free(t);
        return -1;
    }
    for (int i = 0; ports && i < game_native_count; i++) {
        const NativeEntry *e = &game_natives[i];
        if (!has_prefix(e->name, c->exclude, c->nexclude) &&
            (!c->ninclude || has_prefix(e->name, c->include, c->ninclude)))
            t->entries[t->ported++] = *e;
    }
    for (int i = 0; i < generated; i++) {
        const NativeEntry *e = &generated_natives[i];
        if (!has_prefix(e->name, c->exclude, c->nexclude))
            t->entries[t->ported + t->generated++] = *e;
    }
    t->count = t->ported + t->generated;
    for (int i = 0; use_generated && i < generated_index_count; i++)
        if (!has_prefix(generated_index[i].name, c->exclude, c->nexclude))
            t->index[t->index_count++] = generated_index[i];
    return 0;
}

void natives_free(NativeTable *t)
{
    free(t->entries);
    free(t->index);
    memset(t, 0, sizeof *t);
}

NativeRuntime *natives_restart(NativeRuntime *old, const NativeTable *t, NativeMode mode, Snes *snes)
{
    native_destroy(old);
    snes->intercept = NULL;
    snes->intercept_ctx = NULL;
    NativeRuntime *rt = native_create(t->entries, t->count, mode);
    native_set_index(rt, t->index, t->index_count);   /* used only with generated entries */
    native_attach(rt, snes);
    return rt;
}
