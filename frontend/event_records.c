/* The locations' event scripts read a command at a time (frontend/event_records.h). */
#include "event_records.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event/records.h"
#include "event/script.h"
#include "file_store.h"
#include "packed.h"

/* An unpacked script: its objects' count, then each object's 16 functions'
 * offsets, then the code; the engine's offsets count from the byte after
 * the count (game/event/calls.c). The built-in commands follow the script
 * from offset $16FF (game/field/location.c), and change as the game runs:
 * no record reads them. */
enum { FUNCTIONS = 16, BUILT_IN_COMMANDS = 0x16FF, WORD = 4 };

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

/* One script's records, as game/event/records.h lays them out. */
static bool put_script(Out *o, const uint8_t *u, size_t unpacked)
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
    size_t block = o->length;
    uint32_t head[2] = { event_records_language(), covered };
    bool ok = put(o, NULL, sizeof head) && put(o, NULL, 2u * covered) && put(o, NULL, -(o->length - block) % WORD);
    if (ok) {
        put32_at(o, block, head[0]);
        put32_at(o, block + 4, head[1]);
    }
    size_t records = o->length;
    union {
        EventRecord record;
        uint8_t room[EVENT_RECORD_HEAD + 4 * EVENT_PARAMETER_COUNT];
    } r;
    for (uint16_t at = code; ok && at < covered; at++) {
        if (!starts[at])
            continue;
        unsigned bytes = event_record_read(s + at, (uint16_t)(covered - at), &r.record);
        if (!bytes)
            continue;
        size_t word = (o->length - records) / WORD + 1;
        if (word > 0xFFFF)
            break;   /* past what the index reaches: the rest run from their bytes */
        o->bytes[block + 8 + 2u * at] = (uint8_t)word;
        o->bytes[block + 8 + 2u * at + 1] = (uint8_t)(word >> 8);
        /* the values as the target reads them: little-endian words */
        ok = put(o, &r.record, EVENT_RECORD_HEAD);
        for (unsigned v = 0; ok && v < r.record.values; v++) {
            uint8_t le[4] = { (uint8_t)r.record.value[v], (uint8_t)(r.record.value[v] >> 8),
                              (uint8_t)(r.record.value[v] >> 16), (uint8_t)(r.record.value[v] >> 24) };
            ok = put(o, le, sizeof le);
        }
    }
    free(starts);
    return ok;
}

int event_records_write(const AssetSource *source, const char *path)
{
    Out o = { NULL, 0, 0 };
    uint32_t count = asset_catalogue_count;
    size_t table = 12;
    bool ok = put(&o, "CTER", 4) && put(&o, NULL, 8) && put(&o, NULL, 8u * count);
    if (ok) {
        put32_at(&o, 4, asset_catalogue_version);
        put32_at(&o, 8, count);
    }
    unsigned scripts = 0;
    for (AssetId id = 0; ok && id < count; id++) {
        if (strcmp(asset_codec_names[asset_catalogue[id].codec], "lz:event"))
            continue;
        uint32_t packed_length = 0;
        const uint8_t *packed = source->resource(source->ctx, id, &packed_length);
        size_t unpacked = 0;
        bool long_runs = false;
        uint8_t *u = packed ? packed_unpack(packed, packed_length, &unpacked, &long_runs) : NULL;
        if (!u)
            continue;
        ok = put(&o, NULL, -o.length % WORD);
        size_t at = o.length;
        ok = ok && put_script(&o, u, unpacked);
        free(u);
        if (ok && o.length > at) {
            put32_at(&o, table + 8u * id, (uint32_t)at);
            put32_at(&o, table + 8u * id + 4, (uint32_t)(o.length - at));
            scripts++;
        }
    }
    if (ok)
        ok = file_store_replace(path, o.bytes, o.length) == 0;
    if (ok)
        printf("event records: %u scripts, %zu bytes, language %08X\n", scripts, o.length,
               (unsigned)event_records_language());
    else
        fprintf(stderr, "ct_native: can't write the event records to %s\n", path);
    free(o.bytes);
    return ok ? 0 : 1;
}
