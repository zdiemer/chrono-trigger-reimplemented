/* The battle's animation scripts and the menus' object scripts as text
 * (frontend/commands.h) without a ROM: made-up scripts of each language --
 * commands with operands, fixed parameters, a word, a byte of the running
 * object's, a measured command and its data, a byte no command reads, a
 * command two rows read alike -- are written and read back as the same
 * bytes; an edited script is what the store over the text serves; and text
 * that is wrong, or would change a script's length, is refused at its line.
 * Where the ROM is staged (roms/ct-usa.sfc), every script of the cartridge
 * reads back as its bytes too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "commands.h"
#include "scene/scene_data.h"

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

/* A battle animation script: a wait, a sound, a screen effect giving three
 * settings, a byte no command reads, a pose two opcodes play alike (the
 * second written @06), then its ends. */
static const uint8_t BATTLE[] = { 0x20, 0x08, 0x78, 0x4E, 0x80, 0x03, 0xAA, 0xBB, 0xEA, 0x06, 0x03 };

/* A menu object script: placed at a word each way, a loop on a byte of its
 * own, a return, then a forgetting. */
static const uint8_t MENU[] = { 0x2C, 0x34, 0x12, 0x78, 0x56, 0x1B, 0x05, 0xFE, 0x37 };

static Decoded *imported(const AssetStore *base, const char *text, unsigned *line, const char **why)
{
    return decoded_import(&command_text, text, strlen(text), base, line, why);
}

static void refused(const AssetStore *base, const char *text, unsigned at)
{
    unsigned line = 0;
    const char *why = "";
    Decoded *d = imported(base, text, &line, &why);
    CHECK(d == NULL && line == at);
    if (d || line != at)
        fprintf(stderr, "  refused at line %u, not %u (%s)\n", line, at, why);
    decoded_close(d);
}

/* The text of the battle script: its lines given, then `end` to its length. */
static void battle_text(char *out, size_t room, const AssetEntry *e, const char *lines, unsigned bytes, int more)
{
    snprintf(out, room, "[%s]\n%s", e->name, lines);
    for (int k = 0; k < (int)e->length - (int)bytes + more; k++)
        strncat(out, "end\n", room - strlen(out) - 1);
}

static void the_cartridge(void)
{
    FILE *f = fopen("roms/ct-usa.sfc", "rb");
    if (!f) {
        printf("commands_test: no ROM staged, the cartridge's scripts not read back\n");
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
    bool same = size == ASSET_ROM_SIZE && decoded_round_trip(&command_text, &store, &why);
    CHECK(same);
    if (!same)
        fprintf(stderr, "  the cartridge's %s doesn't read back\n", why);
    else
        printf("commands_test: every script of the cartridge reads back as its bytes\n");
    /* the scenes' copied resources lie where the table at $FE:0000 points */
    enum { TABLE = 0x3E0000 };   /* $FE:0000 in the image */
    for (unsigned k = 0; size == ASSET_ROM_SIZE && k < SCENE_RESOURCE_COUNT; k++) {
        const SceneResource *r = &SCENE_RESOURCE_ROWS[k];
        uint32_t pointer = (uint32_t)(rom[TABLE + 3 * k] | rom[TABLE + 3 * k + 1] << 8 | rom[TABLE + 3 * k + 2] << 16);
        CHECK(!r->copied || asset_catalogue[r->asset].at + r->offset == pointer);
    }
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
    const AssetEntry *battle = named("battle_animation_calls"), *menu = named("epoch_boarded_in_a_scene");
    CHECK(made_up_bytes && battle && menu && battle->length > sizeof BATTLE && menu->length > sizeof MENU);
    if (!made_up_bytes || !battle || !menu)
        return 1;
    memcpy(made_up(battle), BATTLE, sizeof BATTLE);
    memcpy(made_up(menu), MENU, sizeof MENU);
    /* The enemies' scripts: one enemy's, starting after the table of where
     * each starts: a block always held, its attack, and the section's end;
     * an attack four bytes, as the table of their lengths says. */
    const AssetEntry *enemies = named("enemy_scripts"), *lengths = named("enemy_action_length");
    CHECK(enemies && lengths);
    if (!enemies || !lengths)
        return 1;
    uint8_t *script = made_up(enemies);
    uint16_t first = (uint16_t)((enemies->at & 0xFFFF) + 2);
    const uint8_t ENEMY[] = { (uint8_t)first, (uint8_t)(first >> 8), 0x00, 0x00, 0x00, 0x00, 0xFE,
                              0x01, 0xAA, 0xBB, 0xCC, 0xFE, 0xFF };
    memcpy(script, ENEMY, sizeof ENEMY);
    made_up(lengths)[0x01] = 4;
    AssetStore made = { made_up_find, NULL };
    const char *why = "";
    CHECK(decoded_round_trip(&command_text, &made, &why));

    size_t len = 0;
    char *text = decoded_export(&command_text, &made, &len, &why);
    CHECK(text != NULL);
    if (!text)
        return 1;
    CHECK(strstr(text, "wait_frames frames=08\nplay_sound voices=") != NULL);
    CHECK(strstr(text, " sound=4E\nscreen_effect settings=03 data=AA.BB\nbytes EA\nplay_and_wait animation=03 @06\n"));
    CHECK(strstr(text, "place x=1234 y=5678\nloop byte=slot:05 by=FE\nreturn_from_call\nforget_everything\n"));
    char enemy_text[128];
    snprintf(enemy_text, sizeof enemy_text, "starts %04X\n# enemy 00\nif always 00 00 00\nthen\ndo attack AA BB CC\n"
                                            "done\nend\n", first);
    CHECK(strstr(text, enemy_text) != NULL);
    free(text);

    /* An edited wait served, the rest as it was. */
    static char edit[8192];
    battle_text(edit, sizeof edit, battle, "wait_frames frames=10\nplay_sound sound=4E\n", 4, 0);
    unsigned line = 0;
    Decoded *d = imported(&made, edit, &line, &why);
    CHECK(d != NULL);
    if (!d) {
        fprintf(stderr, "  line %u: %s\n", line, why);
    } else {
        AssetStore over;
        decoded_store(d, &over);
        const uint8_t *got = served(&over, battle);
        static const uint8_t want[] = { 0x20, 0x10, 0x78, 0x4E, 0x00 };
        CHECK(got && !memcmp(got, want, sizeof want));
        CHECK(served(&over, menu) == made_up(menu));
        decoded_close(d);
    }

    /* Wrong text, refused where it is wrong; a script's length kept. */
    battle_text(edit, sizeof edit, battle, "wait_frames frames=10\nfly away=01\n", 3, 0);
    refused(&made, edit, 3);                                                 /* no such command */
    battle_text(edit, sizeof edit, battle, "wait_frames frames=100\n", 2, 0);
    refused(&made, edit, 2);                                                 /* no form has that */
    battle_text(edit, sizeof edit, battle, "wait_frames frames=10 @21\n", 2, 0);
    refused(&made, edit, 2);                                                 /* not the command's opcode */
    battle_text(edit, sizeof edit, battle, "bytes 0G\n", 1, 0);
    refused(&made, edit, 2);
    battle_text(edit, sizeof edit, battle, "wait_frames frames=10\n", 2, 1);
    refused(&made, edit, (unsigned)battle->length - 2 + 3);                  /* one byte longer */
    battle_text(edit, sizeof edit, battle, "wait_frames frames=10\n", 2, -1);
    refused(&made, edit, 1);                                                 /* one byte shorter: at its [name] */
    snprintf(edit, sizeof edit, "[enemy_scripts]\nstarts %04X\nif always 00 00\n", first);
    refused(&made, edit, 3);                                                 /* a condition's bytes short */
    snprintf(edit, sizeof edit, "[enemy_scripts]\nstarts %04X\nif always 00 00 00\nthen\ndo fly 00\n", first);
    refused(&made, edit, 5);                                                 /* no such action */
    snprintf(edit, sizeof edit, "[enemy_scripts]\nstarts %04X\nif always 00 00 00\nthen\ndo attack AA BB\n", first);
    refused(&made, edit, 5);                                                 /* an attack is four bytes */
    /* lines longer than any condition or action: refused, not read past their room */
    snprintf(edit, sizeof edit, "[enemy_scripts]\nstarts %04X\nif always", first);
    for (int k = 0; k < 40; k++)
        strcat(edit, " 00");
    strcat(edit, "\n");
    refused(&made, edit, 3);
    snprintf(edit, sizeof edit, "[enemy_scripts]\nstarts %04X\nif always 00 00 00\nthen\ndo attack", first);
    for (int k = 0; k < 40; k++)
        strcat(edit, " AA");
    strcat(edit, "\n");
    refused(&made, edit, 5);
    snprintf(edit, sizeof edit, "[enemy_scripts]\nstarts %04X\nif always 00 00 00 @05\n", first);
    refused(&made, edit, 3);                                                 /* kind $05 isn't `always` */

    the_cartridge();
    free(made_up_bytes);
    if (failures) {
        fprintf(stderr, "commands_test: %d failures\n", failures);
        return 1;
    }
    printf("commands_test: every script written and read back, edits served, wrong text refused, OK\n");
    return 0;
}
