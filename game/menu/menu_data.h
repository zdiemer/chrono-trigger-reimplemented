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
 * lines ($C2:5FE6): the byte offset of each column's tile in the text's tile
 * buffer, 32 columns, a word each. A line of text is two rows of tiles; the
 * first 16 columns are its left half, $10 bytes a tile, and the other 16 its
 * right half, $200 bytes on. */
enum { MENU_TEXT_COLUMNS = 32 };
static inline uint16_t menu_text_column_offset(const GameState *g, unsigned column)
{
    return rom_read16(g, 0xC25FE6u + 2u * column);
}

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

/* The cartridge's save RAM: set once a game has been finished ($30:7FE2),
 * which gives the load screen its fourth row. */
static inline bool menu_saves_finished_game(const GameState *g) { return rom_read8(g, 0x307FE2u) != 0; }

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

#endif
