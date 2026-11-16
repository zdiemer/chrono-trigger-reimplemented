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

/* The resource found last in each 4 KiB of the cartridge: reads come in
 * runs through a few resources at once, and a run finds its resource with one
 * comparison rather than a search of the catalogue. */
enum { NEAR_SHIFT = 12, NEARBY = 32 };

AssetId asset_containing(uint32_t at)
{
    static uint16_t nearby[NEARBY];   /* the resource, plus 1; 0 for none yet */
    uint32_t a = cartridge_address(at);
    if (a == NOT_ROM)
        return ASSET_NONE;
    uint16_t *near = &nearby[(a >> NEAR_SHIFT) & (NEARBY - 1)];
    if (*near && a - asset_catalogue[*near - 1u].at < asset_catalogue[*near - 1u].length)
        return (AssetId)(*near - 1u);
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
    *near = lo;
    return (AssetId)(lo - 1);
}

static AssetFault outside, unread;

const AssetFault *asset_outside_reads(void) { return &outside; }
const AssetFault *asset_unread_reads(void) { return &unread; }

static bool left_untyped(AssetId id)
{
    uint8_t type = asset_catalogue[id].type;
    return type == ASSET_TYPE_MIXED || type == ASSET_TYPE_UNTYPED || type == ASSET_TYPE_UNDETERMINED;
}

static AssetId unread_rows[ASSET_UNREAD_KEPT];
static unsigned unread_rows_kept;

unsigned asset_unread_rows(const AssetId **rows)
{
    *rows = unread_rows;
    return unread_rows_kept;
}

static void note_if_unread(AssetId id, uint32_t at)
{
    if (!left_untyped(id))
        return;
    if (unread.count++ == 0)
        unread.first = cartridge_address(at);
    for (unsigned i = 0; i < unread_rows_kept; i++)
        if (unread_rows[i] == id)
            return;
    if (unread_rows_kept < ASSET_UNREAD_KEPT)
        unread_rows[unread_rows_kept++] = id;
}

void asset_note_outside(uint32_t at)
{
    if (cartridge_address(at) == NOT_ROM)
        return;   /* work RAM a pointer in the data names: not the cartridge's */
    if (outside.count++ == 0)
        outside.first = at & 0xFFFFFFu;
}

/* ---- The store ---------------------------------------------------------------------------------- */

static const AssetSource *source;

/* The resources read lately, and their bytes: the reads come in runs through
 * a few resources at once, and a run asks the source once. A new source asks
 * again. */
enum { HELD = 16 };
static struct {
    AssetId id;
    const uint8_t *bytes;
} held[HELD];
static bool holding;

static void forget_found(void);

void assets_use(const AssetSource *s)
{
    source = s;
    forget_found();
    for (unsigned i = 0; i < HELD; i++)
        held[i].id = ASSET_NONE;
    holding = true;
}

/* A resource's bytes from the source, or NULL where there is no source or it
 * hasn't the resource as the catalogue has it. */
static const uint8_t *resource_bytes(AssetId id)
{
    if (!holding)
        assets_use(source);
    if (held[id % HELD].id == id)
        return held[id % HELD].bytes;
    if (!source)
        return NULL;
    uint32_t length = 0;
    const uint8_t *bytes = source->resource(source->ctx, id, &length);
    held[id % HELD].id = id;
    held[id % HELD].bytes = bytes && length == asset_catalogue[id].length ? bytes : NULL;
    return held[id % HELD].bytes;
}

const uint8_t *asset_form(AssetForm form, AssetId id, uint32_t *length)
{
    if (!source || !source->form)
        return NULL;
    return source->form(source->ctx, (uint8_t)form, id, length);
}

/* The resource a read by address found last in each 4 KiB of the cartridge,
 * with where it lies and the store's bytes for it, so that the next read in
 * it finds its byte with one comparison: what asset_containing and
 * resource_bytes would answer, kept together until a new source. */
static struct {
    uint32_t at, length;    /* length 0: none yet */
    const uint8_t *bytes;
    AssetId id;
    bool untyped;
} found[NEARBY];

static void forget_found(void)
{
    for (unsigned i = 0; i < NEARBY; i++)
        found[i].length = 0;
}

/* The store's copy of the cartridge byte at `at` (its cartridge address
 * `a`), found afresh: the resource it lies in, kept as the one found last in
 * its 4 KiB. */
static const uint8_t *find_served(uint32_t at, uint32_t a)
{
    AssetId id = asset_containing(at);
    if (id == ASSET_NONE) {
        asset_note_outside(at);
        return NULL;
    }
    note_if_unread(id, at);
    const uint8_t *bytes = resource_bytes(id);
    const uint32_t i = (a >> NEAR_SHIFT) & (NEARBY - 1);
    found[i].at = asset_catalogue[id].at;
    found[i].length = asset_catalogue[id].length;
    found[i].bytes = bytes;
    found[i].id = id;
    found[i].untyped = left_untyped(id);
    return bytes ? bytes + (a - asset_catalogue[id].at) : NULL;
}

/* The store's copy of the cartridge byte at `at`, or NULL: the address is in
 * no resource (counted), there is no store, or it hasn't the resource. */
static const uint8_t *served(uint32_t at)
{
    uint32_t a = cartridge_address(at);
    const uint32_t i = (a >> NEAR_SHIFT) & (NEARBY - 1);
    if (a == NOT_ROM || a - found[i].at >= found[i].length)
        return find_served(at, a);
    if (found[i].untyped)
        note_if_unread(found[i].id, at);
    return found[i].bytes ? found[i].bytes + (a - found[i].at) : NULL;
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
    note_if_unread(resource, e->at + offset);
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
    return rom_read8(g, asset_place_address(resource, place));
}

uint16_t rom_read16_in_bank_of(const GameState *g, AssetId resource, uint16_t place)
{
    return rom_read16(g, asset_place_address(resource, place));
}

RomRun rom_run_from(uint32_t at)
{
    RomRun run = { at, 0, NULL };
    uint32_t a = cartridge_address(at);
    AssetId id = asset_containing(at);
    /* A row marked unread has no run: each of its reads goes by rom_read8, which notes it. */
    const uint8_t *bytes = id == ASSET_NONE || left_untyped(id) ? NULL : resource_bytes(id);
    if (!bytes)
        return run;
    const AssetEntry *e = &asset_catalogue[id];
    uint32_t in_resource = e->length - (a - e->at), in_bank = 0x10000u - (uint16_t)at;
    run.bytes = bytes + (a - e->at);
    run.length = in_resource < in_bank ? in_resource : in_bank;
    return run;
}

const uint8_t *rom_bytes_on(uint32_t at, uint32_t length)
{
    RomRun run = rom_run_from(at);
    if (!run.bytes || length <= run.length)
        return run.bytes;
    /* on into the next bank: a resource holds its bytes there straight after */
    RomRun next = rom_run_from(at + run.length);
    return next.bytes == run.bytes + run.length && length - run.length <= next.length ? run.bytes : NULL;
}

void rom_read_span(const GameState *g, uint32_t at, uint8_t *to, uint32_t length)
{
    const uint8_t *bytes = rom_bytes_on(at, length);
    for (uint32_t i = 0; i < length; i++)
        to[i] = bytes ? rom_cartridge_read(g, at + i, bytes + i) : rom_read8(g, at + i);
}

const AssetUnpacked *rom_unpacked(uint32_t at)
{
    AssetId id = asset_containing(at);
    if (id == ASSET_NONE || cartridge_address(at) != asset_catalogue[id].at || left_untyped(id))
        return NULL;
    uint32_t length = 0;
    const AssetUnpacked *u = (const AssetUnpacked *)asset_form(ASSET_FORM_UNPACKED, id, &length);
    return u && length >= sizeof *u && length - sizeof *u >= u->length ? u : NULL;
}

uint8_t rom_run_read8(const GameState *g, const RomRun *run, uint32_t at)
{
    uint32_t i = at - run->at;
    if (i < run->length)
        return rom_cartridge_read(g, at, run->bytes + i);
    return rom_read8(g, at);
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
