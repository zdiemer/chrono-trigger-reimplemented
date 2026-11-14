/* The catalogue's lookups -- which resource a cartridge address lies in -- and
 * the cartridge's reads (game/rom.h), served from the driver's store. */
#include "data/assets.h"

#include <stddef.h>

#include "rom.h"
#include "wram.h"

/* The cartridge is HiROM: banks $C0-$FF hold it whole, banks $40-$7D mirror
 * them, and the upper half of each of banks $00-$3F and $80-$BF mirrors the
 * same half of its bank there. Anything else isn't the cartridge's. */
enum { ROM_BANKS = 0xC0, BANK_MASK = 0x3F, UPPER_HALF = 0x8000, NOT_ROM = 0xFFFFFFFFu };

static uint32_t cartridge_address(uint32_t at)
{
    uint8_t bank = (uint8_t)(at >> 16);
    uint16_t offset = (uint16_t)at;
    if (bank >= ROM_BANKS || (bank >= 0x40 && bank < 0x7E))
        return (uint32_t)(ROM_BANKS | (bank & BANK_MASK)) << 16 | offset;
    if (offset >= UPPER_HALF && bank != 0x7E && bank != 0x7F)
        return (uint32_t)(ROM_BANKS | (bank & BANK_MASK)) << 16 | offset;
    return NOT_ROM;
}

bool asset_is_cartridge(uint32_t at) { return cartridge_address(at) != NOT_ROM; }

AssetId asset_containing(uint32_t at)
{
    static AssetId last = ASSET_NONE;   /* reads come in runs through one resource */
    uint32_t a = cartridge_address(at);
    if (a == NOT_ROM)
        return ASSET_NONE;
    if (last != ASSET_NONE && a - asset_catalogue[last].at < asset_catalogue[last].length)
        return last;
    /* The rows are in address order and don't overlap: the last one starting
     * at or before `a`, if `a` is inside it. */
    uint16_t lo = 0, hi = asset_catalogue_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
        if (asset_catalogue[mid].at <= a)
            lo = (uint16_t)(mid + 1);
        else
            hi = mid;
    }
    if (lo == 0 || a - asset_catalogue[lo - 1].at >= asset_catalogue[lo - 1].length)
        return ASSET_NONE;
    last = (AssetId)(lo - 1);
    return last;
}

static AssetFault outside;

const AssetFault *asset_outside_reads(void) { return &outside; }

void asset_note_outside(uint32_t at)
{
    if (cartridge_address(at) == NOT_ROM)
        return;   /* work RAM a pointer in the data names: not the cartridge's */
    if (outside.count++ == 0)
        outside.first = at & 0xFFFFFFu;
}

/* ---- The store ---------------------------------------------------------------------------------- */

static const AssetStore *store;

void assets_use(const AssetStore *s)
{
    store = s;
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        asset_served[i] = (AssetServed){ NULL, 0 };
}

/* A resource's bytes from the store, or NULL where there is no store or it
 * hasn't the resource. A resource is asked for once, by name and type, and
 * what the store answers kept. */
static const uint8_t *resource_bytes(AssetId id)
{
    if (!store)
        return NULL;
    const AssetEntry *e = &asset_catalogue[id];
    AssetServed *s = &asset_served[id];
    if (!s->asked) {
        uint32_t length = 0;
        s->bytes = store->find(store->ctx, e->name, asset_type_names[e->type], &length);
        if (length != e->length)
            s->bytes = NULL;
        s->asked = 1;
    }
    return s->bytes;
}

/* The store's copy of the cartridge byte at `at`, or NULL: the address is in
 * no resource (counted), there is no store, or it hasn't the resource. */
static const uint8_t *served(uint32_t at)
{
    AssetId id = asset_containing(at);
    if (id == ASSET_NONE) {
        asset_note_outside(at);
        return NULL;
    }
    const uint8_t *bytes = resource_bytes(id);
    return bytes ? bytes + (cartridge_address(at) - asset_catalogue[id].at) : NULL;
}

/* Where a pointer in the data names RAM, the byte is the game's RAM's
 * (wram_data_read8), never the cartridge's. */
uint8_t rom_read8(const GameState *g, uint32_t at)
{
    if (cartridge_address(at) == NOT_ROM)
        return wram_data_read8(g, at & 0xFFFFFFu);
    return rom_cartridge_read(g, at, served(at));
}

/* The read is the same one the address would make: timed as a cartridge
 * read at the address the original keeps the byte at. */
uint8_t asset_read8(const GameState *g, AssetId resource, uint32_t offset)
{
    const AssetEntry *e = &asset_catalogue[resource];
    if (offset >= e->length)
        return rom_read8(g, e->at + offset);
    const uint8_t *bytes = resource_bytes(resource);
    return rom_cartridge_read(g, e->at + offset, bytes ? bytes + offset : NULL);
}

uint16_t asset_read16(const GameState *g, AssetId resource, uint32_t offset)
{
    uint8_t low = asset_read8(g, resource, offset);
    return (uint16_t)(low | asset_read8(g, resource, offset + 1) << 8);
}

uint8_t asset_read8_at_place(const GameState *g, AssetId resource, uint16_t place)
{
    const AssetEntry *e = &asset_catalogue[resource];
    uint16_t offset = (uint16_t)(place - (uint16_t)e->at);
    if (offset >= e->length)
        return rom_read8(g, (e->at & 0xFF0000u) | place);
    return asset_read8(g, resource, offset);
}

uint16_t asset_read16_at_place(const GameState *g, AssetId resource, uint16_t place)
{
    const AssetEntry *e = &asset_catalogue[resource];
    uint16_t offset = (uint16_t)(place - (uint16_t)e->at);
    if (offset >= e->length)
        return rom_read16(g, (e->at & 0xFF0000u) | place);
    return asset_read16(g, resource, offset);
}

uint32_t asset_address(AssetId resource) { return asset_catalogue[resource].at; }

uint32_t asset_place_address(AssetId resource, uint16_t place) { return (asset_catalogue[resource].at & 0xFF0000u) | place; }

uint8_t rom_read8_in_bank_of(const GameState *g, AssetId resource, uint16_t place)
{
    return rom_read8(g, (asset_catalogue[resource].at & 0xFF0000u) | place);
}

uint16_t rom_read16(const GameState *g, uint32_t at)
{
    uint8_t low = rom_read8(g, at);
    return (uint16_t)(low | rom_read8(g, at + 1) << 8);
}

PfData rom_data(uint32_t at, uint32_t length)
{
    if ((at >> 16) == 0x7E || (at >> 16) == 0x7F)
        return PF_GAME(at);
    PfData data = { .addr = at, .byte_at = served };
    uint32_t left = length, cursor = at;
    while (left) {
        AssetId id = asset_containing(cursor);
        uint32_t span = 1;
        if (id == ASSET_NONE) {
            asset_note_outside(cursor);
        } else {
            const AssetEntry *e = &asset_catalogue[id];
            span = e->length - (cartridge_address(cursor) - e->at);
            uint32_t bank_left = 0x10000u - (uint16_t)cursor;
            if (span > bank_left)
                span = bank_left;
            if (left == length && span >= length)
                data.ptr = served(at);
            else
                (void)served(cursor);
        }
        if (span > left)
            span = left;
        left -= span;
        cursor = (at & 0xFF0000u) | (uint16_t)(cursor + span);
    }
    return data;
}

PfData rom_data_mode(uint32_t at, uint32_t length, uint8_t mode)
{
    if (mode & 0x08)
        return rom_data(at, length ? 1 : 0);
    if (!(mode & 0x10) || !length)
        return rom_data(at, length);
    uint32_t first = (at & 0xFF0000u) | (uint16_t)(at - length + 1);
    PfData data = rom_data(first, length);
    data.addr = at;
    data.ptr = NULL;
    return data;
}
