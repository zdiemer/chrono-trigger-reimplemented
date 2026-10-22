#include "assets.h"

#include <stdlib.h>
#include <string.h>

/* ---- The ROM ----------------------------------------------------------------------------------- */

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

const uint8_t *asset_rom_image(const uint8_t *file, size_t len, size_t *size)
{
    size_t header = len % 0x8000 == 0x200 ? 0x200 : 0;
    *size = len - header;
    return file + header;
}

/* Where a resource's bytes are in the image: HiROM, so the image is banks
 * $C0-$FF in order. */
static size_t image_offset(uint32_t at) { return (size_t)(at - 0xC00000u); }

static const AssetEntry *entry_named(const char *name, const char *type)
{
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        if (!strcmp(e->name, name))
            return strcmp(asset_type_names[e->type], type) ? NULL : e;
    }
    return NULL;
}

static const uint8_t *rom_find(void *ctx, const char *name, const char *type, uint32_t *length)
{
    const AssetRom *r = ctx;
    const AssetEntry *e = entry_named(name, type);
    if (!e || image_offset(e->at) + e->length > r->size)
        return NULL;
    *length = e->length;
    return r->rom + image_offset(e->at);
}

void asset_store_rom(AssetStore *store, AssetRom *ctx, const uint8_t *rom, size_t size)
{
    ctx->rom = rom;
    ctx->size = size;
    store->find = rom_find;
    store->ctx = ctx;
}

/* ---- The pack ---------------------------------------------------------------------------------- */

/* "CTASSETS", the format, how many resources, the catalogue's version and the
 * ROM's CRC-32; then a directory entry a resource (where its bytes are in the
 * pack, how many, its name and its type, each a length byte and the letters);
 * then the bytes. Numbers are little-endian. */
static const char MAGIC[8] = { 'C', 'T', 'A', 'S', 'S', 'E', 'T', 'S' };
enum { FORMAT = 1, HEAD_BYTES = 20, ENTRY_FIXED_BYTES = 10 };

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

static size_t entry_bytes(const AssetEntry *e)
{
    return ENTRY_FIXED_BYTES + strlen(e->name) + strlen(asset_type_names[e->type]);
}

uint8_t *asset_pack_build(const AssetStore *store, uint32_t rom_crc, size_t *len, const char **why)
{
    size_t total = HEAD_BYTES, data = HEAD_BYTES;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        total += entry_bytes(&asset_catalogue[i]) + asset_catalogue[i].length;
        data += entry_bytes(&asset_catalogue[i]);
    }
    uint8_t *pack = malloc(total);
    if (!pack) {
        *why = "out of memory";
        return NULL;
    }
    memcpy(pack, MAGIC, sizeof MAGIC);
    put16(pack + 8, FORMAT);
    put16(pack + 10, asset_catalogue_count);
    put32(pack + 12, asset_catalogue_version);
    put32(pack + 16, rom_crc);
    size_t dir = HEAD_BYTES;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        const char *type = asset_type_names[e->type];
        uint32_t n = 0;
        const uint8_t *bytes = store->find(store->ctx, e->name, type, &n);
        if (!bytes || n != e->length) {
            free(pack);
            *why = "the store hasn't every resource of the catalogue";
            return NULL;
        }
        put32(pack + dir, (uint32_t)data);
        put32(pack + dir + 4, n);
        dir += 8;
        size_t name_len = strlen(e->name), type_len = strlen(type);
        pack[dir++] = (uint8_t)name_len;
        memcpy(pack + dir, e->name, name_len);
        dir += name_len;
        pack[dir++] = (uint8_t)type_len;
        memcpy(pack + dir, type, type_len);
        dir += type_len;
        memcpy(pack + data, bytes, n);
        data += n;
    }
    *len = total;
    return pack;
}

uint8_t *asset_pack_import(const uint8_t *rom, size_t size, size_t *len, const char **why)
{
    if (size != ASSET_ROM_SIZE || crc32_of(rom, size) != ASSET_ROM_CRC32) {
        *why = "not Chrono Trigger (USA): its CRC-32 isn't 2D206BF7";
        return NULL;
    }
    AssetRom ctx;
    AssetStore store;
    asset_store_rom(&store, &ctx, rom, size);
    return asset_pack_build(&store, ASSET_ROM_CRC32, len, why);
}

typedef struct {
    const char *name, *type;   /* into the pack: not terminated */
    uint8_t name_len, type_len;
    const uint8_t *bytes;
    uint32_t length;
} PackEntry;

struct AssetPack {
    uint16_t count;
    PackEntry *entries;
};

AssetPack *asset_pack_open(const uint8_t *bytes, size_t len, const char **why)
{
    if (len < HEAD_BYTES || memcmp(bytes, MAGIC, sizeof MAGIC)) {
        *why = "not an asset pack";
        return NULL;
    }
    if (get16(bytes + 8) != FORMAT || get32(bytes + 12) != asset_catalogue_version) {
        *why = "a pack of another format or catalogue: import the ROM again";
        return NULL;
    }
    AssetPack *pack = calloc(1, sizeof *pack);
    uint16_t count = (uint16_t)get16(bytes + 10);
    PackEntry *entries = calloc(count ? count : 1, sizeof *entries);
    if (!pack || !entries) {
        free(pack);
        free(entries);
        *why = "out of memory";
        return NULL;
    }
    size_t at = HEAD_BYTES;
    for (uint16_t i = 0; i < count; i++) {
        PackEntry *e = &entries[i];
        if (at + 9 > len)
            goto cut;
        uint32_t data = get32(bytes + at), n = get32(bytes + at + 4);
        e->name_len = bytes[at + 8];
        at += 9;
        if (at + e->name_len + 1 > len)
            goto cut;
        e->name = (const char *)bytes + at;
        at += e->name_len;
        e->type_len = bytes[at++];
        if (at + e->type_len > len || data > len || n > len - data)
            goto cut;
        e->type = (const char *)bytes + at;
        at += e->type_len;
        e->bytes = bytes + data;
        e->length = n;
    }
    pack->count = count;
    pack->entries = entries;
    return pack;
cut:
    free(entries);
    free(pack);
    *why = "the pack is cut short";
    return NULL;
}

static int same(const char *s, uint8_t n, const char *z) { return strlen(z) == n && !memcmp(s, z, n); }

static const uint8_t *pack_find(void *ctx, const char *name, const char *type, uint32_t *length)
{
    const AssetPack *pack = ctx;
    for (uint16_t i = 0; i < pack->count; i++) {
        const PackEntry *e = &pack->entries[i];
        if (same(e->name, e->name_len, name)) {
            if (!same(e->type, e->type_len, type))
                return NULL;
            *length = e->length;
            return e->bytes;
        }
    }
    return NULL;
}

void asset_pack_store(AssetPack *pack, AssetStore *store)
{
    store->find = pack_find;
    store->ctx = pack;
}

void asset_pack_close(AssetPack *pack)
{
    if (pack) {
        free(pack->entries);
        free(pack);
    }
}
