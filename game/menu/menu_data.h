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

/* The healing's rising numbers: their four colours ($FF:CB9C), the first
 * sprite palette's fifth to eighth, and the upload of their digits' tiles
 * ($C2:FC5A, a record of menu_upload_record's). */
enum { MENU_ROM_AMOUNT_COLOURS = 0xCB9C, MENU_AMOUNT_COLOURS = 4, MENU_FIRST_AMOUNT_COLOUR = 0x84,
       MENU_UPLOAD_AMOUNT_DIGITS = 0xFC5A };

/* The heights the healing's numbers rise to, a frame at a time, above
 * their member's sprite: the HP's path from its start, the MP's from the
 * 43rd, each ending with a negative height ($FF:CEBA). */
static inline uint8_t menu_amount_height(const GameState *g, uint8_t step) { return rom_read8(g, 0xFFCEBAu + step); }
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

/* ---- The items screen ----------------------------------------------------------------------- */

/* The items screen's window command lists (bank $FF, menu/windows.h): the
 * screen's, the main menu's panel for it, and the window of members an item
 * is used on. */
enum { MENU_WINDOWS_ITEMS_SCREEN = 0xC0A6, MENU_WINDOWS_ITEMS_PANEL = 0xC05D, MENU_WINDOWS_ITEMS_MEMBERS = 0xC0E4 };

/* The label of an item's kind its row shows ("Weapon", "Item" and the
 * rest): where the string is in bank $FF, from a word an item at $FF:B310. */
static inline uint32_t menu_item_kind_label(const GameState *g, uint8_t item)
{
    return 0xFF0000u | rom_read16(g, 0xFFB310u + 2u * item);
}

/* Who an item of the rest is used on, a byte an item by its place in the
 * category ($CC:1BB5): one member, or every member. */
static inline uint8_t menu_item_targets(const GameState *g, uint8_t index) { return rom_read8(g, 0xCC1BB5u + index); }

/* The categories in the order the sort writes them (the rest first, then the
 * weapons, the armour, the helmets and the accessories): each one's first
 * item ($C2:AC12) and how many it has ($C2:AC17). */
static inline uint8_t menu_sort_category_first(const GameState *g, unsigned order) { return rom_read8(g, 0xC2AC12u + order); }
static inline uint8_t menu_sort_category_items(const GameState *g, unsigned order) { return rom_read8(g, 0xC2AC17u + order); }

/* The label of the figure the window of who can equip an item shows, by the
 * item's category ($C2:AF3A, a word each): where in bank $FF its string is. */
static inline uint16_t menu_item_power_label_at(const GameState *g, uint8_t category)
{
    return rom_read16(g, 0xC2AF3Au + 2u * category);
}

/* The chances an item used by chance has, the limit of each of its first
 * three outcomes in a hundred ($C2:B13D). */
static inline uint8_t menu_chance_limit(const GameState *g, unsigned outcome) { return rom_read8(g, 0xC2B13Du + outcome); }

/* A tab's figure (menu_use_effect's index): its offset among a character's
 * figures ($FF:CF2B), its cap ($FF:CF32), and its base's offset among the
 * bases ($C2:B315). */
static inline uint8_t menu_figure_offset(const GameState *g, uint8_t figure) { return rom_read8(g, 0xFFCF2Bu + figure); }
static inline uint8_t menu_figure_cap(const GameState *g, uint8_t figure) { return rom_read8(g, 0xFFCF32u + figure); }
static inline uint8_t menu_base_figure_offset(const GameState *g, uint8_t figure) { return rom_read8(g, 0xC2B315u + figure); }

/* The same tables by stat: a character's seven stats in the order of their
 * caps, each one's place among the record's stats ($FF:CF2C) and the most it
 * comes to with what the character wears ($FF:CF33, the speed's 16 and the
 * others' 99; the text's limits are the same bytes, menu_number_limit). */
enum { MENU_STATS = 7 };
static inline uint8_t menu_stat_place(const GameState *g, unsigned stat) { return rom_read8(g, 0xFFCF2Cu + stat); }
static inline uint8_t menu_stat_cap(const GameState *g, unsigned stat) { return rom_read8(g, 0xFFCF33u + stat); }

/* Ayla's fists' part of her attack, by her level ($CC:2D69, a word a
 * level; the original doubles the level in a byte). */
static inline uint16_t menu_fist_power(const GameState *g, uint8_t level) { return rom_read16(g, 0xCC2D69u + (uint8_t)(level * 2)); }

/* Where each member's sprite stands beside its row of the window an item is
 * used on ($C2:B045, its Y and its X's low byte). */
typedef struct {
    uint8_t y, x;
} MenuSpritePlace;
static inline MenuSpritePlace menu_member_sprite_place(const GameState *g, uint8_t member)
{
    return (MenuSpritePlace){ rom_read8(g, 0xC2B045u + 2u * member), rom_read8(g, 0xC2B046u + 2u * member) };
}

/* The items screen's colours in bank $FF: the list's eight ($CC34, row 5's
 * first half) and its highlight's four ($CBB4, row 2's), for
 * menu_rom_colour. */
enum { MENU_ROM_ITEMS_LIST_COLOURS = 0xCC34, MENU_ROM_ITEMS_HIGHLIGHT_COLOURS = 0xCBB4 };

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

/* The main menu ($FF): its window ($BD2E) and the window an option's
 * screen opens from ($BD51); its icons' sprites, the six options' along the
 * top, each its place, tile and attributes ($BD5C), the lit icon's tile $20
 * on; the icons' colours, eight ($CC14), and the lit option's own eight
 * ($CC24, a set an option); and the arrows' three ($9E0A). */
enum {
    MENU_WINDOWS_MAIN_MENU = 0xBD2E, MENU_WINDOWS_OPTION_OPENING = 0xBD51,
    MENU_MAIN_MENU_OPTIONS = 6, MENU_LIT_ICON_TILES = 0x20,
    MENU_ROM_MAIN_MENU_COLOURS = 0xCC14, MENU_ROM_OPTION_COLOURS = 0xCC24, MENU_ROM_ARROW_COLOURS = 0x9E0A,
    MENU_MAIN_MENU_COLOURS = 8, MENU_ARROW_COLOURS = 3,
};
static inline uint16_t menu_option_colour(const GameState *g, uint8_t option, unsigned i)
{
    return menu_rom_colour(g, (uint16_t)(MENU_ROM_OPTION_COLOURS + 2u * MENU_MAIN_MENU_COLOURS * option), i);
}
typedef struct {
    uint8_t x, y, tile, attributes;
} MenuIcon;
static inline MenuIcon menu_main_menu_icon(const GameState *g, unsigned option)
{
    uint32_t at = 0xFFBD5Cu + 4u * option;
    return (MenuIcon){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2), rom_read8(g, at + 3) };
}

/* Naming a character: the letters' grid, eight rows of ten letters, each
 * the letter's text code ($FF:C9AC); the battle mode's screen, its tiles
 * ($FF:F008, tilemap words), and its sixteen colours ($FF:CE9A). */
enum { MENU_NAME_GRID_COLUMNS = 10 };
static inline uint8_t menu_name_grid_letter(const GameState *g, unsigned row, unsigned column)
{
    return rom_read8(g, 0xFFC9ACu + row * MENU_NAME_GRID_COLUMNS + column);
}
static inline uint16_t menu_battle_mode_screen_tile(const GameState *g, unsigned tile)
{
    return rom_read16(g, 0xFFF008u + 2u * tile);
}
static inline uint16_t menu_battle_mode_colour(const GameState *g, unsigned i) { return rom_read16(g, 0xFFCE9Au + 2u * i); }

/* The screens' uploads of their tilemap buffers and line tables, by what
 * they send: the front layer's and the back layer's whole ($7E:2E00, $C00
 * bytes; $7E:3E00, $1000 bytes); the front layer's rows from the third
 * ($7E:2E80, $640 bytes), from $7E:3400 (the shop's members' strips, $600
 * bytes),
 * its lower half ($7E:3600), the back layer's rows from the third
 * ($7E:3E80), the text layer's ($7E:4E00, $680 bytes), the window line
 * tables ($7E:5D00), the text layer's first two rows ($7E:4E00, $80
 * bytes) where a window over the list keeps its lines, and the text
 * layer's whole ($1000 bytes) to the third layer's tilemap where the gate's
 * screen moves it. */
enum {
    MENU_UPLOAD_FRONT_LAYER_WHOLE = 0xFBCE,
    MENU_UPLOAD_BACK_LAYER_WHOLE = 0xFBF8,
    MENU_UPLOAD_FRONT_LAYER = 0xFBE3,
    MENU_UPLOAD_FRONT_LAYER_LOWER_HALF = 0xFBEA,
    MENU_UPLOAD_FRONT_LAYER_STRIPS = 0xFBF1,
    MENU_UPLOAD_BACK_LAYER = 0xFBFF,
    MENU_UPLOAD_TEXT_LAYER = 0xFC1B,
    MENU_UPLOAD_LINE_TABLES = 0xFC3E,
    MENU_UPLOAD_WINDOW_LINES = 0xFC37,
    MENU_UPLOAD_TEXT_LAYER_WHOLE = 0xFC14,
};

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
/* The items each of the equipment screen's slots takes ($C2:89DC): a range
 * of item numbers, the first and the one past the last, a pair a slot. */
static inline uint8_t menu_slot_first_item(const GameState *g, uint8_t slot) { return rom_read8(g, 0xC289DCu + 2u * slot); }
static inline uint8_t menu_slot_past_items(const GameState *g, uint8_t slot) { return rom_read8(g, 0xC289DDu + 2u * slot); }

static inline uint8_t menu_script_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }
/* A colour of the palettes the scripts load rows from, a word (its second
 * byte the next address's, across a bank's end too). */
static inline uint16_t menu_colour(const GameState *g, uint32_t at)
{
    return (uint16_t)(rom_read8(g, at & 0xFFFFFFu) | rom_read8(g, (at + 1) & 0xFFFFFFu) << 8);
}
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

/* The way the party faces coming back to the world map from a location, by
 * the way it faced arriving there (the low two bits of the location's flags):
 * turned round ($C2:2509, four bytes). */
static inline uint8_t menu_return_flags(const GameState *g, uint8_t kind) { return rom_read8(g, 0xC22509u + kind); }

/* The sizes of the waves on the layer over the map ($C0:F900): a byte a
 * step, 1024 steps a turn, of which the second half is read below zero. */
static inline uint8_t menu_wave_size(const GameState *g, uint16_t step) { return rom_read8(g, 0xC0F900u + step); }

/* A sine of 1024 steps a turn, its values -255 to 255: the wave's size at
 * the step, below zero in the second half of the turn ($C2:225E); and the
 * cosine, the sine a quarter turn on ($C2:225A). */
enum { MENU_SINE_STEPS = 0x400 };
static inline int16_t menu_sine(const GameState *g, uint16_t phase)
{
    uint16_t step = phase & (MENU_SINE_STEPS - 1);
    int16_t size = menu_wave_size(g, step);
    return step >= MENU_SINE_STEPS / 2 ? (int16_t)-size : size;
}
static inline int16_t menu_cosine(const GameState *g, uint16_t phase)
{
    return menu_sine(g, (uint16_t)(phase + MENU_SINE_STEPS / 4));
}

/* The tilts the Day of Lavos's turning world leans back by
 * (menu/turning_world.c): how much its distances grow a line pair down the
 * screen, in 256ths ($C6:EB23, the steeper the later). The frame's guard
 * keeps the tilt among them; the original's index would read on into the
 * code after the table. */
enum { MENU_WORLD_TILTS = 24 };
static inline uint8_t menu_world_tilt_step(const GameState *g, uint16_t tilt) { return rom_read8(g, 0xC6EB23u + tilt); }

/* A sine wave of 256 steps a turn ($C0:F800), as signed bytes: the table the
 * field reads as its directions' velocity factors, read whole by the scenes'
 * objects, through the data bank. */
static inline int8_t menu_sine_step(const GameState *g, uint8_t angle) { return (int8_t)rom_read8(g, 0xC0F800u + angle); }

/* The menus' random bytes ($C0:FE00, 256 of them), which the sprite-object
 * mode's scenes take in turn; the healing draws six from a frame's place,
 * the last few reading on past the table. */
static inline uint8_t menu_random_byte(const GameState *g, unsigned index) { return rom_read8(g, 0xC0FE00u + index); }

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

/* ---- The sprites the menus' records show -------------------------------------------------- */

/* A sprite's assignment says what it is made of: its graphics, its frames,
 * its colours and its animations, a number each into the tables below, and
 * its size (bits 0-1: 1, 2, 3 or 6 blocks of 32 by 32 pixels; bit 4: the
 * second of its colour sets). The playable characters' sprites and the
 * others' are three sets of assignments in bank $E4, whose places the table
 * at $C2:F55C gives (a word a set, taken at the set's number doubled as a
 * byte); the first two sets' assignments are five bytes, the rest's ten. The
 * tables of places are at the end of the bank ($E4:FFE0 on, a word each). */
enum { MENU_SPRITE_ASSIGNMENT_BYTES = 5, MENU_SPRITE_BANK = 0xE40000 };
static inline uint16_t menu_sprite_set_place(const GameState *g, uint8_t set_doubled)
{
    return rom_read16(g, 0xC2F55Cu + set_doubled);
}
static inline uint8_t menu_sprite_assignment_byte(const GameState *g, uint16_t at)
{
    return rom_read8(g, MENU_SPRITE_BANK | at);
}
/* The graphics and the frames of a sprite: a 24-bit address each, three
 * bytes a sprite in the tables $E4:FFE0 and $E4:FFE2 place; `at` is the entry's
 * place, the table's own offset added. */
static inline uint16_t menu_sprite_graphics_table(const GameState *g) { return rom_read16(g, 0xE4FFE0u); }
static inline uint16_t menu_sprite_frames_table(const GameState *g) { return rom_read16(g, 0xE4FFE2u); }
static inline uint32_t menu_sprite_place(const GameState *g, uint16_t at)
{
    return (uint32_t)rom_read16(g, MENU_SPRITE_BANK + at) | (uint32_t)rom_read8(g, MENU_SPRITE_BANK + at + 2u) << 16;
}
/* A sprite's animations: where their frames and their frame times start in
 * bank $E4, a word a sprite in the tables $E4:FFE4 and $E4:FFE6 place (the
 * next sprite's frames' start ends this one's). */
static inline uint16_t menu_sprite_animation_frames_table(const GameState *g) { return rom_read16(g, 0xE4FFE4u); }
static inline uint16_t menu_sprite_animation_times_table(const GameState *g) { return rom_read16(g, 0xE4FFE6u); }
static inline uint16_t menu_sprite_table_word(const GameState *g, uint16_t at) { return rom_read16(g, MENU_SPRITE_BANK + at); }
/* A sprite's colours: twelve colours (24 bytes) at the place the table
 * $E4:FFE8 gives, a word a sprite; its second set $200 bytes on. */
static inline uint16_t menu_sprite_colours_table(const GameState *g) { return rom_read16(g, 0xE4FFE8u); }
static inline uint8_t menu_sprite_colour_byte(const GameState *g, uint16_t at) { return rom_read8(g, MENU_SPRITE_BANK | at); }

/* The blocks a sprite's size class has ($C2:F562), and the bytes a frame of
 * that many blocks takes in its frames ($C2:F755: 40 a block). */
static inline uint8_t menu_sprite_blocks(const GameState *g, uint8_t size) { return rom_read8(g, 0xC2F562u + (size & 3u)); }
static inline uint8_t menu_sprite_frame_bytes(const GameState *g, uint8_t blocks) { return rom_read8(g, 0xC2F755u + blocks); }

/* A byte of a sprite's graphics, frames or animation lists, wherever its
 * record's pointer puts it. */
static inline uint8_t menu_sprite_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* The first tile of each of the opening's seven records' sprites ($C2:F548, a
 * word each: tile and attributes), and of the eighth's ($C2:F556). */
enum { MENU_OPENING_RECORDS = 7 };
static inline uint16_t menu_opening_record_tile(const GameState *g, unsigned record)
{
    return (uint16_t)(rom_read8(g, 0xC2F548u + 2u * record) | rom_read8(g, 0xC2F549u + 2u * record) << 8);
}
static inline uint16_t menu_last_record_tile(const GameState *g) { return rom_read16(g, 0xC2F556u); }

/* A byte of pixels mirrored left to right ($C0:FD00). */
static inline uint8_t menu_mirrored_pixels(const GameState *g, uint8_t b) { return rom_read8(g, 0xC0FD00u + b); }

/* ---- The menus' input and portraits -------------------------------------------------------- */

/* The frames held buttons wait before they repeat, by the pacing's phase
 * ($C2:84D0: 0 quick, 1 before the first repeat). */
static inline uint8_t menu_repeat_delay(const GameState *g, uint8_t phase) { return rom_read8(g, 0xC284D0u + phase); }

/* A character's large portrait: the first of its 36 tiles ($FF:CE70, a byte
 * a character), and where in bank $FF its sixteen colours are ($FF:CE78, a
 * word each of the eight). */
enum { MENU_PORTRAIT_BANK = 0xFF0000 };
static inline uint8_t menu_large_portrait_first_tile(const GameState *g, uint8_t character)
{
    return rom_read8(g, 0xFFCE70u + character);
}
static inline uint16_t menu_large_portrait_colours(const GameState *g, unsigned slot)
{
    return rom_read16(g, 0xFFCE78u + 2u * slot);
}
static inline uint16_t menu_large_portrait_colour(const GameState *g, uint16_t colours, unsigned i)
{
    return rom_read16(g, MENU_PORTRAIT_BANK | (uint16_t)(colours + 2u * i));
}

/* A character's element: lightning, shadow, water, fire or none ($C2:A2C6, a
 * byte a character, which the ROM keeps doubled: the offset of the element's
 * word in its label's tables). */
typedef enum { MENU_LIGHTNING, MENU_SHADOW, MENU_WATER, MENU_FIRE, MENU_NO_ELEMENT } MenuElement;
static inline MenuElement menu_character_element(const GameState *g, uint8_t character)
{
    return (MenuElement)(rom_read8(g, 0xC2A2C6u + character) / 2u);
}

/* An element's label, six tiles by two beside a member's portrait: its first
 * tile word ($FF:CE88), and its sixteen colours (where in bank $FF they are,
 * $FF:CE92). */
static inline uint16_t menu_element_label_tile(const GameState *g, MenuElement element)
{
    return rom_read16(g, 0xFFCE88u + 2u * element);
}
static inline uint16_t menu_element_label_colour(const GameState *g, MenuElement element, unsigned i)
{
    uint16_t colours = rom_read16(g, 0xFFCE92u + 2u * element);
    return rom_read16(g, MENU_PORTRAIT_BANK | (uint16_t)(colours + 2u * i));
}

/* The equipment screen's slots: each slot's label, ten glyphs in bank C2
 * ($C2:A3BA, ten bytes a slot: where the string starts), and the slot's
 * item's place in a character's record ($C2:A4AB). */
enum { MENU_SLOT_LABEL_BYTES = 10 };
static inline uint32_t menu_slot_label(const GameState *g, uint8_t slot)
{
    (void)g;
    return 0xC20000u | (uint16_t)(0xA3BA + slot * MENU_SLOT_LABEL_BYTES);
}
static inline uint8_t menu_slot_record_place(const GameState *g, uint8_t slot) { return rom_read8(g, 0xC2A4ABu + slot); }

/* The equipment screen's per-line band table at rest, as the slots window
 * closes ($C2:AA06): six bands, three bytes each (the lines, the value),
 * and the table's end. */
enum { MENU_SLOTS_BANDS_AT_REST = 6 };
static inline uint8_t menu_slots_bands_at_rest_byte(const GameState *g, unsigned i) { return rom_read8(g, 0xC2AA06u + i); }

/* The equipment screen's four colours ($FF:9C70) and the four the main
 * menu's panel for it highlights with ($FF:CBAC), in bank $FF for
 * menu_rom_colour. */
enum { MENU_ROM_EQUIPMENT_COLOURS = 0x9C70, MENU_ROM_PANEL_HIGHLIGHT_COLOURS = 0xCBAC };

/* ---- The techs ------------------------------------------------------------------------------ */

/* The techs come in groups whose members do them together: each character's
 * own eight (groups 0-6), then fifteen groups of three double techs, then
 * fifteen triple techs, a group each. Each group's first tech is a byte at
 * $C2:BD40; the next group's first ends it, so the last group's end is read
 * from the byte past the table, the first of the kinds' table that follows. */
enum { MENU_TECH_GROUPS = 37 };
static inline uint8_t menu_tech_group_first(const GameState *g, uint8_t group) { return rom_read8(g, 0xC2BD40u + group); }

/* Each kind of tech's groups (the characters' own, the doubles, the triples):
 * its first group and how many it has ($C2:BD65, two bytes a kind). */
static inline uint8_t menu_tech_kind_first_group(const GameState *g, uint8_t kind) { return rom_read8(g, 0xC2BD65u + 2u * kind); }
static inline uint8_t menu_tech_kind_groups(const GameState *g, uint8_t kind) { return rom_read8(g, 0xC2BD66u + 2u * kind); }

/* A group's members, a bit a character from Crono's bit 7 ($CC:2963); and a
 * character's bit in such a byte ($FF:F9BB, nine entries, the last 0). */
static inline uint8_t menu_tech_group_members(const GameState *g, uint8_t group) { return rom_read8(g, 0xCC2963u + group); }
static inline uint8_t menu_character_bit(const GameState *g, uint8_t character) { return rom_read8(g, 0xFFF9BBu + character); }

/* A tech's effect record ($CC:213F, twelve bytes a tech, as the battle reads
 * it): for a healing tech, the second byte's low five bits are its restoring's
 * factor and the third's top two bits what it restores (HP, MP). */
enum { MENU_TECH_EFFECT_BYTES = 12, MENU_TECH_EFFECT_FACTOR = 1, MENU_TECH_EFFECT_RESTORES = 2 };
static inline uint8_t menu_tech_effect_byte(const GameState *g, uint8_t tech, unsigned n)
{
    return rom_read8(g, 0xCC213Fu + (uint32_t)(tech * MENU_TECH_EFFECT_BYTES) + n);
}

/* Who a tech is used on: the first of its two bytes at $CC:1ACB, bit 0 set
 * for every member at once. */
static inline uint8_t menu_tech_targets(const GameState *g, uint8_t tech)
{
    return rom_read8(g, 0xCC0000u | (uint16_t)(0x1ACB + 2u * tech));
}

/* What each member's part of a tech costs in MP ($CC:253B), by the part's
 * number: a single tech's is the tech itself. */
static inline uint8_t menu_tech_part_mp(const GameState *g, uint16_t part) { return rom_read8(g, 0xCC0000u | (uint16_t)(0x253B + part)); }

/* The items' descriptions: a table of where each string starts, a word each
 * in bank $CC ($CC:2EB1), by the item's number. */
enum { MENU_ITEM_DESCRIPTIONS = 0xCC2EB1 };

/* The techs' descriptions: a table of where each string starts, a word each
 * in bank $CC ($CC:3A09); after the techs' come the captions of the kinds,
 * from $76. */
enum { MENU_TECH_DESCRIPTIONS_BANK = 0xCC, MENU_TECH_DESCRIPTIONS = 0x3A09, MENU_TECH_KIND_CAPTIONS = 0x76 };

/* The techs screen's member rows: the sprite beside each row stands this far
 * down the screen ($C2:BB1A, a byte a row: the three rows, then the row a
 * slide brings in from above and the one from below). */
static inline uint8_t menu_tech_row_sprite_y(const GameState *g, uint16_t row) { return rom_read8(g, 0xC2BB1Au + row); }

/* The window of members a tech is used on: each member's sprite's place
 * ($C2:BF2F, a row's Y then its X's low byte). */
static inline MenuSpritePlace menu_tech_target_sprite_place(const GameState *g, uint8_t member)
{
    return (MenuSpritePlace){ rom_read8(g, 0xC2BF2Fu + 2u * member), rom_read8(g, 0xC2BF30u + 2u * member) };
}

/* The frame a character's sprite holds while it uses a healing tech
 * ($C2:B519, a byte a character). */
static inline uint8_t menu_tech_casting_frame(const GameState *g, uint8_t character) { return rom_read8(g, 0xC2B519u + character); }

/* The main menu's panel for the techs: the colours of its kinds' tabs and
 * its list ($FF:CE04 and $FF:CDF4, eight each), for menu_rom_colour. */
enum { MENU_ROM_TECH_PANEL_COLOURS = 0xCE04, MENU_ROM_TECH_PANEL_LIST_COLOURS = 0xCDF4 };

/* ---- The party exchange -------------------------------------------------------------------- */

/* The places a member's row and sprite go to on the exchange's screen
 * (menu/exchange.c), twelve: the active party's three, the reserve's four
 * shown, the reserve's rows above and below those (where a slide brings one
 * in from), and the active party's three in the middle. Where the row's
 * status window is drawn on the front layer ($C2:CDCB, a word each) and
 * where the sprite stands ($C2:CE6E, its Y then its X's low byte). */
enum { MENU_EXCHANGE_PLACES = 12 };
static inline uint16_t menu_exchange_row_at(const GameState *g, unsigned place) { return rom_read16(g, 0xC2CDCBu + 2u * place); }
static inline MenuSpritePlace menu_exchange_sprite_place(const GameState *g, unsigned place)
{
    return (MenuSpritePlace){ rom_read8(g, 0xC2CE6Eu + 2u * place), rom_read8(g, 0xC2CE6Fu + 2u * place) };
}

/* Its panel's colours ($FF:CC64, eight, palette row 6's first half); its
 * highlight takes the items screen's four. */
enum { MENU_ROM_EXCHANGE_PANEL_COLOURS = 0xCC64, MENU_EXCHANGE_PANEL_COLOURS = 8, MENU_EXCHANGE_HIGHLIGHT_COLOURS = 4 };

/* The menus' labels, set by the typesetter (menu/typeset.h): the strings
 * at $FF:CF3B. */
enum { MENU_LABEL_STRINGS = 0xFFCF3B };

/* ---- The menus' display ------------------------------------------------------------------- */

/* The colors the menus' color math ramps step through ($C2:8771, sixteen
 * words), copied into work RAM before each table is built. */
static inline uint16_t menu_math_ramp_color(const GameState *g, unsigned i) { return rom_read16(g, 0xC28771u + 2u * i); }

/* The values of the window line table's sixteen groups ($FF:FC57). */
static inline uint8_t menu_window_line_value(const GameState *g, unsigned group) { return rom_read8(g, 0xFFFC57u + group); }

/* ---- The save code ------------------------------------------------------------------------ */

/* What a saved game holds: eight runs of work RAM, stored one after the other
 * from the slot's start ($FF:FA7B, five bytes a run: where it is, a 24-bit
 * address, then its length). */
enum { MENU_SAVED_RUNS = 8, MENU_SAVED_RUN_BYTES = 5 };
typedef struct {
    uint32_t at;
    uint16_t bytes;
} MenuSavedRun;
static inline uint8_t menu_saved_runs_byte(const GameState *g, unsigned i) { return rom_read8(g, 0xFFFA7Bu + i); }
static inline MenuSavedRun menu_saved_run(const GameState *g, unsigned run)
{
    uint32_t at = 0xFFFA7Bu + run * MENU_SAVED_RUN_BYTES;
    return (MenuSavedRun){ (uint32_t)rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16, rom_read16(g, at + 3) };
}

/* What a New Game+ takes over from the game it carries on from: two runs
 * ($FF:FBC1, seven bytes a run: where it starts in the saved game, where it
 * goes in work RAM, a 24-bit address, and its length). */
enum { MENU_CARRIED_RUNS = 2, MENU_CARRIED_RUN_BYTES = 7 };
typedef struct {
    uint16_t from;
    uint32_t at;
    uint16_t bytes;
} MenuCarriedRun;
static inline uint8_t menu_carried_runs_byte(const GameState *g, unsigned i) { return rom_read8(g, 0xFFFBC1u + i); }
static inline MenuCarriedRun menu_carried_run(const GameState *g, unsigned run)
{
    uint32_t at = 0xFFFBC1u + run * MENU_CARRIED_RUN_BYTES;
    return (MenuCarriedRun){ rom_read16(g, at), (uint32_t)rom_read16(g, at + 2) | (uint32_t)rom_read8(g, at + 4) << 16,
                             rom_read16(g, at + 5) };
}

/* Where each slot's game is in save memory, as the original's code addresses
 * it on the bus ($FF:FBE1, a word a slot). */
static inline uint16_t menu_saved_game_on_the_bus(const GameState *g, uint8_t slot) { return rom_read16(g, 0xFFFBE1u + 2u * slot); }

/* A character's record as a new game starts it ($CC:0000, $50 bytes a
 * character), and the places in it of the equipment a New Game+ puts back on
 * ($FF:FBCF, four bytes: the helmet's, the armour's, the accessory's and the
 * weapon's, taken from the last). */
enum { MENU_STARTING_RECORD_BYTES = 0x50, MENU_STARTING_EQUIPMENT = 4 };
static inline uint8_t menu_starting_record_byte(const GameState *g, uint8_t character, uint8_t at)
{
    return rom_read8(g, 0xCC0000u + (uint32_t)(character * MENU_STARTING_RECORD_BYTES) + at);
}
static inline uint8_t menu_starting_equipment_place(const GameState *g, unsigned which) { return rom_read8(g, 0xFFFBCFu + which); }

/* What a new game starts the characters' records and what they have learned
 * from ($CC:0000: the seven records, then the techs learned); and the
 * settings it starts with ($C2:FCA6, before the button assignments). */
enum { MENU_NEW_GAME_RECORDS_BYTES = 0x280 };
static inline uint8_t menu_new_game_record_byte(const GameState *g, unsigned i) { return rom_read8(g, 0xCC0000u + i); }
static inline uint8_t menu_new_game_setting(const GameState *g, unsigned i) { return rom_read8(g, 0xC2FCA6u + i); }

/* A weapon's colours: its colour set ($CD:6CEC, a byte a weapon) and the
 * set's three colours ($D1:4B00, three words a set), which go in its
 * wearer's sprite palette -- the battle's colours for its members too
 * (battle/setup_data.h). */
enum { MENU_WEAPON_COLOURS = 3 };
static inline uint8_t menu_weapon_colour_set(const GameState *g, uint8_t weapon) { return rom_read8(g, 0xCD6CECu + weapon); }
static inline uint16_t menu_weapon_colour(const GameState *g, uint8_t set, unsigned i)
{
    return rom_read16(g, 0xD14B00u + (uint32_t)(set * MENU_WEAPON_COLOURS + i) * 2u);
}

/* ---- The techs as the menus open ---------------------------------------------------------- */

/* The double techs' characters: two for each of the fifteen groups
 * ($CC:24B3, three bytes a group, the characters in the second and third). */
static inline uint8_t menu_double_tech_character(const GameState *g, uint8_t group, unsigned which)
{
    return rom_read8(g, 0xCC24B4u + 3u * group + which);
}
/* Each character's part in each of a double group's three techs ($CC:28DB,
 * six bytes a group: a tech's first character's part, then its second's). */
static inline uint8_t menu_double_tech_part(const GameState *g, uint8_t group, unsigned tech, unsigned which)
{
    return rom_read8(g, 0xCC28DBu + 6u * group + 2u * tech + which);
}
/* The triple techs' parts, one after another: each tech's characters' in the
 * order of their bits from Crono's ($CC:2935). */
static inline uint8_t menu_triple_tech_part(const GameState *g, unsigned n) { return rom_read8(g, 0xCC2935u + n); }

/* The single techs each of the five triple techs a rock gives needs learned
 * by its characters ($CC:2953, three a rock: the tech's number from 1). */
enum { MENU_ROCKS = 5 };
static inline uint8_t menu_rock_tech_needs(const GameState *g, unsigned rock, unsigned which)
{
    return rom_read8(g, 0xCC2953u + 3u * rock + which);
}

/* How many of a character's single techs show in the menus before its
 * locked ones are unlocked ($FF:F951, a byte a character; $FF none locked). */
static inline uint8_t menu_techs_before_the_lock(const GameState *g, uint8_t character) { return rom_read8(g, 0xFFF951u + character); }

/* ---- The screens' digits ------------------------------------------------------------------ */

/* Two sets of the ten digits in four-colour tiles: the small digits a
 * screen's number tiles are made from ($FF:CAFC), and the items screen's
 * amounts' ($FF:9390), sixteen bytes a digit. */
enum { MENU_DIGITS = 10, MENU_DIGIT_BYTES = 16 };
static inline uint8_t menu_small_digit_byte(const GameState *g, unsigned digit, unsigned at)
{
    return rom_read8(g, 0xFFCAFCu + digit * MENU_DIGIT_BYTES + at);
}
static inline uint8_t menu_amount_digit_byte(const GameState *g, unsigned digit, unsigned at)
{
    return rom_read8(g, 0xFF9390u + digit * MENU_DIGIT_BYTES + at);
}

/* ---- The settings screen ------------------------------------------------------------------ */

/* Each option's place among the settings' cells ($C2:C4F9: the sound output
 * first, as the menus unpack them, the byte after the last for the speeds,
 * which have no value of their own), and the first of its value texts among
 * the descriptions ($C2:C505): the text shown is the first plus the value. */
enum { MENU_SETTINGS_OPTIONS = 12 };
static inline uint8_t menu_settings_option_cell(const GameState *g, uint8_t option) { return rom_read8(g, 0xC2C4F9u + option); }
static inline uint8_t menu_settings_first_value_text(const GameState *g, uint8_t option)
{
    return rom_read8(g, 0xC2C505u + option);
}

/* Each option's icon at the description window's left ($FF:D562): the tile
 * word of its top left, the four tiles on from it. The descriptions' strings
 * ($FF:D3FE), an option's first, then the values' texts. */
enum { MENU_SETTINGS_DESCRIPTIONS = 0xFFD3FE };
static inline uint16_t menu_settings_option_icon(const GameState *g, uint8_t option)
{
    return rom_read16(g, 0xFFD562u + 2u * option);
}

/* Each of the ten boxes' place in the front layer's tilemap ($C2:C41B, a
 * tile's index), and whether it is ticked for a cell of 0 ($C2:C42F: the
 * sound output's, whose cell holds mono). */
enum { MENU_SETTINGS_BOXES = 10 };
static inline uint16_t menu_settings_box_place(const GameState *g, uint8_t box) { return rom_read16(g, 0xC2C41Bu + 2u * box); }
static inline bool menu_settings_box_ticked_for_off(const GameState *g, uint8_t box)
{
    return (rom_read8(g, 0xC2C42Fu + box) & 1u) != 0;
}

/* The buttons an assignment can be given, in the order left and right step
 * through them ($C2:C207, seven and an end); the game's own assignments
 * ($C2:FCA9, nine); and the glyph of a button ($FF:D554, the buttons from
 * the top bit down and a ninth for none). */
enum { MENU_SETTINGS_BUTTON_CHOICES = 7, MENU_SETTINGS_NO_MORE_CHOICES = 0xFF };
static inline uint8_t menu_settings_button_choice(const GameState *g, unsigned i) { return rom_read8(g, 0xC2C207u + i); }
static inline uint8_t menu_default_button_assignment(const GameState *g, unsigned i) { return rom_read8(g, 0xC2FCA9u + i); }
static inline uint16_t menu_button_glyph(const GameState *g, unsigned bit_from_the_top)
{
    return rom_read16(g, 0xFFD554u + 2u * bit_from_the_top);
}

/* Each assignment's row on the settings screen ($C2:C5A7: its tile index in
 * the front layer's tilemap, from $7E:2E00's start), and the assignment a
 * row gives a button to ($C2:C2BD). */
enum { MENU_SETTINGS_BUTTON_ROWS = 6 };
static inline uint16_t menu_settings_button_row_place(const GameState *g, unsigned row)
{
    return rom_read16(g, 0xC2C5A7u + 2u * row);
}
static inline uint8_t menu_settings_row_assignment(const GameState *g, unsigned row) { return rom_read8(g, 0xC2C2BDu + row); }

/* The assignments a button given to one is taken from ($C2:C2C5, the place
 * of a list in bank C2 for each of the eight: a count, then the
 * assignments). The table has seven lists, so the eighth's place is the
 * first list's first two bytes, and its list lies in bank C2's code. */
static inline uint16_t menu_settings_conflicts_place(const GameState *g, unsigned assignment)
{
    return rom_read16(g, 0xC2C2C5u + 2u * assignment);
}
static inline uint8_t menu_settings_conflicts_count(const GameState *g, unsigned assignment)
{
    return rom_read8(g, 0xC20000u | menu_settings_conflicts_place(g, assignment));
}
static inline uint8_t menu_settings_conflict(const GameState *g, unsigned assignment, unsigned i)
{
    return rom_read8(g, 0xC20000u | (uint16_t)(menu_settings_conflicts_place(g, assignment) + 1u + i));
}

/* The panel's colours ($FF:CBE4, 24). */
enum { MENU_ROM_SETTINGS_PANEL_COLOURS = 0xCBE4 };

/* ---- The shops ---------------------------------------------------------------------------- */

/* Each shop's goods ($CC:2C6D, the place of a shop's list in bank $CC; a
 * list is the items it sells, 0 after the last, thirty-two bytes read). */
enum { MENU_SHOP_LIST_BYTES = 32 };
static inline uint8_t menu_shop_good(const GameState *g, uint8_t shop, unsigned i)
{
    uint16_t list = rom_read16(g, 0xCC2C6Du + 2u * shop);
    return rom_read8(g, 0xCC0000u | (uint16_t)(list + i));
}

/* Where each member's sprite stands along the shop's bottom window
 * ($C2:DC74, an X each). */
static inline uint8_t menu_shop_member_x(const GameState *g, unsigned member) { return rom_read8(g, 0xC2DC74u + member); }

/* The place in a character's record of the slot an item of each category
 * goes in ($C2:DE1D; the ROM has a copy at $C2:894F): the weapon, the
 * armour, the helmet, the accessory. The other items' category reads the
 * byte past the table, the first of the routine after it in both copies, 8,
 * a byte of the record that is no slot. */
static inline uint8_t menu_category_slot_place(const GameState *g, uint8_t category) { return rom_read8(g, 0xC2DE1Du + category); }

/* The seven figures the shop's members' window compares, by their places in
 * a character's record ($C2:E1DC). */
enum { MENU_COMPARED_FIGURES = 7 };
static inline uint8_t menu_compared_figure_place(const GameState *g, unsigned figure) { return rom_read8(g, 0xC2E1DCu + figure); }

/* The quantity window's list of what an item of each category shows beside
 * its count ($C2:DFC5, a window list in bank $FF each). */
static inline uint16_t menu_shop_quantity_windows(const GameState *g, uint8_t category) { return rom_read16(g, 0xC2DFC5u + 2u * category); }

/* The upload of the member sprites' graphics the shop unpacks ($C2:DB98, a
 * record of menu_upload_record's). */
enum { MENU_UPLOAD_SHOP_SPRITES = 0xDB98 };

/* ---- The End of Time's gate screen -------------------------------------------------------- */

/* The swirl's graphics, packed ($FF:E8DC); the first tile of each of its six
 * rows ($FF:F6F2, a tilemap word each); its eight colours ($FF:F706) and four
 * more ($FF:F6FE); and the wave its bands of lines follow ($FF:F759, 64
 * words). */
enum { MENU_GATE_PACKED_GRAPHICS = 0xFFE8DC, MENU_GATE_ROWS = 6, MENU_GATE_PALETTE_COLOURS = 8, MENU_GATE_COLOURS = 4,
       MENU_GATE_WAVE = 64 };
static inline uint16_t menu_gate_row_first_tile(const GameState *g, unsigned row) { return rom_read16(g, 0xFFF6F2u + 2u * row); }
static inline uint16_t menu_gate_palette_colour(const GameState *g, unsigned i) { return rom_read16(g, 0xFFF706u + 2u * i); }
static inline uint16_t menu_gate_colour(const GameState *g, unsigned i) { return rom_read16(g, 0xFFF6FEu + 2u * i); }
static inline uint16_t menu_gate_wave(const GameState *g, unsigned i) { return rom_read16(g, 0xFFF759u + 2u * i); }

#endif
