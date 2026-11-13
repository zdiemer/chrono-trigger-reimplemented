/* The fonts' text (frontend/fonts.h) without a ROM: every font of a store of
 * made-up glyphs is written and read back as the same bytes, widths too; an
 * edited pixel and width are what the store over the text serves, while the
 * fonts the text doesn't name come from the store under it; and text that is
 * wrong is refused at its line. Where the ROM is staged (roms/ct-usa.sfc,
 * from the source directory), every font of the cartridge reads back as its
 * bytes too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "fonts.h"

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

static const AssetEntry *named(const char *name)
{
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        if (!strcmp(asset_catalogue[i].name, name))
            return &asset_catalogue[i];
    return NULL;
}

static const uint8_t *served(const AssetStore *s, const AssetEntry *e)
{
    uint32_t n = 0;
    const uint8_t *bytes = s->find(s->ctx, e->name, asset_type_names[e->type], &n);
    return bytes && n == e->length ? bytes : NULL;
}

static const uint8_t *made_up(const AssetEntry *e) { return made_up_bytes + made_up_at[e - asset_catalogue]; }

/* Text that must be refused, and the line it is refused at. */
static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    Decoded *d = decoded_import(&font_text, text, strlen(text), base, &line, &why);
    CHECK(d == NULL);
    if (d || line != at)
        fprintf(stderr, "  refused at line %u, not %u (%s): %.60s\n", line, at, why, text);
    decoded_close(d);
}

/* A made-up 8x8 font of two glyphs' text, with `glyph` lines and pixel
 * lines as given. */
static char *small_text(const char *first, const char *pixels)
{
    static char text[1024];
    snprintf(text, sizeof text, "[small_font]\n%s\n%s", first, pixels);
    return text;
}

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("fonts_test: no ROM staged, the cartridge's fonts not read back\n");
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
    bool same = size == ASSET_ROM_SIZE && decoded_round_trip(&font_text, &store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("fonts_test: every font of the cartridge reads back as its bytes\n");
    free(file);
}

int main(void)
{
    size_t total = 0;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        made_up_at[i] = (uint32_t)total;
        total += asset_catalogue[i].length;
    }
    made_up_bytes = malloc(total);
    for (size_t k = 0; k < total; k++)   /* every bit of every plane */
        made_up_bytes[k] = (uint8_t)(k * 37 + (k >> 8) * 11 + 5);
    AssetStore made = { made_up_find, NULL };
    const AssetEntry *dialog = named("dialog_font"), *widths = named("dialog_font_widths"),
                     *small = named("small_font"), *numbers = named("battle_number_font");
    CHECK(dialog && widths && small && numbers);
    if (!dialog || !widths || !small || !numbers)
        return 1;

    const char *why = "";
    CHECK(decoded_round_trip(&font_text, &made, &why));

    /* An edit: glyph $A0's top left pixel made color 3, the right-most of
     * its second line color 2, and its width 9. */
    size_t len = 0;
    char *text = decoded_export(&font_text, &made, &len, &why);
    CHECK(text != NULL);
    if (!text)
        return 1;
    char *glyph = strstr(text, "glyph A0 width ");
    CHECK(glyph != NULL);
    if (!glyph)
        return 1;
    const char *header = "glyph A0 width 9", *rest = strchr(glyph, '\n');
    char *edited = malloc(len + 16);
    size_t before = (size_t)(glyph - text), after = len - (size_t)(rest - text);
    memcpy(edited, text, before);
    memcpy(edited + before, header, strlen(header));
    memcpy(edited + before + strlen(header), rest, after);
    size_t edited_len = before + strlen(header) + after;
    char *first = edited + before + strlen(header) + 1, *second = first + 13;
    first[0] = '3';
    second[11] = '2';
    unsigned line = 0;
    Decoded *d = decoded_import(&font_text, edited, edited_len, &made, &line, &why);
    CHECK(d != NULL);
    AssetStore over;
    if (d) {
        decoded_store(d, &over);
        const uint8_t *got = served(&over, dialog), *was = made_up(dialog);
        const uint32_t left = 0xA0 * 24, right = 256 * 24 + 0x50 * 24;   /* glyph $A0's: even, its right four high */
        CHECK(got && got[left] == (was[left] | 0x80) && got[left + 1] == (was[left + 1] | 0x80));
        CHECK(got && got[right + 2] == (was[right + 2] & ~0x10) && got[right + 3] == (was[right + 3] | 0x10));
        CHECK(got && !memcmp(got, was, left) && !memcmp(got + left + 2, was + left + 2, right - left - 2));
        const uint8_t *w = served(&over, widths);
        CHECK(w && w[0] == 9 && !memcmp(w + 1, made_up(widths) + 1, widths->length - 1));
        CHECK(served(&over, small) && !memcmp(served(&over, small), made_up(small), small->length));
    }
    decoded_close(d);
    free(edited);
    free(text);

    /* Only the fonts the text names are its own: a two-glyph text of the
     * battle's numbers isn't the whole font, so it is refused at its end;
     * the whole font, all pixels '1', is served, the dialog font's widths
     * and glyphs from under it. */
    char *one = malloc(numbers->length / 16 * (10 + 8 * 24) + 64);
    strcpy(one, "# just one\r\n[battle_number_font]\r\n");
    for (unsigned g = 0; g < numbers->length / 16; g++) {
        sprintf(one + strlen(one), "glyph %02X\r\n", g);
        for (unsigned y = 0; y < 8; y++)
            strcat(one, "11111111   # all one\r\n");
    }
    d = decoded_import(&font_text, one, strlen(one), &made, &line, &why);
    CHECK(d != NULL);
    if (d) {
        decoded_store(d, &over);
        const uint8_t *got = served(&over, numbers);
        CHECK(got && got[0] == 0xFF && got[1] == 0 && got[numbers->length - 2] == 0xFF);
        CHECK(served(&over, widths) == made_up(widths) && served(&over, dialog) == made_up(dialog));
    }
    decoded_close(d);
    free(one);

    /* Wrong text, refused where it is wrong. */
    const char *pixels = "........\n........\n........\n........\n........\n........\n........\n........\n";
    refused(&made, "glyph 00\n", 1);
    refused(&made, "[no_such_font]\n", 1);
    refused(&made, "# a comment\n[bit_mask]\n", 2);              /* a resource, not a font */
    refused(&made, "[dialog_font_widths]\n", 1);                 /* given with its font */
    refused(&made, small_text("glyph 01", pixels), 2);           /* out of order */
    refused(&made, small_text("glyph 00 width 5", pixels), 2);   /* this font has no widths */
    refused(&made, small_text("glyf 00", pixels), 2);
    refused(&made, small_text("glyph 00", "........\n....4...\n"), 4);
    refused(&made, small_text("glyph 00", "........\n.......\n"), 4);
    refused(&made, small_text("glyph 00", "........\nglyph 01\n"), 4);
    refused(&made, small_text("glyph 00", pixels), 1);           /* too few glyphs: at its [name] */
    refused(&made, "[dialog_font]\nglyph 00\n", 1);             /* its lines missing at the end */
    refused(&made, "[small_font]\n[small_font]\n", 1);          /* no glyphs */
    char *widthless = malloc(256 * 200 + 64);
    strcpy(widthless, "[dialog_font]\n");
    for (unsigned g = 0; g <= 0xA0; g++) {
        sprintf(widthless + strlen(widthless), "glyph %02X\n", g);
        for (unsigned y = 0; y < 12; y++)
            strcat(widthless, "............\n");
    }
    refused(&made, widthless, 2 + 0xA0 * 13);                    /* $A0 has a width */
    free(widthless);

    the_cartridge();
    free(made_up_bytes);
    if (failures)
        fprintf(stderr, "fonts_test: %d failures\n", failures);
    else
        printf("fonts_test: every font written and read back, an edit served, wrong text refused, OK\n");
    return failures != 0;
}
