/* Native module storage, its address projection and cartridge save compatibility.
 * No ROM: only the documented save-layout metadata is supplied below. The
 * actual save/load/checksum routines and the native video/save backends run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/party.h"
#include "menu/saves.h"
#include "platform/native/host.h"
#include "port/state_host.h"
#include "port/vars_ram.h"
#include "rom.h"
#include "vars.h"
#include "wram.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

/* The format's projection, expressed as addresses and lengths, not asset bytes. */
static const struct { uint32_t address; uint16_t length; } saved_runs[] = {
    { 0x7E2400, 1456 }, { 0x7E2C23, 51 }, { 0x7E0400, 16 }, { 0x000100, 16 },
    { 0x7F0000, 512 }, { 0x00027E, 2 }, { 0x000290, 17 }, { 0x7E2C7C, 30 }
};

uint8_t rom_read8(const GameState *g, uint32_t at)
{
    (void)g;
    if (at >= 0xFFFA7B && at < 0xFFFA7B + 5 * 8) {
        unsigned run = (at - 0xFFFA7B) / 5, byte = (at - 0xFFFA7B) % 5;
        return byte < 3 ? (uint8_t)(saved_runs[run].address >> (8 * byte))
                        : (uint8_t)(saved_runs[run].length >> (8 * (byte - 3)));
    }
    if (at >= 0xFFFBE1 && at < 0xFFFBE1 + 6) {
        unsigned slot = (at - 0xFFFBE1) / 2, byte = (at - 0xFFFBE1) % 2;
        return (uint8_t)((0x6000 + slot * SAVED_GAME_BYTES) >> (8 * byte));
    }
    fprintf(stderr, "unexpected asset read %06X\n", (unsigned)at);
    abort();
}
uint16_t rom_read16(const GameState *g, uint32_t at)
{
    return (uint16_t)(rom_read8(g, at) | rom_read8(g, at + 1) << 8);
}

/* New Game+ and machine wrappers are linked but never part of these tests. */
void party_equip(GameState *g, uint8_t c, uint8_t item) { (void)g; (void)c; (void)item; abort(); }
void party_fill_hp(GameState *g) { (void)g; abort(); }
void party_fill_mp(GameState *g) { (void)g; abort(); }
void party_wear_it_shows(GameState *g) { (void)g; abort(); }
bool party_record_shows_characters(const GameState *g) { (void)g; abort(); }
bool menu_on_page_work_ram(Snes *s) { (void)s; abort(); }

static uint8_t read_game(void *ctx, uint32_t at) { return state_host_read8(ctx, at); }
static void write_game(void *ctx, uint32_t at, uint8_t value) { state_host_write8(ctx, at, value); }

static void address_view(void)
{
    GameState *g = state_host_create(NULL), *other = state_host_create(NULL);
    CHECK(g && other);
    if (!g || !other)
        exit(1);
    CHECK(location_id(g) == 0x5555 && location_x(g) == 0x55);
    set_location_id(g, 0x1234);
    CHECK(state_host_read8(g, 0x7E0100) == 0x34);
    CHECK(state_host_read8(g, 0x800101) == 0x12);
    CHECK(location_id(other) == 0x5555);
    CHECK(state_host_record_time(other));
    set_location_id(other, 0x1234);
    CHECK(location_id(other) == 0x1234);
    vars_peek(true);
    CHECK(location_id(other) == 0x1234);
    vars_peek(false);
    CHECK(state_host_read8(other, 0x0100) == 0x34);
    unsigned row_count;
    const StateHostTime *rows = state_host_time(other, &row_count);
    uint64_t clocks = 0;
    for (unsigned i = 0; i < row_count; i++)
        clocks += rows[i].bus_clocks;
    CHECK(clocks == 32); /* two word accesses; guard and address lookups are free */
    state_host_write8(g, 0x000101, 0xAB);
    CHECK(location_id(g) == 0xAB34);
    /* Directly inspect/mutate the generated field: it is the sole storage. */
    CHECK(state_host_vars_const(g)->locations.location.id == 0xAB34);
    state_host_vars(g)->locations.location.id = 0xFE12;
    CHECK(location_id(g) == 0xFE12 && wram_read16(g, 0x0100) == 0xFE12);
    wram_write16(g, 0x7E0101, 0x6789); /* spans the map and x fields */
    CHECK(location_id(g) == 0x8912 && location_x(g) == 0x67);
    hw_port_seek(g, 0x0100, 0);
    CHECK(hw_port_read(g) == 0x12);
    hw_port_write(g, 0x45);
    CHECK(location_id(g) == 0x4512);
    wram_copy(g, 0x7E0100, 0x7E0112, 5);
    CHECK(location_next_id(g) == 0x4512 && location_next_x(g) == 0x67);
    wram_fill16(g, 0x0104, 2, 0xFACE); /* crosses record boundaries */
    CHECK(location_flags(g) == 0xCE && location_previous_id(g) == 0xCEFA);
    wram_fill8(g, 0x0100, 16, 0);
    CHECK(location_id(g) == 0 && location_stored_flags(g) == 0);
    CHECK(location_next_id(g) == 0x4512); /* outside that clear */
    set_location_id(g, 0x1234);
    set_location_x(g, 0x56);
    Platform pf = {0};
    vid_host_start(&pf, read_game, write_game, g);
    vid_send_vram(&pf, PF_AT(0), 0, PF_GAME(0x7E0100), 3,
                  &(VidVramPort){ .step = 1, .after_high = true });
    const VidFrame *frame = vid_host_picture(&pf);
    CHECK(frame->vram[0] == 0x34 && frame->vram[1] == 0x12 && frame->vram[2] == 0x56);
    vid_host_stop(&pf);
    CHECK(state_host_strays(g, NULL, NULL) == 0);
    state_host_destroy(g);
    state_host_destroy(other);
}

static void cartridge_save(const char *path)
{
    static uint8_t cartridge[STATE_HOST_SRAM_BYTES], exported[STATE_HOST_SRAM_BYTES];
    FILE *file = fopen(path, "rb");
    CHECK(file);
    if (!file)
        return;
    CHECK(fread(cartridge, 1, sizeof cartridge, file) == sizeof cartridge);
    CHECK(fgetc(file) == EOF);
    fclose(file);
    GameState *g = state_host_create(cartridge);
    CHECK(g);
    if (!g)
        return;
    Platform pf = {0};
    pf.save.bytes = state_host_sram(g);
    unsigned held = 0;
    for (uint8_t slot = 0; slot < MENU_SAVE_SLOTS; slot++) {
        if (saves_check_slot(g, &pf, slot) != SAVED_GAME_HELD)
            continue;
        held++;
        set_location_next_id(g, 0x5678);
        saves_load_game(g, &pf, slot);
        unsigned offset = slot * SAVED_GAME_BYTES + 0x5F3;
        CHECK(location_id(g) == (uint16_t)(cartridge[offset] | cartridge[offset + 1] << 8));
        CHECK(location_x(g) == cartridge[offset + 2]);
        CHECK(location_previous_id(g) == (uint16_t)(cartridge[offset + 5] | cartridge[offset + 6] << 8));
        CHECK(location_stored_flags(g) == cartridge[offset + 14]);
        CHECK(location_next_id(g) == 0x5678);
        set_location_id(g, 0x0123);
        set_location_x(g, 0x45);
        set_location_previous_id(g, 0x0234);
        set_location_stored_flags(g, 0x67);
        saves_save_game(g, &pf, slot);
        CHECK(saves_check_slot(g, &pf, slot) == SAVED_GAME_HELD);
        bool dirty = false;
        memcpy(exported, sv_host_bytes(&pf, &dirty), sizeof exported);
        CHECK(dirty && exported[offset] == 0x23 && exported[offset + 1] == 1);
        CHECK(exported[offset + 2] == 0x45 && exported[offset + 14] == 0x67);
        /* A fresh state's load consumes the serialized bytes, not the old fields. */
        GameState *loaded = state_host_create(exported);
        CHECK(loaded);
        if (loaded) {
            Platform second = {0};
            second.save.bytes = state_host_sram(loaded);
            saves_load_game(loaded, &second, slot);
            CHECK(location_id(loaded) == 0x0123 && location_previous_id(loaded) == 0x0234);
            CHECK(location_x(loaded) == 0x45 && location_stored_flags(loaded) == 0x67);
            CHECK(location_next_id(loaded) == 0x5555);
            state_host_destroy(loaded);
        }
    }
    CHECK(held > 0);
    state_host_destroy(g);
}

int main(int argc, char **argv)
{
    address_view();
    cartridge_save(argc > 1 ? argv[1] : "saves/fantasyanime/ctsave12-side_quests1.srm");
    printf("vars_host_test: %d failures\n", failures);
    return failures ? 1 : 0;
}
