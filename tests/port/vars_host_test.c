/* Native module storage, its address projection and cartridge save compatibility.
 * No ROM: only the documented save-layout metadata is supplied below. The
 * actual save/load/checksum routines and the native video/save backends run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charge.h"
#include "cpu_io.h"
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

/* New Game+ party operations are outside this save-format test. */
void party_equip(GameState *g, uint8_t c, uint8_t item) { (void)g; (void)c; (void)item; abort(); }
void party_fill_hp(GameState *g) { (void)g; abort(); }
void party_fill_mp(GameState *g) { (void)g; abort(); }
void party_wear_it_shows(GameState *g) { (void)g; abort(); }
bool party_record_shows_characters(const GameState *g) { (void)g; abort(); }

static uint8_t read_game(void *ctx, uint32_t at) { return state_host_read8(ctx, at); }

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
    vid_host_start(&pf, read_game, g);
    vid_send_vram(&pf, PF_AT(0), 0, PF_GAME(0x7E0100), 3,
                  &(VidVramPort){ .step = 1, .after_high = true });
    const VidFrame *frame = vid_host_picture(&pf);
    CHECK(frame->vram[0] == 0x34 && frame->vram[1] == 0x12 && frame->vram[2] == 0x56);
    vid_host_stop(&pf);
    CHECK(state_host_strays(g, NULL, NULL) == 0);
    state_host_destroy(g);
    state_host_destroy(other);
}

static unsigned range_fault_count(void)
{
    unsigned count, total = 0;
    const VarsRangeFault *faults = vars_range_faults(&count);
    for (unsigned i = 0; i < count; i++)
        total += (unsigned)faults[i].count;
    return total;
}

static void alias_views(void)
{
    GameState *g = state_host_create(NULL);
    CHECK(g);
    if (!g)
        return;
    VarsHost *h = state_host_vars(g);
    /* Internal views and cross-mode views must share initial contents and
     * partial writes, not just synchronize at a mode transition. */
    set_character_records_hp(g, 2, 0xABCD);
    CHECK(h->core.characters[2].hp == 0xABCD);
    CHECK(character_records_record_byte(g, 2, 4) == 0xAB);
    set_character_records_record_byte(g, 2, 4, 0x12);
    CHECK(character_records_hp(g, 2) == 0x12CD);
    set_palette_animations_frames(g, 0, 0x1234);
    CHECK(palette_animations_fade_mode(g, 0) == 0x34);
    CHECK(palette_animations_fade_level(g, 0) == 0x12);
    set_warp_pixel_set_masks(g, 1, 0x67);
    CHECK(h->palettes.palette_animations[0].frames == 0x1267);
    set_palette_animations_fade_level(g, 0, 0x89);
    CHECK(warp_pixel_set_masks(g, 2) == 0x89);
    set_field_screen_scroll_target_x(g, 0x42);
    CHECK(battle_object_x(g, 2) == 0x42);
    set_battle_object_x(g, 3, 0x24);
    CHECK(field_screen_scroll_target_y(g) == 0x24);
    set_scene_planet_fade_steps(g, 5);
    CHECK(h->field.field_window_shape_corners[3].x == 5);
    set_menu_past_the_names(g, 0x563412);
    CHECK(h->core.party_gold.low == 0x3412 && h->core.party_gold.high == 0x56);
    set_party_gold_high(g, 0x78);
    CHECK(menu_past_the_names(g) == 0x783412);
    set_oam_buffer_tile(g, 3, 0x9A);
    CHECK(h->core.sprites[3].tile == 0x9A);
    /* The core's buffer is the owner: the address view reads its write, and
     * the scene's table beside it keeps its own bytes. */
    CHECK(state_host_read8(g, 0x7E070E) == 0x9A);
    hw_port_seek(g, 0x070E, 0);
    hw_port_write(g, 0xBC);
    CHECK(oam_buffer_tile(g, 3) == 0xBC);
    Platform pf = {0};
    vid_host_start(&pf, read_game, g);
    vid_send_vram(&pf, PF_AT(0), 0, PF_GAME(0x7E070E), 1,
                  &(VidVramPort){ .step = 1, .after_high = true });
    CHECK(vid_host_picture(&pf)->vram[0] == 0xBC);
    vid_host_stop(&pf);
    h->data.decompress_source = 0xFEDCBA;
    CHECK(decompress_source(g) == 0xFEDCBA);
    state_host_write8(g, 0x800301, 0x12);
    CHECK(h->data.decompress_source == 0xFE12BA);

    /* Deliberate inventory and character tails reach the following owner.
     * Checked accesses diagnose that same reach, without C array overruns. */
    vars_range_reset();
    set_inventory_items_unchecked(g, INVENTORY_ITEMS_COUNT, 0x36);
    CHECK(h->menu.menu_inventory_past_the_items[0] == 0x36);
    set_character_records_hp_unchecked(g, CHARACTER_RECORDS_COUNT, 0x4567);
    CHECK(state_host_read8(g, 0x7E2833) == 0x67 && state_host_read8(g, 0x7E2834) == 0x45);
    CHECK(range_fault_count() == 0);
    set_inventory_items(g, INVENTORY_ITEMS_COUNT, 0x73);
    CHECK(h->menu.menu_inventory_past_the_items[0] == 0x73);
    CHECK(range_fault_count() == 1);
    set_character_records_record_byte(g, 0, CHARACTER_RECORDS_RECORD_BYTES, 0xA5);
    CHECK(h->core.characters[1].id == 0xA5);
    CHECK(range_fault_count() == 2);
    set_oam_buffer_x(g, OAM_BUFFER_COUNT, 0x5A);
    CHECK(oam_high_table_byte(g, 0) == 0x5A);
    CHECK(range_fault_count() == 3);
    vars_range_reset();
    state_host_destroy(g);
}

static uint8_t pattern(uint32_t address)
{
    return (uint8_t)((address * 2654435761u) >> 24);
}

static void records_and_composed_views(void)
{
    GameState *g = state_host_create(NULL);
    CHECK(g);
    if (!g)
        return;
    VarsHost *h = state_host_vars(g);
    /* Script offsets and typed combatant fields are two views of one record. */
    set_battle_stats_hp(g, 10, 0x1234);
    CHECK(h->battle.combatants[10].hp == 0x1234);
    CHECK(battle_stats_block_byte(g, 10, 3) == 0x34);
    set_battle_stats_block_byte(g, 10, 4, 0xAB);
    CHECK(h->battle.combatants[10].hp == 0xAB34);
    h->battle.combatants[10].experience = 0x654321;
    CHECK(battle_stats_block_byte(g, 10, 0x15) == 0x65);
    CHECK(state_host_read8(g, 0x7E6340) == 0x21);

    /* Menu direct pages are alternate views over field objects. Unaligned
     * words and 24-bit pointers still span those typed fields correctly. */
    for (unsigned record = 0; record < 9; record++) {
        set_menu_record_graphics(g, record, 0x123400u + record);
        CHECK(menu_record_graphics(g, record) == 0x123400u + record);
        CHECK(state_host_read8(g, 0x7E1804 + record * 64) == record);
    }
    set_menu_record_graphics(g, 8, 0xFE123456);
    CHECK(menu_record_graphics(g, 8) == 0x123456);
    CHECK(state_host_read8(g, 0x7E1A06) == 0x12);
    set_menu_record_tile(g, 8, 0xABCD);
    CHECK(menu_record_colours(g, 8) == 0xAB);
    set_menu_record_x(g, 8, 0x2345);
    CHECK(menu_record_x_low(g, 8) == 0x45);
    CHECK(menu_record_x(g, 7) == 0x5555);
    vars_range_reset();
    set_menu_record_x(g, 9, 0x6789);
    CHECK(state_host_read8(g, 0x7E1A4F) == 0x89);
    CHECK(range_fault_count() == 1);
    vars_range_reset();

    SceneProgramPage pages[] = { scene_program_own(), scene_program_channel(0), scene_program_channel(1) };
    for (unsigned i = 0; i < 3; i++) {
        set_scene_call_product(g, pages[i], 0x12345678u + i);
        set_scene_call_quotient(g, pages[i], 0xABCDEF00u + i);
        /* This four-byte view crosses two owning words within each record. */
        set_scene_call_bump_push(g, pages[i], 0x76543210u + i);
        CHECK(scene_call_product(g, pages[i]) == (0x32105678u + i * 0x10001u));
        CHECK(scene_call_quotient(g, pages[i]) == 0xABCD7654u);
        CHECK(scene_call_bump_push(g, pages[i]) == 0x76543210u + i);
    }
    CHECK(state_host_read8(g, 0x7E0AF6) == 0x11);
    CHECK(state_host_read8(g, 0x7E0BF6) == 0x12);
    /* The battle's product keeps its own word beside the menu tasks whose bytes
     * the original reuses for it: writing it leaves the tasks alone, and the
     * address view answers with whichever owner wrote last. */
    set_menu_tasks_state(g, 0, 0x11);
    set_battle_product(g, 0xBEEF);
    CHECK(h->battle.battle_product == 0xBEEF);
    CHECK(h->menu.tasks[0].state == 0x11);
    CHECK(menu_tasks_state(g, 0) == 0x11);
    CHECK(state_host_read8(g, 0x7E00AF) == 0xEF);
    CHECK(state_host_read8(g, 0x7E00B0) == 0xBE);
    set_menu_tasks_state(g, 0, 0x22);
    CHECK(state_host_read8(g, 0x7E00B0) == 0x22);
    CHECK(battle_product(g) == 0xBEEF);
    /* A write by address is the original's one byte: every owner takes it. */
    state_host_write8(g, 0x7E00B0, 0x33);
    CHECK(h->menu.tasks[0].state == 0x33);
    CHECK(battle_product(g) == 0x33EF);
    /* Reading a byte another owner wrote last is a crossing: counted, not
     * answered with that owner's value. */
    vars_host_crossings_reset();
    set_menu_tasks_state(g, 0, 0x44);
    CHECK(battle_product(g) == 0x33EF);
    CHECK(vars_host_crossings(NULL, NULL) == 1);
    vars_host_crossings_reset();
    CHECK(vars_host_crossings(NULL, NULL) == 0);
    CHECK(range_fault_count() == 0);
    state_host_destroy(g);
}

static void projection_boundaries(void)
{
    GameState *g = state_host_create(NULL);
    CHECK(g);
    if (!g)
        return;
    /* Fill first, then read: duplicate fields, page dispatch mistakes and
     * stride/padding holes cannot hide behind a write immediately read back. */
    for (uint32_t a = 0x7E0000; a < 0x800000; a++)
        state_host_write8(g, a, pattern(a));
    unsigned retired = 0;
    for (uint32_t a = 0x7E0000; a < 0x800000; a++) {
        uint8_t value = 0;
        bool owned = vars_host_read8(state_host_vars_const(g), a, &value);
        CHECK(state_host_read8(g, a) == (owned ? pattern(a) : 0));
        if (!owned) retired++;
    }
    CHECK(retired == 1416); /* 257 retired sound cells, 1,138 unused/padding bytes and 21 of scratch nothing reads */
    CHECK(character_records_hp(g, 6) == (uint16_t)(pattern(0x7E27E3) | pattern(0x7E27E4) << 8));
    CHECK(palette_animations_frames(g, 11) == (uint16_t)(pattern(0x7E05AA) | pattern(0x7E05AB) << 8));
    /* Views cross resource portions and typed records at awkward boundaries.
     * Compare every row/element with the serialized address after the fill. */
    for (unsigned row = 0; row < 64; row++)
        for (unsigned layer = 0; layer < 3; layer++)
            for (unsigned column = 0; column < 64; column++)
                CHECK(field_layer_maps(g, layer, row, column) == pattern(0x7E3000 + row * 256 + layer * 64 + column));
    for (unsigned slot = 0; slot < 64; slot++)
        for (unsigned at = 0; at < 64; at++)
            CHECK(menu_object_slots_bytes_byte(g, slot, at) == pattern(0x7E0B30 + slot * 64 + at));
    for (unsigned item = 0; item < 256; item++) {
        CHECK(battle_item_list_item(g, item) == pattern(0x7E1580 + item * 5));
        CHECK(battle_item_list_spare(g, item) == pattern(0x7E1584 + item * 5));
    }
    uint8_t value;
    CHECK(!vars_host_read8(state_host_vars_const(g), 0x7DFFFF, &value));
    CHECK(!vars_host_read8(state_host_vars_const(g), 0x800000, &value));
    state_host_destroy(g);
}

static void promoted_scalar_storage(void)
{
    GameState *g = state_host_create(NULL);
    CHECK(g);
    if (!g) return;
    VarsHost *h = state_host_vars(g);
    set_scene_sprite_index(g, 0x1234);
    set_scene_sprite_high_pointer(g, 0x5678);
    set_scene_sprite_offset_x(g, 0x9ABC);
    set_scene_sprite_offset_y(g, 0xDE);
    CHECK(h->scene.scene_sprite_index == 0x1234);
    CHECK(h->scene.scene_sprite_high_pointer == 0x5678);
    CHECK(h->scene.scene_sprite_offset_x == 0x9ABC);
    CHECK(h->scene.scene_sprite_offset_y == 0xDE);
    for (unsigned member = 0; member < 3; member++) {
        set_field_kept_party_x(g, member, (uint16_t)(0x1234 + member));
        CHECK(h->field.field_kept_party_x[member] == 0x1234 + member);
        h->field.field_kept_party_x[member] = (uint16_t)(0xABCD + member);
        CHECK(state_host_read8(g, 0x7F1D09 + 2 * member) == 0xCD + member);
        CHECK(state_host_read8(g, 0x7F1D0A + 2 * member) == 0xAB);
    }
    state_host_destroy(g);
}

static void designed_records(void)
{
    GameState *g = state_host_create(NULL);
    CHECK(g);
    if (!g) return;
    VarsHost *h = state_host_vars(g);
    FieldObjectState *object = &h->field.objects[17];
    object->x.fine = 0x23;
    object->x.tile = 0x45;
    object->y.tile = 0x67;
    object->role = 9;
    CHECK(field_object_x_fine(g, 17) == 0x23);
    CHECK(field_object_x_tile(g, 17) == 0x45);
    CHECK(field_object_y_tile(g, 17) == 0x67);
    CHECK(field_object_role(g, 17) == 9);
    CHECK(state_host_read8(g, 0x7E1823) == 0x45);
    set_field_object_velocity_y(g, 17, 0xABCD);
    CHECK(object->velocity_y == 0xABCD);
    CHECK(h->field.objects[16].velocity_y == 0x5555);

    /* The menu record's graphics pointer has its own field beside the field
     * objects' coordinates the original keeps in the same bytes. */
    h->field.objects[2].x.fine = 0x77;
    set_menu_record_graphics(g, 0, 0x563412);
    CHECK(h->menu.menu_record_graphics[0] == 0x563412);
    CHECK(h->field.objects[2].x.fine == 0x77);
    CHECK(state_host_read8(g, 0x7E1804) == 0x12);
    h->battle.draw_objects[4] = (BattleDrawObject){ 12, 34, 2 };
    CHECK(battle_object_x(g, 4) == 12 && battle_object_y(g, 4) == 34);
    CHECK(battle_object_part_count(g, 4) == 2);
    set_character_records_record_byte(g, 3, 0x0B, 77);
    CHECK(h->core.characters[3].base_power == 77);
    h->core.characters[3].name.letters[0] = 0x42;
    CHECK(character_names_byte(g, 3, 0) == 0x42);
    h->menu.window.lines[2] = 0xFACE;
    CHECK(menu_window_line_values(g, 2) == 0xFACE);
    h->menu.tasks[2].countdown = 13;
    CHECK(menu_tasks_countdown(g, 2) == 13);
    state_host_destroy(g);
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
        /* The cartridge projection enters actual owner storage, including a
         * whole-record owner whose typed fields are aliases. */
        for (unsigned byte = 0; byte < CHARACTER_RECORDS_RECORD_BYTES; byte++)
            CHECK(character_records_record_byte(g, 0, byte) == cartridge[slot * SAVED_GAME_BYTES + 0x200 + byte]);
        CHECK(state_host_vars_const(g)->core.characters[0].hp ==
              (uint16_t)(cartridge[slot * SAVED_GAME_BYTES + 0x203] | cartridge[slot * SAVED_GAME_BYTES + 0x204] << 8));
        CHECK(memcmp(state_host_vars_const(g)->core.inventory_items,
                     cartridge + slot * SAVED_GAME_BYTES, INVENTORY_ITEMS_COUNT) == 0);
        state_host_vars(g)->core.characters[0].hp = 0x1234;
        state_host_vars(g)->core.inventory_items[0] = 0x56;
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
        CHECK(exported[slot * SAVED_GAME_BYTES + 0x203] == 0x34);
        CHECK(exported[slot * SAVED_GAME_BYTES + 0x204] == 0x12);
        CHECK(exported[slot * SAVED_GAME_BYTES] == 0x56);
        static const struct { unsigned offset, length; } reserved[] = {
            { 0x45C, 36 }, { 0x481, 255 }, { 0x58A, 6 }, { 0x5A1, 11 }, { 0x80A, 4 }
        };
        for (unsigned r = 0; r < sizeof reserved / sizeof *reserved; r++)
            CHECK(memcmp(exported + slot * SAVED_GAME_BYTES + reserved[r].offset,
                         cartridge + slot * SAVED_GAME_BYTES + reserved[r].offset,
                         reserved[r].length) == 0);
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
            CHECK(character_records_hp(loaded, 0) == 0x1234 && inventory_items(loaded, 0) == 0x56);
            state_host_destroy(loaded);
        }
    }
    CHECK(held > 0);
    state_host_destroy(g);
}

int main(int argc, char **argv)
{
    address_view();
    alias_views();
    records_and_composed_views();
    projection_boundaries();
    promoted_scalar_storage();
    designed_records();
    cartridge_save(argc > 1 ? argv[1] : "saves/fantasyanime/ctsave12-side_quests1.srm");
    printf("vars_host_test: %d failures\n", failures);
    return failures ? 1 : 0;
}
