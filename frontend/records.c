#include "records.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- The tables ---------------------------------------------------------------------------------- */

/* A table of records of a few bytes each, a record a line: a field
 * sprite's five (game/field/field_data.h, FieldSpriteRecord), an enemy's ten
 * (the same five, the fifth its flags, then its header's:
 * game/battle/setup_data.h), or another's N, its fields unnamed. */
static const char *const SPRITE_FIELDS = "graphics assembly palette animations layout";
static const char *const ENEMY_FIELDS =
    "graphics assembly palette animations flags cursor-x cursor-y half-width half-height motion";

/* A table of each location's run of records: a word a location and one past
 * the last, where its run starts in the bank, then the runs, one after
 * another. A field is a byte or a word. */
enum { LOCATIONS = 0x200, RUNS_AT = 2 * (LOCATIONS + 1), MOST_FIELDS = 8 };

typedef struct {
    const char *codec, *comment;
    unsigned record, fields;
    uint8_t bytes[MOST_FIELDS];   /* each field's: 1 or 2 */
} Runs;

static const Runs RUNS[] = {
    /* the exits (game/field/field_data.h, FieldExit) */
    { "struct:location-exits",
      "# each exit: its tile (x y), the tiles past it (reach: bit 7 down), the location\n"
      "# it leads to (a word: the location in the low 9 bits), where the party stands there\n",
      7, 6, { 1, 1, 1, 2, 1, 1 } },
    /* the chests (game/event/chests.c, FieldChest) */
    { "struct:location-chests", "# each chest: its tile (a word), and what it holds (a word)\n", 4, 2, { 2, 2 } },
};

typedef struct {
    const Runs *runs;      /* or NULL: records */
    unsigned record;
    const char *fields;    /* the records' fields' names, or NULL */
} Table;

static bool table_of(const AssetEntry *e, Table *t)
{
    const char *c = asset_codec_names[e->codec];
    *t = (Table){ NULL, 0, NULL };
    for (size_t i = 0; i < sizeof RUNS / sizeof *RUNS; i++) {
        if (!strcmp(c, RUNS[i].codec)) {
            t->runs = &RUNS[i];
            t->record = RUNS[i].record;
            return true;
        }
    }
    if (!strcmp(c, "struct:sprite-records:5"))
        *t = (Table){ NULL, 5, SPRITE_FIELDS };
    else if (!strcmp(c, "struct:sprite-records:10"))
        *t = (Table){ NULL, 10, ENEMY_FIELDS };
    else if (!strncmp(c, "struct:records:", 15))
        t->record = (unsigned)strtoul(c + 15, NULL, 10);
    return t->record > 0 && t->record <= 64;
}

static bool decodes(const AssetEntry *e, bool *named)
{
    Table t;
    *named = true;
    return table_of(e, &t);
}

/* ---- Writing ------------------------------------------------------------------------------------- */

static uint16_t word_at(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void put_record(DecodedText *t, const Runs *r, const uint8_t *p)
{
    char hex[8];
    for (unsigned f = 0, at = 0; f < r->fields; at += r->bytes[f], f++) {
        snprintf(hex, sizeof hex, r->bytes[f] == 2 ? "%s%04X" : "%s%02X", f ? " " : "",
                 r->bytes[f] == 2 ? word_at(p + at) : p[at]);
        decoded_put(t, hex);
    }
    decoded_put(t, "\n");
}

/* A location's run, as the bank's words say where it starts: its place in
 * the resource, which starts `base` into the bank. */
static uint32_t run_start(const uint8_t *bytes, unsigned location, uint16_t base)
{
    return (uint16_t)(word_at(bytes + 2 * location) - base);
}

/* The runs, where the words run on from the table's end in whole records
 * and the rest of the slot is clear: false where they don't. */
static bool write_runs(DecodedText *t, const Runs *r, const uint8_t *bytes, uint32_t len, uint16_t base)
{
    if (len < RUNS_AT || run_start(bytes, 0, base) != RUNS_AT)
        return false;
    uint32_t end = run_start(bytes, LOCATIONS, base);
    for (unsigned l = 0; l < LOCATIONS; l++) {
        uint32_t first = run_start(bytes, l, base), next = run_start(bytes, l + 1, base);
        if (next < first || (next - RUNS_AT) % r->record || next > end)
            return false;
    }
    if (end > len)
        return false;
    for (uint32_t at = end; at < len; at++)
        if (bytes[at])
            return false;
    decoded_put(t, r->comment);
    for (unsigned l = 0; l < LOCATIONS; l++) {
        uint32_t first = run_start(bytes, l, base), next = run_start(bytes, l + 1, base);
        if (first == next)
            continue;
        char line[32];
        snprintf(line, sizeof line, "location %03X\n", l);
        decoded_put(t, line);
        for (uint32_t at = first; at < next; at += r->record)
            put_record(t, r, bytes + at);
    }
    return true;
}

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    (void)store;
    Table table;
    table_of(e, &table);
    if (table.runs)
        return write_runs(t, table.runs, bytes, e->length, (uint16_t)e->at);
    for (uint32_t n = 0; n < e->length / table.record; n++) {
        if (n % 16 == 0) {
            char line[160];
            snprintf(line, sizeof line, "# %03X%s%s\n", (unsigned)n, table.fields ? ": " : "", table.fields ? table.fields : "");
            decoded_put(t, line);
        }
        char hex[4];
        for (unsigned k = 0; k < table.record; k++) {
            snprintf(hex, sizeof hex, k + 1 < table.record ? "%02X " : "%02X\n", bytes[n * table.record + k]);
            decoded_put(t, hex);
        }
    }
    return e->length % table.record == 0;
}

/* ---- Reading ------------------------------------------------------------------------------------- */

/* Hex numbers of the widths given (2 digits a byte, 4 a word), a space
 * between: whether the line is just those. */
static bool hex_fields(const char *s, const char *end, const uint8_t *bytes, unsigned n, unsigned *v)
{
    for (unsigned k = 0; k < n; k++) {
        if (k && (s == end || *s++ != ' '))
            return false;
        v[k] = 0;
        for (unsigned d = 0; d < 2u * bytes[k]; d++, s++) {
            char c = s < end ? *s : 0;
            int x = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (x < 0)
                return false;
            v[k] = v[k] << 4 | (unsigned)x;
        }
    }
    return s == end;
}

static void set_word(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* The table's words up to location `to`, each where the runs so far end. */
static void runs_to(DecodedReading *r, const Runs *runs, unsigned to)
{
    uint16_t base = (uint16_t)asset_catalogue[r->entry].at;
    for (; r->part <= to; r->part++)
        set_word(r->bytes + 2 * r->part, (uint16_t)(base + RUNS_AT + r->items * runs->record));
}

static const char *read_runs_line(DecodedReading *r, const Runs *runs, const char *s, const char *end)
{
    unsigned v[MOST_FIELDS];
    if (decoded_word(&s, end, "location")) {
        if (!decoded_number(&s, end, 16, LOCATIONS - 1, &v[0]) || s != end)
            return "a location is `location NNN`, 000-1FF";
        if (r->lines && v[0] < r->part)
            return "the locations go in order, each once";
        r->lines = 1;
        runs_to(r, runs, v[0]);
        return NULL;
    }
    if (!r->lines)
        return "a record before any `location NNN`";
    if (!hex_fields(s, end, runs->bytes, runs->fields, v))
        return "a record is its fields in hex, two digits a byte and four a word, as the comment says";
    uint32_t at = RUNS_AT + r->items * runs->record;
    if (at + runs->record > asset_catalogue[r->entry].length)
        return "more records than the table has room for";
    uint8_t *p = r->bytes + at;
    for (unsigned f = 0; f < runs->fields; p += runs->bytes[f], f++) {
        if (runs->bytes[f] == 2)
            set_word(p, v[f]);
        else
            *p = (uint8_t)v[f];
    }
    r->items++;
    return NULL;
}

static const char *read_record_line(DecodedReading *r, unsigned record, const char *s, const char *end)
{
    uint8_t byte_fields[64];
    unsigned v[64];
    memset(byte_fields, 1, sizeof byte_fields);
    if (!hex_fields(s, end, byte_fields, record, v))
        return "a record is its bytes in hex, as many as the table's others";
    if ((r->items + 1) * record > asset_catalogue[r->entry].length)
        return "more records than the table has";
    for (unsigned k = 0; k < record; k++)
        r->bytes[r->items * record + k] = (uint8_t)v[k];
    r->items++;
    return NULL;
}

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    (void)d;
    Table table;
    table_of(&asset_catalogue[r->entry], &table);
    return table.runs ? read_runs_line(r, table.runs, s, end) : read_record_line(r, table.record, s, end);
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    (void)d;
    const AssetEntry *e = &asset_catalogue[r->entry];
    Table table;
    table_of(e, &table);
    if (table.runs) {
        runs_to(r, table.runs, LOCATIONS);
        return NULL;
    }
    return r->items * table.record == e->length ? NULL : "fewer records than the table has";
}

const DecodedKind record_text = {
    "records",
    "# Chrono Trigger's tables of records, from your cartridge: not for sharing.\n"
    "# A record a line in hex; the locations' exits and chests, each location's\n"
    "# under `location NNN`. Edit and start the game again; delete the file to\n"
    "# write it afresh.\n",
    decodes, write_resource, read_line, read_end,
};
