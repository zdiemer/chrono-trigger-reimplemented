/* The palettes' text (frontend/palettes.h) without a ROM: every palette of a
 * store of made-up words is written and read back as the same bytes, an
 * edited color is what the store over the text serves while the rest come
 * from the store under it, and text that is wrong is refused at its line.
 * Where the ROM is staged (roms/ct-usa.sfc, from the source directory), every
 * palette of the cartridge reads back as its bytes too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "palettes.h"

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

static const AssetEntry *first_palette(void)
{
    unsigned row, rows;
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        if (palette_codec(&asset_catalogue[i], &row, &rows))
            return &asset_catalogue[i];
    return NULL;
}

/* Text that must be refused, and the line it is refused at. */
static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    AssetPalettes *p = palettes_import(text, strlen(text), base, &line, &why);
    CHECK(p == NULL);
    if (p || line != at)
        fprintf(stderr, "  refused at line %u, not %u: %s\n", line, at, text);
    palettes_close(p);
}

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("palettes_test: no ROM staged, the cartridge's palettes not read back\n");
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
    bool same = size == ASSET_ROM_SIZE && palettes_round_trip(&store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("palettes_test: every palette of the cartridge reads back as its bytes\n");
    free(file);
}

int main(void)
{
    size_t total = 0;
    unsigned palettes = 0, row, rows;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        made_up_at[i] = (uint32_t)total;
        total += asset_catalogue[i].length;
        palettes += palette_codec(&asset_catalogue[i], &row, &rows);
    }
    made_up_bytes = malloc(total);
    for (size_t k = 0; k < total; k++)   /* every bit, the colors' top one too */
        made_up_bytes[k] = (uint8_t)(k * 37 + (k >> 8) * 11 + 5);
    AssetStore made = { made_up_find, NULL };
    CHECK(palettes > 0);

    const char *why = "";
    CHECK(palettes_round_trip(&made, &why));

    /* An edited color: the first palette's second color made pure red, its
     * top bit kept. */
    const AssetEntry *e = first_palette();
    size_t len = 0;
    char *text = palettes_export(&made, &len, &why);
    CHECK(text != NULL && e != NULL);
    if (!text || !e)
        return 1;
    char header[96];
    snprintf(header, sizeof header, "[%s]\n", e->name);
    char *at = strstr(text, header);
    CHECK(at != NULL);
    char *second = at ? strchr(strchr(strchr(at, '\n') + 1, '\n') + 1, '\n') + 1 : NULL;   /* past the row comment */
    const uint8_t *was = made_up_bytes + made_up_at[e - asset_catalogue];
    char *edited = malloc(len + 16);
    size_t edited_len = 0;
    if (second && edited) {
        const char *rest = strchr(second, '\n');
        size_t before = (size_t)(second - text);
        memcpy(edited, text, before);
        edited_len = before + (size_t)sprintf(edited + before, "31 0 0%s", was[3] & 0x80 ? " +" : "");
        memcpy(edited + edited_len, rest, len - (size_t)(rest - text));
        edited_len += len - (size_t)(rest - text);
    }
    unsigned line = 0;
    AssetPalettes *p = palettes_import(edited, edited_len, &made, &line, &why);
    CHECK(p != NULL);
    AssetStore served;
    if (p) {
        palettes_store(p, &served);
        uint32_t n = 0;
        const uint8_t *got = served.find(served.ctx, e->name, asset_type_names[e->type], &n);
        CHECK(got && n == e->length && got[2] == 31 && got[3] == (was[3] & 0x80));
        CHECK(got && !memcmp(got + 4, was + 4, n - 4) && got[0] == was[0] && got[1] == was[1]);
        const AssetEntry *other = &asset_catalogue[0] == e ? &asset_catalogue[1] : &asset_catalogue[0];
        CHECK(served.find(served.ctx, other->name, asset_type_names[other->type], &n) ==
              made_up_bytes + made_up_at[other - asset_catalogue]);
    }
    palettes_close(p);
    free(edited);
    free(text);

    /* Only the palettes the text names are its own. */
    snprintf(header, sizeof header, "# just one\n[%s]\n", e->name);
    char *one = malloc(strlen(header) + 12 * (e->length / 2) + 1);
    strcpy(one, header);
    for (uint32_t c = 0; c < e->length / 2; c++)
        strcat(one, " 1  2  3\r\n");
    p = palettes_import(one, strlen(one), &made, &line, &why);
    CHECK(p != NULL);
    if (p) {
        palettes_store(p, &served);
        uint32_t n = 0;
        const uint8_t *got = served.find(served.ctx, e->name, asset_type_names[e->type], &n);
        CHECK(got && got[0] == (1 | 2 << 5) && got[1] == (2 >> 3 | 3 << 2));
    }
    palettes_close(p);

    /* Wrong text, refused where it is wrong. */
    char bad[256];
    refused(&made, "1 2 3\n", 1);
    refused(&made, "[no_such_palette]\n", 1);
    refused(&made, "# a comment\n[bit_mask]\n", 2);   /* a resource, not a palette */
    snprintf(bad, sizeof bad, "[%s]\n1 2 32\n", e->name);
    refused(&made, bad, 2);
    snprintf(bad, sizeof bad, "[%s]\n1 2\n", e->name);
    refused(&made, bad, 2);
    snprintf(bad, sizeof bad, "[%s]\n1 2 3 x\n", e->name);
    refused(&made, bad, 2);
    snprintf(bad, sizeof bad, "[%s]\n1 2 3\n", e->name);
    refused(&made, bad, 2);                           /* too few colors */
    char *twice = malloc(2 * strlen(one) + 1);
    strcpy(twice, one);
    strcat(twice, one + strlen("# just one\n"));
    refused(&made, twice, 2 + (unsigned)(e->length / 2) + 1);
    strcat(one, "1 1 1\n");
    refused(&made, one, 2 + (unsigned)(e->length / 2) + 1);   /* too many */
    free(twice);
    free(one);

    the_cartridge();
    free(made_up_bytes);
    if (failures)
        fprintf(stderr, "palettes_test: %d failures\n", failures);
    else
        printf("palettes_test: %u palettes written and read back, an edit served, wrong text refused, OK\n",
               palettes);
    return failures != 0;
}
