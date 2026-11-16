/* The menus' object scripts' records (menu/records.h): a scene's found as its
 * scripts load, a cartridge script's as the interpreter asks, and each
 * checked against the script's bytes before the interpreter takes it. */
#include "menu/records.h"

#include "data/assets.h"
#include "menu/menu_data.h"
#include "port/vars_ram.h"

enum { RECORDS_HEAD = 8, WORD = 4 };

/* The records the source keeps for resource `id`, into `block`: false, and
 * none, where it keeps none or they were read by other rows. */
static bool records_of(AssetId id, MenuRecordBlock *block)
{
    *block = (MenuRecordBlock){ NULL, NULL, 0, 0 };
    uint32_t length = 0;
    const uint8_t *bytes = id == ASSET_NONE ? NULL : asset_form(ASSET_FORM_SCRIPT_RECORDS, id, &length);
    if (!bytes || length < RECORDS_HEAD || (uintptr_t)bytes % WORD)
        return false;
    const uint32_t *head = (const uint32_t *)(const void *)bytes;
    uint32_t covered = head[1];
    uint32_t records_at = (RECORDS_HEAD + 2u * covered + (WORD - 1)) & ~(uint32_t)(WORD - 1);
    if (head[0] != menu_records_language() || covered > 0xFFFFu || records_at > length)
        return false;
    block->index = (const uint16_t *)(const void *)(bytes + RECORDS_HEAD);
    block->records = (const uint32_t *)(const void *)(bytes + records_at);
    block->covered = (uint16_t)covered;
    block->words = (length - records_at) / WORD;
    return true;
}

void menu_records_use(GameState *g, uint32_t packed, uint32_t at)
{
    MenuRecords *r = menu_records(g);
    r->scene_at = at;
    (void)records_of(asset_containing(packed), &r->scene);
}

/* The record at `offset` of a script, where it has one for `opcode`. */
static const MenuRecord *record_in(const MenuRecordBlock *block, uint32_t offset, uint8_t opcode)
{
    if (offset >= block->covered || !block->index[offset])
        return NULL;
    uint32_t word = block->index[offset] - 1u;
    if (word + MENU_RECORD_HEAD / WORD > block->words)
        return NULL;
    const MenuRecord *record = (const MenuRecord *)(const void *)(block->records + word);
    if (record->opcode != opcode || word + MENU_RECORD_HEAD / WORD + record->values > block->words)
        return NULL;
    return record;
}

/* Whether the script's bytes at `at` are still the ones the record was read
 * from (its opcode the caller's), read on from the opcode as the
 * interpreter reads a command's operands. */
static bool still_made_from(const GameState *g, uint32_t at, const MenuRecord *record)
{
    /* the program's look, not the game's work (port/vars_ram.h) */
    vars_peek(g, true);
    uint8_t k = 1;
    while (k < record->bytes && menu_script_byte(g, at + k) == record->made_from[k])
        k++;
    vars_peek(g, false);
    return k == record->bytes;
}

const MenuRecord *menu_record_at(GameState *g, uint32_t at, uint8_t opcode)
{
    const MenuRecords *r = menu_records(g);
    const MenuRecord *record = NULL;
    uint32_t offset = at - r->scene_at;
    if (offset < r->scene.covered) {
        record = record_in(&r->scene, offset, opcode);
    } else {
        /* a script in the cartridge: its resource's records, by its place in it */
        AssetId id = asset_containing(at);
        MenuRecordBlock block;
        if (records_of(id, &block))
            record = record_in(&block, at - asset_catalogue[id].at, opcode);
    }
    return record && still_made_from(g, at, record) ? record : NULL;
}
