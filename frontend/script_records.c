/* The scripts read a command at a time (frontend/script_records.h). */
#include "script_records.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event/records.h"
#include "event/script.h"
#include "file_store.h"
#include "menu/records.h"
#include "packed.h"

/* An unpacked event script: its objects' count, then each object's 16
 * functions' offsets, then the code; the engine's offsets count from the
 * byte after the count (game/event/calls.c). The built-in commands follow
 * the script from offset $16FF (game/field/location.c), and change as the
 * game runs: no record reads them. */
enum { FUNCTIONS = 16, BUILT_IN_COMMANDS = 0x16FF, WORD = 4 };

/* The most an object script's index reaches: a bank. */
enum { BANK_BYTES = 0x10000 };

typedef struct {
    uint8_t *bytes;
    size_t length, room;
} Out;

static bool put(Out *o, const void *bytes, size_t n)
{
    if (o->length + n > o->room) {
        size_t room = o->room ? o->room * 2 : 1u << 20;
        while (room < o->length + n)
            room *= 2;
        uint8_t *grown = realloc(o->bytes, room);
        if (!grown)
            return false;
        o->bytes = grown, o->room = room;
    }
    if (bytes)
        memcpy(o->bytes + o->length, bytes, n);
    else
        memset(o->bytes + o->length, 0, n);
    o->length += n;
    return true;
}

static void put32_at(Out *o, size_t at, uint32_t v)
{
    for (unsigned k = 0; k < 4; k++)
        o->bytes[at + k] = (uint8_t)(v >> 8 * k);
}

/* ---- A script's records, either language's ----------------------------------------------------- */

/* A command's record read into `room`, by its language: its size in bytes,
 * its head's in *head (the values follow it, a word each), or 0 for none. */
typedef unsigned (*ReadRecord)(const uint8_t *command, uint16_t available, void *room, unsigned *head);

enum {
    EVENT_ROOM = EVENT_RECORD_HEAD + 4 * EVENT_PARAMETER_COUNT,
    MENU_ROOM = MENU_RECORD_HEAD + 4 * MENU_RECORD_MOST_VALUES,
};
typedef union {
    EventRecord event;
    MenuRecord menu;
    uint8_t room[EVENT_ROOM > MENU_ROOM ? EVENT_ROOM : MENU_ROOM];
} RecordRoom;

static unsigned read_event_record(const uint8_t *command, uint16_t available, void *room, unsigned *head)
{
    *head = EVENT_RECORD_HEAD;
    return event_record_read(command, available, &((RecordRoom *)room)->event);
}

static unsigned read_menu_record(const uint8_t *command, uint16_t available, void *room, unsigned *head)
{
    *head = MENU_RECORD_HEAD;
    return menu_record_read(command, available, &((RecordRoom *)room)->menu);
}

/* The records of the commands marked in `starts` of the script `s`
 * (`covered` bytes), as game/event/records.h and game/menu/records.h lay
 * them out. */
static bool put_records(Out *o, uint32_t language, const uint8_t *s, uint16_t covered, const uint8_t *starts,
                        ReadRecord read)
{
    size_t block = o->length;
    bool ok = put(o, NULL, 8) && put(o, NULL, 2u * covered) && put(o, NULL, -(o->length - block) % WORD);
    if (ok) {
        put32_at(o, block, language);
        put32_at(o, block + 4, covered);
    }
    size_t records = o->length;
    RecordRoom r;
    for (uint16_t at = 0; ok && at < covered; at++) {
        unsigned head = 0, bytes = starts[at] ? read(s + at, (uint16_t)(covered - at), &r, &head) : 0;
        if (!bytes)
            continue;
        size_t word = (o->length - records) / WORD + 1;
        if (word > 0xFFFF)
            break;   /* past what the index reaches: the rest run from their bytes */
        o->bytes[block + 8 + 2u * at] = (uint8_t)word;
        o->bytes[block + 8 + 2u * at + 1] = (uint8_t)(word >> 8);
        /* the values as the target reads them: little-endian words */
        ok = put(o, r.room, head);
        const uint32_t *value = (const uint32_t *)(const void *)(r.room + head);
        for (unsigned v = 0; ok && v < (bytes - head) / 4; v++) {
            uint8_t le[4] = { (uint8_t)value[v], (uint8_t)(value[v] >> 8), (uint8_t)(value[v] >> 16),
                              (uint8_t)(value[v] >> 24) };
            ok = put(o, le, sizeof le);
        }
    }
    return ok;
}

/* ---- The locations' event scripts ------------------------------------------------------------- */

/* The places a script's commands start, as far as its bytes tell: marked in
 * `starts`, from the code's start, each function's and each jump's. */
static void find_commands(const uint8_t *s, uint16_t covered, uint16_t code, const uint8_t *objects_functions,
                          unsigned functions, uint8_t *starts)
{
    uint16_t *pending = malloc(sizeof *pending * ((size_t)covered + functions + 1));
    if (!pending)
        return;
    unsigned waiting = 0;
    pending[waiting++] = code;
    for (unsigned f = 0; f < functions; f++) {
        uint16_t to = (uint16_t)(objects_functions[2 * f] | objects_functions[2 * f + 1] << 8);
        if (to >= code && to < covered)
            pending[waiting++] = to;
    }
    while (waiting) {
        uint16_t at = pending[--waiting];
        while (at < covered && !starts[at]) {
            const EventOpcode *row = event_opcode(s[at]);
            uint16_t length = row ? event_command_length(row, s + at, (uint16_t)(covered - at)) : 0;
            if (!length)
                break;
            starts[at] = 1;
            if (row->jump && row->jump < length) {
                uint16_t from = (uint16_t)(at + row->jump), distance = s[from];
                uint16_t to = row->back ? (uint16_t)(from - distance) : (uint16_t)(from + distance);
                if (to >= code && to < covered && !starts[to] && waiting < (unsigned)covered + functions)
                    pending[waiting++] = to;
            }
            at = (uint16_t)(at + length);
        }
    }
    free(pending);
}

static bool put_event_script(Out *o, const uint8_t *u, size_t unpacked)
{
    if (unpacked < 2)
        return true;
    const uint8_t *s = u + 1;
    size_t length = unpacked - 1;
    uint16_t covered = (uint16_t)(length < BUILT_IN_COMMANDS ? length : BUILT_IN_COMMANDS);
    unsigned functions = u[0] * FUNCTIONS;
    uint16_t code = (uint16_t)(2 * functions);
    if (code >= covered)
        return true;
    uint8_t *starts = calloc(covered, 1);
    if (!starts)
        return false;
    find_commands(s, covered, code, s, functions, starts);
    bool ok = put_records(o, event_records_language(), s, covered, starts, read_event_record);
    free(starts);
    return ok;
}

/* ---- The menus' object scripts ---------------------------------------------------------------- */

/* The places an object script's commands start: one after another from its
 * start, and past bytes no command could be read from, the next place one
 * could (frontend/commands.c's walk). */
static bool put_object_script(Out *o, const uint8_t *s, size_t length)
{
    uint16_t covered = (uint16_t)(length < BANK_BYTES ? length : BANK_BYTES - 1);
    if (!covered)
        return true;
    uint8_t *starts = calloc(covered, 1);
    if (!starts)
        return false;
    RecordRoom r;
    for (uint16_t at = 0; at < covered;) {
        if (menu_record_read(s + at, (uint16_t)(covered - at), &r.menu)) {
            starts[at] = 1;
            at = (uint16_t)(at + r.menu.bytes);
        } else {
            at++;
        }
    }
    bool ok = put_records(o, menu_records_language(), s, covered, starts, read_menu_record);
    free(starts);
    return ok;
}

/* ---- The file ------------------------------------------------------------------------------- */

typedef enum { NO_SCRIPT, EVENT_SCRIPT, OBJECT_SCRIPT, PACKED_OBJECT_SCRIPTS } ScriptKind;

static ScriptKind script_kind(AssetId id)
{
    const char *codec = asset_codec_names[asset_catalogue[id].codec];
    if (!strcmp(codec, "lz:event"))
        return EVENT_SCRIPT;
    if (!strcmp(codec, "script:object"))
        return OBJECT_SCRIPT;
    if (!strcmp(codec, "lz:object"))
        return PACKED_OBJECT_SCRIPTS;
    return NO_SCRIPT;
}

static bool put_script(Out *o, const AssetSource *source, AssetId id, ScriptKind kind)
{
    uint32_t packed_length = 0;
    const uint8_t *bytes = source->resource(source->ctx, id, &packed_length);
    if (!bytes)
        return true;
    if (kind == OBJECT_SCRIPT)
        return put_object_script(o, bytes, packed_length);
    size_t unpacked = 0;
    bool long_runs = false;
    uint8_t *u = packed_unpack(bytes, packed_length, &unpacked, &long_runs);
    if (!u)
        return true;
    bool ok = kind == EVENT_SCRIPT ? put_event_script(o, u, unpacked) : put_object_script(o, u, unpacked);
    free(u);
    return ok;
}

int script_records_write(const AssetSource *source, const char *path)
{
    Out o = { NULL, 0, 0 };
    uint32_t count = asset_catalogue_count;
    size_t table = 12;
    bool ok = put(&o, "CTSR", 4) && put(&o, NULL, 8) && put(&o, NULL, 8u * count);
    if (ok) {
        put32_at(&o, 4, asset_catalogue_version);
        put32_at(&o, 8, count);
    }
    unsigned scripts[PACKED_OBJECT_SCRIPTS + 1] = { 0 };
    for (AssetId id = 0; ok && id < count; id++) {
        ScriptKind kind = script_kind(id);
        if (kind == NO_SCRIPT)
            continue;
        ok = put(&o, NULL, -o.length % WORD);
        size_t at = o.length;
        ok = ok && put_script(&o, source, id, kind);
        if (ok && o.length > at) {
            put32_at(&o, table + 8u * id, (uint32_t)at);
            put32_at(&o, table + 8u * id + 4, (uint32_t)(o.length - at));
            scripts[kind]++;
        }
    }
    if (ok)
        ok = file_store_replace(path, o.bytes, o.length) == 0;
    if (ok)
        printf("script records: %u event scripts (language %08X), %u object scripts (language %08X), %zu bytes\n",
               scripts[EVENT_SCRIPT], (unsigned)event_records_language(),
               scripts[OBJECT_SCRIPT] + scripts[PACKED_OBJECT_SCRIPTS], (unsigned)menu_records_language(), o.length);
    else
        fprintf(stderr, "ct_native: can't write the script records to %s\n", path);
    free(o.bytes);
    return ok ? 0 : 1;
}
