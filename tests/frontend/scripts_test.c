/* The event scripts' text (frontend/scripts.h) without a ROM: a made-up
 * script of commands, jumps, functions and bytes no command reads is written
 * and read back as the same bytes; an edited script -- a command added
 * before a jump, a parameter changed -- is what the store over the text
 * serves, packed again; the scripts the text doesn't name come from the
 * store under it; and text that is wrong is refused at its line. Where the
 * ROM is staged (roms/ct-usa.sfc, from the source directory), every script
 * of the cartridge reads back as its bytes too, and with a path the text is
 * written there to look at. A command that says a line shows it beside it.
 * The language's table and the places' layout the
 * text is written from hold what the text relies on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "event/script.h"
#include "lz.h"
#include "overworld/places.h"
#include "scripts.h"

#undef CHECK   /* the event table's row macro; the test's own below */

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

/* A packed resource made of `n` bytes, in its slot, the rest zero. */
static bool pack_into(const AssetEntry *e, const uint8_t *u, size_t n)
{
    memset(made_up(e), 0, e->length);
    return lz_pack(u, n, false, made_up(e), e->length);
}

/* A made-up script: one object, its functions at the code's start, the
 * second at a jump's place; a command, a condition jumping forward, bytes
 * no row reads, and the return. */
static const uint8_t SCRIPT[] = {
    1,                                                   /* objects */
    0x20, 0, 0x22, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0,
    0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0,
    0x0F,                                                /* $20: face up (self) */
    0x17,                                                /* $21: face down */
    0x1A, 0x01, 0x01,                                    /* $22: if_answer 1, jump to $25 */
    0x09,                                                /* $25: take calls */
    0x00,                                                /* return */
};

/* A made-up script that speaks: no objects, a table of strings chosen
 * (strings_f70000, use_text) and its first line said. */
static const uint8_t SAYING[] = { 0, 0xB8, 0x00, 0x00, 0xF7, 0xBB, 0x00, 0x00 };

/* Made-up places: two exits, an event, a halt, two scripts, and a byte after. */
static const uint8_t PLACES[] = {
    2, 0x85, 0x10, 0x01, 0x05, 0x00, 0x08, 0x1E, 0x86, 0x11, 0x02, 0xFF, 0x01, 0x00, 0x00,
    1, 0x90, 0x20, 0x00,
    1, 0x91, 0x21, 0x07,
    2, 0x00, 0x30, 0x10, 0x30,
    0xAB,
};
/* ... and for a slot too small for those, a record of each list, all zero. */
static const uint8_t FEW_PLACES[] = { 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0 };

static char *exported(const AssetStore *s, size_t *len)
{
    const char *why = "";
    char *text = decoded_export(&script_text, s, len, &why);
    CHECK(text != NULL);
    return text;
}

static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    Decoded *d = decoded_import(&script_text, text, strlen(text), base, &line, &why);
    CHECK(d == NULL && line == at);
    if (d || line != at)
        fprintf(stderr, "  refused at line %u, not %u (%s): %s\n", line, at, why, strrchr(text, ':'));
    decoded_close(d);
}

static void the_cartridge(const char *out)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("scripts_test: no ROM staged, the cartridge's scripts not read back\n");
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
    bool same = size == ASSET_ROM_SIZE && decoded_round_trip(&script_text, &store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("scripts_test: every script of the cartridge reads back as its bytes\n");
    if (out) {
        size_t n = 0;
        char *text = decoded_export(&script_text, &store, &n, &why);
        FILE *o = text ? fopen(out, "wb") : NULL;
        if (o) {
            fwrite(text, 1, n, o);
            fclose(o);
        }
        free(text);
    }
    free(file);
}

/* Every jump's distance lies inside its command, at every length the command
 * can take: a row that jumps has no measure, and its jump byte is one of its
 * own. */
static void jumps_lie_inside_their_commands(void)
{
    for (unsigned op = 0; op < 256; op++) {
        const EventOpcode *e = event_opcode((uint8_t)op);
        if (e && e->jump)
            CHECK(e->measure == NULL && e->jump < e->length);
    }
}

/* The places' records are fields of a byte or a word, in order, filling the
 * record. */
static void place_records_are_bytes_and_words(void)
{
    for (unsigned list = 0; list < PLACE_STREAMS; list++) {
        const PlaceStream *l = &overworld_place_streams[list];
        CHECK(l->fields >= 1 && l->fields <= MOST_PLACE_FIELDS && l->field_at[0] == 0);
        for (unsigned f = 0; f < l->fields; f++) {
            unsigned next = f + 1 < l->fields ? l->field_at[f + 1] : l->record_bytes;
            CHECK(next > l->field_at[f] && next - l->field_at[f] <= 2);
        }
    }
}

int main(int argc, char **argv)
{
    jumps_lie_inside_their_commands();
    place_records_are_bytes_and_words();
    size_t total = 0;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        made_up_at[i] = (uint32_t)total;
        total += asset_catalogue[i].length;
    }
    made_up_bytes = calloc(total, 1);
    if (!made_up_bytes)
        return 1;
    /* every script the made-up one and every world map's places the
     * made-up ones, so each reads back */
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        bool named_too;
        if (!script_text.decodes(&asset_catalogue[i], &named_too))
            continue;
        if (!strcmp(asset_codec_names[asset_catalogue[i].codec], "lz:place-lists"))
            CHECK(pack_into(&asset_catalogue[i], PLACES, sizeof PLACES) ||
                  pack_into(&asset_catalogue[i], FEW_PLACES, sizeof FEW_PLACES));
        else
            CHECK(pack_into(&asset_catalogue[i], SCRIPT, sizeof SCRIPT));
    }
    /* ... but one, which says the first line of a table: "Hi", a break, "b" */
    const AssetEntry *saying = named("field_packed_script.2"), *strings = named("strings_f70000"),
                     *first = named("strings_f70000.0");
    CHECK(saying && strings && first);
    if (!saying || !strings || !first)
        return 1;
    CHECK(pack_into(saying, SAYING, sizeof SAYING));
    made_up(strings)[0] = (uint8_t)first->at, made_up(strings)[1] = (uint8_t)(first->at >> 8);
    static const uint8_t HI[] = { 0xA7, 0xC2, 0x05, 0xBB, 0x00 };
    memcpy(made_up(first), HI, sizeof HI);
    AssetStore made = { made_up_find, NULL };
    const AssetEntry *script = named("field_packed_script.0");
    CHECK(script != NULL);
    if (!script)
        return 1;

    const char *why = "";
    CHECK(decoded_round_trip(&script_text, &made, &why));
    size_t len = 0;
    char *text = exported(&made, &len);
    if (!text)
        return 1;
    /* the commands by name, the jump's place and the second function's */
    CHECK(strstr(text, "functions 00 L0020 L0022 L0020") != NULL);
    CHECK(strstr(text, "L0022:\nif_answer answer=01 to=L0025\nL0025:\ntake_calls takes=01\n") != NULL);
    CHECK(strstr(text, "face object=self facing=") != NULL);
    CHECK(strstr(text, "use_text text=F70000\nsay window=00 line=00   # \"Hi b\"\n") != NULL);
    CHECK(strstr(text, "exit 85 10 01 0005 08 1E\nexit 86 11 02 01FF 00 00\nevent 90 20 00\nhalt 91 21 07\n"
                       "script 3000\nscript 3010\nrest AB\n") != NULL);
    free(text);

    /* A world map's places edited: an event added, the rest dropped. */
    const AssetEntry *places = named("overworld_place_lists.0");
    CHECK(places != NULL);
    if (places) {
        char text_of[512];
        snprintf(text_of, sizeof text_of,
                 "[%s]\npacked short\nexit 85 10 01 0005 08 1E\nevent 90 20 00\nevent 92 22 01\nhalt 91 21 07\n"
                 "script 3000\n", places->name);
        unsigned at = 0;
        Decoded *pd = decoded_import(&script_text, text_of, strlen(text_of), &made, &at, &why);
        CHECK(pd != NULL);
        if (pd) {
            AssetStore over;
            decoded_store(pd, &over);
            uint8_t u[64];
            size_t n = 0;
            bool long_runs;
            const uint8_t *packed = served(&over, places);
            static const uint8_t want[] = { 1, 0x85, 0x10, 0x01, 0x05, 0x00, 0x08, 0x1E, 2, 0x90, 0x20, 0x00, 0x92,
                                            0x22, 0x01, 1, 0x91, 0x21, 0x07, 1, 0x00, 0x30 };
            CHECK(packed && lz_unpack(packed, places->length, u, &n, &long_runs) && n == sizeof want &&
                  !memcmp(u, want, sizeof want));
            decoded_close(pd);
        }
        /* a list with no record, which the game reads as 65,536; lists out of order */
        snprintf(text_of, sizeof text_of, "[%s]\npacked short\nexit 85 10 01 0005 08 1E\nhalt 91 21 07\n",
                 places->name);
        refused(&made, text_of, 4);
        snprintf(text_of, sizeof text_of, "[%s]\npacked short\nevent 90 20 00\nexit 85 10 01 0005 08 1E\n",
                 places->name);
        refused(&made, text_of, 3);
        snprintf(text_of, sizeof text_of, "[%s]\npacked short\nexit 85 10 01 05 08 1E\n", places->name);
        refused(&made, text_of, 3);
    }

    /* An edited script: a command put before the condition, which moves the
     * jump's place and the second function's on; and a parameter changed. */
    char edit[1024];
    snprintf(edit, sizeof edit,
             "[%s]\npacked short\nobjects 1\nfunctions 00 L0020 L0022 L0020 L0020 L0020 L0020 L0020 L0020"
             " L0020 L0020 L0020 L0020 L0020 L0020 L0020 L0020\n"
             "L0020:\nface object=self facing=00\nface object=self facing=01\ntake_calls takes=00\n"
             "L0022:\nif_answer answer=02 to=L0025\nL0025:\ntake_calls takes=01\nreturn_from_function\n",
             script->name);
    unsigned line = 0;
    Decoded *d = decoded_import(&script_text, edit, strlen(edit), &made, &line, &why);
    CHECK(d != NULL);
    if (!d) {
        fprintf(stderr, "  line %u: %s\n", line, why);
    } else {
        AssetStore over;
        decoded_store(d, &over);
        const uint8_t *packed = served(&over, script);
        uint8_t u[256];
        size_t n = 0;
        bool long_runs;
        CHECK(packed && lz_unpack(packed, script->length, u, &n, &long_runs));
        static const uint8_t want[] = { 0x0F, 0x17, 0x08, 0x1A, 0x02, 0x01, 0x09, 0x00 };
        CHECK(n == 33 + sizeof want && !memcmp(u + 33, want, sizeof want));
        CHECK(u[3] == 0x23 && u[4] == 0);   /* the second function moved on past the command put in */
        const AssetEntry *other = named("field_packed_script.1");
        CHECK(other && served(&over, other) == made_up(other));
        decoded_close(d);
    }

    /* Wrong text, refused where it is wrong. */
    char wrong[1024];
    const char *table = "objects 1\nfunctions 00 L0020 L0020 L0020 L0020 L0020 L0020 L0020 L0020"
                        " L0020 L0020 L0020 L0020 L0020 L0020 L0020 L0020\nL0020:\n";
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\n%sface object=self facing=009\n", script->name, table);
    refused(&made, wrong, 6);                                            /* no form has that */
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\n%sfly away=01\n", script->name, table);
    refused(&made, wrong, 6);                                            /* no such command */
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\n%sif_answer answer=01 to=L0099\n", script->name, table);
    refused(&made, wrong, 6);                                            /* no such place */
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\n%sL0020:\n", script->name, table);
    refused(&made, wrong, 6);                                            /* a place named twice */
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\n%sbytes 0G\n", script->name, table);
    refused(&made, wrong, 6);
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\nobjects 1\nfunctions 00 L0020\n", script->name);
    refused(&made, wrong, 4);                                            /* 15 functions short */
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\n%sif_answer answer=01 to=L0200\n", script->name, table);
    {
        /* a place more than 255 bytes on */
        char far[4096];
        snprintf(far, sizeof far, "%s", wrong);
        for (int k = 0; k < 20; k++)
            strcat(far, "bytes 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n");
        strcat(far, "L0200:\nreturn_from_function\n");
        refused(&made, far, 6);
    }
    snprintf(wrong, sizeof wrong, "[%s]\npacked short\n%sface object=self facing=00 @1D\n", script->name, table);
    refused(&made, wrong, 6);                                            /* not that opcode's form */
    the_cartridge(argc > 1 ? argv[1] : NULL);
    free(made_up_bytes);
    if (failures == 0)
        printf("scripts_test: every script written and read back, edits served, wrong text refused, OK\n");
    return failures != 0;
}
