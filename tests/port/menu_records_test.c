/* Checks the menus' object scripts read a command at a time
 * (game/menu/records.h): a command taken from its record is the command the
 * interpreter decodes from the same bytes, for every opcode the language has
 * and a spread of operand bytes; and the interpreter takes a record only
 * while the script's bytes are still those it was read from. No ROM is
 * needed. Exits non-zero on failure.
 *
 *   menu_records_test */
#include <stdio.h>
#include <string.h>

#include "data/assets.h"
#include "menu/records.h"
#include "menu/script.h"
#include "port/state_host.h"

/* Where a world map's scene's scripts are unpacked to (overworld/setup.c). */
enum { SCENE_AT = 0x7F0400 };

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

typedef union {
    MenuRecord record;
    uint8_t room[MENU_RECORD_HEAD + 4 * MENU_PARAMETER_COUNT];
} RecordRoom;

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return *seed >> 16;
}

static unsigned check_decoding(GameState *g)
{
    RecordRoom r;
    unsigned recorded = 0;
    uint32_t seed = 1;
    for (unsigned op = 0; op < 256; op++) {
        const ScriptOpcode *row = menu_script_opcode((uint8_t)op);
        if (!row || !row->run)
            continue;
        for (unsigned trial = 0; trial < 64; trial++) {
            uint8_t bytes[MENU_RECORD_BYTES];
            bytes[0] = (uint8_t)op;
            for (unsigned k = 1; k < sizeof bytes; k++)
                bytes[k] = (uint8_t)(trial < 4 ? trial * 0x40 + k : next_random(&seed));
            unsigned size = menu_record_read(bytes, sizeof bytes, &r.record);
            EXPECT(size != 0, "opcode %02X: a record", op);
            if (!size)
                continue;
            recorded++;
            for (unsigned k = 0; k < sizeof bytes; k++)
                state_host_write8(g, SCENE_AT + k, bytes[k]);
            ScriptCommand decoded, taken;
            menu_script_decode(g, row, 3, SCENE_AT, NULL, &decoded);
            menu_script_decode_record(row, 3, SCENE_AT, NULL, &r.record, &taken);
            EXPECT(r.record.opcode == op && r.record.bytes >= row->length && r.record.bytes <= MENU_RECORD_BYTES,
                   "opcode %02X: the record's head", op);
            EXPECT(!memcmp(r.record.made_from, bytes, r.record.bytes), "opcode %02X: the bytes it was read from", op);
            EXPECT(size == MENU_RECORD_HEAD + 4u * r.record.values, "opcode %02X: its size", op);
            EXPECT(!memcmp(&decoded, &taken, sizeof decoded), "opcode %02X trial %u: taken as decoded", op, trial);
        }
    }
    return recorded;
}

/* ---- The interpreter takes a record only while the bytes are its own ----------------------------- */

static uint32_t block[64];
static AssetId scene_resource;

static const uint8_t *no_resource(void *ctx, AssetId id, uint32_t *length)
{
    (void)ctx, (void)id, (void)length;
    return NULL;
}

static const uint8_t *records_form(void *ctx, uint8_t form, AssetId id, uint32_t *length)
{
    (void)ctx;
    if (form != ASSET_FORM_SCRIPT_RECORDS || id != scene_resource)
        return NULL;
    *length = sizeof block;
    return (const uint8_t *)block;
}

static void check_verified(GameState *g)
{
    scene_resource = ASSET_NONE;
    for (AssetId id = 0; id < asset_catalogue_count && scene_resource == ASSET_NONE; id++)
        if (!strcmp(asset_codec_names[asset_catalogue[id].codec], "lz:object"))
            scene_resource = id;
    EXPECT(scene_resource != ASSET_NONE, "the catalogue has a scene's scripts");
    if (scene_resource == ASSET_NONE)
        return;
    /* scripts of 4 offsets whose command at 2 counts up a byte of the object (opcode $0B) */
    const uint8_t command[] = { 0x0B, 0x40 };
    RecordRoom r;
    unsigned bytes = menu_record_read(command, sizeof command, &r.record);
    EXPECT(bytes != 0 && bytes <= sizeof block - 16, "the command has a record");
    if (!bytes)
        return;
    uint8_t *b = (uint8_t *)block;
    memset(block, 0, sizeof block);
    block[0] = menu_records_language();
    block[1] = 4;
    b[8 + 2 * 2] = 1;   /* offset 2's record: the first, a word on */
    memcpy(b + 16, &r.record, bytes);
    static AssetSource source = { no_resource, NULL, records_form };
    assets_use(&source);
    menu_records_use(g, asset_catalogue[scene_resource].at, SCENE_AT);
    state_host_write8(g, SCENE_AT + 2, command[0]);
    state_host_write8(g, SCENE_AT + 3, command[1]);
    EXPECT(menu_record_at(g, SCENE_AT + 2, command[0]) != NULL, "the record of a command whose bytes are its own");
    EXPECT(menu_record_at(g, SCENE_AT + 1, 0) == NULL, "no record where the index has none");
    EXPECT(menu_record_at(g, SCENE_AT + 2, 0x0C) == NULL, "no record for another opcode");
    state_host_write8(g, SCENE_AT + 3, 0x41);
    EXPECT(menu_record_at(g, SCENE_AT + 2, command[0]) == NULL, "no record once the script's bytes changed");
    block[0] ^= 1;
    menu_records_use(g, asset_catalogue[scene_resource].at, SCENE_AT);
    state_host_write8(g, SCENE_AT + 3, command[1]);
    EXPECT(menu_record_at(g, SCENE_AT + 2, command[0]) == NULL, "no records read by another language");
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
    EXPECT(recorded > 1000, "records for every command (%u)", recorded);
    check_verified(g);
    if (!failures)
        printf("menu records: %u commands' records taken as their bytes decode\n", recorded);
    return failures != 0;
}
