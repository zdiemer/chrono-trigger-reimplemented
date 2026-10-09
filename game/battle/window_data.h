/* Battle: what the windows and the numbers over the combatants read from the
 * ROM.
 *
 * Every read of cartridge data the window's files make goes through here, by
 * what the data is (the asset seam, docs/PLAN.md Phase 3), at the index the
 * original uses, so a build that loads these from somewhere else maps them in
 * this one file. */
#ifndef CT_GAME_BATTLE_WINDOW_DATA_H
#define CT_GAME_BATTLE_WINDOW_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The window's digit tiles, 0-9 ($CC:F903), and the number sprites' ($CC:F90D).
 * A digit past 9 reads on into what follows, as the original does. */
static inline uint8_t battle_digit_tile(const GameState *g, uint16_t digit) { return rom_read8(g, 0xCCF903u + digit); }
static inline uint8_t battle_sprite_digit_tile(const GameState *g, uint16_t digit)
{
    return rom_read8(g, 0xCCF90Du + digit);
}

/* A member's command window, seven tiles by six: with Tech, or with Combo when
 * it can join a combination tech ($CC:FA41, $CC:FA6B). */
static inline uint8_t battle_command_window_tile(const GameState *g, bool combo, unsigned i)
{
    return rom_read8(g, (combo ? 0xCCFA6Bu : 0xCCFA41u) + i);
}

/* The command windows' frame, seven cells by six, a tile and its attributes
 * each ($D1:59FC). */
static inline uint8_t battle_command_frame_tile(const GameState *g, unsigned cell)
{
    return rom_read8(g, 0xD159FCu + 2 * cell);
}
static inline uint8_t battle_command_frame_attributes(const GameState *g, unsigned cell)
{
    return rom_read8(g, 0xD159FCu + 2 * cell + 1);
}

/* The frames tilemap as a battle starts it, and as the command page puts it
 * back, a tile and its attributes a cell ($D1:5800). */
static inline uint8_t battle_first_frames_tile(const GameState *g, unsigned cell) { return rom_read8(g, 0xD15800u + 2 * cell); }
static inline uint8_t battle_first_frames_attributes(const GameState *g, unsigned cell)
{
    return rom_read8(g, 0xD15800u + 2 * cell + 1);
}

/* The frames tilemap as the tech page leaves it, a tile and its attributes a
 * cell ($D1:5A50). */
static inline uint8_t battle_tech_page_frames_tile(const GameState *g, unsigned cell)
{
    return rom_read8(g, 0xD15A50u + 2 * cell);
}
static inline uint8_t battle_tech_page_frames_attributes(const GameState *g, unsigned cell)
{
    return rom_read8(g, 0xD15A50u + 2 * cell + 1);
}

/* The frames tilemap as the item page puts it, a tile and its attributes a
 * cell ($D1:5BD0). */
static inline uint8_t battle_item_page_frames_tile(const GameState *g, unsigned cell)
{
    return rom_read8(g, 0xD15BD0u + 2 * cell);
}
static inline uint8_t battle_item_page_frames_attributes(const GameState *g, unsigned cell)
{
    return rom_read8(g, 0xD15BD0u + 2 * cell + 1);
}

/* The techs' and the items' names, eleven characters each ($CC:15C4,
 * $CC:0B5E). */
enum { BATTLE_NAME_CHARACTERS = 11 };
static inline uint8_t battle_tech_name_char(const GameState *g, uint8_t tech, unsigned i)
{
    return rom_read8(g, 0xCC15C4u + (uint32_t)tech * BATTLE_NAME_CHARACTERS + i);
}
static inline uint8_t battle_item_name_char(const GameState *g, uint8_t item, unsigned i)
{
    return rom_read8(g, 0xCC0B5Eu + (uint32_t)item * BATTLE_NAME_CHARACTERS + i);
}

/* The headings of the tech list's sections, the double techs' and the triple
 * techs', eighteen tiles each ($CC:FB4B). */
enum { BATTLE_TECH_HEADING_TILES = 18 };
static inline uint8_t battle_tech_heading_tile(const GameState *g, bool triple, unsigned i)
{
    return rom_read8(g, 0xCCFB4Bu + (triple ? BATTLE_TECH_HEADING_TILES : 0) + i);
}

/* The four corners of the frame around the member choosing, a sprite each:
 * where it goes from the member's position, its tile and its attributes
 * ($CC:F604). */
typedef struct {
    int8_t dx, dy;
    uint8_t tile, attributes;
} ChoosingFrameCorner;
static inline ChoosingFrameCorner battle_choosing_frame_corner(const GameState *g, unsigned corner)
{
    uint32_t at = 0xCCF604u + 4 * corner;
    return (ChoosingFrameCorner){ (int8_t)rom_read8(g, at), (int8_t)rom_read8(g, at + 1), rom_read8(g, at + 2),
                                  rom_read8(g, at + 3) };
}

/* Where each combatant's stat block starts in battle_stats, by battle slot
 * ($CC:F8ED, a word each); a slot past the combatants (the menu's nobody)
 * reads on into what follows, as the original does. */
static inline uint16_t battle_stat_block_place(const GameState *g, uint8_t slot)
{
    return rom_read16(g, 0xCCF8EDu + 2u * slot);
}

/* A party slot's bit in a byte of members, the first slot's the top bit
 * ($CC:F9FB). */
static inline uint8_t battle_member_bit(const GameState *g, uint8_t member) { return rom_read8(g, 0xCCF9FBu + member); }

/* The tech page's column beside the list, nine tiles a row for its six rows
 * ($CC:FA95). */
static inline uint8_t battle_tech_page_column_tile(const GameState *g, unsigned row, unsigned column)
{
    return rom_read8(g, 0xCCFA95u + row * 9 + column);
}

/* Where the cursor points at each of the item list's rows, as a cell counted
 * from the list's first ($CC:FAE9 holds byte offsets). */
static inline uint16_t battle_item_cursor_cell(const GameState *g, uint8_t row)
{
    return (uint16_t)(rom_read16(g, 0xCCFAE9u + 2u * row) / 2);
}

/* How far right of a number's position each of its four digit sprites goes,
 * by how many of its digits are blank in front, which centers it ($CC:F5C4). */
static inline uint8_t battle_digit_sprite_offset(const GameState *g, unsigned blanks, unsigned place)
{
    return rom_read8(g, 0xCCF5C4u + 4 * blanks + place);
}

/* Where each number command's set of numbers starts among the number
 * records, a record being 4 bytes ($CC:F829, by the command's low bits). */
static inline uint8_t battle_number_set_start(const GameState *g, unsigned set) { return rom_read8(g, 0xCCF829u + set); }

/* Enemies whose name the target display leaves out ($E1:DE80, by enemy). */
static inline bool battle_enemy_name_hidden(const GameState *g, uint8_t enemy) { return rom_read8(g, 0xE1DE80u + enemy) != 0; }

#endif
