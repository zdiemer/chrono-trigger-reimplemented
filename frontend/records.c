#include "records.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a record's bytes are, by the table's codec: a field sprite's five
 * (game/field/field_data.h, FieldSpriteRecord), an enemy's ten (the same
 * five, the fifth its flags, then its header's: game/battle/setup_data.h). */
static const char *const SPRITE_FIELDS = "graphics assembly palette animations layout";
static const char *const ENEMY_FIELDS =
    "graphics assembly palette animations flags cursor-x cursor-y half-width half-height motion";

/* The locations' exits: a word a location and one past the last, where its
 * exits start in the table, then the exits, seven bytes each
 * (game/field/field_data.h, FieldExit). */
enum { LOCATIONS = 0x200, EXITS_AT = 2 * (LOCATIONS + 1), EXIT_BYTES = 7 };

typedef enum { SPRITE_RECORDS, LOCATION_EXITS } Table;

static bool table_of(const AssetEntry *e, Table *t, unsigned *record)
{
    const char *c = asset_codec_names[e->codec];
    if (!strcmp(c, "struct:location-exits")) {
        *t = LOCATION_EXITS;
        return true;
    }
    if (strncmp(c, "struct:sprite-records:", 22) != 0)
        return false;
    *t = SPRITE_RECORDS;
    *record = (unsigned)strtoul(c + 22, NULL, 10);
    return *record == 5 || *record == 10;
}

static bool decodes(const AssetEntry *e, bool *named)
{
    Table t;
    unsigned record;
    *named = true;
    return table_of(e, &t, &record);
}

/* ---- Writing ------------------------------------------------------------------------------------- */

static void put_bytes(DecodedText *t, const uint8_t *p, unsigned n)
{
    char hex[4];
    for (unsigned k = 0; k < n; k++) {
        snprintf(hex, sizeof hex, k + 1 < n ? "%02X " : "%02X\n", p[k]);
        decoded_put(t, hex);
    }
}

static uint16_t word_at(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void put_exit(DecodedText *t, const uint8_t *p)
{
    char line[48];
    snprintf(line, sizeof line, "%02X %02X %02X %04X %02X %02X\n", p[0], p[1], p[2], word_at(p + 3), p[5], p[6]);
    decoded_put(t, line);
}

/* The exits, where the table's words run on from its end in whole exits
 * and the rest of the slot is clear: false where they don't. */
static bool write_exits(DecodedText *t, const uint8_t *bytes, uint32_t len)
{
    if (len < EXITS_AT || word_at(bytes) != EXITS_AT)
        return false;
    uint32_t end = word_at(bytes + 2 * LOCATIONS);
    for (unsigned l = 0; l < LOCATIONS; l++) {
        uint16_t first = word_at(bytes + 2 * l), next = word_at(bytes + 2 * l + 2);
        if (next < first || (next - EXITS_AT) % EXIT_BYTES || next > end)
            return false;
    }
    if (end > len)
        return false;
    for (uint32_t at = end; at < len; at++)
        if (bytes[at])
            return false;
    for (unsigned l = 0; l < LOCATIONS; l++) {
        uint16_t first = word_at(bytes + 2 * l), next = word_at(bytes + 2 * l + 2);
        if (first == next)
            continue;
        char line[32];
        snprintf(line, sizeof line, "location %03X\n", l);
        decoded_put(t, line);
        for (uint32_t at = first; at < next; at += EXIT_BYTES)
            put_exit(t, bytes + at);
    }
    return true;
}

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    (void)store;
    Table table = SPRITE_RECORDS;
    unsigned record = 1;
    table_of(e, &table, &record);
    if (table == LOCATION_EXITS) {
        decoded_put(t, "# each exit: its tile (x y), the tiles past it (reach: bit 7 down), the location\n"
                       "# it leads to (a word: the location in the low 9 bits), where the party stands there\n");
        return write_exits(t, bytes, e->length);
    }
    for (uint32_t n = 0; n < e->length / record; n++) {
        if (n % 16 == 0) {
            char line[160];
            snprintf(line, sizeof line, "# %03X: %s\n", (unsigned)n, record == 5 ? SPRITE_FIELDS : ENEMY_FIELDS);
            decoded_put(t, line);
        }
        put_bytes(t, bytes + n * record, record);
    }
    return e->length % record == 0;
}

/* ---- Reading ------------------------------------------------------------------------------------- */

/* Hex numbers of the widths given (2 a byte, 4 a word), a space between:
 * whether the line is just those. */
static bool hex_fields(const char *s, const char *end, const unsigned *digits, unsigned n, unsigned *v)
{
    for (unsigned k = 0; k < n; k++) {
        if (k && (s == end || *s++ != ' '))
            return false;
        v[k] = 0;
        for (unsigned d = 0; d < digits[k]; d++, s++) {
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

/* The table's words up to location `to`, each where the exits so far end. */
static void runs_to(DecodedReading *r, unsigned to)
{
    for (; r->part <= to; r->part++)
        set_word(r->bytes + 2 * r->part, EXITS_AT + r->items * EXIT_BYTES);
}

static const char *read_exits_line(DecodedReading *r, const char *s, const char *end)
{
    unsigned v[6];
    static const unsigned LOCATION_DIGITS[] = { 3 }, EXIT_DIGITS[] = { 2, 2, 2, 4, 2, 2 };
    if (decoded_word(&s, end, "location")) {
        while (s < end && *s == ' ')
            s++;
        if (!hex_fields(s, end, LOCATION_DIGITS, 1, v) || v[0] >= LOCATIONS)
            return "a location is `location NNN`, 000-1FF";
        if (r->lines && v[0] < r->part)
            return "the locations go in order, each once";
        r->lines = 1;
        runs_to(r, v[0]);
        return NULL;
    }
    if (!r->lines)
        return "an exit before any `location NNN`";
    if (!hex_fields(s, end, EXIT_DIGITS, 6, v))
        return "an exit is `x y reach location to_x to_y` in hex, the location four digits";
    uint32_t at = EXITS_AT + r->items * EXIT_BYTES;
    if (at + EXIT_BYTES > asset_catalogue[r->entry].length)
        return "more exits than the table has room for";
    uint8_t *p = r->bytes + at;
    p[0] = (uint8_t)v[0], p[1] = (uint8_t)v[1], p[2] = (uint8_t)v[2];
    set_word(p + 3, v[3]);
    p[5] = (uint8_t)v[4], p[6] = (uint8_t)v[5];
    r->items++;
    return NULL;
}

static const char *read_record_line(DecodedReading *r, unsigned record, const char *s, const char *end)
{
    static const unsigned BYTE_DIGITS[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
    unsigned v[10];
    if (!hex_fields(s, end, BYTE_DIGITS, record, v))
        return record == 5 ? "a record is its 5 bytes in hex" : "a record is its 10 bytes in hex";
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
    Table table = SPRITE_RECORDS;
    unsigned record = 1;
    table_of(&asset_catalogue[r->entry], &table, &record);
    return table == LOCATION_EXITS ? read_exits_line(r, s, end) : read_record_line(r, record, s, end);
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    (void)d;
    const AssetEntry *e = &asset_catalogue[r->entry];
    Table table = SPRITE_RECORDS;
    unsigned record = 1;
    table_of(e, &table, &record);
    if (table == LOCATION_EXITS) {
        runs_to(r, LOCATIONS);
        return NULL;
    }
    return r->items * record == e->length ? NULL : "fewer records than the table has";
}

const DecodedKind record_text = {
    "records",
    "# Chrono Trigger's tables of records, from your cartridge: not for sharing.\n"
    "# The sprites' records, a record a line in hex; the locations' exits, each\n"
    "# location's under `location NNN`. Edit and start the game again; delete the\n"
    "# file to write it afresh.\n",
    decodes, write_resource, read_line, read_end,
};
