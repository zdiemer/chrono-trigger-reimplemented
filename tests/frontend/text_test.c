/* The game's text (frontend/text.h) without a ROM: every string, name table
 * and the dictionary of a store of made-up text are written and read back as
 * the same bytes; an edited string and name are what the store over the
 * text serves, the rest from the store under it; and text that is wrong is
 * refused at its line. Where the ROM is staged (roms/ct-usa.sfc, from the
 * source directory), every text resource of the cartridge reads back as its
 * bytes too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "text.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

static uint8_t *made_up_bytes;
static uint32_t made_up_at[0x10000];

static const uint8_t *made_up_find(void *ctx, const char *name, const char *type, uint32_t *length)
{
    (void)ctx;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        if (!strcmp(e->name, name) && !strcmp(asset_type_names[e->type], type)) {
            *length = e->length;
            return made_up_bytes + made_up_at[i];
        }
    }
    return NULL;
}

static const uint8_t *served(const AssetStore *s, const AssetEntry *e)
{
    uint32_t n = 0;
    const uint8_t *bytes = s->find(s->ctx, e->name, asset_type_names[e->type], &n);
    return bytes && n == e->length ? bytes : NULL;
}

static uint8_t *made_up(const AssetEntry *e) { return made_up_bytes + made_up_at[e - asset_catalogue]; }

static const char *codec_of(const AssetEntry *e) { return asset_codec_names[e->codec]; }

/* Strings of glyphs, spaces, words, codes and their operands, and bytes no
 * glyph spells; each ended where its slot ends. */
static const uint8_t PATTERN[] = { 0xEF, 0xA7, 0xBE, 0x21, 0xEF, 0x05, 0xEF, 0xC5, 0x13, 0x03, 0x0A, 0xF5, 0x9F,
                                   0x01, 0x7F, 0x12, 0x00, 0x12, 0x09, 0xE1, 0xEE, 0xF3, 0x0B, 0x22, 0xEF, 0xEF };

/* The dictionary: 128 words of three glyphs, kept last first; the last
 * two alike, so neither is written as a word. */
static void made_up_dictionary(uint8_t *d, uint32_t len)
{
    memset(d, 0, len);
    uint32_t at = 0x100;
    for (int w = 127; w >= 0; w--) {
        unsigned spelt = w == 127 ? 126 : (unsigned)w;
        d[2 * w] = (uint8_t)(0xFA00 + at);
        d[2 * w + 1] = (uint8_t)((0xFA00 + at) >> 8);
        d[at] = 3;
        d[at + 1] = (uint8_t)(0xA0 + spelt % 26);
        d[at + 2] = (uint8_t)(0xBA + spelt / 26 % 26);
        d[at + 3] = 0xEF;
        at += 4;
    }
}

static const AssetEntry *first_of(const char *codec, uint32_t at_least)
{
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        if (!strcmp(codec_of(&asset_catalogue[i]), codec) && asset_catalogue[i].length >= at_least)
            return &asset_catalogue[i];
    return NULL;
}

/* Text that must be refused, and the line it is refused at. */
static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    Decoded *d = decoded_import(&game_text, text, strlen(text), base, &line, &why);
    CHECK(d == NULL);
    if (d || line != at)
        fprintf(stderr, "  refused at line %u, not %u (%s): %.70s\n", line, at, why, text);
    decoded_close(d);
}

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("text_test: no ROM staged, the cartridge's text not read back\n");
        return;
    }
    uint8_t *file = malloc(ASSET_ROM_SIZE + 0x200);
    size_t len = file ? fread(file, 1, ASSET_ROM_SIZE + 0x200, f) : 0;
    fclose(f);
    size_t size;
    const uint8_t *rom = asset_rom_image(file, len, &size);
    AssetRom ctx;
    AssetStore store;
    asset_store_rom(&store, &ctx, rom, size);
    const char *why = "";
    bool same = size == ASSET_ROM_SIZE && decoded_round_trip(&game_text, &store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("text_test: every text resource of the cartridge reads back as its bytes\n");
    free(file);
}

int main(void)
{
    size_t total = 0;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        made_up_at[i] = (uint32_t)total;
        total += asset_catalogue[i].length;
    }
    made_up_bytes = calloc(total, 1);
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        const char *codec = codec_of(e);
        uint8_t *b = made_up(e);
        if (!strcmp(codec, "text:dialog")) {
            for (uint32_t k = 0; k + 1 < e->length; k++)
                b[k] = PATTERN[(k + i) % sizeof PATTERN];
            b[e->length - 1] = 0;
        } else if (!strncmp(codec, "text:names:", 11)) {
            for (uint32_t k = 0; k < e->length; k++)
                b[k] = (uint8_t)(k % 5 == 0 ? 0x2F : k % 7 == 0 ? 0xEF : 0xA0 + (k * 13) % 0x50);
        } else if (!strcmp(codec, "text:dialog-dictionary")) {
            made_up_dictionary(b, e->length);
        }
    }
    AssetStore made = { made_up_find, NULL };
    const char *why = "";
    CHECK(decoded_round_trip(&game_text, &made, &why));
    if (failures)
        fprintf(stderr, "  %s doesn't read back\n", why);

    /* A string written afresh, and a name edited. */
    const AssetEntry *string = first_of("text:dialog", 16), *names = first_of("text:names:11", 22);
    CHECK(string && names);
    if (!string || !names)
        return 1;
    /* the whole text, its string and its first name replaced */
    size_t len = 0;
    char *whole = decoded_export(&game_text, &made, &len, &why);
    CHECK(whole != NULL);
    if (!whole)
        return 1;
    char header[64];
    snprintf(header, sizeof header, "[%s]\n# 0\n", names->name);
    char *first_name = strstr(whole, header), *after = first_name ? strchr(first_name + strlen(header), '\n') : NULL;
    CHECK(after != NULL);
    if (!after)
        return 1;
    size_t room = len + 1024;
    char *text = malloc(room);
    snprintf(text, room, "%.*s\"{2F}Edge  Bro \"%s", (int)(first_name + strlen(header) - whole), whole, after);
    char *string_text = strstr(text, "[");
    while (string_text && strncmp(string_text + 1, string->name, strlen(string->name)))
        string_text = strstr(string_text + 1, "\n[") ? strstr(string_text + 1, "\n[") + 1 : NULL;
    CHECK(string_text != NULL);
    if (!string_text)
        return 1;
    /* the string's own lines dropped: it is given whole in a text of its own */
    char *next = strstr(string_text + 1, "\n[");
    memmove(string_text, next + 1, strlen(next + 1) + 1);
    char own[256];
    snprintf(own, sizeof own, "[%s]\nHi! <Aa >{Crono}{line}\n{space}9{wait 0A}{01 7F}{tech}\n", string->name);
    unsigned line = 0;
    Decoded *d = decoded_import(&game_text, text, strlen(text), &made, &line, &why);
    Decoded *d_own = decoded_import(&game_text, own, strlen(own), &made, &line, &why);
    CHECK(d != NULL && d_own != NULL);
    if (!d || !d_own)
        fprintf(stderr, "  line %u: %s\n", line, why);
    AssetStore over, own_over;
    if (d && d_own) {
        decoded_store(d, &over);
        decoded_store(d_own, &own_over);
        static const uint8_t want[] = { 0xA7, 0xC2, 0xDE, 0xEF, 0x21, 0x13, 0x05, 0xEF, 0xDD, 0x03, 0x0A,
                                        0x01, 0x7F, 0x12, 0x00, 0x00 };
        const uint8_t *got = served(&own_over, string);
        CHECK(got && !memcmp(got, want, sizeof want));
        for (uint32_t k = sizeof want; got && k < string->length; k++)
            CHECK(got[k] == 0);
        static const uint8_t name[] = { 0x2F, 0xA4, 0xBD, 0xC0, 0xBE, 0xEF, 0xEF, 0xA1, 0xCB, 0xC8, 0xEF };
        got = served(&over, names);
        CHECK(got && !memcmp(got, name, sizeof name) && !memcmp(got + 11, made_up(names) + 11, 11));
        const AssetEntry *other = first_of("text:names:10", 1);
        CHECK(other && !memcmp(served(&over, other), made_up(other), other->length));
        CHECK(served(&own_over, names) == made_up(names) && served(&own_over, other) == made_up(other));
    }
    decoded_close(d);
    decoded_close(d_own);
    free(text);
    free(whole);

    /* Plain text packed with the dictionary's words as it is read: the
     * longest word at each glyph, a run ended by `|` packed on its own, one
     * marked {unpacked} kept as its glyphs. */
    {
        char plain[256];
        snprintf(plain, sizeof plain, "[%s]\nAa Ba x|Aa {unpacked}Aa{space}\n", string->name);
        unsigned line = 0;
        const char *why = "";
        Decoded *packed = decoded_import(&game_text, plain, strlen(plain), &made, &line, &why);
        CHECK(packed != NULL);
        if (packed) {
            AssetStore over;
            decoded_store(packed, &over);
            static const uint8_t want[] = { 0x21, 0x22, 0xD1, 0x21, 0xA0, 0xBA, 0xEF, 0x00 };
            const uint8_t *got = served(&over, string);
            CHECK(got && !memcmp(got, want, sizeof want));
        }
        decoded_close(packed);
    }

    /* Wrong text, refused where it is wrong. */
    char wrong[512];
    snprintf(wrong, sizeof wrong, "[%s]\nHi\nHo{lime}\n", string->name);
    refused(&made, wrong, 3);
    snprintf(wrong, sizeof wrong, "[%s]\nHi{line\n", string->name);
    refused(&made, wrong, 2);
    snprintf(wrong, sizeof wrong, "[%s]\n<Zz >\n", string->name);                  /* no such word */
    refused(&made, wrong, 2);
    snprintf(wrong, sizeof wrong, "[%s]\n<Yf >\n", string->name);                  /* two words alike */
    refused(&made, wrong, 2);
    snprintf(wrong, sizeof wrong, "[%s]\nHi\n~\n", string->name);                  /* not a glyph */
    refused(&made, wrong, 3);
    snprintf(wrong, sizeof wrong, "[%s]\n", string->name);
    for (uint32_t k = 0; k < string->length; k++)
        strcat(wrong, "a");
    strcat(wrong, "\n");
    refused(&made, wrong, 1);                                                      /* too long: at its [name] */
    /* A string ending in a code that takes the next byte, at its last line;
     * the same code given its byte is read. */
    static const char *const wanting[] = { "{03}", "{01}", "{02}", "{12}", "Hi{12 00}{03}" };
    for (size_t k = 0; k < sizeof wanting / sizeof *wanting; k++) {
        snprintf(wrong, sizeof wrong, "[%s]\nHi\n\n%s\n# a comment\n", string->name, wanting[k]);
        refused(&made, wrong, 4);
    }
    snprintf(wrong, sizeof wrong, "[%s]\nHi{wait 0A}{01 7F}{tech}\n", string->name);
    {
        unsigned line;
        const char *why = "";
        Decoded *fine = decoded_import(&game_text, wrong, strlen(wrong), &made, &line, &why);
        CHECK(fine != NULL);
        decoded_close(fine);
    }
    snprintf(wrong, sizeof wrong, "[%s]\n\"Bronze Edge\"\n", names->name);
    refused(&made, wrong, 1);                                                      /* fewer names */
    snprintf(wrong, sizeof wrong, "[%s]\n\"Bronze Edg\"\n", names->name);
    refused(&made, wrong, 2);
    snprintf(wrong, sizeof wrong, "[%s]\nBronze Edge\n", names->name);
    refused(&made, wrong, 2);
    const AssetEntry *dictionary = first_of("text:dialog-dictionary", 1);
    CHECK(dictionary != NULL);
    if (dictionary) {
        snprintf(wrong, sizeof wrong, "[%s]\n21 \"the \"\n21 \"a\"\n", dictionary->name);
        refused(&made, wrong, 3);
        snprintf(wrong, sizeof wrong, "[%s]\n21 \"the \"\n", dictionary->name);
        refused(&made, wrong, 1);                                                  /* 127 words short */
        snprintf(wrong, sizeof wrong, "[%s]\n20 \"the \"\n", dictionary->name);
        refused(&made, wrong, 2);
    }

    the_cartridge();
    free(made_up_bytes);
    if (failures)
        fprintf(stderr, "text_test: %d failures\n", failures);
    else
        printf("text_test: every text resource written and read back, edits served, wrong text refused, OK\n");
    return failures != 0;
}
