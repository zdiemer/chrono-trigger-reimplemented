/* The tables of records (frontend/records.h) without a ROM: made-up sprite
 * records and exits are written and read back as the same bytes; an edited
 * record and a location given more exits are what the store over the text
 * serves, the rest from the store under it; and text that is wrong is
 * refused at its line. Where the ROM is staged (roms/ct-usa.sfc, from the
 * source directory), every table of the cartridge reads back too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "records.h"

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

static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    Decoded *d = decoded_import(&record_text, text, strlen(text), base, &line, &why);
    CHECK(d == NULL);
    if (d || line != at)
        fprintf(stderr, "  refused at line %u, not %u (%s): %.70s\n", line, at, why, text);
    decoded_close(d);
}

/* Exits for every third location, one or two each, the table's end the
 * slot's or short of it. */
static void made_up_exits(uint8_t *b, uint32_t len)
{
    enum { LOCATIONS = 0x200, EXITS_AT = 2 * (LOCATIONS + 1) };
    memset(b, 0, len);
    uint32_t at = EXITS_AT;
    for (unsigned l = 0; l <= LOCATIONS; l++) {
        b[2 * l] = (uint8_t)at;
        b[2 * l + 1] = (uint8_t)(at >> 8);
        for (unsigned k = 0; l < LOCATIONS && l % 3 == 0 && k < 1 + l % 2 && at + 7 <= len - 7; k++, at += 7) {
            uint8_t exit[7] = { (uint8_t)l, (uint8_t)k, 0x81, (uint8_t)l, (uint8_t)(0x80 | l >> 8), 5, 9 };
            memcpy(b + at, exit, 7);
        }
    }
}

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("records_test: no ROM staged, the cartridge's records not read back\n");
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
    bool same = size == ASSET_ROM_SIZE && decoded_round_trip(&record_text, &store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("records_test: every table of records of the cartridge reads back as its bytes\n");
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
        made_up_bytes[k] = (uint8_t)(k * 29 + 3);
    const AssetEntry *exits = named("location_exit_lists"), *sprites = named("field_sprite_records"),
                     *enemies = named("battle_sprite_records");
    CHECK(exits && sprites && enemies);
    if (!exits || !sprites || !enemies)
        return 1;
    made_up_exits(made_up(exits), exits->length);
    AssetStore made = { made_up_find, NULL };
    const char *why = "";
    CHECK(decoded_round_trip(&record_text, &made, &why));

    /* A sprite's record edited; location 1 given an exit, the rest the same. */
    size_t len = 0;
    char *text = decoded_export(&record_text, &made, &len, &why);
    CHECK(text != NULL);
    if (!text)
        return 1;
    char *first = strstr(strstr(text, "[field_sprite_records]"), "# 000: ");
    memcpy(strchr(first, '\n') + 1, "AB CD", 5);
    char *bigger = malloc(len + 64), *location3 = strstr(text, "location 003\n");
    CHECK(location3 != NULL);
    if (!location3)
        return 1;
    snprintf(bigger, len + 64, "%.*slocation 001\n10 20 00 8123 04 05\n%s", (int)(location3 - text), text, location3);
    unsigned line = 0;
    Decoded *d = decoded_import(&record_text, bigger, strlen(bigger), &made, &line, &why);
    CHECK(d != NULL);
    if (!d)
        fprintf(stderr, "  line %u: %s\n", line, why);
    AssetStore over;
    if (d) {
        decoded_store(d, &over);
        const uint8_t *got = served(&over, sprites), *was = made_up(sprites);
        CHECK(got && got[0] == 0xAB && got[1] == 0xCD && !memcmp(got + 2, was + 2, sprites->length - 2));
        got = served(&over, exits);
        was = made_up(exits);
        /* location 0's exit, then 1's new one, the rest a place on */
        static const uint8_t added[] = { 0x10, 0x20, 0x00, 0x23, 0x81, 0x04, 0x05 };
        CHECK(got && got[2] == was[2] && got[4] == was[4] + 7 && got[6] == was[6] + 7);
        CHECK(got && !memcmp(got + 1026, was + 1026, 7) && !memcmp(got + 1033, added, 7));
        CHECK(got && !memcmp(got + 1040, was + 1033, 7 * 10));
        CHECK(served(&over, enemies) && !memcmp(served(&over, enemies), made_up(enemies), enemies->length));
    }
    decoded_close(d);
    free(bigger);
    free(text);

    refused(&made, "[location_exit_lists]\n10 20 00 8123 04 05\n", 2);         /* before any location */
    refused(&made, "[location_exit_lists]\nlocation 200\n", 2);
    refused(&made, "[location_exit_lists]\nlocation 002\nlocation 001\n", 3);
    refused(&made, "[location_exit_lists]\nlocation 002\n10 20 00 123 04 05\n", 3);
    refused(&made, "[field_sprite_records]\n01 02 03 04\n", 2);
    refused(&made, "[field_sprite_records]\n01 02 03 04 05\n", 1);                /* too few records */
    refused(&made, "[battle_sprite_records]\n01 02 03 04 05\n", 2);

    the_cartridge();
    free(made_up_bytes);
    if (failures)
        fprintf(stderr, "records_test: %d failures\n", failures);
    else
        printf("records_test: every table written and read back, edits served, wrong text refused, OK\n");
    return failures != 0;
}
