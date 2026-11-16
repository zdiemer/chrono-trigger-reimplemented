/* Checks the event scripts read a command at a time (game/event/records.h):
 * a record says what the engine decodes from the same bytes, parameter by
 * parameter, for every opcode the records cover and a spread of operand
 * bytes; and the engine takes a record only while the script's bytes are
 * still those it was read from. No ROM is needed. Exits non-zero on failure.
 *
 *   event_records_test */
#include <stdio.h>
#include <string.h>

#include "data/assets.h"
#include "event/records.h"
#include "event/script.h"
#include "port/state_host.h"
#include "vars.h"

enum { AT = 0x0400 };

static int failures;

#define EXPECT(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* A decoded parameter, and a record's value as the command's field holds it. */
static uint32_t field_of(const EventCommand *c, unsigned p)
{
    switch ((EventParameter)p) {
#define FIELD(type, name) case EVENT_PARAMETER_##name: return (uint32_t)c->name;
    EVENT_PARAMETERS(FIELD)
#undef FIELD
    default: return 0;
    }
}

static uint32_t as_field(unsigned p, uint32_t value)
{
    switch ((EventParameter)p) {
#define FIELD(type, name) case EVENT_PARAMETER_##name: return (uint32_t)(type)value;
    EVENT_PARAMETERS(FIELD)
#undef FIELD
    default: return 0;
    }
}

static const EventArg *arg_of(const EventOpcode *row, unsigned p)
{
    switch ((EventParameter)p) {
#define ARG(type, name) case EVENT_PARAMETER_##name: return &row->name;
    EVENT_PARAMETERS(ARG)
#undef ARG
    default: return NULL;
    }
}

/* The kinds whose value the bytes alone decide (the rest hold an operand). */
static bool decided_by_the_bytes(EventArgKind kind)
{
    switch (kind) {
    case ARG_CONSTANT: case ARG_BYTE: case ARG_BYTE_PLUS_ONE: case ARG_BITS: case ARG_WORD: case ARG_WORD_BITS:
    case ARG_ADDRESS: case ARG_VARIABLE: case ARG_OBJECT: case ARG_MEMBER:
        return true;
    default:
        return false;
    }
}

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return *seed >> 16;
}

static unsigned check_decoding(GameState *g)
{
    union {
        EventRecord record;
        uint8_t room[EVENT_RECORD_HEAD + 4 * EVENT_PARAMETER_COUNT];
    } r;
    unsigned recorded = 0;
    uint32_t seed = 1;
    for (unsigned op = 0; op < 256; op++) {
        const EventOpcode *row = event_opcode((uint8_t)op);
        if (!row)
            continue;
        for (unsigned trial = 0; trial < 64; trial++) {
            uint8_t bytes[EVENT_RECORD_BYTES];
            bytes[0] = (uint8_t)op;
            for (unsigned k = 1; k < sizeof bytes; k++)
                bytes[k] = (uint8_t)(trial < 4 ? trial * 0x40 + k : next_random(&seed));
            if (!event_record_read(bytes, sizeof bytes, &r.record))
                continue;
            recorded++;
            for (unsigned k = 0; k < sizeof bytes; k++)
                set_event_script_byte(g, AT + k, bytes[k]);
            EventCommand c = event_decode(g, NULL, row, AT);
            EXPECT(r.record.opcode == op && r.record.bytes >= 1 && r.record.bytes <= EVENT_RECORD_BYTES,
                   "opcode %02X: the record's head", op);
            EXPECT(!memcmp(r.record.made_from, bytes, r.record.bytes), "opcode %02X: the bytes it was read from", op);
            unsigned v = 0;
            for (unsigned i = event_named_from[op]; i < event_named_from[op + 1]; i++, v++) {
                unsigned p = event_named_parameters[i];
                EventArgKind kind = arg_of(row, p)->kind;
                uint32_t value = r.record.value[v] & EVENT_RECORD_VALUE_BITS;
                EXPECT(r.record.value[v] >> EVENT_RECORD_KIND_SHIFT == kind, "opcode %02X parameter %u: its kind", op, p);
                if (!decided_by_the_bytes(kind))
                    continue;
                EXPECT(as_field(p, value) == field_of(&c, p), "opcode %02X parameter %u: the record's %X, decoded %X",
                       op, p, (unsigned)as_field(p, value), (unsigned)field_of(&c, p));
            }
            EXPECT(v == r.record.values, "opcode %02X: a value a named parameter", op);
        }
    }
    return recorded;
}

/* ---- The engine takes a record only while the bytes are its own ---------------------------------- */

static uint32_t block[64];
static AssetId script_resource;

static const uint8_t *no_resource(void *ctx, AssetId id, uint32_t *length)
{
    (void)ctx, (void)id, (void)length;
    return NULL;
}

static const uint8_t *records_form(void *ctx, uint8_t form, AssetId id, uint32_t *length)
{
    (void)ctx;
    if (form != ASSET_FORM_EVENT_RECORDS || id != script_resource)
        return NULL;
    *length = sizeof block;
    return (const uint8_t *)block;
}

static void check_verified(GameState *g)
{
    script_resource = ASSET_NONE;
    for (AssetId id = 0; id < asset_catalogue_count && script_resource == ASSET_NONE; id++)
        if (!strcmp(asset_codec_names[asset_catalogue[id].codec], "lz:event"))
            script_resource = id;
    EXPECT(script_resource != ASSET_NONE, "the catalogue has a location's script");
    if (script_resource == ASSET_NONE)
        return;
    /* a script of 4 offsets whose command at 2 is "set the speed" (opcode $89, a byte) */
    const uint8_t command[] = { 0x89, 0x40 };
    union {
        EventRecord record;
        uint8_t room[EVENT_RECORD_HEAD + 4 * EVENT_PARAMETER_COUNT];
    } r;
    unsigned bytes = event_record_read(command, sizeof command, &r.record);
    EXPECT(bytes != 0 && bytes <= sizeof block - 16, "the command has a record");
    if (!bytes)
        return;
    uint8_t *b = (uint8_t *)block;
    memset(block, 0, sizeof block);
    block[0] = event_records_language();
    block[1] = 4;
    b[8 + 2 * 2] = 1;   /* offset 2's record: the first, a word on */
    memcpy(b + 16, &r.record, bytes);
    static AssetSource source = { no_resource, NULL, records_form };
    assets_use(&source);
    event_records_use(g, asset_catalogue[script_resource].at);
    set_event_script_byte(g, 2, command[0]);
    set_event_script_byte(g, 3, command[1]);
    EXPECT(event_record_at(g, 2, command[0]) != NULL, "the record of a command whose bytes are its own");
    EXPECT(event_record_at(g, 1, 0) == NULL, "no record where the index has none");
    set_event_script_byte(g, 3, 0x41);
    EXPECT(event_record_at(g, 2, command[0]) == NULL, "no record once the script's bytes changed");
    block[0] ^= 1;
    event_records_use(g, asset_catalogue[script_resource].at);
    set_event_script_byte(g, 3, command[1]);
    EXPECT(event_record_at(g, 2, command[0]) == NULL, "no records read by another language");
    assets_use(NULL);
}

int main(void)
{
    GameState *g = state_host_create(NULL);
    if (!g) {
        printf("FAIL: no game state\n");
        return 1;
    }
    unsigned recorded = check_decoding(g);
    EXPECT(recorded > 1000, "records for most commands (%u)", recorded);
    check_verified(g);
    if (!failures)
        printf("event records: %u commands' records decode as their bytes do\n", recorded);
    return failures != 0;
}
