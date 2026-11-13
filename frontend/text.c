#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Glyphs ------------------------------------------------------------------------------------ */

/* The font's glyphs from $A0: letters, digits and signs. The small font the
 * names are set in has other glyphs where the dialog's has a note, a heart,
 * an ellipsis and infinity; those, the hash sign (the text's comments start
 * at one) and the blanks after are written in hex. */
enum { FIRST_GLYPH = 0xA0, SPACE = 0xEF, NOTE = 0xEE, HEART = 0xF0, INFINITY_SIGN = 0xF2 };

static const char *const SIGNS[] = {
    "!", "?", "/", "\xE2\x80\x9C", "\xE2\x80\x9D", ":", "&", "(", ")", "'", ".", ",", "=", "-", "+", "%",
    "\xE2\x99\xAA", " ", "\xE2\x99\xA5", "\xE2\x80\xA6", "\xE2\x88\x9E",
};
enum { FIRST_SIGN = 0xDE };

/* A glyph's spelling, or NULL where it is written in hex. */
static const char *glyph_text(uint8_t code, bool names, char letter[2])
{
    if (code >= FIRST_GLYPH && code < FIRST_GLYPH + 26)
        letter[0] = (char)('A' + code - FIRST_GLYPH);
    else if (code >= FIRST_GLYPH + 26 && code < FIRST_GLYPH + 52)
        letter[0] = (char)('a' + code - FIRST_GLYPH - 26);
    else if (code >= FIRST_GLYPH + 52 && code < FIRST_SIGN)
        letter[0] = (char)('0' + code - FIRST_GLYPH - 52);
    else if (code >= FIRST_SIGN && code <= INFINITY_SIGN && !(names && (code == NOTE || code >= HEART)))
        return SIGNS[code - FIRST_SIGN];
    else
        return NULL;
    letter[1] = 0;
    return letter;
}

/* The glyph spelt at s: its code and length, or false. */
static bool glyph_at(const char *s, const char *end, bool names, uint8_t *code, size_t *n)
{
    char c0 = *s;
    if ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || (c0 >= '0' && c0 <= '9')) {
        *code = (uint8_t)(c0 <= '9' ? FIRST_GLYPH + 52 + c0 - '0'
                          : c0 <= 'Z' ? FIRST_GLYPH + c0 - 'A' : FIRST_GLYPH + 26 + c0 - 'a');
        *n = 1;
        return true;
    }
    for (unsigned c = FIRST_SIGN; c <= INFINITY_SIGN; c++) {
        char letter[2];
        const char *g = glyph_text((uint8_t)c, names, letter);
        size_t len = g ? strlen(g) : 0;
        if (len && (size_t)(end - s) >= len && !memcmp(s, g, len)) {
            *code = (uint8_t)c;
            *n = len;
            return true;
        }
    }
    return false;
}

/* ---- The dictionary ------------------------------------------------------------------------------ */

/* 128 words, codes $21-$A0: a pointer each into the bank, then the words,
 * each its count of glyphs and the glyphs. */
enum { WORDS = 128, FIRST_WORD = 0x21, DICTIONARY_AT = 0xFA00, POINTER_BYTES = 2 * WORDS };

typedef struct {
    const uint8_t *glyphs[WORDS];
    uint8_t length[WORDS];
} Dictionary;

/* The catalogue's row of the dictionary, found once: the catalogue doesn't change. */
static const AssetEntry *dictionary_entry(int *index)
{
    static int found = -2;
    for (uint16_t i = 0; found == -2 && i < asset_catalogue_count; i++)
        if (!strcmp(asset_codec_names[asset_catalogue[i].codec], "text:dialog-dictionary"))
            found = i;
    if (found == -2)
        found = -1;
    *index = found;
    return found >= 0 ? &asset_catalogue[found] : NULL;
}

/* The words of a dictionary's bytes: false where a pointer leads outside them. */
static bool words_of(const uint8_t *bytes, uint32_t len, Dictionary *d)
{
    if (!bytes || len < POINTER_BYTES)
        return false;
    for (unsigned w = 0; w < WORDS; w++) {
        uint32_t at = (uint32_t)(bytes[2 * w] | bytes[2 * w + 1] << 8) - DICTIONARY_AT;
        if (at < POINTER_BYTES || at >= len || at + 1 + bytes[at] > len)
            return false;
        d->length[w] = bytes[at];
        d->glyphs[w] = bytes + at + 1;
    }
    return true;
}

/* The word whose glyphs those are, where only one is: its place, or -1. */
static int word_spelt(const Dictionary *d, const uint8_t *glyphs, unsigned n)
{
    int found = -1;
    for (unsigned w = 0; w < WORDS; w++) {
        if (d->length[w] == n && !memcmp(d->glyphs[w], glyphs, n)) {
            if (found >= 0)
                return -1;
            found = (int)w;
        }
    }
    return found;
}

/* ---- Control codes -------------------------------------------------------------------------- */

enum { CODE_END = 0x00, CODE_WAIT = 0x03, CODE_NAMED = 0x12, NAMED_TECH = 0, NAMED_ENEMY = 1 };
enum { FIRST_BREAK = 0x05, LAST_BREAK = 0x0C };

static const struct {
    uint8_t code;
    const char *name;
} CODES[] = {
    { 0x04, "stop" }, { 0x05, "line" }, { 0x06, "line indent" }, { 0x07, "line read" },
    { 0x08, "line read indent" }, { 0x09, "page" }, { 0x0A, "page indent" }, { 0x0B, "page read" },
    { 0x0C, "page read indent" }, { 0x0D, "number 1" }, { 0x0E, "number 2" }, { 0x0F, "number 3" },
    { 0x10, "pause" }, { 0x11, "character" }, { 0x13, "Crono" }, { 0x14, "Marle" }, { 0x15, "Lucca" },
    { 0x16, "Robo" }, { 0x17, "Frog" }, { 0x18, "Ayla" }, { 0x19, "Magus" }, { 0x1A, "leader" },
    { 0x1B, "member 1" }, { 0x1C, "member 2" }, { 0x1D, "member 3" }, { 0x1E, "Nadia" }, { 0x1F, "item" },
    { 0x20, "Epoch" },
};

static const char *code_name(uint8_t code)
{
    for (size_t i = 0; i < sizeof CODES / sizeof *CODES; i++)
        if (CODES[i].code == code)
            return CODES[i].name;
    return NULL;
}

/* ---- Writing ------------------------------------------------------------------------------------- */

typedef enum { TEXT_DIALOG, TEXT_NAMES, TEXT_DICTIONARY } Form;

static bool form_of(const AssetEntry *e, Form *f, unsigned *glyphs)
{
    const char *c = asset_codec_names[e->codec];
    if (!strcmp(c, "text:dialog"))
        *f = TEXT_DIALOG;
    else if (!strcmp(c, "text:dialog-dictionary"))
        *f = TEXT_DICTIONARY;
    else if (!strncmp(c, "text:names:", 11) && (*glyphs = (unsigned)strtoul(c + 11, NULL, 10)) > 0)
        *f = TEXT_NAMES;
    else
        return false;
    return true;
}

static bool decodes(const AssetEntry *e, bool *named)
{
    Form f;
    unsigned glyphs;
    *named = true;
    return form_of(e, &f, &glyphs);
}

/* A line being written: how much of it is, and the spaces not yet written:
 * a space at either end is written {space}, since a line's ends are trimmed
 * as it is read. */
typedef struct {
    size_t len;
    unsigned spaces;
} Line;

static void line_put(DecodedText *t, Line *l, const char *s)
{
    for (; l->spaces; l->spaces--, l->len++)
        decoded_put(t, " ");
    decoded_put(t, s);
    l->len += strlen(s);
}

static void line_end(DecodedText *t, Line *l)
{
    if (l->spaces) {
        l->spaces--;
        line_put(t, l, "{space}");
    }
    if (l->len)
        decoded_put(t, "\n");
    l->len = 0;
}

static void put_glyph(DecodedText *t, Line *l, uint8_t code, bool names)
{
    char letter[2], hex[8];
    const char *g = glyph_text(code, names, letter);
    if (code == SPACE) {
        if (l->len == 0 && !l->spaces)
            line_put(t, l, "{space}");
        else
            l->spaces++;
        return;
    }
    if (!g) {
        snprintf(hex, sizeof hex, "{%02X}", code);
        g = hex;
    }
    line_put(t, l, g);
}

static void put_word(DecodedText *t, Line *l, const Dictionary *d, unsigned w)
{
    line_put(t, l, "<");
    for (unsigned k = 0; k < d->length[w]; k++) {
        char letter[2], hex[8];
        const char *g = glyph_text(d->glyphs[w][k], false, letter);
        if (!g) {
            snprintf(hex, sizeof hex, "{%02X}", d->glyphs[w][k]);
            g = hex;
        }
        line_put(t, l, g);
    }
    line_put(t, l, ">");
}

static void write_string(DecodedText *t, const uint8_t *s, uint32_t len, const Dictionary *d)
{
    Line l = { .len = 0 };
    char code[24];
    uint32_t last = len - 1;   /* the end, which isn't written */
    for (uint32_t i = 0; i < last; i++) {
        uint8_t b = s[i];
        const char *name = code_name(b);
        bool operand = i + 1 < last;
        if (b >= FIRST_GLYPH) {
            put_glyph(t, &l, b, false);
            continue;
        }
        if (b >= FIRST_WORD && d && word_spelt(d, d->glyphs[b - FIRST_WORD], d->length[b - FIRST_WORD]) >= 0) {
            put_word(t, &l, d, b - FIRST_WORD);
            continue;
        }
        if (b == CODE_WAIT && operand)
            snprintf(code, sizeof code, "{wait %02X}", s[++i]);
        else if (b == CODE_NAMED && operand && s[i + 1] <= NAMED_ENEMY)
            snprintf(code, sizeof code, "{%s}", s[++i] == NAMED_TECH ? "tech" : "enemy");
        else if ((b == 0x01 || b == 0x02 || b == CODE_NAMED) && operand)
            snprintf(code, sizeof code, "{%02X %02X}", b, s[i + 1]), i++;
        else if (name)
            snprintf(code, sizeof code, "{%s}", name);
        else
            snprintf(code, sizeof code, "{%02X}", b);
        line_put(t, &l, code);
        if (b >= FIRST_BREAK && b <= LAST_BREAK)
            line_end(t, &l);
    }
    line_end(t, &l);
}

static void write_names(DecodedText *t, const uint8_t *bytes, uint32_t len, unsigned glyphs)
{
    char line[32];
    for (uint32_t n = 0; n < len / glyphs; n++) {
        if (n % 16 == 0) {
            snprintf(line, sizeof line, "# %u\n", (unsigned)n);
            decoded_put(t, line);
        }
        Line l = { .len = 0 };
        line_put(t, &l, "\"");
        for (unsigned k = 0; k < glyphs; k++)
            put_glyph(t, &l, bytes[n * glyphs + k], true);
        line_put(t, &l, "\"");
        line_end(t, &l);
    }
}

static bool write_dictionary(DecodedText *t, const uint8_t *bytes, uint32_t len)
{
    Dictionary d;
    if (!words_of(bytes, len, &d))
        return false;
    /* the words as they are kept: each the one whose glyphs come next */
    uint32_t at = POINTER_BYTES, written = 0;
    while (written < WORDS) {
        unsigned w = 0;
        while (w < WORDS && d.glyphs[w] != bytes + at + 1)
            w++;
        if (w == WORDS)
            return false;
        char code[16];
        snprintf(code, sizeof code, "%02X ", w + FIRST_WORD);
        Line l = { .len = 0 };
        line_put(t, &l, code);
        line_put(t, &l, "\"");
        for (unsigned k = 0; k < d.length[w]; k++)
            put_glyph(t, &l, d.glyphs[w][k], false);
        line_put(t, &l, "\"");
        line_end(t, &l);
        at += 1u + d.length[w];
        written++;
    }
    for (; at < len; at++)   /* what follows the last word */
        if (bytes[at])
            return false;
    return true;
}

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    Form f = TEXT_DIALOG;
    unsigned glyphs = 0;
    form_of(e, &f, &glyphs);
    if (f == TEXT_NAMES) {
        write_names(t, bytes, e->length, glyphs);
        return e->length % glyphs == 0;
    }
    if (f == TEXT_DICTIONARY)
        return write_dictionary(t, bytes, e->length);
    int index;
    const AssetEntry *de = dictionary_entry(&index);
    uint32_t n = 0;
    const uint8_t *dictionary = de ? store->find(store->ctx, de->name, asset_type_names[de->type], &n) : NULL;
    Dictionary d;
    bool words = dictionary && n == de->length && words_of(dictionary, n, &d);
    if (e->length == 0 || bytes[e->length - 1] != CODE_END)
        return false;
    write_string(t, bytes, e->length, words ? &d : NULL);
    return true;
}

/* ---- Reading ------------------------------------------------------------------------------------- */

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* Bytes in hex, two digits each, a space between: how many, or 0. */
static size_t hex_bytes(const char *s, const char *end, uint8_t *out, size_t most)
{
    size_t n = 0;
    while (s < end) {
        if (n == most || end - s < 2 || hex_digit(s[0]) < 0 || hex_digit(s[1]) < 0)
            return 0;
        out[n++] = (uint8_t)(hex_digit(s[0]) << 4 | hex_digit(s[1]));
        s += 2;
        if (s < end && *s++ != ' ')
            return 0;
    }
    return n;
}

static const char *put_made(DecodedReading *r, const uint8_t *bytes, size_t n)
{
    uint8_t *p = decoded_grow(r, (uint32_t)n);
    if (!p)
        return "out of memory";
    memcpy(p, bytes, n);
    return NULL;
}

/* What's in braces: a code's name, `wait XX`, `space`, or bytes in hex. */
static const char *read_code(DecodedReading *r, const char *s, const char *end)
{
    uint8_t bytes[8];
    size_t n = 0;
    for (size_t i = 0; i < sizeof CODES / sizeof *CODES; i++)
        if ((size_t)(end - s) == strlen(CODES[i].name) && !memcmp(s, CODES[i].name, (size_t)(end - s)))
            return put_made(r, &CODES[i].code, 1);
    if ((size_t)(end - s) == 5 && !memcmp(s, "space", 5))
        bytes[0] = SPACE, n = 1;
    else if ((size_t)(end - s) == 4 && !memcmp(s, "tech", 4))
        bytes[0] = CODE_NAMED, bytes[1] = NAMED_TECH, n = 2;
    else if ((size_t)(end - s) == 5 && !memcmp(s, "enemy", 5))
        bytes[0] = CODE_NAMED, bytes[1] = NAMED_ENEMY, n = 2;
    else if ((size_t)(end - s) > 5 && !memcmp(s, "wait ", 5) && hex_bytes(s + 5, end, bytes + 1, 1) == 1)
        bytes[0] = CODE_WAIT, n = 2;
    else
        n = hex_bytes(s, end, bytes, sizeof bytes);
    return n ? put_made(r, bytes, n) : "a code in braces is a code's name, or bytes in hex";
}

/* Glyphs, and bytes in braces, from s to end, into out: how many, or -1. */
static int read_glyphs(const char *s, const char *end, bool names, uint8_t *out, size_t most)
{
    size_t n = 0;
    while (s < end) {
        uint8_t code;
        size_t len;
        if (*s == '{') {
            const char *close = memchr(s, '}', (size_t)(end - s));
            size_t got = close ? hex_bytes(s + 1, close, out + n, most - n) : 0;
            if (!got && close && close - s == 6 && !memcmp(s + 1, "space", 5) && n < most)
                out[n] = SPACE, got = 1;
            if (!got)
                return -1;
            n += got;
            s = close + 1;
        } else if (n < most && glyph_at(s, end, names, &code, &len)) {
            out[n++] = code;
            s += len;
        } else {
            return -1;
        }
    }
    return (int)n;
}

/* A word of the dictionary beneath, by its glyphs. */
static const char *read_word(const Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    int index;
    const AssetEntry *de = dictionary_entry(&index);
    Dictionary dictionary;
    uint8_t glyphs[256];
    int n = read_glyphs(s, end, false, glyphs, sizeof glyphs);
    if (n < 0)
        return "a word is the dictionary's glyphs in angle brackets";
    if (!de || !words_of(decoded_beneath(d, index), de->length, &dictionary))
        return "the store under the text hasn't the dictionary";
    int w = word_spelt(&dictionary, glyphs, (unsigned)n);
    uint8_t code = (uint8_t)(FIRST_WORD + w);
    return w >= 0 ? put_made(r, &code, 1) : "no word of the dictionary is that";
}

static const char *read_string_line(const Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    while (s < end) {
        uint8_t code;
        size_t len;
        if (*s == '{' || *s == '<') {
            const char *close = memchr(s, *s == '{' ? '}' : '>', (size_t)(end - s));
            if (!close)
                return *s == '{' ? "a code's braces aren't closed" : "a word's angle brackets aren't closed";
            const char *wrong = *s == '{' ? read_code(r, s + 1, close) : read_word(d, r, s + 1, close);
            if (wrong)
                return wrong;
            s = close + 1;
        } else if (glyph_at(s, end, false, &code, &len)) {
            const char *wrong = put_made(r, &code, 1);
            if (wrong)
                return wrong;
            s += len;
        } else {
            return "not a glyph of the font: write it in hex in braces";
        }
    }
    return NULL;
}

/* A quoted run of glyphs: the glyphs inside the quotes, or -1. */
static int quoted(const char *s, const char *end, bool names, uint8_t *out, size_t most)
{
    if (end - s < 2 || *s != '"' || end[-1] != '"')
        return -1;
    return read_glyphs(s + 1, end - 1, names, out, most);
}

static const char *read_name_line(DecodedReading *r, unsigned glyphs, const char *s, const char *end)
{
    uint8_t name[256];
    if (quoted(s, end, true, name, sizeof name) != (int)glyphs)
        return glyphs == 11 ? "a name is its 11 glyphs in quotes" : "a name is its glyphs in quotes, as many as the others";
    if ((r->items + 1) * glyphs > asset_catalogue[r->entry].length)
        return "more names than the table has";
    memcpy(r->bytes + r->items * glyphs, name, glyphs);
    r->items++;
    return NULL;
}

static const char *read_dictionary_line(DecodedReading *r, const char *s, const char *end)
{
    uint8_t glyphs[256];
    int code_hi = s < end ? hex_digit(s[0]) : -1, code_lo = end - s > 1 ? hex_digit(s[1]) : -1;
    if (code_hi < 0 || code_lo < 0 || end - s < 3 || s[2] != ' ')
        return "a word is its code, $21-$A0, and its glyphs in quotes";
    unsigned code = (unsigned)(code_hi << 4 | code_lo);
    int n = quoted(s + 3, end, false, glyphs, sizeof glyphs);
    if (code < FIRST_WORD || code >= FIRST_WORD + WORDS || n < 0)
        return "a word is its code, $21-$A0, and its glyphs in quotes";
    uint8_t *pointer = r->bytes + 2 * (code - FIRST_WORD);
    if (pointer[0] | pointer[1])
        return "that word is given twice";
    uint32_t at = POINTER_BYTES + r->made_len;
    if (at + 1 + (uint32_t)n > asset_catalogue[r->entry].length)
        return "the words take more room than the dictionary has";
    pointer[0] = (uint8_t)(DICTIONARY_AT + at);
    pointer[1] = (uint8_t)((DICTIONARY_AT + at) >> 8);
    uint8_t length = (uint8_t)n;
    const char *wrong = put_made(r, &length, 1);
    return wrong ? wrong : put_made(r, glyphs, (size_t)n);
}

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    Form f = TEXT_DIALOG;
    unsigned glyphs = 0;
    form_of(&asset_catalogue[r->entry], &f, &glyphs);
    if (f == TEXT_NAMES)
        return read_name_line(r, glyphs, s, end);
    if (f == TEXT_DICTIONARY)
        return read_dictionary_line(r, s, end);
    return read_string_line(d, r, s, end);
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    (void)d;
    const AssetEntry *e = &asset_catalogue[r->entry];
    Form f = TEXT_DIALOG;
    unsigned glyphs = 0;
    form_of(e, &f, &glyphs);
    if (f == TEXT_NAMES)
        return r->items * glyphs == e->length ? NULL : "fewer names than the table has";
    if (f == TEXT_DICTIONARY) {
        for (unsigned w = 0; w < WORDS; w++)
            if (!(r->bytes[2 * w] | r->bytes[2 * w + 1]))
                return "the dictionary has its 128 words, each once";
        memcpy(r->bytes + POINTER_BYTES, r->made, r->made_len);
        return NULL;
    }
    if (r->made_len >= e->length)
        return "the string is longer than the cartridge's, its end included";
    memcpy(r->bytes, r->made, r->made_len);   /* then its end, and nothing after it */
    return NULL;
}

const DecodedKind game_text = {
    "text",
    "# Chrono Trigger's text, from your cartridge: not for sharing.\n"
    "# Strings as they read, a glyph a letter, <words> of the dictionary, {codes}\n"
    "# in braces ({line}, {page read}, {Crono}, {wait 0A}, or any bytes in hex: {F3});\n"
    "# names in quotes; the dictionary a word a line. An edited string must fit its\n"
    "# slot. Edit and start the game again; delete the file to write it afresh.\n",
    decodes, write_resource, read_line, read_end,
};
