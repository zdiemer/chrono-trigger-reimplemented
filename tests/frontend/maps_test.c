/* The maps' text (frontend/maps.h) without a ROM: every packed resource of a
 * store of made-up maps, looks, tilemaps and streams is written and read back
 * as the same bytes; an edited cell is what the store over the text serves,
 * packed again, while the resources the text doesn't name come from the
 * store under it; and text that is wrong is refused at its line. Where the
 * ROM is staged (roms/ct-usa.sfc, from the source directory), every map of
 * the cartridge reads back as its bytes too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "lz.h"
#include "maps.h"

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

static uint8_t *made_up(const AssetEntry *e) { return made_up_bytes + made_up_at[e - asset_catalogue]; }

static const char *codec_of(const AssetEntry *e) { return asset_codec_names[e->codec]; }

static bool is_map_kind(const AssetEntry *e)
{
    const char *c = codec_of(e);
    return !strncmp(c, "lz", 2) && strcmp(c, "lz:2bpp") && strcmp(c, "lz:4bpp") && strcmp(c, "lz:8bpp") &&
           strcmp(c, "lz:4bpp-chunky") && strcmp(c, "lz:event") && strcmp(c, "lz:place-lists");
}

/* A made-up location's map, 16x16 layers (BG3's where `bg3`): its bytes. */
static size_t made_up_map(uint8_t *image, bool bg3, bool plain, unsigned seed)
{
    static const uint8_t header[6] = { 0x00, 0x00, 0x00, 0x00, 0x13, 0x00 };
    memcpy(image, header, 6);
    bg3 = bg3 && !plain;
    if (plain)
        image[4] = 0;
    if (bg3)
        image[1] = 0x80 | 0x20;
    size_t at = 6, layers = bg3 ? 3 : 2;
    for (size_t k = 0; k < layers * 256; k++)
        image[at++] = plain ? 0 : (uint8_t)((k / 16 + seed) % 5);
    /* attributes: a run of 200 tiles, three single tiles, then a run of the rest */
    static const uint8_t attributes[] = { 0x80, 0x00, 0x00, 200, 0x01, 0x00, 0x40, 0x02, 0x80, 0x00,
                                          0x01, 0x00, 0x40, 0x83, 0x00, 0x80, 53 };
    static const uint8_t plain_attributes[] = { 0x80, 0x00, 0x00, 0x00 };
    memcpy(image + at, plain ? plain_attributes : attributes, plain ? sizeof plain_attributes : sizeof attributes);
    return at + (plain ? sizeof plain_attributes : sizeof attributes);
}

/* Fill a resource's slot with made-up bytes of its kind, packed: the most
 * that fits. */
static void fill(const AssetEntry *e, unsigned i)
{
    static uint8_t image[LZ_MOST];
    memset(made_up(e), 0, e->length);
    if (!strcmp(codec_of(e), "lz:field-map")) {
        for (unsigned plain = 0; plain < 2; plain++) {
            size_t len = made_up_map(image, i % 2, plain, i);
            for (unsigned runs = 0; runs < 2; runs++)
                if (lz_pack(image, len, runs, made_up(e), e->length))
                    return;
        }
        CHECK(!"a made-up map fits every map's slot");
        return;
    }
    static const size_t sizes[] = { 203, 64, 19, 8, 1 };
    for (size_t s = 0; s < sizeof sizes / sizeof *sizes; s++) {
        for (size_t k = 0; k < sizes[s]; k++)
            image[k] = (uint8_t)((k % 9) * 0x1D ^ i);
        if (lz_pack(image, sizes[s], i % 2, made_up(e), e->length))
            return;
    }
    CHECK(!"made-up bytes fit every packed slot");
}

static uint8_t *unpacked_of(const uint8_t *bytes, const AssetEntry *e, size_t *len)
{
    static uint8_t out[LZ_MOST];
    bool runs;
    return bytes && lz_unpack(bytes, e->length, out, len, &runs) ? out : NULL;
}

/* Text that must be refused, and the line it is refused at. */
static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    Decoded *d = decoded_import(&map_text, text, strlen(text), base, &line, &why);
    CHECK(d == NULL);
    if (d || line != at)
        fprintf(stderr, "  refused at line %u, not %u (%s): %.70s\n", line, at, why, text);
    decoded_close(d);
}

/* A map's text: header lines, then its layers' rows of `cell`, then attributes. */
static char *map_text_of(const char *header, unsigned layers, const char *cell, const char *attributes)
{
    static char text[1 << 16];
    snprintf(text, sizeof text, "[field_packed_map.1]\npacked long\n%s", header);
    for (unsigned layer = 0; layer < layers; layer++) {
        char line[64];
        snprintf(line, sizeof line, "map bg%u\n", layer + 1);
        strcat(text, line);
        for (unsigned row = 0; row < 16; row++)
            for (unsigned k = 0; k < 16; k++) {
                strcat(text, cell);
                strcat(text, k < 15 ? " " : "\n");
            }
    }
    strcat(text, attributes);
    return text;
}

static const char HEADER[] = "bg1 16x16\nbg2 16x16 drift 00\nbg3 16x16 none drift 00\nscreens 13\ncolor-math 00\n";

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("maps_test: no ROM staged, the cartridge's maps not read back\n");
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
    bool same = size == ASSET_ROM_SIZE && decoded_round_trip(&map_text, &store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("maps_test: every map of the cartridge reads back as its bytes\n");
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
    for (size_t k = 0; k < total; k++)
        made_up_bytes[k] = (uint8_t)(k * 37 + 5);
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        if (is_map_kind(&asset_catalogue[i]))
            fill(&asset_catalogue[i], i);
    AssetStore made = { made_up_find, NULL };
    const AssetEntry *map = named("field_packed_map.3"), *tilemap = named("overworld_top_layer_tilemaps.0"),
                     *looks = named("tile_looks_address.0"), *colors = named("menu_scene_stream_c6fea0.0"),
                     *regions = named("overworld_region_maps.0");
    CHECK(map && tilemap && looks && colors && regions);
    if (!map || !tilemap || !looks || !colors || !regions)
        return 1;

    const char *why = "";
    CHECK(decoded_round_trip(&map_text, &made, &why));
    if (failures)
        fprintf(stderr, "  %s doesn't read back\n", why);

    /* Edits: the map's first BG1 tile and its first tile's attributes, a
     * tilemap entry, a look's top right entry and a region. */
    size_t len = 0;
    char *text = decoded_export(&map_text, &made, &len, &why);
    CHECK(text != NULL);
    if (!text)
        return 1;
    char *m = strstr(text, "[field_packed_map.3]"), *t = strstr(text, "[overworld_top_layer_tilemaps.0]"),
         *l = strstr(text, "[tile_looks_address.0]"), *c = strstr(text, "[menu_scene_stream_c6fea0.0]"),
         *g = strstr(text, "[overworld_region_maps.0]");
    CHECK(m && t && l && c && g);
    if (!m || !t || !l || !c || !g)
        return 1;
    memcpy(strstr(m, "map bg1\n# row 0\n") + 16, "7E", 2);
    memcpy(strstr(m, "attributes\n# row 0\n") + 19, "0A0B0C", 6);
    memcpy(strstr(t, "# row 0\n") + 8, "123:4:h-p", 9);
    memcpy(strstr(l, "# look 000\n") + 11 + 10, "3FF:7:-v-", 9);   /* the first look's top right */
    memcpy(strstr(g, "# row 0\n") + 8, "F", 1);
    unsigned line = 0;
    Decoded *d = decoded_import(&map_text, text, len, &made, &line, &why);
    CHECK(d != NULL);
    if (!d)
        fprintf(stderr, "  line %u: %s\n", line, why);
    AssetStore over;
    if (d) {
        decoded_store(d, &over);
        size_t n = 0, was_n = 0;
        static uint8_t was[LZ_MOST];
        const uint8_t *u = unpacked_of(made_up(map), map, &was_n);
        CHECK(u != NULL);
        if (u)
            memcpy(was, u, was_n);
        u = unpacked_of(served(&over, map), map, &n);
        /* the first tile's attributes leave the run they were in: a single
         * tile, then a run of the rest of it */
        size_t at = 6 + (was[1] & 0x80 ? 768 : 512);
        unsigned run = was[at + 3] ? was[at + 3] : 256;
        uint8_t attributes[] = { 0x0A, 0x0B, 0x0C, 0x80, 0x00, 0x00, (uint8_t)(run - 1) };
        CHECK(u && u[6] == 0x7E && !memcmp(u + 7, was + 7, at - 7));
        CHECK(u && n == was_n + 3 && !memcmp(u + at, attributes, sizeof attributes) &&
              !memcmp(u + at + 7, was + at + 4, was_n - at - 4));
        u = unpacked_of(served(&over, tilemap), tilemap, &n);
        CHECK(u && u[0] == 0x23 && u[1] == (0x01 | 4 << 2 | 0x40 | 0x20));
        u = unpacked_of(served(&over, looks), looks, &n);
        CHECK(u && u[2] == 0xFF && u[3] == (0x03 | 7 << 2 | 0x80));
        u = unpacked_of(served(&over, regions), regions, &n);
        CHECK(u && (u[0] >> 4) == 0xF);
    }
    decoded_close(d);
    free(text);

    /* Only the resources the text names are its own: ground kinds, and two colors. */
    char one[512];
    snprintf(one, sizeof one, "[overworld_ground_kinds.0]\npacked short\n0123\n4567\n"
                              "[menu_scene_stream_c6fea0.0]\npacked long\n31.00.15+ 00.31.00\n");
    const AssetEntry *kinds = named("overworld_ground_kinds.0");
    d = decoded_import(&map_text, one, strlen(one), &made, &line, &why);
    CHECK(d != NULL);
    if (d) {
        decoded_store(d, &over);
        size_t n = 0;
        const uint8_t *u = unpacked_of(served(&over, kinds), kinds, &n);
        CHECK(u && n == 4 && u[0] == 0x01 && u[1] == 0x23 && u[2] == 0x45 && u[3] == 0x67);
        u = unpacked_of(served(&over, colors), colors, &n);
        CHECK(u && n == 4 && u[0] == 31 && u[1] == (15 << 2 | 0x80) && u[2] == 0xE0 && u[3] == 0x03);
        CHECK(served(&over, map) == made_up(map) && served(&over, tilemap) == made_up(tilemap));
    }
    decoded_close(d);

    /* A map read whole from its text, packed again. */
    char *whole = map_text_of(HEADER, 2, "01", "attributes\n000000 000000\n");
    d = decoded_import(&map_text, whole, strlen(whole), &made, &line, &why);
    CHECK(d != NULL);
    if (d) {
        decoded_store(d, &over);
        size_t n = 0;
        const AssetEntry *one_map = named("field_packed_map.1");
        const uint8_t *u = unpacked_of(served(&over, one_map), one_map, &n);
        CHECK(u && n == 6 + 512 + 4 && u[4] == 0x13 && u[6] == 1 && u[517] == 1 && u[518] == 0x80 && u[521] == 2);
    }
    decoded_close(d);

    /* Wrong text, refused where it is wrong. */
    refused(&made, "[bit_mask]\n", 1);                                                   /* not a map */
    refused(&made, "[overworld_ground_kinds.0]\n0123\n", 2);                             /* how it packs unsaid */
    refused(&made, "[overworld_ground_kinds.0]\npacked short\n01234\n", 3);              /* half a byte */
    refused(&made, "[overworld_ground_kinds.0]\npacked short\n012345\n", 3);             /* too many a row */
    refused(&made, "[overworld_ground_kinds.0]\npacked short\n01\n0123\n", 4);           /* a short row not last */
    refused(&made, "[overworld_ground_kinds.0]\npacked short\n01G3\n", 3);
    refused(&made, "[overworld_top_layer_tilemaps.0]\npacked short\n400:0:---\n", 3);   /* tile past 3FF */
    refused(&made, "[overworld_top_layer_tilemaps.0]\npacked short\n001:8:---\n", 3);   /* palette past 7 */
    refused(&made, "[overworld_top_layer_tilemaps.0]\npacked short\n001:0:v--\n", 3);   /* flags out of place */
    refused(&made, "[menu_scene_stream_c6fea0.0]\npacked short\n32.00.00\n", 3);
    refused(&made, "[tile_looks_address.0]\npacked short\n000:0:--- 000:0:---\n", 1);   /* half a look: at its [name] */
    refused(&made, "[tile_looks_address.0]\npacked short\n000:0:---\n", 3);             /* a look is two across */
    refused(&made, "[tile_looks_address.0]\npacked short\nrest 01 02 03 04 05 06 07 08\n", 3);
    refused(&made, "[tile_looks_address.0]\npacked short\nrest 01\n000:0:--- 000:0:---\n", 4);
    refused(&made, map_text_of("bg1 16x17\n", 0, "", ""), 3);
    refused(&made, map_text_of("bg1 16x16\nbg2 16x16 drift\n", 0, "", ""), 4);
    refused(&made, map_text_of("bg1 16x16\nbg2 16x16 drift 00\nbg3 16x16 drift 00 none\n", 0, "", ""), 5);
    refused(&made, map_text_of("bg1 16x16\nbg2 16x16 drift 00\nbg3 16x16 none drift 00\nscreens\n", 0, "", ""), 6);
    refused(&made, map_text_of(HEADER, 0, "", "map bg2\n"), 8);
    refused(&made, map_text_of(HEADER, 1, "01", "map bg2\n01\n"), 26);                   /* a row short of the width */
    refused(&made, map_text_of(HEADER, 2, "01", ""), 1);                                 /* no attributes: at its [name] */
    refused(&made, map_text_of(HEADER, 2, "01", "attributes\n800000\n"), 43);            /* the run bit */
    refused(&made, map_text_of(HEADER, 2, "01", "attributes\nrest 00\n"), 43);
    char *many = map_text_of(HEADER, 2, "01", "attributes\n");
    for (unsigned row = 0; row < 17; row++)
        strcat(many, "000000 000000 000000 000000 000000 000000 000000 000000 "
                     "000000 000000 000000 000000 000000 000000 000000 000000\n");
    refused(&made, many, 43 + 16);                                                       /* more tiles than BG1 */
    /* An edit that doesn't fit: a map unlike itself tile to tile, in a small slot. */
    char *big = malloc(1 << 16);
    snprintf(big, 1 << 16, "[field_packed_map.31]\npacked short\n%smap bg1\n", HEADER);
    uint32_t x = 1;
    for (unsigned layer = 0; layer < 2; layer++) {
        if (layer)
            strcat(big, "map bg2\n");
        for (unsigned k = 0; k < 256; k++) {
            char cell[4];
            x = x * 1103515245u + 12345u;
            snprintf(cell, sizeof cell, "%02X%c", (unsigned)(x >> 24), k % 16 == 15 ? '\n' : ' ');
            strcat(big, cell);
        }
    }
    strcat(big, "attributes\n");
    refused(&made, big, 1);
    free(big);

    the_cartridge();
    free(made_up_bytes);
    if (failures)
        fprintf(stderr, "maps_test: %d failures\n", failures);
    else
        printf("maps_test: every map written and read back, edits served, wrong text refused, OK\n");
    return failures != 0;
}
