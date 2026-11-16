/* The loaded script's records (event/records.h): found as the script loads,
 * and each one checked against the script's bytes as the engine asks for it. */
#include "event/records.h"

#include "data/assets.h"

enum { RECORDS_HEAD = 8, WORD = 4 };

void event_records_use(GameState *g, uint32_t packed)
{
    EventRecords *r = event_records(g);
    *r = (EventRecords){ NULL, NULL, 0, 0 };
    AssetId id = asset_containing(packed);
    uint32_t length = 0;
    const uint8_t *block = id == ASSET_NONE ? NULL : asset_form(ASSET_FORM_SCRIPT_RECORDS, id, &length);
    if (!block || length < RECORDS_HEAD || (uintptr_t)block % WORD)
        return;
    const uint32_t *head = (const uint32_t *)(const void *)block;
    uint32_t covered = head[1];
    uint32_t records_at = (RECORDS_HEAD + 2u * covered + (WORD - 1)) & ~(uint32_t)(WORD - 1);
    if (head[0] != event_records_language() || covered > 0xFFFFu || records_at > length)
        return;
    r->index = (const uint16_t *)(const void *)(block + RECORDS_HEAD);
    r->records = (const uint32_t *)(const void *)(block + records_at);
    r->covered = (uint16_t)covered;
    r->words = (length - records_at) / WORD;
}

const EventRecord *event_record_at(GameState *g, uint16_t at, uint8_t opcode)
{
    const EventRecords *r = event_records(g);
    if (at >= r->covered || !r->index[at])
        return NULL;
    uint32_t word = r->index[at] - 1u;
    if (word + EVENT_RECORD_HEAD / WORD > r->words)
        return NULL;
    const EventRecord *record = (const EventRecord *)(const void *)(r->records + word);
    if (record->opcode != opcode || word + EVENT_RECORD_HEAD / WORD + record->values > r->words)
        return NULL;
    for (uint8_t k = 1; k < record->bytes; k++)
        if (script_byte(g, (uint16_t)(at + k)) != record->made_from[k])
            return NULL;
    return record;
}
