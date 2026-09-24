/* Battle: the screen a battle starts with.
 *
 * The battle's first entry lays out what the screen is built from: the
 * tables the sprite drawing looks a sprite's high-table bits up in, the
 * battlefield grid as the location's map gives it (battle/battlefield.c marks
 * the combatants on it every frame), the window and menu tilemaps blank, and
 * the colors every battle starts with. The second entry, once the combatants
 * are known, gets the objects' graphics ready: each object's sprite entries
 * and their size, the enemies' palettes in their sprites, the small tiles the
 * window and the cursors use, the cursors' sprites and colors, and where each
 * object's graphics go in VRAM when the load steps upload them
 * (battle/display.c). */
#include "battle/battle.h"
#include "battle/setup.h"

#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The sprite drawing's tables ------------------------------------------------------------ */

/* The OAM buffer's low table, byte by byte, and a high-table byte for every
 * four sprites. */
enum { OAM_LOW_BYTES = 0x200, OAM_ENTRY_BYTES = 4, SPRITES_PER_HIGH_BYTE = 4, OAM_BYTES_PER_HIGH_BYTE = 16 };

/* For each byte of the OAM buffer, the bit its sprite's X high bit is in its
 * high-table byte, the mask that clears that bit, and which high-table byte
 * it is: what the drawing looks up rather than works out a sprite at a time. */
// $CC:E414
void setup_sprite_tables(GameState *g)
{
    for (uint16_t at = 0; at < OAM_LOW_BYTES; at++) {
        uint8_t place = (uint8_t)(at / OAM_ENTRY_BYTES % SPRITES_PER_HIGH_BYTE);
        set_battle_oam_x_high_bits(g, at, setup_sprite_x_high_bit(g, place));
        set_battle_oam_high_bits(g, at, setup_sprite_x_high_clear(g, place));
        set_battle_oam_high_offsets(g, at, (uint8_t)(at / OAM_BYTES_PER_HIGH_BYTE));
    }
}

/* ---- The battlefield grid ------------------------------------------------------------------- */

/* A grid of 16 by 16 cells: two rows along the top and three along the bottom
 * from the edge rows, the rows between from the middle row. Then each cell
 * takes the location's map under it: the upper layer's top bit as it is, and
 * the lower layer's (which blocks a move on the field) as the cell's
 * blocked bit. */
enum { GRID_SIDE = 16, TOP_EDGE_ROWS = 2, BOTTOM_EDGE_ROW = 13, MAP_BIT = 0x80, CELL_UPPER = 0x80, CELL_BLOCKED = 0x40 };

static uint8_t starting_cell(const GameState *g, unsigned row, unsigned column)
{
    if (row < TOP_EDGE_ROWS)
        return setup_grid_edge_cell(g, (uint16_t)(row * GRID_SIDE + column));
    if (row < BOTTOM_EDGE_ROW)
        return setup_grid_middle_cell(g, (uint8_t)column);
    return setup_grid_edge_cell(g, (uint16_t)((row - BOTTOM_EDGE_ROW) * GRID_SIDE + column));
}

static uint8_t cell_from_the_map(const GameState *g, unsigned cell)
{
    return (uint8_t)((battle_location_upper_attributes(g, cell) & MAP_BIT ? CELL_UPPER : 0)
                     | (battle_location_lower_attributes(g, cell) & MAP_BIT ? CELL_BLOCKED : 0));
}

static void leave_last_map_bit(GameState *g)
{
    setup_leave(g, 0x80, (uint8_t)((battle_location_lower_attributes(g, 0) & MAP_BIT) >> 1));
}

// $CC:E466
void setup_base_grid(GameState *g)
{
    for (unsigned row = 0; row < GRID_SIDE; row++)
        for (unsigned column = 0; column < GRID_SIDE; column++) {
            unsigned cell = row * GRID_SIDE + column;
            set_battle_base_grid(g, cell, starting_cell(g, row, column) | cell_from_the_map(g, cell));
        }
    ORIGINAL_EFFECT(leave_last_map_bit(g));
}

/* ---- The tilemaps --------------------------------------------------------------------------- */

/* A blank tile of the window's layer: the attributes put it in palette 2, in
 * front, among the second 256 tiles, at tile 0 of those; the load tilemap's
 * blank is the last of them. */
enum { BLANK_TILE = 0x00, WINDOW_ATTRIBUTES = 0x29, LOAD_BLANK_TILE = 0xFF };
enum { MENU_LIST_TILES = 0x280, MENU_TILEMAP_TILES = 0xC0, WINDOW_TILEMAP_TILES = 0x260, FRAME_TILES = 0xC0 };
enum { LOAD_TILEMAP_TILES = 0x80, LOAD_TILEMAP_2_BYTES = 0x200, LOAD_TILEMAP_2_FROM_ROM = 0xC0 };

/* The load tilemap's four runs of sixteen tiles, counting up from their first
 * tile, from its 33rd tile on. */
enum { FIRST_RUN_AT = 0x20, RUN_TILES = 16 };
static const uint8_t run_first_tiles[] = { 0x00, 0x30, 0x10, 0x40 };

static uint16_t tile_word(uint8_t tile, uint8_t attributes) { return (uint16_t)(attributes << 8 | tile); }

// $CC:E4BA
void setup_tilemaps(GameState *g)
{
    for (unsigned i = 0; i < MENU_LIST_TILES; i++) {
        set_battle_menu_list_tiles(g, i, BLANK_TILE);
        set_battle_menu_list_attributes(g, i, WINDOW_ATTRIBUTES);
    }
    for (unsigned i = 0; i < MENU_TILEMAP_TILES; i++) {
        set_battle_menu_tilemap_tile(g, i, BLANK_TILE);
        set_battle_menu_tilemap_attributes(g, i, WINDOW_ATTRIBUTES);
    }
    for (unsigned i = 0; i < WINDOW_TILEMAP_TILES; i++) {
        set_battle_window_tilemap_tile(g, i, BLANK_TILE);
        set_battle_window_tilemap_attributes(g, i, WINDOW_ATTRIBUTES);
    }
    for (unsigned i = 0; i < FRAME_TILES; i++) {
        set_battle_window_frames_tilemap_tile(g, i, setup_window_frames_tilemap(g, (uint16_t)(2 * i)));
        set_battle_window_frames_tilemap_attributes(g, i, setup_window_frames_tilemap(g, (uint16_t)(2 * i + 1)));
    }
    for (unsigned i = 0; i < LOAD_TILEMAP_TILES; i++)
        set_battle_load_tilemap(g, i, tile_word(LOAD_BLANK_TILE, WINDOW_ATTRIBUTES));
    for (unsigned run = 0; run < sizeof run_first_tiles; run++)
        for (unsigned i = 0; i < RUN_TILES; i++)
            set_battle_load_tilemap(g, FIRST_RUN_AT + run * RUN_TILES + i,
                                    tile_word((uint8_t)(run_first_tiles[run] + i), WINDOW_ATTRIBUTES));
    for (uint16_t at = 0; at < LOAD_TILEMAP_2_FROM_ROM; at++)
        set_battle_load_tilemap_2(g, at, setup_load_tilemap_start(g, at));
    for (uint16_t at = LOAD_TILEMAP_2_FROM_ROM; at < LOAD_TILEMAP_2_BYTES; at += 2) {
        set_battle_load_tilemap_2(g, at, LOAD_BLANK_TILE);
        set_battle_load_tilemap_2(g, at + 1u, WINDOW_ATTRIBUTES);
    }
}

/* ---- The colors ----------------------------------------------------------------------------- */

/* Three runs of the palette every battle starts with: where each run starts
 * and how many colors it has. */
static const struct {
    uint8_t first, colors;
} fixed_runs[] = { { 8, 4 }, { 12, 4 }, { 157, 3 } };
enum { PALETTE_COLORS = 256 };

/* The party's sprite palettes: 5, 6 and 7, as the attribute bits hold them. */
enum { FIRST_PARTY_PALETTE = 0x0A, PALETTE_STEP = 2 };

// $CC:E6B7
void setup_colors(GameState *g)
{
    for (unsigned run = 0; run < sizeof fixed_runs / sizeof fixed_runs[0]; run++)
        for (unsigned color = 0; color < fixed_runs[run].colors; color++)
            set_palette_base(g, fixed_runs[run].first + color, setup_fixed_color(g, run, color));
    for (unsigned color = 0; color < PALETTE_COLORS; color++)
        set_palette_working(g, color, palette_base(g, color));
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t palette = (uint8_t)(FIRST_PARTY_PALETTE + PALETTE_STEP * member);
        set_battle_object_palette_initial(g, member, palette);
        set_battle_object_palette(g, member, palette);
    }
}

/* ---- The objects' sprites (the second entry) -------------------------------------------- */

enum { SPRITE_ENTRIES = 44, SPRITE_ENTRIES_PER_OBJECT = 4 };

uint8_t setup_extra_parts(uint8_t kind)
{
    enum { MOST_EXTRA_PARTS = 5 };
    return kind < 3 ? kind : MOST_EXTRA_PARTS;
}

/* Every object's four sprite entries at their object's position, with the
 * tiles and attributes they start with. */
// $CC:ED87
void setup_sprite_entries(GameState *g)
{
    for (unsigned entry = 0; entry < SPRITE_ENTRIES; entry++) {
        set_battle_object_sprite_entries_x_offset(g, entry, 0);
        set_battle_object_sprite_entries_y_offset(g, entry, 0);
        set_battle_object_sprite_entries_tile(g, entry, setup_sprite_entry_byte(g, (uint16_t)(2 * entry)));
        set_battle_object_sprite_entries_attributes(g, entry, setup_sprite_entry_byte(g, (uint16_t)(2 * entry + 1)));
    }
}

/* Whether every combatant's extra parts end within the slots, as the game's
 * formations do: the size bits and the enemies' sprite rows step over them,
 * and parts past the last slot would step past the tables. */
bool setup_objects_parts_fit(const GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        if (battle_slot_present(g, slot) && slot + setup_extra_parts(battle_object_kind(g, slot)) >= BATTLE_COMBATANTS)
            return false;
    return true;
}

/* What the original counts the extra parts down in, for the last combatant it
 * met: none left after one or two, two fewer than the kind after five. */
static uint8_t parts_left(uint8_t kind) { return kind < 3 ? 0 : (uint8_t)(kind - 2); }

/* Every present combatant's sprites and its extra parts' are large. */
enum { ALL_LARGE = 0xAA };

// $CC:EDAA
void setup_size_bits(GameState *g)
{
    uint8_t last_kind = 0;
    bool any = false;
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!battle_slot_present(g, slot))
            continue;
        uint8_t kind = battle_object_kind(g, slot);
        unsigned extra = setup_extra_parts(kind);
        for (unsigned part = 0; part <= extra; part++)
            set_battle_object_size_bits(g, slot + part, ALL_LARGE);
        slot += extra;
        last_kind = kind;
        any = true;
    }
    if (any)
        ORIGINAL_EFFECT(setup_leave(g, 0x80, parts_left(last_kind)));
}

/* An enemy's sprites, and its extra parts', take its palette. The palettes of
 * the enemies' sprites follow the party's by 16 attribute steps. */
enum { ENEMY_PALETTE_BASE = 0x20, ENEMY_ROW_ENTRY = SPRITE_ENTRIES_PER_OBJECT * BATTLE_FIRST_ENEMY };

static void leave_last_enemy_row(GameState *g, uint8_t attributes, uint8_t kind)
{
    setup_leave(g, 0x80, attributes);
    setup_leave(g, 0x82, parts_left(kind));
}

// $CC:EDE9
void setup_enemy_sprite_attributes(GameState *g)
{
    uint8_t last_attributes = 0, last_kind = 0;
    bool any = false;
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        unsigned slot = BATTLE_FIRST_ENEMY + enemy;
        if (!battle_slot_present(g, slot))
            continue;
        uint8_t attributes = (uint8_t)(battle_object_palette_initial(g, slot) + ENEMY_PALETTE_BASE);
        uint8_t kind = battle_object_kind(g, slot);
        unsigned extra = setup_extra_parts(kind);
        for (unsigned row = enemy; row <= enemy + extra; row++)
            for (unsigned sprite = 0; sprite < SPRITE_ENTRIES_PER_OBJECT; sprite++)
                set_battle_object_sprite_entries_attributes(g, ENEMY_ROW_ENTRY + row * SPRITE_ENTRIES_PER_OBJECT + sprite,
                                                            attributes);
        enemy += extra;
        last_attributes = attributes;
        last_kind = kind;
        any = true;
    }
    if (any)
        ORIGINAL_EFFECT(leave_last_enemy_row(g, last_attributes, last_kind));
}

/* ---- Tiles, the cursors' sprites and colors ------------------------------------------------- */

/* The sixteen two-bitplane tiles go in as four-bitplane tiles whose upper
 * planes are empty, then the 64 four-bitplane tiles as they are, all after
 * the first 512 bytes of the decoded tiles. */
enum { TILES_AT = 0x200, TWO_PLANE_TILES = 16, TWO_PLANE_BYTES = 16, FOUR_PLANE_BYTES = 32, FOUR_PLANE_TILE_BYTES = 0x800 };

// $CC:EE8C
void setup_tiles(GameState *g)
{
    for (unsigned tile = 0; tile < TWO_PLANE_TILES; tile++)
        for (unsigned i = 0; i < FOUR_PLANE_BYTES; i++)
            set_battle_decoded_tiles(g, TILES_AT + tile * FOUR_PLANE_BYTES + i,
                                     i < TWO_PLANE_BYTES ? setup_two_plane_tiles(g, (uint16_t)(tile * TWO_PLANE_BYTES + i)) : 0);
    for (uint16_t i = 0; i < FOUR_PLANE_TILE_BYTES; i++)
        set_battle_decoded_tiles(g, TILES_AT + TWO_PLANE_TILES * FOUR_PLANE_BYTES + i, setup_four_plane_tiles(g, i));
    ORIGINAL_EFFECT(setup_leave_word(g, 0x80, 0));
}

/* The first four sprites are the cursors': at the corner, small, with tile 0
 * in front of everything in palette 1, until the menus place them. */
enum { CURSOR_SPRITES = 4, CURSOR_ATTRIBUTES = 0x32, CURSORS_HIGH_BITS = 0xFF };
enum { KEPT_SPRITE_COLORS = 8, CURSOR_COLORS = 3 };
static const uint8_t cursor_palette_first[2] = { 173, 189 };

// $CC:EED0
void setup_cursor_sprites_and_colors(GameState *g)
{
    for (unsigned sprite = 0; sprite < CURSOR_SPRITES; sprite++) {
        set_oam_buffer_x(g, sprite, 0);
        set_oam_buffer_y(g, sprite, 0);
        set_oam_buffer_tile(g, sprite, 0);
        set_oam_buffer_attributes(g, sprite, CURSOR_ATTRIBUTES);
    }
    set_oam_high_table_byte(g, 0, CURSORS_HIGH_BITS);
    for (unsigned color = 0; color < KEPT_SPRITE_COLORS; color++)
        set_battle_kept_sprite_colors(g, color, setup_kept_sprite_color(g, color));
    for (unsigned palette = 0; palette < 2; palette++)
        for (unsigned color = 0; color < CURSOR_COLORS; color++)
            set_palette_working(g, cursor_palette_first[palette] + color, setup_cursor_color(g, palette, color));
}

/* ---- Where each object's graphics go ------------------------------------------------------ */

/* An object's graphics go to VRAM in two uploads. A large enemy (graphics
 * kind 2) has its own pair, one for the first enemy's slot and one for any
 * other; every other object's sizes follow its kind and its places its slot. */
enum { UPLOADS = 2, LARGE_KIND = 2 };
typedef struct {
    uint16_t vram, size, source;
} Upload;
static const Upload large_first_enemy[UPLOADS] = { { 0x0400, 0x0500, 0x6BAD }, { 0x0700, 0x0100, 0x71AD } };
static const Upload large_other[UPLOADS] = { { 0x0680, 0x0100, 0x70AD }, { 0x0780, 0x0500, 0x72AD } };

static void set_upload(GameState *g, unsigned upload, unsigned slot, Upload u)
{
    unsigned at = upload * BATTLE_COMBATANTS + slot;
    set_battle_upload_vram_address(g, at, u.vram);
    set_battle_upload_size(g, at, u.size);
    set_battle_upload_source(g, at, u.source);
}

/* The worn items that let the party see an enemy's HP. */
static bool shows_enemy_hp(uint8_t item) { return item == 0xA4 || item == 0xB8; }

// $CC:EF34
void setup_uploads(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!battle_slot_present(g, slot))
            continue;
        uint8_t kind = battle_object_kind(g, slot);
        for (unsigned upload = 0; upload < UPLOADS; upload++) {
            Upload u;
            if (kind == LARGE_KIND)
                u = slot == BATTLE_FIRST_ENEMY ? large_first_enemy[upload] : large_other[upload];
            else
                u = (Upload){ setup_upload_vram(g, upload, (uint8_t)slot), setup_upload_size(g, upload, kind),
                              setup_upload_source(g, upload, (uint8_t)slot) };
            set_upload(g, upload, slot, u);
        }
    }
    ORIGINAL_EFFECT(setup_leave_word(g, 0x80, BATTLE_COMBATANTS));
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (shows_enemy_hp(battle_stats_worn_item(g, member))) {
            set_battle_sees_enemy_hp(g, (uint8_t)(battle_sees_enemy_hp(g) + 1));
            break;
        }
    set_battle_graphics_load_step(g, (uint8_t)(battle_graphics_load_step(g) + 1));
}
