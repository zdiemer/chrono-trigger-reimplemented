/* The world map set up: what the sprite-object mode does once its own start
 * (menu/display.c) has the screen ready, before the overworld's main loop
 * takes over (main_loop.c); and the map loaded again where the menus or the
 * Epoch's screens give the screen back.
 *
 * Each world map (the locations from $1F0) has a record in the ROM naming
 * the streams its screen is built from, a byte a stream: the sprites' and the
 * map's graphics, the palettes, the map's tile groups and its maps, the top
 * layer's tilemap, the ground's kinds and the regions, the lists of places,
 * the scene's script and the graphics the script sends to VRAM as it plays.
 * Each byte names a stream in its own table, or none. Setting the map up
 * unpacks them where the mode reads them -- the graphics through a buffer in
 * bank $7F on their way to VRAM -- puts the party's members' sprites and
 * colours in, starts the overworld's own state afresh, puts the camera on
 * the party, draws the map's two planes whole and starts the scene's script.
 *
 * Unpacking is the decompressor's (data/decompress.c), which still takes the
 * machine: the loads reach it through an Unpacker the entries supply, which
 * says where the original calls it from. Everything else is the game's state
 * and the video service. */
#include "overworld/setup.h"

#include "charge.h"
#include "cpu_io.h"
#include "data/decompress.h"
#include "menu/bridge.h"
#include "menu/display.h"
#include "menu/map.h"
#include "menu/object.h"
#include "menu/objects.h"
#include "menu/screen_objects.h"
#include "menu/sprites.h"
#include "original_effects.h"
#include "overworld/overworld.h"
#include "overworld/music.h"
#include "overworld/overworld_data.h"
#include "overworld/sign.h"
#include "platform/timing.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- Where the streams go ---------------------------------------------------------------- */

/* Graphics are unpacked into a buffer in bank $7F and sent from there to
 * VRAM, the buffer used again by the next; a stream of several blocks takes
 * $1000 bytes a block. */
enum { STAGING = RAM_MENU_WORLD_MAP_TILES, BLOCK_BYTES = 0x1000 };

/* Where the mode keeps what the map is made of, and the graphics and colours
 * the scene's script sends to VRAM as it plays (its upload command names
 * them). */
enum {
    TILE_GROUPS = 0x7E3000,
    MAPS = 0x7E4000,
    SCENE_STAGE = RAM_TILE_ANIMATION_TABLES,   /* the scene's script, and the lists before it */
    SCENE_TILES = 0x7EB800,
    SCENE_COLOURS = 0x7EC000,
    SCENE_SPRITES = 0x7EC800,
    LATE_SCENE_COLOURS = 0x7EC600,
};

/* Where in VRAM the mode's screen reads its tiles (menu/display.c's
 * object_mode_screen): the sprites' from word 0, the party's among them from
 * $1000; the map's from $2000; the top layer's from $7000, and its tilemap
 * at $7800. */
enum {
    SPRITE_TILES_VRAM = 0x0000,
    PARTY_TILES_VRAM = 0x1000,
    LATE_SPRITE_TILES_VRAM = 0x0C00,
    MAP_TILES_VRAM = 0x2000,
    TOP_LAYER_TILES_VRAM = 0x7000,
    TOP_LAYER_TILEMAP_VRAM = 0x7800,
};

/* Send graphics from bank $7F, where they are staged, to VRAM. */
static void send_staged(Platform *pf, uint16_t vram, uint32_t from, uint16_t bytes)
{
    menu_send_loaded_graphics(pf, vram, (uint16_t)from, bytes);
}

/* ---- The record ------------------------------------------------------------------------- */

/* What each byte of a world map's record names, and which table it names it
 * in: the four pages of the sprites' graphics; the map's seven blocks of
 * tiles; the top layer's tiles and its tilemap; the palette; and the rest. */
enum {
    RECORD_SPRITE_PAGES = 0x0C,
    RECORD_MAP_TILES = 0x00,
    RECORD_SCENE_TILES = 0x07,
    RECORD_TOP_LAYER_TILES = 0x08,
    RECORD_PALETTE = 0x0A,
    RECORD_SCENE_COLOURS = 0x0B,
    RECORD_TILE_GROUPS = 0x10,
    RECORD_MAPS = 0x11,
    RECORD_GROUND_KINDS = 0x12,
    RECORD_REGIONS = 0x13,
    RECORD_TOP_LAYER_TILEMAP = 0x14,
    RECORD_PLACE_LISTS = 0x15,
    RECORD_SCENE_SCRIPT = 0x16,
};
enum { SPRITE_PAGES = 4, MAP_TILE_BLOCKS = 7 };

/* A byte with its top bit set names no stream. */
enum { NO_STREAM = 0x80 };

static uint8_t record_byte(const GameState *g, uint8_t field)
{
    return overworld_map_record_byte(g, overworld_map_record(g), field);
}

/* Unpack a stream to `to`; `site` is where the original's call to the
 * unpacker stands, which the machine's unpacker makes its call from. */
static bool unpack(const Unpacker *u, uint32_t site, uint32_t stream, uint32_t to)
{
    return u->unpack(u->ctx, site, stream, to);
}

/* One stream the record names, from its table, unpacked to `to`. A record
 * that names none leaves the buffer as it was. */
static bool load_named(GameState *g, const Unpacker *u, uint32_t site, uint8_t field, OverworldStreamTable table,
                       uint32_t to)
{
    uint8_t number = record_byte(g, field);
    if (number & NO_STREAM) {
        set_decompress_dest(g, to);
        return true;
    }
    return unpack(u, site, overworld_stream(g, table, number), to);
}

/* For the original's code and shadow mode: the count of blocks the block
 * loads work with, in the mode's ninth and tenth bytes. */
static void leave_blocks_counted(GameState *g, uint16_t blocks)
{
    enum { COUNT_CELL = 0x08 };
    set_menu_object_scratch_bytes_byte(g, COUNT_CELL, (uint8_t)blocks);
    set_menu_object_scratch_bytes_byte(g, COUNT_CELL + 1, (uint8_t)(blocks >> 8));
}

/* Several blocks the record names in a row, each to the staging buffer's
 * next $1000 bytes. */
static bool load_blocks(GameState *g, const Unpacker *u, uint32_t site, uint8_t first_field, unsigned blocks,
                        OverworldStreamTable table)
{
    for (unsigned b = 0; b < blocks; b++)
        if (!load_named(g, u, site, (uint8_t)(first_field + b), table, STAGING + b * BLOCK_BYTES))
            return false;
    /* the destination is moved on past each block, round the bank */
    set_decompress_dest(g, (STAGING & 0xFF0000u) | (uint16_t)(STAGING + blocks * BLOCK_BYTES));
    ORIGINAL_EFFECT(leave_blocks_counted(g, (uint16_t)blocks));
    return true;
}

/* ---- The sprites --------------------------------------------------------------------------- */

/* The sprite pages' bytes the original reads its record through, a pointer
 * twelve bytes into the record (in the mode's cells $10-$12). */
static void leave_the_sprite_pages_pointer(GameState *g)
{
    enum { POINTER_CELL = 0x10 };
    uint32_t record = overworld_map_record(g);
    uint16_t at = (uint16_t)(record + RECORD_SPRITE_PAGES);
    set_menu_object_scratch_bytes_byte(g, POINTER_CELL, (uint8_t)at);
    set_menu_object_scratch_bytes_byte(g, POINTER_CELL + 1, (uint8_t)(at >> 8));
    set_menu_object_scratch_bytes_byte(g, POINTER_CELL + 2, (uint8_t)(record >> 16));
}

// $C2:2768
/* The four pages of the map's sprites. */
static bool load_the_sprite_pages(GameState *g, const Unpacker *u)
{
    ORIGINAL_EFFECT(leave_the_sprite_pages_pointer(g));
    return load_blocks(g, u, 0xC227A2, RECORD_SPRITE_PAGES, SPRITE_PAGES, OVERWORLD_SPRITE_GRAPHICS);
}

/* The party's sprites: every character's are one stream, a $400-byte block
 * of each character's tiles and, $AC00 into the staging buffer, two rows of
 * $40 bytes each, the row after the first $200 bytes on. Each member's goes
 * to its place's own, the three in a row from $B000 and from $BC00. */
enum { PARTY_STREAM = 0, CHARACTER_TILE_BYTES = 0x400, CHARACTER_ROW_BYTES = 0x40, SECOND_ROW = 0x200 };
enum { PARTY_TILES = 0x7FB000, PARTY_ROWS = 0x7FBC00, CHARACTER_ROWS_FROM = 0xAC00 };
enum { PARTY_SPRITES_BYTES = 0x1000 };

/* Where a character's tiles and rows are in the staging buffer, as the
 * original works them out from the character's byte: a member's byte with its
 * top bit set (an empty place) reaches past the characters, round the bank. */
static uint16_t character_tiles(uint8_t character)
{
    uint16_t doubled = (uint16_t)(character << 9);
    return (uint16_t)((uint16_t)(doubled << 1) + (uint16_t)STAGING + (doubled >> 15));
}

static uint16_t character_rows(uint8_t character) { return (uint16_t)((uint16_t)(character << 6) + CHARACTER_ROWS_FROM); }

static void copy_in_bank(GameState *g, uint16_t from, uint32_t to, uint16_t bytes)
{
    wram_copy(g, (to & 0xFF0000u) | from, to, bytes);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
}

/* For the original's code and shadow mode: the last member's rows' places,
 * which the copy keeps in the mode's ninth to twelfth bytes. */
static void leave_the_rows_copied(GameState *g, uint16_t from, uint16_t to)
{
    enum { FROM_CELL = 0x08, TO_CELL = 0x0A };
    set_menu_object_scratch_bytes_byte(g, FROM_CELL, (uint8_t)from);
    set_menu_object_scratch_bytes_byte(g, FROM_CELL + 1, (uint8_t)(from >> 8));
    set_menu_object_scratch_bytes_byte(g, TO_CELL, (uint8_t)to);
    set_menu_object_scratch_bytes_byte(g, TO_CELL + 1, (uint8_t)(to >> 8));
}

// $C2:2B61 (with $C2:2BBC and $C2:2BCF for each member)
static bool load_the_party_sprites(GameState *g, const Unpacker *u)
{
    if (!unpack(u, 0xC22B7C, overworld_stream(g, OVERWORLD_SPRITE_GRAPHICS, PARTY_STREAM), STAGING))
        return false;
    for (unsigned place = 0; place < PARTY_PLACES; place++) {
        uint8_t character = party_members(g, place);
        uint32_t rows_to = PARTY_ROWS + place * CHARACTER_ROW_BYTES;
        copy_in_bank(g, character_tiles(character), PARTY_TILES + place * CHARACTER_TILE_BYTES, CHARACTER_TILE_BYTES);
        copy_in_bank(g, character_rows(character), rows_to, CHARACTER_ROW_BYTES);
        copy_in_bank(g, (uint16_t)(character_rows(character) + SECOND_ROW), rows_to + SECOND_ROW, CHARACTER_ROW_BYTES);
    }
    enum { LAST_PLACE = PARTY_PLACES - 1 };
    ORIGINAL_EFFECT(leave_the_rows_copied(g, character_rows(party_members(g, LAST_PLACE)),
                                          (uint16_t)(PARTY_ROWS + LAST_PLACE * CHARACTER_ROW_BYTES)));
    return true;
}

/* ---- The colours ------------------------------------------------------------------------------ */

/* The map's palette is unpacked whole into the loaded palette; the party's
 * colours come after it: every character's in one stream, sixteen colours a
 * character from $8E00, of which each member's first eight go to its place's
 * sprite palette (colours 192, 208 and 224). The first sprite palette starts
 * with black and three greys, whatever the map's. */
enum { PARTY_COLOURS_STREAM = 0, PARTY_COLOURS = 0x7F8E00, CHARACTER_COLOUR_BYTES = 0x20, MEMBER_COLOUR_BYTES = 0x10 };
enum { FIRST_MEMBER_COLOUR = 192, COLOURS_A_PALETTE = 16, FIRST_SPRITE_COLOUR = 128 };
static const uint16_t FIRST_SPRITE_GREYS[] = { 0x0000, 0x14A5, 0x294A, 0x6B5A };

static uint16_t character_colours(uint8_t character)
{
    return (uint16_t)((uint16_t)(character << 5) + (uint16_t)PARTY_COLOURS);
}

// $C2:2882 (with $C2:28ED for each member)
static bool load_the_palette(GameState *g, const Unpacker *u)
{
    if (!load_named(g, u, 0xC228AB, RECORD_PALETTE, OVERWORLD_PALETTES, RAM_MENU_LOADED_PALETTE))
        return false;
    if (!unpack(u, 0xC228CC, overworld_stream(g, OVERWORLD_PALETTES, PARTY_COLOURS_STREAM), PARTY_COLOURS))
        return false;
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        copy_in_bank(g, character_colours(party_members(g, place)),
                     RAM_MENU_LOADED_PALETTE + 2u * (FIRST_MEMBER_COLOUR + place * COLOURS_A_PALETTE),
                     MEMBER_COLOUR_BYTES);
    for (unsigned i = 0; i < sizeof FIRST_SPRITE_GREYS / sizeof FIRST_SPRITE_GREYS[0]; i++)
        set_menu_loaded_palette(g, FIRST_SPRITE_COLOUR + i, FIRST_SPRITE_GREYS[i]);
    return true;
}

/* The mode shows the loaded palette from its own copy in low RAM, but for
 * the last sprite palette, which every world map takes from the ROM. The
 * original's copies each run a byte long: the second's last byte, the one
 * after the last palette's colours in the ROM, lands in the count of users
 * of the palette's first row, which the fades read. */
enum { PALETTE_BYTES = 0x200, LAST_ROW_FROM = 0x1E0, ROW_BYTES = 0x20 };

static void keep_the_palette(GameState *g)
{
    for (unsigned colour = 0; colour < LAST_ROW_FROM / 2; colour++) {
        uint16_t c = menu_loaded_palette(g, colour);
        set_menu_object_palette_byte(g, 2 * colour, (uint8_t)c);
        set_menu_object_palette_byte(g, 2 * colour + 1, (uint8_t)(c >> 8));
    }
    for (unsigned i = 0; i < ROW_BYTES; i++)
        set_menu_object_palette_byte(g, LAST_ROW_FROM + i, overworld_last_sprite_colours_byte(g, i));
    set_menu_palette_row_users(g, 0, overworld_last_sprite_colours_byte(g, ROW_BYTES));
    ORIGINAL_EFFECT(charge_block_move(g, PALETTE_BYTES + 1 + ROW_BYTES + 1));
}

/* ---- The Black Omen ------------------------------------------------------------------------ */

/* From the story's point $D4 four of the world maps show the Black Omen:
 * its sprites' tiles in the sprites' VRAM (where the late maps keep them
 * free), its colours as one of the mode's sprite palettes (which of two, on
 * one map, a story flag decides), and its scene's graphics. */
enum { OMEN_RISES = 0xD4, OMEN_SPRITES = 3, OMEN_COLOURS = 3, OMEN_SCENE_COLOURS = 18, OMEN_TILES_BYTES = 0x800 };
typedef struct {
    uint16_t location;
    uint16_t tiles_vram;
    bool colours;
    uint8_t row, other_row;   /* the sprite palette its colours go to, and where the flag is set */
} OmenMap;
static const OmenMap OMEN_MAPS[] = {
    { 0x1F0, LATE_SPRITE_TILES_VRAM, true, 9, 9 },
    { 0x1F1, LATE_SPRITE_TILES_VRAM, true, 11, 10 },
    { 0x1F2, LATE_SPRITE_TILES_VRAM, true, 9, 9 },
    { 0x1F6, SPRITE_TILES_VRAM, false, 0, 0 },
};
enum { OMEN_FLAG_BYTE = 1, OMEN_FLAG_BIT = 0x80 };

static const OmenMap *omen_map(const GameState *g)
{
    if (story_line(g) < OMEN_RISES)
        return 0;
    for (unsigned i = 0; i < sizeof OMEN_MAPS / sizeof OMEN_MAPS[0]; i++)
        if (location_id(g) == OMEN_MAPS[i].location)
            return &OMEN_MAPS[i];
    return 0;
}

// $C2:2D91 (with $C2:7B5A, $C2:7B7F and $C2:7B9F)
static bool load_the_black_omen(GameState *g, const Unpacker *u, Platform *pf)
{
    const OmenMap *m = omen_map(g);
    if (!m)
        return true;
    if (!unpack(u, 0xC27B7A, overworld_stream(g, OVERWORLD_SPRITE_GRAPHICS, OMEN_SPRITES), STAGING))
        return false;
    TIMED_AS(g, 0xC22D70, send_staged(pf, m->tiles_vram, STAGING, OMEN_TILES_BYTES));
    if (m->colours) {
        bool other = (menu_story_flags_bytes_byte(g, OMEN_FLAG_BYTE) & OMEN_FLAG_BIT) != 0;
        uint8_t row = other ? m->other_row : m->row;
        /* the mode's own palette, named through bank 0's view of low RAM */
        enum { LOW_RAM_IN_BANK_0 = 0x7E0000 };
        if (!unpack(u, 0xC27B9A, overworld_stream(g, OVERWORLD_PALETTES, OMEN_COLOURS),
                    RAM_MENU_OBJECT_PALETTE - LOW_RAM_IN_BANK_0 + (uint32_t)row * ROW_BYTES))
            return false;
    }
    return unpack(u, 0xC27BBF, overworld_stream(g, OVERWORLD_PALETTES, OMEN_SCENE_COLOURS), LATE_SCENE_COLOURS);
}

/* ---- The graphics ------------------------------------------------------------------------- */

/* The scene's sprites, which its script sends to VRAM as it plays, are the
 * sprites' graphics' second stream. */
enum { SCENE_SPRITES_STREAM = 1 };

// $C2:2D47 (with $C2:2D91 and $C2:27BC)
/* The palette sent to CGRAM, the Black Omen where it shows, and the scene's
 * sprites. */
static bool finish_the_graphics(GameState *g, const Unpacker *u, Platform *pf)
{
    TIMED_AS(g, 0xC22D47, menu_send_the_loaded_palette(pf));
    if (!load_the_black_omen(g, u, pf))
        return false;
    return unpack(u, 0xC227D9, overworld_stream(g, OVERWORLD_SPRITE_GRAPHICS, SCENE_SPRITES_STREAM), SCENE_SPRITES);
}

// $C2:2CC1 (with $C2:2768, $C2:2B61, $C2:27DE, $C2:2826, $C2:2854, $C2:2882 and $C2:2D47)
/* The map's graphics: its sprites, the party's, the map's tiles, the top
 * layer's tiles and tilemap, each sent to VRAM from the staging buffer; the
 * palette, kept in low RAM and sent to CGRAM; the Black Omen where it shows;
 * and the scene's sprites. */
static bool load_the_graphics(GameState *g, const Unpacker *u, Platform *pf)
{
    if (!load_the_sprite_pages(g, u))
        return false;
    TIMED_AS(g, 0xC22D70, send_staged(pf, SPRITE_TILES_VRAM, STAGING, SPRITE_PAGES * BLOCK_BYTES));
    if (!load_the_party_sprites(g, u))
        return false;
    TIMED_AS(g, 0xC22D70, send_staged(pf, PARTY_TILES_VRAM, PARTY_TILES, PARTY_SPRITES_BYTES));
    if (!overworld_load_the_map_tiles(g, u))
        return false;
    TIMED_AS(g, 0xC22D70, send_staged(pf, MAP_TILES_VRAM, STAGING, MAP_TILE_BLOCKS * BLOCK_BYTES));
    if (!load_named(g, u, 0xC2284F, RECORD_TOP_LAYER_TILES, OVERWORLD_MAP_GRAPHICS, STAGING))
        return false;
    TIMED_AS(g, 0xC22D70, send_staged(pf, TOP_LAYER_TILES_VRAM, STAGING, BLOCK_BYTES));
    if (!load_named(g, u, 0xC2287D, RECORD_TOP_LAYER_TILEMAP, OVERWORLD_TOP_LAYER_TILEMAPS, STAGING))
        return false;
    TIMED_AS(g, 0xC22D70, send_staged(pf, TOP_LAYER_TILEMAP_VRAM, STAGING, BLOCK_BYTES));
    if (!load_the_palette(g, u))
        return false;
    keep_the_palette(g);
    return finish_the_graphics(g, u, pf);
}

// $C2:27DE
bool overworld_load_the_map_tiles(GameState *g, const Unpacker *u)
{
    return load_blocks(g, u, 0xC2280C, RECORD_MAP_TILES, MAP_TILE_BLOCKS, OVERWORLD_MAP_GRAPHICS);
}

/* ---- The map and its places ------------------------------------------------------------------- */

/* The scene's own graphics and colours, which its script sends as it
 * plays. */
static bool load_the_scene_graphics(GameState *g, const Unpacker *u)
{
    return load_named(g, u, 0xC2292B, RECORD_SCENE_TILES, OVERWORLD_MAP_GRAPHICS, SCENE_TILES)   // $C2:2902
           && load_named(g, u, 0xC22959, RECORD_SCENE_COLOURS, OVERWORLD_PALETTES, SCENE_COLOURS);   // $C2:2930
}

/* The lists of places come unpacked one after another, each its count of
 * records and the records: the exits, the events and the halts, then the
 * events' scripts' places, two bytes each, whose count isn't kept. Every
 * map's lists have records (the original counts down after each, so a count
 * of 0 would be 65,536 of them; no map has one). */
typedef struct {
    uint32_t to;
    uint8_t record_bytes;
} PlaceListStream;
static const PlaceListStream PLACE_LIST_STREAMS[] = {
    { RAM_OVERWORLD_EXITS, 7 },
    { RAM_OVERWORLD_EVENTS, 3 },
    { RAM_OVERWORLD_HALTS, 3 },
    { RAM_OVERWORLD_EVENT_SCRIPTS, 2 },
};
enum { KEPT_COUNTS = 3 };

// $C2:2A44
/* The places, unpacked to the scene's stage and spread to their lists. The
 * original streams the records through the work RAM port, which it leaves
 * past the last list. */
static bool load_the_places(GameState *g, const Unpacker *u)
{
    if (!load_named(g, u, 0xC22A6D, RECORD_PLACE_LISTS, OVERWORLD_PLACE_LISTS, SCENE_STAGE))
        return false;
    uint32_t at = SCENE_STAGE;
    uint32_t end = 0;
    for (unsigned list = 0; list < sizeof PLACE_LIST_STREAMS / sizeof PLACE_LIST_STREAMS[0]; list++) {
        const PlaceListStream *l = &PLACE_LIST_STREAMS[list];
        uint8_t count = wram_read8(g, at);
        if (list < KEPT_COUNTS)
            set_overworld_list_lengths(g, list, count);
        uint16_t bytes = (uint16_t)(count * l->record_bytes);
        wram_copy(g, at + 1, l->to, bytes);
        at += 1u + bytes;
        end = l->to + bytes;
    }
    ORIGINAL_EFFECT(hw_port_seek(g, (uint16_t)end, (uint8_t)(end >> 16)));
    return true;
}

// $C2:295E, $C2:298C, $C2:29BA, $C2:29E8, $C2:2A16
static bool load_the_tile_groups(GameState *g, const Unpacker *u)
{
    return load_named(g, u, 0xC22987, RECORD_TILE_GROUPS, OVERWORLD_TILE_GROUPS, TILE_GROUPS);
}

static bool load_the_maps(GameState *g, const Unpacker *u)
{
    return load_named(g, u, 0xC229B5, RECORD_MAPS, OVERWORLD_MAPS, MAPS);
}

static bool load_the_ground(GameState *g, const Unpacker *u)
{
    return load_named(g, u, 0xC229E3, RECORD_GROUND_KINDS, OVERWORLD_GROUND_KINDS, RAM_OVERWORLD_GROUND_KINDS);
}

static bool load_the_regions(GameState *g, const Unpacker *u)
{
    return load_named(g, u, 0xC22A11, RECORD_REGIONS, OVERWORLD_REGION_MAPS, RAM_OVERWORLD_REGIONS);
}

static bool load_the_scene_script(GameState *g, const Unpacker *u)
{
    return load_named(g, u, 0xC22A3F, RECORD_SCENE_SCRIPT, OVERWORLD_SCENE_SCRIPTS, SCENE_STAGE);
}

/* The buffer the sign's name and the map view's sprite graphics share,
 * cleared through the work RAM port, which it leaves past it. */
enum { SIGN_BUFFER_BYTES = 0x800 };

// $C2:2BFB
static void clear_the_sign_buffer(GameState *g)
{
    wram_fill8(g, RAM_OVERWORLD_SIGN_LABEL, SIGN_BUFFER_BYTES, 0);
    uint32_t end = RAM_OVERWORLD_SIGN_LABEL + SIGN_BUFFER_BYTES;
    ORIGINAL_EFFECT(hw_port_seek(g, (uint16_t)end, (uint8_t)(end >> 16)));
}

/* Draw both planes of the map whole, the land's first. */
enum { LAND_PLANE = 0, OVER_THE_LAND = 1 };

static void draw_the_map(GameState *g, Platform *pf)
{
    menu_map_redraw_plane(g, pf, LAND_PLANE);
    menu_map_redraw_plane(g, pf, OVER_THE_LAND);
}

/* ---- The overworld's own state ------------------------------------------------------------- */

// $C2:26D6
/* The map screens work on their own copy of the story's flags and how far
 * the story has got (main_loop.c gives the flags back as the world map
 * ends). */
enum { STORY_FLAGS_AT = 0x1F0, STORY_FLAGS = 0x10 };

static void take_the_story_flags(GameState *g)
{
    for (unsigned i = 0; i < STORY_FLAGS; i++)
        set_menu_story_flags_bytes_byte(g, i, event_memory_byte(g, STORY_FLAGS_AT + i));
    set_menu_story_progress(g, story_line(g));
    ORIGINAL_EFFECT(charge_block_move(g, STORY_FLAGS));
}

// $C2:2703
/* The party stands on the location's place, which the location keeps in 8x8
 * tiles. */
enum { LOCATION_TILE_PIXELS = 8 };

static void put_the_party_where_it_arrives(GameState *g)
{
    WorldSpot at = { (uint16_t)(location_x(g) * LOCATION_TILE_PIXELS), (uint16_t)(location_y(g) * LOCATION_TILE_PIXELS) };
    overworld_set_party_spot(g, PARTY_LEADER, at);
}

/* The places found under the leader, none yet. */
enum { NOTHING_FOUND = 0xFFFF };

// $C2:2C1D-$C2:2C57
/* The overworld starts on the map with nothing under way: no screen, no
 * event, no sign, no place found, nobody idle, the fixed colour black, the
 * song playing the one the field left, and the Epoch not entering the Black
 * Omen. */
static void start_the_overworld_afresh(GameState *g)
{
    overworld_set_mode(g, OVERWORLD_STARTING);
    set_menu_mode_beside(g, 0);
    set_menu_object_shadows_fixed_blue(g, 0);
    set_menu_object_shadows_fixed_red(g, 0);
    set_menu_object_shadows_fixed_green(g, 0);
    set_overworld_event(g, 0);
    set_overworld_event_before(g, 0);
    set_overworld_ground_news(g, 0);
    set_overworld_sign(g, 0);
    set_overworld_sign_again(g, 0);
    set_overworld_step_begun(g, 0);
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        set_overworld_idle_frames(g, place, 0);
    for (unsigned list = 0; list < OVERWORLD_FOUND_COUNT; list++)
        set_overworld_found(g, list, NOTHING_FOUND);
    set_menu_song_playing(g, current_song(g));
    overworld_set_vehicle_state(g, VEHICLE_EPOCH,
                                (uint8_t)(overworld_vehicle_state(g, VEHICLE_EPOCH) & ~EPOCH_ENTERING_THE_BLACK_OMEN));
}

// $C2:274D
/* Note the world map's record, which every load reads. */
static void note_the_record(GameState *g) { set_overworld_map_record(g, overworld_map_record_of(g, location_id(g))); }

/* The scene's script starts as the overworld's first object. */
enum { SCENE_SCRIPT = RAM_TILE_ANIMATION_TABLES };

// $C2:2C1D
/* The overworld started afresh and its map loaded: the graphics, the map and
 * its ground, regions and places, the scene's script and graphics; the sign's
 * buffer cleared; the map drawn; the scene's script started. */
static bool load_the_world_map(GameState *g, const Unpacker *u, Platform *pf)
{
    start_the_overworld_afresh(g);
    note_the_record(g);
    if (!load_the_graphics(g, u, pf) || !load_the_tile_groups(g, u) || !load_the_maps(g, u)
        || !load_the_ground(g, u) || !load_the_regions(g, u) || !load_the_places(g, u)
        || !load_the_scene_script(g, u) || !load_the_scene_graphics(g, u))
        return false;
    clear_the_sign_buffer(g);
    draw_the_map(g, pf);
    TIMED_AS(g, 0xC204D9, (void)menu_start_script(g, SCENE_SCRIPT, MENU_ORDINARY_SLOT));
    return true;
}

// $C2:23C6-$C2:23CE
/* The screen shows from the next frame: the mode sends its frame and
 * palette, and the vblank handler and the controllers' reading are on. */
static void show_from_the_next_frame(GameState *g, Platform *pf)
{
    enum { SEND_FRAME_AND_PALETTE = 0x03 };
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | SEND_FRAME_AND_PALETTE));
    tm_enable_interrupts(pf, PF_AT(0xC223CC), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
}

// $C2:23A8-$C2:23CE
bool overworld_set_up(GameState *g, const Unpacker *u, Platform *pf)
{
    TIMED_AS(g, 0xC226A8, menu_clear_all_of_vram(pf));
    set_menu_random_index(g, 0);
    set_menu_random_beside(g, 0);
    TIMED_AS(g, 0xC20BF6, menu_lay_out_the_sprite_nodes(g));
    TIMED_AS(g, 0xC20471, menu_free_every_object(g));
    overworld_mark_sound_sending(g, false);
    set_menu_fades_running(g, 0);
    take_the_story_flags(g);
    put_the_party_where_it_arrives(g);
    TIMED_AS(g, 0xC22720, menu_map_centre_on_the_party(g));
    if (!load_the_world_map(g, u, pf))
        return false;
    show_from_the_next_frame(g, pf);
    return true;
}

// $C2:2C93
/* The map back after the menus or the map view: the sign's sprites made
 * afresh and its buffer cleared, the top layer's wave started over, the
 * graphics, the map and its ground, the scene's graphics loaded again, and the
 * map drawn where the camera is. */
enum { WAVE_STARTS_AT = 0xA925 };

bool overworld_load_the_map_again(GameState *g, const Unpacker *u, Platform *pf)
{
    overworld_build_the_sign_script(g);
    clear_the_sign_buffer(g);
    set_menu_top_layer_wave_at(g, WAVE_STARTS_AT);
    if (!load_the_graphics(g, u, pf) || !load_the_tile_groups(g, u) || !load_the_ground(g, u)
        || !load_the_scene_graphics(g, u))
        return false;
    draw_the_map(g, pf);
    return true;
}

/* ---- The entries ------------------------------------------------------------------------------- */

/* The unpacker's own entry, run as the original's long call to it reaches it
 * (it may stop, where the machine isn't the one it was called with). */
enum { UNPACKER = 0xC30002 };

static bool unpack_on_the_machine(void *ctx, uint32_t site, uint32_t stream, uint32_t to)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    set_decompress_source(g, stream);
    set_decompress_dest(g, to);
    return menu_run_long(s, site, UNPACKER, decompress);
}

Unpacker overworld_unpacker_on_the_machine(Snes *s) { return (Unpacker){ unpack_on_the_machine, s }; }

/* As C, the unpacker called as C. */
static bool unpack_the_stream(void *ctx, uint32_t site, uint32_t stream, uint32_t to)
{
    (void)site;
    GameState *g = ctx;
    set_decompress_source(g, stream);
    set_decompress_dest(g, to);
    TIMED_AS(g, UNPACKER, (void)unpack_stream(g));
    return true;
}

Unpacker overworld_unpacker(GameState *g) { return (Unpacker){ unpack_the_stream, g }; }

static Unpacker on_the_machine(Snes *s) { return overworld_unpacker_on_the_machine(s); }

/* Where the main loop starts. */
enum { MAIN_LOOP = 0xC223CF };

// $C2:23A8
/* The set up goes on to the main loop; what it leaves in the registers the
 * loop sets again before it reads. */
void overworld_set_up_entry(Snes *s)
{
    Unpacker u = on_the_machine(s);
    if (overworld_set_up(state_of(s), &u, m_platform(s)))
        m_stop_at(s, MAIN_LOOP, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

// $C2:2C93
void overworld_load_the_map_again_entry(Snes *s)
{
    Unpacker u = on_the_machine(s);
    if (overworld_load_the_map_again(state_of(s), &u, m_platform(s))) {
        m_sep(s, CPU_FLAG_M);
        m_rts(s);
    }
}

/* The map's tiles come back with a 16-bit accumulator, the carry of the
 * count's last comparison (set: the count is up) and X the last block's
 * place in its table, as the unpacker hands X back; where none was named, the
 * staging buffer's place, which the original sets the destination from. */
static void leave_the_last_block(Snes *s)
{
    const GameState *g = state_of(s);
    s->cpu.x = (uint16_t)STAGING;
    for (unsigned b = MAP_TILE_BLOCKS; b-- > 0;) {
        uint8_t number = record_byte(g, (uint8_t)(RECORD_MAP_TILES + b));
        if (!(number & NO_STREAM)) {
            s->cpu.x = (uint8_t)(number * 3u);
            break;
        }
    }
    m_set_flag(s, CPU_FLAG_C, true);
}

// $C2:2D47
/* Reached by falling out of the graphics' last block move, with a 16-bit
 * accumulator; it comes back with an 8-bit one. */
void overworld_finish_the_graphics_entry(Snes *s)
{
    Unpacker u = on_the_machine(s);
    m_sep(s, CPU_FLAG_M);
    if (finish_the_graphics(state_of(s), &u, m_platform(s)))
        m_rts(s);
}

// $C2:27DE
void overworld_load_the_map_tiles_entry(Snes *s)
{
    Unpacker u = on_the_machine(s);
    if (!overworld_load_the_map_tiles(state_of(s), &u))
        return;
    vars_peek(true);
    leave_the_last_block(s);
    vars_peek(false);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

/* ---- The guards ---------------------------------------------------------------------------- */

/* The loads run on the mode's page with the data bank on low RAM, on a world
 * map whose record is in the ROM; the record noted where they read it. */
static bool on_a_world_map(const GameState *g)
{
    uint16_t location = (uint16_t)(location_id(g) & 0x1FF);
    return location >= OVERWORLD_FIRST_MAP && location < OVERWORLD_FIRST_MAP + OVERWORLD_WORLD_MAPS;
}

static bool record_noted(const GameState *g)
{
    return overworld_map_record(g) == overworld_map_record_of(g, location_id(g));
}

/* Setting up puts the camera on the party, which must put it on the map to
 * draw it. */
bool overworld_can_be_set_up(const GameState *g) { return on_a_world_map(g) && menu_map_party_centres_on_the_map(g); }

bool overworld_set_up_runs(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    bool ok = overworld_can_be_set_up(state_of(s));
    vars_peek(false);
    return ok;
}

/* Loading the map again draws it where the camera is, through the upload
 * buffer the main loop has just started over. */
bool overworld_map_can_be_loaded_again(const GameState *g)
{
    return on_a_world_map(g) && record_noted(g) && menu_map_can_redraw(g, LAND_PLANE)
           && menu_map_can_redraw(g, OVER_THE_LAND);
}

bool overworld_map_again_runs(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    bool ok = overworld_map_can_be_loaded_again(state_of(s));
    vars_peek(false);
    return ok;
}

/* The palette's send reaches the video registers through the data bank. */
bool overworld_graphics_finish_runs(Snes *s) { return menu_objects_on_page(s) && m_io_bank(s->cpu.db); }

/* The map's tiles load on a world map whose record is noted (the map view's
 * plane is built from them too: menu/epoch.c). */
bool overworld_map_tiles_can_load(const GameState *g) { return on_a_world_map(g) && record_noted(g); }

bool overworld_map_tiles_run(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    bool ok = overworld_map_tiles_can_load(state_of(s));
    vars_peek(false);
    return ok;
}
