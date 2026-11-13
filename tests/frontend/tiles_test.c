/* The graphics' text (frontend/tiles.h) without a ROM: every tile resource
 * of a store of made-up tiles, packed and not, is written and read back as
 * the same bytes; an edited pixel is what the store over the text serves,
 * packed again where the resource is packed, while the resources the text
 * doesn't name come from the store under it; and text that is wrong is
 * refused at its line. Where the ROM is staged (roms/ct-usa.sfc, from the
 * source directory), every graphics resource of the cartridge reads back as
 * its bytes too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "lz.h"
#include "tiles.h"

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

static bool is_packed_tiles(const AssetEntry *e)
{
    const char *codec = asset_codec_names[e->codec];
    return !strcmp(codec, "lz:2bpp") || !strcmp(codec, "lz:4bpp");
}

/* Text that must be refused, and the line it is refused at. */
static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    Decoded *d = decoded_import(&tile_text, text, strlen(text), base, &line, &why);
    CHECK(d == NULL);
    if (d || line != at)
        fprintf(stderr, "  refused at line %u, not %u (%s): %.60s\n", line, at, why, text);
    decoded_close(d);
}

/* A packed resource of `tiles` 2bpp tiles, every line given as `line`. */
static char *packed_text(const char *name, const char *form, unsigned tiles, const char *line)
{
    static char text[1 << 16];
    snprintf(text, sizeof text, "[%s]\n%s\n", name, form);
    for (unsigned first = 0; first < tiles; first += 16) {
        unsigned across = tiles - first < 16 ? tiles - first : 16;
        for (unsigned y = 0; y < 8; y++) {
            for (unsigned k = 0; k < across; k++) {
                strcat(text, line);
                strcat(text, k + 1 < across ? " " : "\n");
            }
        }
    }
    return text;
}

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("tiles_test: no ROM staged, the cartridge's graphics not read back\n");
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
    bool same = size == ASSET_ROM_SIZE && decoded_round_trip(&tile_text, &store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("tiles_test: every graphics resource of the cartridge reads back as its bytes\n");
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
    /* A packed resource's slot holds made-up tiles, packed: a few, made with
     * a pattern that packs into the smallest slot. */
    static uint8_t image[LZ_MOST];
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        if (!is_packed_tiles(e))
            continue;
        size_t len = 4 * 32 + (i % 3) * 32;
        for (size_t k = 0; k < len; k++)
            image[k] = (uint8_t)((k % 7) * 0x11 ^ i);
        memset(made_up(e), 0, e->length);
        CHECK(lz_pack(image, len, i % 2, made_up(e), e->length) != 0);
    }
    /* The credits' letters unpack to 256 two-plane tiles of 0, which pack
     * small: room for an edit to outgrow. */
    const AssetEntry *slot = named("field_credits_letters_packed");
    memset(image, 0, 4096);
    CHECK(slot && lz_pack(image, 4096, true, made_up(slot), slot->length) != 0);
    /* The battlefield's pool, cut by made-up descriptors into tiles of two
     * and three planes by turns up to 64 bytes short of its end, each tile
     * named twice (mirrored the second time); and a descriptor past it. */
    const AssetEntry *pool = named("battlefield_tile_pool"), *descriptors = named("battle_tile_descriptors");
    CHECK(pool && descriptors);
    if (!pool || !descriptors)
        return 1;
    uint32_t pool_tiles = 0;
    for (uint32_t k = 0; k < descriptors->length; k++)   /* the rest name the first tile */
        made_up(descriptors)[k] = k % 2 ? 0x20 : 0x00;
    for (uint32_t at = 0, k = 0; at + 24 <= pool->length - 64 && 4 * k + 6 <= descriptors->length; k++) {
        bool two = k % 2 == 0;
        uint16_t word = (uint16_t)(at / 8 | (two ? 0x2000 : 0));
        made_up(descriptors)[4 * k] = made_up(descriptors)[4 * k + 2] = (uint8_t)word;
        made_up(descriptors)[4 * k + 1] = (uint8_t)(word >> 8);
        made_up(descriptors)[4 * k + 3] = (uint8_t)((word | 0x4000) >> 8);
        at += two ? 16 : 24;
        pool_tiles = k + 1;
    }
    made_up(descriptors)[descriptors->length - 2] = 0xFF;
    made_up(descriptors)[descriptors->length - 1] = 0x1F;
    AssetStore made = { made_up_find, NULL };
    const AssetEntry *flat = named("battlefield_tiles"), *sprites = named("sprite_tiles_d3"),
                     *packed = named("setup_character_graphics.0"), *letters = named("field_credits_letters_packed");
    CHECK(flat && sprites && packed && letters);
    if (!flat || !sprites || !packed || !letters)
        return 1;

    const char *why = "";
    CHECK(decoded_round_trip(&tile_text, &made, &why));

    /* An edit: the battlefield's first tile's top left pixel color 3, the
     * first character's first tile's top right pixel color F, and the pool's
     * second tile's (three planes) top left pixel color 7. */
    size_t len = 0;
    char *text = decoded_export(&tile_text, &made, &len, &why);
    CHECK(text != NULL);
    if (!text)
        return 1;
    char *first_flat = strstr(text, "[battlefield_tiles]");
    char *first_packed = strstr(text, "[setup_character_graphics.0]");
    CHECK(first_flat && first_packed);
    if (!first_flat || !first_packed)
        return 1;
    first_flat = strstr(first_flat, "# 0000\n") + 7;
    first_packed = strstr(first_packed, "# 0000\n") + 7;
    first_flat[0] = '3';
    first_packed[7] = 'F';
    char *first_pool = strstr(strstr(text, "[battlefield_tile_pool]"), "# 0000\n");
    CHECK(first_pool != NULL);
    if (!first_pool)
        return 1;
    first_pool[7 + 9] = '7';
    unsigned line = 0;
    Decoded *d = decoded_import(&tile_text, text, len, &made, &line, &why);
    CHECK(d != NULL);
    AssetStore over;
    if (d) {
        decoded_store(d, &over);
        const uint8_t *got = served(&over, flat), *was = made_up(flat);
        CHECK(got && got[0] == (was[0] | 0x80) && got[1] == (was[1] | 0x80) && !memcmp(got + 2, was + 2, flat->length - 2));
        size_t got_len = 0, was_len = 0;
        bool runs, was_runs;
        static uint8_t unpacked[LZ_MOST];
        got = served(&over, packed);
        CHECK(got && lz_unpack(got, packed->length, unpacked, &got_len, &runs));
        CHECK(lz_unpack(made_up(packed), packed->length, image, &was_len, &was_runs));
        CHECK(got_len == was_len && runs == was_runs);
        CHECK((unpacked[0] & 1) && (unpacked[1] & 1) && (unpacked[16] & 1) && (unpacked[17] & 1));
        CHECK(!memcmp(unpacked + 2, image + 2, 14) && !memcmp(unpacked + 18, image + 18, was_len - 18));
        CHECK(served(&over, sprites) && !memcmp(served(&over, sprites), made_up(sprites), sprites->length));
        got = served(&over, pool);
        was = made_up(pool);
        CHECK(got && !memcmp(got, was, 16) && got[16] == (was[16] | 0x80) && got[17] == (was[17] | 0x80));
        CHECK(got && got[32] == (was[32] | 0x80) && !memcmp(got + 33, was + 33, pool->length - 33));
    }
    decoded_close(d);
    free(text);

    /* Only the resources the text names are its own: the credits' letters,
     * four tiles of them all color 1, packed again and served, the rest
     * from under it. */
    char *one = packed_text("field_credits_letters_packed", "packed long  # its runs", 4, "11111111");
    d = decoded_import(&tile_text, one, strlen(one), &made, &line, &why);
    CHECK(d != NULL);
    if (d) {
        decoded_store(d, &over);
        const uint8_t *got = served(&over, letters);
        size_t got_len = 0;
        bool runs = false;
        static uint8_t unpacked[LZ_MOST];
        CHECK(got && lz_unpack(got, letters->length, unpacked, &got_len, &runs) && runs && got_len == 64);
        CHECK(unpacked[0] == 0xFF && unpacked[1] == 0 && unpacked[62] == 0xFF && unpacked[63] == 0);
        CHECK(served(&over, packed) == made_up(packed) && served(&over, flat) == made_up(flat));
    }
    decoded_close(d);

    /* Wrong text, refused where it is wrong. */
    refused(&made, "........\n", 1);
    refused(&made, "[bit_mask]\n", 1);                                                  /* not graphics */
    refused(&made, packed_text("field_credits_letters_packed", "packed", 1, "........"), 2);
    refused(&made, packed_text("field_credits_letters_packed", "packed sideways", 1, "........"), 2);
    refused(&made, "[field_credits_letters_packed]\n........\n", 2);                  /* how it packs unsaid */
    refused(&made, packed_text("field_credits_letters_packed", "packed short", 1, "....4..."), 3);
    refused(&made, packed_text("field_credits_letters_packed", "packed short", 1, "......."), 3);
    refused(&made, packed_text("field_credits_letters_packed", "packed short", 1, "........."), 3);
    refused(&made, packed_text("sprite_tiles_d3", "packed short", 1, "........"), 2);    /* not packed */
    refused(&made, "[sprite_tiles_d3]\n........ ........\n........\n", 3);           /* a line short a tile */
    refused(&made, "[sprite_tiles_d3]\n.......G\n", 2);
    refused(&made, "[sprite_tiles_d3]\n........\n", 1);                               /* a row cut short: at its [name] */
    refused(&made, packed_text("sprite_tiles_d3", "# flat", 17, "........"), 1);        /* too few tiles */
    char *rows = packed_text("sprite_tiles_d3", "# flat", 15, "1.2.3.1.");
    for (unsigned y = 0; y < 8; y++)
        strcat(rows, "1.2.3.1.\n");
    refused(&made, rows, 11);                                                           /* a short row not last */
    /* Too much for the slot: tiles that don't pack small. */
    char *big = malloc(1 << 20);
    snprintf(big, 1 << 20, "[field_credits_letters_packed]\npacked short\n");
    uint32_t x = 1;
    for (unsigned t = 0; t < 256; t += 16) {
        for (unsigned y = 0; y < 8; y++) {
            for (unsigned k = 0; k < 16; k++) {
                char tile[10];
                for (unsigned p = 0; p < 8; p++) {
                    x = x * 1103515245u + 12345u;
                    tile[p] = ".123"[x >> 30];
                }
                tile[8] = k < 15 ? ' ' : '\n';
                tile[9] = 0;
                strcat(big, tile);
            }
        }
    }
    refused(&made, big, 1);                                      /* doesn't fit: at its [name] */
    /* More tiles than the stream beneath unpacks to, at the row past them. */
    refused(&made, packed_text("field_credits_letters_packed", "packed long", 257, "........"), 2 + 16 * 8 + 1);
    /* The pool: a two-plane tile's pixel past 3, more tiles than it is cut
     * into, bytes after its rest, and the rest wanting. */
    refused(&made, "[battlefield_tile_pool]\n4.......\n", 2);
    char *pool_text = malloc(1 << 20);
    snprintf(pool_text, 1 << 20, "[battlefield_tile_pool]\n");
    for (uint32_t first = 0; first <= pool_tiles; first += 16) {
        uint32_t across = pool_tiles + 1 - first < 16 ? pool_tiles + 1 - first : 16;
        for (unsigned y = 0; y < 8; y++)
            for (uint32_t k = 0; k < across; k++)
                strcat(pool_text, k + 1 < across ? "........ " : "........\n");
    }
    refused(&made, pool_text, 2 + pool_tiles / 16 * 8);              /* a tile past the cut */
    snprintf(pool_text, 1 << 20, "[battlefield_tile_pool]\nrest 00\n........\n");
    refused(&made, pool_text, 3);
    snprintf(pool_text, 1 << 20, "[battlefield_tile_pool]\nrest 00\n");
    refused(&made, pool_text, 1);                                      /* too few: at its [name] */
    free(pool_text);
    free(big);

    the_cartridge();
    free(made_up_bytes);
    if (failures)
        fprintf(stderr, "tiles_test: %d failures\n", failures);
    else
        printf("tiles_test: every tile resource written and read back, edits served, wrong text refused, OK\n");
    return failures != 0;
}
