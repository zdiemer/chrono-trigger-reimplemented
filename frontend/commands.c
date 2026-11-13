#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enemy_scripts.h"
#include "languages.h"
#include "packed.h"

/* ---- The languages ---------------------------------------------------------------------------- */

/* A resource's language, and whether it is packed (a world map's scene's
 * object scripts, which the map's load unpacks into work RAM). */
static const Language *language_of(const AssetEntry *e)
{
    const char *codec = asset_codec_names[e->codec];
    if (!strcmp(codec, "script:battle-animation"))
        return &battle_animation_language;
    if (!strcmp(codec, "script:object") || !strcmp(codec, "lz:object"))
        return &menu_object_language;
    return NULL;
}

static bool packed(const AssetEntry *e) { return !strcmp(asset_codec_names[e->codec], "lz:object"); }

/* The enemies' scripts, which aren't a table's language (frontend/enemy_scripts.h). */
static bool enemies(const AssetEntry *e) { return !strcmp(asset_codec_names[e->codec], "script:enemy-ai"); }

static bool decodes(const AssetEntry *e, bool *named)
{
    *named = true;
    return language_of(e) != NULL || enemies(e);
}

void language_order_params(LanguageRow *row)
{
    /* a stable insertion sort: fixed first, then by operand */
    for (unsigned i = 1; i < row->params; i++) {
        LanguageParam p = row->param[i];
        unsigned key = p.fixed ? 0 : 1u + p.operand, k = i;
        for (; k > 0; k--) {
            const LanguageParam *q = &row->param[k - 1];
            if ((q->fixed ? 0 : 1u + q->operand) <= key)
                break;
            row->param[k] = row->param[k - 1];
        }
        row->param[k] = p;
    }
}

/* A hash of every row of both languages: what the text is written in, which
 * changes when a table does. */
static uint32_t language(void)
{
    static const Language *const languages[] = { &battle_animation_language, &menu_object_language };
    uint32_t h = 0x811C9DC5u;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 0x01000193u)
    for (unsigned l = 0; l < sizeof languages / sizeof *languages; l++) {
        for (unsigned op = 0; op < 256; op++) {
            const LanguageRow *row = languages[l]->row((uint8_t)op);
            MIX(row->known);
            for (const char *c = row->known ? row->name : ""; *c; c++)
                MIX(*c);
            MIX(row->length), MIX(row->measure != NULL), MIX(row->params);
            for (unsigned i = 0; i < row->params; i++) {
                const LanguageParam *p = &row->param[i];
                for (const char *c = p->name; *c; c++)
                    MIX(*c);
                for (const char *c = p->prefix; *c; c++)
                    MIX(*c);
                MIX(p->fixed), MIX(p->value), MIX(p->operand), MIX(p->bytes), MIX(p->bank_at);
            }
        }
    }
#undef MIX
    return h;
}

/* ---- A command ---------------------------------------------------------------------------------- */

enum { MOST_COMMAND = 256, LINE_BYTES = 16 };

/* The bytes the command at c takes, `available` there: its row's, or what
 * its bytes measure; 0 where that can't be told or runs past them. */
static uint16_t command_length(const LanguageRow *row, const uint8_t *c, uint32_t available)
{
    uint16_t room = available > MOST_COMMAND ? MOST_COMMAND : (uint16_t)available;
    uint16_t n = row->measure ? row->measure(c, room) : row->length;
    return n >= 1 && n >= row->length && n <= room ? n : 0;
}

static bool param_inside(const LanguageParam *p, uint16_t length)
{
    return p->fixed || ((unsigned)p->operand + p->bytes <= length && (!p->bank_at || p->bank_at < length));
}

/* Whether a parameter inside the command reads its byte k. */
static bool covered(const LanguageRow *row, unsigned k, uint16_t length)
{
    for (unsigned i = 0; i < row->params; i++) {
        const LanguageParam *p = &row->param[i];
        if (!p->fixed && param_inside(p, length) && ((k >= p->operand && k < (unsigned)p->operand + p->bytes) ||
                                                     (p->bank_at && k == p->bank_at)))
            return true;
    }
    return false;
}

static unsigned value_digits(const LanguageParam *p) { return 2u * (p->bytes + (p->bank_at ? 1u : 0u)); }

static uint32_t param_value(const LanguageParam *p, const uint8_t *c)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < p->bytes; i++)
        v |= (uint32_t)c[p->operand + i] << (8 * i);
    if (p->bank_at)
        v |= (uint32_t)c[p->bank_at] << (8 * p->bytes);
    return v;
}

static void put_param(uint8_t *c, const LanguageParam *p, uint32_t v)
{
    for (unsigned i = 0; i < p->bytes; i++)
        c[p->operand + i] = (uint8_t)(v >> (8 * i));
    if (p->bank_at)
        c[p->bank_at] = (uint8_t)(v >> (8 * p->bytes));
}

/* ---- A command's line, read ------------------------------------------------------------------ */

static bool space(char c) { return c == ' ' || c == '\t'; }

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* A value in hex from s to end, at most 8 digits (exactly `digits` where it
 * isn't 0). */
static bool hex_value(const char *s, const char *end, unsigned digits, uint32_t *v)
{
    if (s == end || end - s > 8 || (digits && (unsigned)(end - s) != digits))
        return false;
    *v = 0;
    for (; s < end; s++) {
        int d = hex_digit(*s);
        if (d < 0)
            return false;
        *v = *v << 4 | (uint32_t)d;
    }
    return true;
}

enum { MOST_WORDS = 24 };

typedef struct {
    const char *key, *key_end, *value, *end;
} Pair;

typedef struct {
    const char *name, *name_end;
    Pair pair[MOST_WORDS];
    unsigned pairs;
    int hint;   /* the @XX opcode, or -1 */
} Line;

static const char *split_line(const char *s, const char *end, Line *l)
{
    *l = (Line){ .hint = -1 };
    l->name = s;
    while (s < end && !space(*s))
        s++;
    l->name_end = s;
    while (s < end) {
        while (s < end && space(*s))
            s++;
        if (s == end)
            break;
        const char *w = s;
        while (s < end && !space(*s))
            s++;
        uint32_t v;
        if (*w == '@') {
            if (l->hint >= 0 || !hex_value(w + 1, s, 2, &v))
                return "an opcode is @ and two hex digits, once";
            l->hint = (int)v;
            continue;
        }
        const char *eq = memchr(w, '=', (size_t)(s - w));
        if (!eq || eq == w || l->pairs == MOST_WORDS)
            return "a command's parameters are name=value";
        l->pair[l->pairs++] = (Pair){ w, eq, eq + 1, s };
    }
    return NULL;
}

static bool word_is(const char *s, const char *end, const char *word)
{
    return (size_t)(end - s) == strlen(word) && !memcmp(s, word, (size_t)(end - s));
}

/* Bytes in hex joined by `between`, into out (room for `most`): how many, or -1. */
static int hex_run(const char *s, const char *end, char between, uint8_t *out, unsigned most)
{
    unsigned n = 0;
    while (s < end) {
        uint32_t v;
        if (n == most || end - s < 2 || !hex_value(s, s + 2, 2, &v))
            return -1;
        out[n++] = (uint8_t)v;
        s += 2;
        if (s < end && *s++ != between)
            return -1;
    }
    return (int)n;
}

/* The command a row makes of a line, into c (room for MOST_COMMAND): its
 * length, or 0 where the line isn't one of the row's. */
static uint16_t encode(const LanguageRow *row, const Line *l, uint8_t *c)
{
    uint8_t data[MOST_COMMAND];
    int data_n = 0;
    bool given[LANGUAGE_MOST_PARAMS] = { false };
    unsigned built = row->length;
    memset(c, 0, MOST_COMMAND);
    c[0] = row->opcode;
    for (unsigned i = 0; i < l->pairs; i++) {
        const Pair *v = &l->pair[i];
        unsigned k = 0;
        for (; k < row->params && !word_is(v->key, v->key_end, row->param[k].name); k++)
            ;
        if (k < row->params) {
            const LanguageParam *p = &row->param[k];
            size_t prefix = strlen(p->prefix);
            uint32_t value;
            if (given[k] || (size_t)(v->end - v->value) < prefix || memcmp(v->value, p->prefix, prefix) ||
                !hex_value(v->value + prefix, v->end, p->fixed ? 0 : value_digits(p), &value))
                return 0;
            if (p->fixed ? value != p->value : false)
                return 0;
            if (!p->fixed) {
                put_param(c, p, value);
                unsigned last = p->bank_at > (unsigned)p->operand + p->bytes - 1 ? p->bank_at : (unsigned)p->operand + p->bytes - 1;
                built = last + 1 > built ? last + 1 : built;
            }
            given[k] = true;
        } else if (v->key_end - v->key > 4 && !memcmp(v->key, "byte", 4)) {
            unsigned at = 0;
            const char *n = v->key + 4;
            for (; n < v->key_end && *n >= '0' && *n <= '9' && at < MOST_COMMAND; n++)
                at = at * 10 + (unsigned)(*n - '0');
            uint32_t bits;
            if (n != v->key_end || at == 0 || at >= row->length || !hex_value(v->value, v->end, 2, &bits))
                return 0;
            c[at] = (uint8_t)bits;
        } else if (word_is(v->key, v->key_end, "data") && !data_n) {
            data_n = hex_run(v->value, v->end, '.', data, sizeof data);
            if (data_n <= 0)
                return 0;
        } else {
            return 0;
        }
    }
    /* a measured command's data in the bytes past its row's that no parameter reads */
    unsigned length = built;
    if (data_n) {
        unsigned put = 0;
        for (unsigned k = row->length; k < MOST_COMMAND && put < (unsigned)data_n; k++) {
            if (covered(row, k, MOST_COMMAND))
                continue;
            c[k] = data[put++];
            length = k + 1 > length ? k + 1 : length;
        }
        if (put < (unsigned)data_n)
            return 0;
    }
    /* a measure reads on past the command where its bytes say so (a screen
     * effect giving no settings reads the next byte's count): zeros there */
    uint16_t n = command_length(row, c, row->measure ? MOST_COMMAND : length);
    if (n == 0 || n != length)
        return 0;
    /* every parameter inside the command given, none outside it */
    for (unsigned k = 0; k < row->params; k++)
        if (!row->param[k].fixed && param_inside(&row->param[k], n) != given[k])
            return 0;
    return n;
}

/* The row a line names: the hinted opcode, or the first of its name's rows
 * its parameters fit; and the command made of it. */
static const char *read_command(const Language *language, const Line *l, uint8_t *c, uint16_t *length,
                                const LanguageRow **found)
{
    bool named = false;
    for (unsigned op = 0; op < 256; op++) {
        const LanguageRow *row = language->row((uint8_t)op);
        if (!row->known || !word_is(l->name, l->name_end, row->name))
            continue;
        named = true;
        if (l->hint >= 0 && l->hint != (int)op)
            continue;
        if ((*length = encode(row, l, c)) != 0) {
            *found = row;
            return NULL;
        }
    }
    if (!named)
        return "not a command of the language, or bytes";
    return l->hint >= 0 ? "that opcode isn't one of the command's, or its parameters aren't the opcode's"
                        : "the command's parameters aren't any of its forms'";
}

/* ---- Writing ---------------------------------------------------------------------------------- */

static void put_hex(DecodedText *t, const uint8_t *p, unsigned n, const char *between)
{
    char s[4];
    for (unsigned k = 0; k < n; k++) {
        snprintf(s, sizeof s, "%02X", p[k]);
        if (k)
            decoded_put(t, between);
        decoded_put(t, s);
    }
}

/* The opcode a command's line is read as, without its @XX; -1 for none. */
static int read_as(const Language *language, const char *text, size_t len)
{
    uint8_t c[MOST_COMMAND];
    Line l;
    uint16_t length;
    const LanguageRow *row = NULL;
    if (split_line(text, text + len, &l) || read_command(language, &l, c, &length, &row))
        return -1;
    return row->opcode;
}

static void write_command(DecodedText *out, const Language *language, const LanguageRow *row, const uint8_t *c,
                          uint16_t length)
{
    char s[48];
    DecodedText line = { 0 }, *t = &line;
    decoded_put(t, row->name);
    for (unsigned i = 0; i < row->params; i++) {
        const LanguageParam *p = &row->param[i];
        if (!param_inside(p, length))
            continue;
        if (p->fixed)
            snprintf(s, sizeof s, " %s=%s%02X", p->name, p->prefix, (unsigned)p->value);
        else
            snprintf(s, sizeof s, " %s=%s%0*X", p->name, p->prefix, (int)value_digits(p), (unsigned)param_value(p, c));
        decoded_put(t, s);
    }
    for (unsigned k = 1; k < row->length && k < length; k++) {
        if (!covered(row, k, length)) {
            snprintf(s, sizeof s, " byte%u=%02X", k, c[k]);
            decoded_put(t, s);
        }
    }
    bool data = false;
    for (unsigned k = row->length; k < length; k++) {
        if (covered(row, k, length))
            continue;
        decoded_put(t, data ? "." : " data=");
        put_hex(t, c + k, 1, "");
        data = true;
    }
    if (!line.full && read_as(language, line.text, line.len) != row->opcode) {
        snprintf(s, sizeof s, " @%02X", row->opcode);
        decoded_put(t, s);
    }
    decoded_put(t, "\n");
    decoded_put(t, "x");   /* room for a NUL */
    if (line.full) {
        out->full = true;
    } else {
        line.text[line.len - 1] = 0;
        decoded_put(out, line.text);
    }
    free(line.text);
}

static uint16_t starts_a_command(const Language *language, const uint8_t *b, uint32_t at, uint32_t len)
{
    const LanguageRow *row = language->row(b[at]);
    return row->known ? command_length(row, b + at, len - at) : 0;
}

static void write_commands(DecodedText *t, const Language *language, const uint8_t *bytes, uint32_t len)
{
    for (uint32_t at = 0; at < len;) {
        uint16_t length = starts_a_command(language, bytes, at, len);
        if (length) {
            write_command(t, language, language->row(bytes[at]), bytes + at, length);
            at += length;
            continue;
        }
        uint32_t n = 1;
        while (at + n < len && n < LINE_BYTES && !starts_a_command(language, bytes, at + n, len))
            n++;
        decoded_put(t, "bytes ");
        put_hex(t, bytes + at, n, " ");
        decoded_put(t, "\n");
        at += n;
    }
}

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    if (enemies(e))
        return enemy_scripts_write(t, store, e, bytes);
    const Language *language = language_of(e);
    char s[64];
    if (!packed(e)) {
        snprintf(s, sizeof s, "# %s script, %u bytes\n", language->what, (unsigned)e->length);
        decoded_put(t, s);
        write_commands(t, language, bytes, e->length);
        return true;
    }
    size_t len = 0;
    bool long_runs = false;
    uint8_t *unpacked = packed_unpack(bytes, e->length, &len, &long_runs);
    if (!unpacked)
        return false;
    snprintf(s, sizeof s, "# %s scripts, %u bytes unpacked\n", language->what, (unsigned)len);
    decoded_put(t, s);
    packed_write_form(t, long_runs);
    write_commands(t, language, unpacked, (uint32_t)len);
    free(unpacked);
    return true;
}

/* ---- Reading ---------------------------------------------------------------------------------- */

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    const AssetEntry *e = &asset_catalogue[r->entry];
    uint8_t c[MOST_COMMAND];
    int n;
    if (enemies(e))
        return enemy_scripts_read_line(d, r, s, end);
    if (packed(e) && r->form == PACKED_UNSAID)
        return packed_read_form(d, r, s, end);
    if (decoded_word(&s, end, "bytes")) {
        while (s < end && space(*s))
            s++;
        n = hex_run(s, end, ' ', c, sizeof c);
        if (n <= 0)
            return "bytes are two hex digits each, a space between";
    } else {
        Line l;
        uint16_t length;
        const LanguageRow *row;
        const char *wrong = split_line(s, end, &l);
        if (!wrong)
            wrong = read_command(language_of(e), &l, c, &length, &row);
        if (wrong)
            return wrong;
        n = length;
    }
    if (packed(e)) {
        if (r->made_len + (uint32_t)n > r->most)
            return "the scripts are longer than the cartridge's: they keep their length";
        uint8_t *p = decoded_grow(r, (uint32_t)n);
        if (!p)
            return "out of memory";
        memcpy(p, c, (size_t)n);
        return NULL;
    }
    if (r->at + (uint32_t)n > e->length)
        return "the script is longer than the cartridge's: it keeps its length";
    memcpy(r->bytes + r->at, c, (size_t)n);
    r->at += (uint32_t)n;
    return NULL;
}

/* A script keeps its length: other tables point into it, and the scene's
 * scripts name places in themselves by their address. */
static const char *read_end(Decoded *d, DecodedReading *r)
{
    const AssetEntry *e = &asset_catalogue[r->entry];
    if (enemies(e))
        return enemy_scripts_read_end(r);
    if (!packed(e))
        return r->at == e->length ? NULL
                                  : "the script is shorter than the cartridge's: it keeps its length, as the tables "
                                    "pointing into it expect";
    if (r->form == PACKED_UNSAID)
        return "a packed resource starts `packed short` or `packed long`";
    if (r->made_len != r->most)
        return "the scripts are shorter than the cartridge's: they keep their length, as their jumps expect";
    return packed_end(d, r);
}

const DecodedKind command_text = {
    "commands",
    "# Chrono Trigger's battle animation scripts, the menus' and the world maps'\n"
    "# object scripts and the enemies' scripts, from your cartridge: not for sharing.\n"
    "# A command a line, its name and its parameters (name=value, in hex) as the\n"
    "# game's tables decode them; bytes no command reads are `bytes`; @XX picks the\n"
    "# opcode where another of the same name would read the same. The enemies'\n"
    "# scripts: `if` a condition, `then`, `do` an action, `done`, `end` a section.\n"
    "# A script keeps its length: other tables point into it. Edit and start the\n"
    "# game again; delete the file to write it afresh.\n",
    decodes, write_resource, read_line, read_end, NULL, language,
};
