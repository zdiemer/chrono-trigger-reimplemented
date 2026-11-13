#include "enemy_scripts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle/enemy_scripts.h"

enum { WORDS_A_LINE = 16, LINE_BYTES = 16, CONDITION_OPERANDS = SCRIPT_CONDITION_BYTES - 1, MOST_ACTION = 32 };

/* The actions' lengths, a byte each by action (enemy_action_length), as the
 * store has them. */
static const char *const LENGTHS = "enemy_action_length";

static const AssetEntry *lengths_entry(int *index)
{
    uint16_t hint = 0;
    *index = decoded_entry(LENGTHS, strlen(LENGTHS), &hint);
    return *index >= 0 ? &asset_catalogue[*index] : NULL;
}

/* An action's bytes, its own included: 0 where the tables don't say. The
 * draw among four and the choosing again are their byte alone, the table's
 * $FF (the actions after the draw are read as actions). */
enum { JUST_ITS_BYTE = 0xFF };

static unsigned action_length(const uint8_t *lengths, uint32_t n, uint8_t action)
{
    unsigned length = lengths && action < n && enemy_action_name(action) ? lengths[action] : 0;
    if (length == JUST_ITS_BYTE)
        return 1;
    return length >= 1 && length <= MOST_ACTION ? length : 0;
}

/* The first condition kind a name is, which reading takes without a hint. */
static int condition_kind(const char *name, size_t len)
{
    for (unsigned k = 0; k < CONDITION_KINDS; k++) {
        const char *n = enemy_condition_name((uint8_t)k);
        if (n && strlen(n) == len && !memcmp(n, name, len))
            return (int)k;
    }
    return -1;
}

static int action_kind(const char *name, size_t len)
{
    for (unsigned k = 0; k < ACTION_KINDS; k++) {
        const char *n = enemy_action_name((uint8_t)k);
        if (n && strlen(n) == len && !memcmp(n, name, len))
            return (int)k;
    }
    return -1;
}

/* ---- Writing ---------------------------------------------------------------------------------- */

static void put_hex(DecodedText *t, const uint8_t *p, unsigned n)
{
    char s[4];
    for (unsigned k = 0; k < n; k++) {
        snprintf(s, sizeof s, " %02X", p[k]);
        decoded_put(t, s);
    }
}

/* Where the scripts start: past the table of where each starts, which runs
 * up to the first script it names. 0 where the table isn't one. */
static uint32_t table_bytes(const AssetEntry *e, const uint8_t *b)
{
    uint32_t first = e->length;
    for (uint32_t at = 0; at + 1 < e->length && at < first; at += 2) {
        uint32_t to = (uint32_t)(b[at] | b[at + 1] << 8) - (e->at & 0xFFFF);
        if (to < first)
            first = to;
    }
    return first >= 2 && first <= e->length ? first & ~1u : 0;   /* whole words; a byte between is a script's */
}

typedef enum { IN_SECTION, IN_CONDITIONS, IN_ACTIONS } Reading;

bool enemy_scripts_write(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *b)
{
    int index;
    const AssetEntry *le = lengths_entry(&index);
    uint32_t n = 0;
    const uint8_t *lengths = le ? store->find(store->ctx, le->name, asset_type_names[le->type], &n) : NULL;
    uint32_t table = table_bytes(e, b);
    char s[96];
    decoded_put(t, "# where each enemy's script starts, then the scripts\n");
    for (uint32_t at = 0; at < table; at += 2) {
        decoded_put(t, at % (2 * WORDS_A_LINE) ? " " : at ? "\nstarts " : "starts ");
        snprintf(s, sizeof s, "%04X", (unsigned)(b[at] | b[at + 1] << 8));
        decoded_put(t, s);
    }
    if (table)
        decoded_put(t, "\n");
    Reading state = IN_SECTION;
    for (uint32_t at = table; at < e->length;) {
        /* the enemies whose scripts start here */
        bool named = false;
        for (uint32_t k = 0; k < table; k += 2) {
            if ((uint32_t)(b[k] | b[k + 1] << 8) - (e->at & 0xFFFF) != at)
                continue;
            snprintf(s, sizeof s, named ? ", %02X" : "# enemy %02X", (unsigned)(k / 2));
            decoded_put(t, s);
            named = true;
        }
        if (named)
            decoded_put(t, "\n");
        uint8_t byte = b[at];
        if (state != IN_ACTIONS && byte == SCRIPT_SECTION_END) {
            decoded_put(t, "end\n");
            state = IN_SECTION;
            at++;
            continue;
        }
        if (state != IN_ACTIONS && byte == SCRIPT_BLOCK_END && state == IN_CONDITIONS) {
            decoded_put(t, "then\n");
            state = IN_ACTIONS;
            at++;
            continue;
        }
        if (state != IN_ACTIONS && byte < CONDITION_KINDS && enemy_condition_name(byte) &&
            at + SCRIPT_CONDITION_BYTES <= e->length) {
            const char *name = enemy_condition_name(byte);
            snprintf(s, sizeof s, "if %s", name);
            decoded_put(t, s);
            put_hex(t, b + at + 1, CONDITION_OPERANDS);
            if (condition_kind(name, strlen(name)) != byte) {
                snprintf(s, sizeof s, " @%02X", byte);
                decoded_put(t, s);
            }
            decoded_put(t, "\n");
            state = IN_CONDITIONS;
            at += SCRIPT_CONDITION_BYTES;
            continue;
        }
        unsigned length = state == IN_ACTIONS ? action_length(lengths, n, byte) : 0;
        if (state == IN_ACTIONS && byte == SCRIPT_BLOCK_END) {
            decoded_put(t, "done\n");
            state = IN_SECTION;
            at++;
            continue;
        }
        if (state == IN_ACTIONS && byte >= SCRIPT_ACTIONS) {
            state = IN_SECTION;   /* any byte from $80 ends the actions, as the battle reads them */
            continue;
        }
        if (length && at + length <= e->length) {
            snprintf(s, sizeof s, "do %s", enemy_action_name(byte));
            decoded_put(t, s);
            put_hex(t, b + at + 1, length - 1);
            decoded_put(t, "\n");
            at += length;
            continue;
        }
        decoded_put(t, "bytes");   /* a byte the battle wouldn't read as one of these here */
        put_hex(t, b + at, 1);
        decoded_put(t, "\n");
        at++;
    }
    return true;
}

/* ---- Reading ---------------------------------------------------------------------------------- */

static bool space(char c) { return c == ' ' || c == '\t'; }

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* Hex values, `digits` each, a space before each: how many into out, or -1. */
static int hex_values(const char *s, const char *end, unsigned digits, uint32_t *out, unsigned most)
{
    unsigned n = 0;
    while (s < end) {
        if (!space(*s))
            return -1;
        while (s < end && space(*s))
            s++;
        if (s == end)
            break;
        if (n == most || (unsigned)(end - s) < digits)
            return -1;
        uint32_t v = 0;
        for (unsigned d = 0; d < digits; d++, s++) {
            int x = hex_digit(*s);
            if (x < 0)
                return -1;
            v = v << 4 | (uint32_t)x;
        }
        out[n++] = v;
    }
    return (int)n;
}

static const char *put(DecodedReading *r, const uint8_t *bytes, unsigned n)
{
    if (r->at + n > asset_catalogue[r->entry].length)
        return "the scripts are longer than the cartridge's: they keep their length";
    memcpy(r->bytes + r->at, bytes, n);
    r->at += n;
    return NULL;
}

const char *enemy_scripts_read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    uint8_t out[MOST_ACTION + 1];
    uint32_t v[WORDS_A_LINE + 1];
    const char *w = s;
    while (s < end && !space(*s))
        s++;
    size_t word = (size_t)(s - w);
#define IS(text) (word == sizeof text - 1 && !memcmp(w, text, word))
    if (IS("starts") || IS("bytes")) {
        bool starts = IS("starts");
        int n = hex_values(s, end, starts ? 4 : 2, v, starts ? WORDS_A_LINE : LINE_BYTES);
        if (n <= 0)
            return starts ? "starts are words in hex, a space between" : "bytes are two hex digits each, a space between";
        unsigned k = 0;
        for (int i = 0; i < n; i++) {
            out[k++] = (uint8_t)v[i];
            if (starts)
                out[k++] = (uint8_t)(v[i] >> 8);
        }
        return put(r, out, k);
    }
    if (IS("then") || IS("done") || IS("end")) {
        if (s != end)
            return "nothing goes after `then`, `done` or `end`";
        out[0] = IS("end") ? SCRIPT_SECTION_END : SCRIPT_BLOCK_END;
        return put(r, out, 1);
    }
    if (!IS("if") && !IS("do"))
        return "a line is `starts`, `if`, `then`, `do`, `done`, `end` or `bytes`";
    bool condition = IS("if");
#undef IS
    while (s < end && space(*s))
        s++;
    const char *name = s;
    while (s < end && !space(*s))
        s++;
    size_t name_len = (size_t)(s - name);
    /* a hint at the end */
    const char *stop = end, *last = end;
    int hint = -1;
    while (last > s && !space(last[-1]))
        last--;
    if (last < end && *last == '@') {
        int hi = end - last == 3 ? hex_digit(last[1]) : -1, lo = end - last == 3 ? hex_digit(last[2]) : -1;
        if (hi < 0 || lo < 0)
            return "a kind is @ and two hex digits, last";
        hint = hi << 4 | lo;
        stop = last;
    }
    int n = hex_values(s, stop, 2, v, MOST_ACTION);
    if (n < 0)
        return "a condition's or an action's bytes are two hex digits each, a space between";
    if (condition) {
        int kind = condition_kind(name, name_len);
        const char *hinted = hint >= 0 ? enemy_condition_name((uint8_t)hint) : NULL;
        if (kind < 0 || (hint >= 0 && (!hinted || strlen(hinted) != name_len || memcmp(hinted, name, name_len))))
            return "not a condition the battle tests, or not that kind's";
        if (n != CONDITION_OPERANDS)
            return "a condition is its test and three bytes";
        out[0] = (uint8_t)(hint >= 0 ? hint : kind);
        for (int k = 0; k < n; k++)
            out[1 + k] = (uint8_t)v[k];
        return put(r, out, SCRIPT_CONDITION_BYTES);
    }
    if (hint >= 0)
        return "an action's name is its kind: it takes no @XX";
    int action = action_kind(name, name_len);
    int index;
    const AssetEntry *le = lengths_entry(&index);
    unsigned length = action >= 0 && le ? action_length(decoded_beneath(d, index), le->length, (uint8_t)action) : 0;
    if (action < 0)
        return "not an action the battle takes";
    if (!length || (unsigned)n != length - 1)
        return "an action has as many bytes as the battle's table of their lengths says";
    out[0] = (uint8_t)action;
    for (int k = 0; k < n; k++)
        out[1 + k] = (uint8_t)v[k];
    return put(r, out, length);
}

const char *enemy_scripts_read_end(DecodedReading *r)
{
    return r->at == asset_catalogue[r->entry].length
               ? NULL
               : "the scripts are shorter than the cartridge's: they keep their length, as the table of their starts "
                 "expects";
}
