/* What the menus' code reads from the ROM.
 *
 * Every such read goes through an accessor here, named for what the data is,
 * so a build that loads it from somewhere other than the ROM image changes
 * this file and nothing that uses it. The accessors take the game's state
 * (game/rom.h), not the machine. */
#ifndef CT_GAME_MENU_DATA_H
#define CT_GAME_MENU_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/vram.h"
#include "rom.h"
#include "state.h"

/* ---- The font ---------------------------------------------------------------------------- */

/* The proportional font the dialog and the menus write with: glyphs 12
 * pixels wide and 12 lines tall, 2 bits a pixel, a line's two bitplanes one
 * byte after the other. A glyph's left eight pixels are 24 bytes of their own
 * at $FF:2060 + 24 a glyph; its right four share a byte with the next glyph's,
 * 24 bytes a pair at $FF:3860, the even glyph in the high nibble. The text
 * engine works out both addresses and hands them to the glyph drawer, which
 * reads the font through them: `at` is a 24-bit address. */
static inline uint8_t menu_font_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* The tile columns of a line of text, where the glyph drawer puts a glyph's
 * lines: the byte offset of each column's tile in the text's tile buffer, a
 * word each. A line of text is two rows of tiles; the first 16 columns are
 * its left half, a tile after another, and the next 16 its right half, two
 * rows of tiles on. In four-colour tiles ($C2:5FE6) a tile is $10 bytes and
 * a row $100; in sixteen-colour ones ($C2:6066) $20 and $200. The tables go
 * on to 64 columns, which no line reaches. */
enum { MENU_TEXT_COLUMNS = 32 };
static inline uint16_t menu_text_column_offset(const GameState *g, unsigned column)
{
    return rom_read16(g, 0xC25FE6u + 2u * column);
}
static inline uint16_t menu_text_column_offset_16_colour(const GameState *g, unsigned column)
{
    return rom_read16(g, 0xC26066u + 2u * column);
}

/* How far the pen moves after each of the font's glyphs ($C2:60E6), by the
 * glyph's number less $A0 (a byte: the two-byte glyphs take their low byte's
 * entry, and the table runs on into what follows it). */
static inline uint8_t menu_glyph_width(const GameState *g, uint8_t index) { return rom_read8(g, 0xC260E6u + index); }

/* ---- The menus' graphics ------------------------------------------------------------------- */

/* The graphics the menus build their tile sets from (game/menu/graphics.c),
 * in bank $FF: four-colour tiles (224 of them from $8E60, the second $100
 * bytes then replaced from $C9FC; more from $D5E4), three-plane tiles
 * ($DBE4), the eight windows' designs ($9E10, $280 bytes a design), and four
 * sixteen-colour tiles at $9CF0. */
enum {
    MENU_ROM_BANK = 0xFF0000u,
    MENU_ROM_FOUR_COLOUR_TILES = 0x8E60, MENU_ROM_FOUR_COLOUR_TILES_BYTES = 0x0E00,
    MENU_ROM_REPLACED_TILES = 0xC9FC, MENU_ROM_REPLACED_TILES_BYTES = 0x0100,
    MENU_ROM_MORE_FOUR_COLOUR_TILES = 0xD5E4, MENU_ROM_MORE_FOUR_COLOUR_TILES_BYTES = 0x0600,
    MENU_ROM_THREE_PLANE_TILES = 0xDBE4, MENU_THREE_PLANE_TILES = 0x95,
    MENU_ROM_WINDOW_DESIGNS = 0x9E10, MENU_WINDOW_DESIGN_BYTES = 0x280, MENU_WINDOW_DESIGNS = 8,
    MENU_ROM_SIXTEEN_COLOUR_TILES = 0x9CF0,
};
static inline uint8_t menu_rom_graphic_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* The menus' colours in bank $FF, a word each: two sets of four for the
 * palette's first rows ($9C68, $9C78, $CBBC) and two of sixteen ($CBC4,
 * $9DF0). */
enum {
    MENU_ROM_FIRST_COLOURS = 0x9C68, MENU_ROM_SECOND_COLOURS = 0x9C78, MENU_ROM_THIRD_COLOURS = 0xCBBC,
    MENU_ROM_ROW_7_COLOURS = 0xCBC4, MENU_ROM_ROW_8_COLOURS = 0x9DF0,
};
static inline uint16_t menu_rom_colour(const GameState *g, uint16_t from, unsigned i)
{
    return rom_read16(g, MENU_ROM_BANK | (uint16_t)(from + 2u * i));
}

/* The upload records the sets are sent with (bank C2, menu_upload_record). */
enum {
    MENU_UPLOADS_FIRST_SETS = 0xFC6F, MENU_UPLOAD_SECOND_SET = 0xFC7F, MENU_UPLOAD_LAST_SET = 0xFC87,
    MENU_UPLOADS_WINDOW_SETS = 0xFC8F, MENU_UPLOAD_WINDOW_DESIGN = 0xC548,
};

/* ---- The items ------------------------------------------------------------------------------ */

/* The items' ids fall in five categories, each from its first id
 * ($C2:88A7: weapons 0, armour $5A, helmets $7B, accessories $94, the rest
 * $BC, and the end $F2). Each category's data (the kind, the price, who can
 * use it) and figures (attack or defence, more for an accessory) are records
 * in bank $CC, where each table starts and how long its records are in bank
 * $FF ($CF0E and $CF18, $CF1D and $CF27). */
enum { MENU_ITEM_CATEGORIES = 5, MENU_ITEMS = 0xF2, MENU_ITEM_TABLES_BANK = 0xCC0000u };
static inline uint8_t menu_item_category_start(const GameState *g, unsigned category)
{
    return rom_read8(g, 0xC288A7u + category);
}
static inline uint16_t menu_item_data_table(const GameState *g, unsigned category) { return rom_read16(g, 0xFFCF0Eu + 2u * category); }
static inline uint8_t menu_item_data_record_bytes(const GameState *g, unsigned category) { return rom_read8(g, 0xFFCF18u + category); }
static inline uint16_t menu_item_figures_table(const GameState *g, unsigned category) { return rom_read16(g, 0xFFCF1Du + 2u * category); }
static inline uint8_t menu_item_figures_record_bytes(const GameState *g, unsigned category) { return rom_read8(g, 0xFFCF27u + category); }
static inline uint8_t menu_item_table_byte(const GameState *g, uint16_t at) { return rom_read8(g, MENU_ITEM_TABLES_BANK | at); }

/* The items' names start at $CC:0B5E, eleven bytes each (menu_item_name). */
enum { MENU_ITEM_NAMES = 0x0B5E };

/* ---- What the typesetter inserts ----------------------------------------------------------- */

/* A number's decimal digits are the glyphs from $D4 on. */
enum { MENU_FIRST_DIGIT_GLYPH = 0xD4 };

/* The glyph a hex digit is set with ($C2:5D03, a word each: 0-9, then A-F). */
static inline uint16_t menu_hex_digit_glyph(const GameState *g, uint8_t digit)
{
    return rom_read16(g, 0xC25D03u + 2u * (digit & 0x0Fu));
}

/* The dictionary: the commonest pieces of the script's English, each its
 * count of glyphs and the glyphs ($DE:FA00, a word each, in bank $DE). */
static inline uint32_t menu_dictionary_word(const GameState *g, uint16_t entry)
{
    return 0xDE0000u | rom_read16(g, 0xDEFA00u + 2u * entry);
}

/* The characters' names are in work RAM, six bytes each from $7E:2C23, Crono's
 * first and the Epoch's eighth; the text finds a character's through the
 * table at $C2:5FD8 (a word each, seven long: an index past it reads on into
 * what follows). */
enum { MENU_NAMES_BANK = 0x7E0000u, MENU_FIRST_NAME = 0x2C23, MENU_NAME_BYTES = 6 };
static inline uint16_t menu_character_name_place(const GameState *g, unsigned index)
{
    return rom_read16(g, 0xC25FD8u + 2u * index);
}

/* The names of the items ($CC:0B5E), the techs ($CC:15C4) and the enemies
 * ($CC:6500), eleven bytes each; an item's and a tech's start with an icon. */
enum { MENU_NAME_TABLE_BYTES = 11 };
static inline uint32_t menu_item_name(const GameState *g, uint8_t item)
{
    (void)g;
    return 0xCC0000u | (uint16_t)(0x0B5E + item * MENU_NAME_TABLE_BYTES);
}
static inline uint32_t menu_tech_name(const GameState *g, uint8_t tech)
{
    (void)g;
    return 0xCC0000u | (uint16_t)(0x15C4 + tech * MENU_NAME_TABLE_BYTES);
}
static inline uint32_t menu_enemy_name(const GameState *g, uint8_t enemy)
{
    (void)g;
    return 0xCC0000u | (uint16_t)(0x6500 + enemy * MENU_NAME_TABLE_BYTES);
}

/* Marle's real name, five glyphs ($C2:6146). */
enum { MENU_MARLE_REAL_NAME = 0xC26146 };

/* ---- Text --------------------------------------------------------------------------------- */

/* The menus' strings, and the numbers they show, wherever the caller's
 * pointer puts them: the ROM's own strings, or names and numbers in work RAM.
 * `at` is a 24-bit address; a string read on past the end of its bank goes on
 * into the next, as the original's indexed reads carry. */
static inline uint8_t menu_text_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* The tiles a hex digit is drawn with ($C2:F27D), one for each of 0-15. */
static inline uint8_t menu_hex_digit_tile(const GameState *g, uint8_t digit)
{
    return rom_read8(g, 0xC2F27Du + (digit & 0x0Fu));
}

/* The limits the text's limited numbers are shown under ($FF:CF33): a value
 * at or over its limit shows as dashes. `which` is the string's operand, the
 * table's index. */
static inline uint8_t menu_number_limit(const GameState *g, uint8_t which) { return rom_read8(g, 0xFFCF33u + which); }

/* The address of each control code's handler in bank C2 ($C2:EFD4), which the
 * original's dispatch leaves behind it. */
static inline uint16_t menu_text_code_handler(const GameState *g, uint8_t code)
{
    return rom_read16(g, 0xC2EFD4u + 2u * code);
}

/* ---- The windows --------------------------------------------------------------------------- */

/* The window command lists, and the runs of tilemap bytes their commands
 * copy, in bank $FF (menu/windows.c). `at` is the offset in the bank. */
static inline uint8_t menu_window_byte(const GameState *g, uint16_t at) { return rom_read8(g, 0xFF0000u | at); }

/* The strings a window command names by number: where each starts in bank
 * $FF ($FF:C457, a word each). */
static inline uint16_t menu_window_string(const GameState *g, uint16_t number)
{
    return rom_read16(g, (0xFFC457u + (uint16_t)(2u * number)) & 0xFFFFFFu);
}

/* A window's inside is a pattern of four rows of four tiles ($C2:EEF5), each
 * the tile of the style that many past its first. */
static inline uint8_t menu_window_inside_tile(const GameState *g, unsigned row, unsigned column)
{
    return rom_read8(g, 0xC2EEF5u + 4u * row + column);
}

/* The eight window designs' colours ($FF:B210): eight colours a design, the
 * upper half of the palette row a window is drawn in. */
enum { MENU_WINDOW_DESIGN_COLOURS = 8 };
static inline uint16_t menu_window_design_colour(const GameState *g, uint8_t design, unsigned i)
{
    return rom_read16(g, 0xFFB210u + 16u * design + 2u * i);
}

/* ---- The load screen ---------------------------------------------------------------------- */

/* The values the load screen's per-line effect gives its slot rows ($C2:D044,
 * 33 bytes). */
static inline uint8_t menu_slot_row_value(const GameState *g, unsigned i) { return rom_read8(g, 0xC2D044u + i); }

/* A character's portrait: the first of its four tiles ($C2:D4B4, seven
 * characters), and its 16 colours ($FF:CC84, 32 bytes a character; the
 * original's index is 16-bit, so it stays in the bank). */
enum { MENU_PORTRAITS = 7 };
static inline uint8_t menu_portrait_first_tile(const GameState *g, uint8_t character)
{
    return rom_read8(g, 0xC2D4B4u + character);
}
static inline uint16_t menu_portrait_colour(const GameState *g, uint8_t character, unsigned i)
{
    return rom_read16(g, 0xFF0000u | (uint16_t)(0xCC84u + character * 32u + 2u * i));
}

/* The load screen's colours past its windows' ($FF:CC74, eight for the
 * upper half of palette row 2; $FF:CE30, the sixteen of row 7). */
enum { MENU_LOAD_SCREEN_TEXT_COLOURS = 8, MENU_LOAD_SCREEN_ROW_7_COLOURS = 16 };
static inline uint16_t menu_load_screen_text_colour(const GameState *g, unsigned i) { return rom_read16(g, 0xFFCC74u + 2u * i); }
static inline uint16_t menu_load_screen_row_7_colour(const GameState *g, unsigned i) { return rom_read16(g, 0xFFCE30u + 2u * i); }

/* The chapters of the story a slot's label names, by where the story had got
 * to ($FF:D024, 26 of them, each the first value of the story's progress in
 * that chapter). */
enum { MENU_CHAPTERS = 26 };
static inline uint8_t menu_chapter_start(const GameState *g, uint8_t chapter) { return rom_read8(g, 0xFFD024u + chapter); }

/* How far right each slot's name label is moved ($C2:D329): pairs of pixels. */
static inline uint8_t menu_slot_name_shift(const GameState *g, uint8_t slot) { return rom_read8(g, 0xC2D329u + slot); }

/* The era each location is in ($FF:D296): a nibble a location, the even
 * location's in the low half of its byte. */
static inline uint8_t menu_era_of_location(const GameState *g, uint16_t location)
{
    uint16_t pair = rom_read16(g, (0xFFD296u + (location >> 1)) & 0xFFFFFFu);
    uint16_t era = (uint16_t)(pair & 0x01FF);
    return (uint8_t)(((location & 1) ? era >> 4 : era) & 0x0F);
}

/* The table of the era labels' graphics ($FF:D396), which the menus' loader
 * takes an era's from. */
enum { MENU_ERA_LABELS = 0xFFD396 };

/* ---- Uploads ------------------------------------------------------------------------------ */

/* An upload of the menus' own, a record in bank C2 laid out as the VRAM
 * queue's entries (menu/vram.h): the VRAM word, the source's address and bank,
 * the size, and a spare byte. `at` is its place in the bank, which the record
 * wraps in. */
static inline MenuUpload menu_upload_record(const GameState *g, uint16_t at)
{
    uint8_t b[8];
    for (unsigned i = 0; i < 8; i++)
        b[i] = rom_read8(g, 0xC20000u | (uint16_t)(at + i));
    return (MenuUpload){ (uint16_t)(b[0] | b[1] << 8), (uint32_t)b[4] << 16 | (uint16_t)(b[2] | b[3] << 8),
                         (uint16_t)(b[5] | b[6] << 8), b[7] };
}

/* The load screen's uploads: the era label's graphic, and the slot's tilemap
 * in three parts. */
enum {
    MENU_UPLOAD_ERA_LABEL = 0xD45F,
    MENU_UPLOAD_SLOT_TILEMAP = 0xFBDC,
    MENU_UPLOAD_SLOT_TILEMAP_2 = 0xFC0D,
    MENU_UPLOAD_SLOT_TILEMAP_3 = 0xFC29,
};

/* ---- The sprite-object mode --------------------------------------------------------------- */

/* The object scripts (menu/script.h), the sprite scripts and the sprite groups
 * they show (menu/sprites.c), wherever their pointers put them: bank C2's and
 * C3's scripts, or ones a screen built in work RAM. `at` is a 24-bit address,
 * whose offset the callers wrap in its bank. */
static inline uint8_t menu_script_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }
static inline uint8_t menu_sprite_script_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }
static inline uint8_t menu_sprite_group_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* The address in bank C2 of each object script command's handler ($C2:0F91),
 * which names the command's time and its registered entry. */
static inline uint16_t menu_script_handler(const GameState *g, uint8_t opcode)
{
    return rom_read16(g, 0xC20F91u + 2u * opcode);
}

/* The sprite scripts an object script gives its object, by number: where each
 * starts in bank C3 ($C3:E000, a word each). */
enum { MENU_SPRITE_SCRIPT_BANK = 0xC3 };
static inline uint16_t menu_sprite_script_start(const GameState *g, uint8_t script)
{
    return rom_read16(g, 0xC3E000u + 2u * script);
}

/* For each plane of the map ($C2:0556, $C2:055C, $C2:0562, a word a plane):
 * where its tile groups and its map are in bank $7E, and where its tilemap is
 * in VRAM. The original indexes them by the plane's number doubled, a byte
 * that a script's plane number 0 turns into $1FE, past the tables into the
 * code after them. */
static inline uint16_t menu_plane_word(const GameState *g, uint32_t table, uint8_t plane)
{
    return rom_read16(g, table + (uint16_t)(2u * plane));
}
static inline uint16_t menu_plane_groups(const GameState *g, uint8_t plane) { return menu_plane_word(g, 0xC20556u, plane); }
static inline uint16_t menu_plane_map(const GameState *g, uint8_t plane) { return menu_plane_word(g, 0xC2055Cu, plane); }
static inline uint16_t menu_plane_tilemap(const GameState *g, uint8_t plane) { return menu_plane_word(g, 0xC20562u, plane); }

/* The scenes' resources the object scripts load (MD2 resources in bank C6,
 * 24-bit pointers to streams in bank C3): graphics, decompressed to the Mode 7
 * plane's buffer, and palettes, to the loaded palette's. A resource is named
 * by its number in its table. */
typedef enum { MENU_SCENE_GRAPHICS, MENU_SCENE_PALETTES } MenuSceneResource;
enum { MENU_SCENE_POINTER_BYTES = 3 };
static inline uint32_t menu_scene_stream(const GameState *g, MenuSceneResource kind, uint8_t number)
{
    uint32_t at = (kind == MENU_SCENE_GRAPHICS ? 0xC6FDF0u : 0xC6FEA0u) + MENU_SCENE_POINTER_BYTES * number;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

/* How the game goes back to the world map from a location a map screen sends
 * the party to, by the location's kind ($C2:2509, four bytes). */
static inline uint8_t menu_return_flags(const GameState *g, uint8_t kind) { return rom_read8(g, 0xC22509u + kind); }

/* The sizes of the waves on the layer over the map ($C0:F900): a byte a
 * step, 1024 steps a turn, of which the second half is read below zero. */
static inline uint8_t menu_wave_size(const GameState *g, uint16_t step) { return rom_read8(g, 0xC0F900u + step); }

/* The menus' random bytes ($C0:FE00, 256 of them), which the sprite-object
 * mode's scenes take in turn. */
static inline uint8_t menu_random_byte(const GameState *g, uint8_t index) { return rom_read8(g, 0xC0FE00u + index); }

/* The sixteen velocities a scene's object may be given at random ($C6:F640):
 * across, then down, each 16.16 fixed point. */
static inline uint32_t menu_scene_velocity_x(const GameState *g, uint8_t pick)
{
    uint32_t at = 0xC6F640u + 8u * pick;
    return rom_read16(g, at) | (uint32_t)rom_read16(g, at + 2) << 16;
}
static inline uint32_t menu_scene_velocity_y(const GameState *g, uint8_t pick)
{
    uint32_t at = 0xC6F640u + 8u * pick + 4;
    return rom_read16(g, at) | (uint32_t)rom_read16(g, at + 2) << 16;
}

/* ---- The Epoch's screens -------------------------------------------------------------------- */

/* The map view: the graphics set its markers are drawn from for each world
 * map ($C2:69F0, a byte a map; the story swaps two of them for $6F), and
 * where on the view the landing marker stands, across, for each ($C2:68E4, a
 * word a map). Maps count from the first world map's location, $1F0. */
enum { MENU_FIRST_WORLD_MAP = 0x01F0, MENU_WORLD_MAPS = 8 };
static inline uint8_t menu_map_view_graphics(const GameState *g, uint16_t map)
{
    return rom_read8(g, 0xC269F0u + map);
}
static inline uint16_t menu_landing_marker_x(const GameState *g, uint16_t map)
{
    return rom_read16(g, 0xC268E4u + (uint16_t)(2u * map));
}

/* The map view's cursor's path: its zoom across ($C6:F560) and down
 * ($C6:F5A0) at each of 32 steps, words, by the step's byte offset. */
static inline uint16_t menu_cursor_path_zoom_x(const GameState *g, uint16_t at) { return rom_read16(g, 0xC6F560u + at); }
static inline uint16_t menu_cursor_path_zoom_y(const GameState *g, uint16_t at) { return rom_read16(g, 0xC6F5A0u + at); }

/* The map view's sprites: the cursor's tiles, two runs of 32 bytes, and
 * their sixteen colours with the byte after them. */
enum { MENU_MAP_VIEW_TILES = 0xC6F500, MENU_MAP_VIEW_MORE_TILES = 0xC6F520, MENU_MAP_VIEW_COLOURS = 0xC6F540 };
static inline uint8_t menu_map_view_colour_byte(const GameState *g, unsigned i) { return rom_read8(g, MENU_MAP_VIEW_COLOURS + i); }

/* The time dial's seven periods: the location each travels to, in the two
 * sets the story moves between ($C2:6C97, and $C2:6CA5 once the story has
 * passed $CC), a word a period, and the angle the gate stands at for each
 * ($C2:6CB3). The original doubles a period in a byte to read the
 * locations, so one past $7F reads from the set's start again. */
enum { MENU_PERIODS = 7, MENU_LATER_PERIODS = 0xCC };
static inline uint16_t menu_period_location(const GameState *g, bool later_set, uint8_t period)
{
    return rom_read16(g, 0xC20000u | (uint16_t)((later_set ? 0x6CA5u : 0x6C97u) + (uint8_t)(period << 1)));
}
static inline uint16_t menu_period_angle(const GameState *g, uint8_t period)
{
    return rom_read16(g, 0xC20000u | (uint16_t)(0x6CB3u + 2u * period));
}

/* The gate's triangle: its three corners about the screen's centre, a signed
 * pair each ($C2:6F2D). */
static inline int8_t menu_gate_corner_x(const GameState *g, unsigned corner) { return (int8_t)rom_read8(g, 0xC26F2Du + 2u * corner); }
static inline int8_t menu_gate_corner_y(const GameState *g, unsigned corner) { return (int8_t)rom_read8(g, 0xC26F2Eu + 2u * corner); }

/* The dial's digits, by period: where each stands ($C2:71DD, across then
 * down), and the sprite scripts that show and hide it ($C2:71EB, $C2:71EC).
 * The original doubles the period in a byte. */
static inline uint8_t menu_digit_x(const GameState *g, uint8_t period) { return rom_read8(g, 0xC271DDu + (uint8_t)(period << 1)); }
static inline uint8_t menu_digit_y(const GameState *g, uint8_t period) { return rom_read8(g, 0xC271DEu + (uint8_t)(period << 1)); }
static inline uint8_t menu_digit_show_script(const GameState *g, uint8_t period) { return rom_read8(g, 0xC271EBu + (uint8_t)(period << 1)); }
static inline uint8_t menu_digit_hide_script(const GameState *g, uint8_t period) { return rom_read8(g, 0xC271ECu + (uint8_t)(period << 1)); }

/* The time dial's compressed streams ($C2:7220): where each is, in the bank
 * its loader names, and where in bank $7F it goes, a word each. */
static inline uint16_t menu_gate_stream_source(const GameState *g, uint8_t stream) { return rom_read16(g, 0xC27220u + 4u * stream); }
static inline uint16_t menu_gate_stream_dest(const GameState *g, uint8_t stream) { return rom_read16(g, 0xC27222u + 4u * stream); }

/* The gate's tiles go to sixteen rows of VRAM ($C2:72B8, a word a row), and
 * its tilemap's rows are picked from a table of row numbers wherever its
 * pointer puts it (the dial's own at $C2:7321). */
static inline uint16_t menu_gate_tile_row(const GameState *g, unsigned row) { return rom_read16(g, 0xC272B8u + 2u * row); }
enum { MENU_GATE_TILEMAP_ROWS = 0xC27321 };
/* The original reads a word and keeps its low byte; where the table is
 * pointed at hardware registers, the second byte's read counts too. */
static inline uint8_t menu_gate_row_pick(const GameState *g, uint32_t at) { return (uint8_t)rom_read16(g, at & 0xFFFFFFu); }

/* The gate's plane: two pixels a byte, a colour in each nibble, the left one
 * in the high nibble ($C6:F700, 1024 bytes). */
static inline uint8_t menu_gate_pixel_pair(const GameState *g, unsigned i) { return rom_read8(g, 0xC6F700u + i); }

/* The End of Time's gate screen's setup, twelve bytes ($FF:D57A). */
static inline uint8_t menu_gate_screen_setup(const GameState *g, unsigned i) { return rom_read8(g, 0xFFD57Au + i); }

/* ---- The menus' display ------------------------------------------------------------------- */

/* The colors the menus' color math ramps step through ($C2:8771, sixteen
 * words), copied into work RAM before each table is built. */
static inline uint16_t menu_math_ramp_color(const GameState *g, unsigned i) { return rom_read16(g, 0xC28771u + 2u * i); }

/* The values of the window line table's sixteen groups ($FF:FC57). */
static inline uint8_t menu_window_line_value(const GameState *g, unsigned group) { return rom_read8(g, 0xFFFC57u + group); }

#endif
