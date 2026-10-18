/* What the field's code reads from the ROM, apart from the map and event data.
 *
 * Every such read goes through an accessor here, named after what the data is
 * for, so a build that loads the data from somewhere other than the ROM image
 * changes this file and nothing that uses it. The accessors take the game's
 * state (game/rom.h), not the machine. */
#ifndef CT_GAME_FIELD_DATA_H
#define CT_GAME_FIELD_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The four tables of the objects' sprite data a sprite record's fields pick
 * from (below), in the order field_sprite_data_tables keeps them. */
typedef enum {
    FIELD_SPRITE_GRAPHICS_SETS,
    FIELD_SPRITE_ASSEMBLIES,
    FIELD_SPRITE_ANIMATIONS,
    FIELD_SPRITE_FRAME_TIMES,
    FIELD_SPRITE_TABLES,
} FieldSpriteTable;

/* Where each of the tables of the objects' sprite data is (field_sprite_data_tables),
 * as a long address: in bank $E4, at the word $E4:FFE0 keeps for it. */
static inline uint32_t field_sprite_data_table(const GameState *g, FieldSpriteTable table)
{
    return 0xE40000u | rom_read16(g, 0xE4FFE0u + 2u * table);
}

/* What a sprite looks like, as a record of five fields, each picking an entry
 * of one of the tables above: its graphics set, its assembly (how its frames
 * lay its tiles out), its palette, its animations, and its layout (how many
 * sprites it is drawn with). The field's records (MD2's field_sprite_records,
 * five bytes from $E4:F000) are the seven playable characters' and then the
 * people's and creatures' a script loads by number; the battle's (MD2's
 * battle_sprite_records, ten bytes from $E4:F600) are the enemies', whose
 * first five bytes are the same fields. */
typedef struct {
    uint8_t graphics;
    uint8_t assembly;
    uint8_t palette;
    uint8_t animations;
    uint8_t layout;
} FieldSpriteRecord;

enum { FIELD_SPRITE_RECORD_BYTES = 5, BATTLE_SPRITE_RECORD_BYTES = 10 };

static inline FieldSpriteRecord field_sprite_record_at(const GameState *g, uint32_t at)
{
    return (FieldSpriteRecord){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2), rom_read8(g, at + 3),
                                rom_read8(g, at + 4) };
}
static inline FieldSpriteRecord field_sprite_record(const GameState *g, uint16_t id)
{
    return field_sprite_record_at(g, 0xE4F000u + (uint16_t)(id * FIELD_SPRITE_RECORD_BYTES));
}
static inline FieldSpriteRecord battle_sprite_record(const GameState *g, uint8_t id)
{
    return field_sprite_record_at(g, 0xE4F600u + (uint16_t)(id * BATTLE_SPRITE_RECORD_BYTES));
}

/* The tables a sprite record's fields pick from, where field_sprite_data_tables
 * points them (`table`, a long address; an entry's place carries into the
 * next bank as the original's long reads carry): the graphics sets and the
 * assemblies, each entry a long address of three bytes; the animations, each
 * entry the place of the sprite's frame table, a word (the next sprite's
 * after it, so the difference is the table's size); and beside it the
 * table of each one's frame times. */
enum { FIELD_LONG_ENTRY_BYTES = 3, FIELD_WORD_ENTRY_BYTES = 2 };
static inline uint32_t field_sprite_long_entry(const GameState *g, uint32_t table, uint8_t index)
{
    uint32_t at = (table + (uint32_t)index * FIELD_LONG_ENTRY_BYTES) & 0xFFFFFFu;
    return (uint32_t)rom_read16(g, at) | (uint32_t)rom_read8(g, (at + 2) & 0xFFFFFFu) << 16;
}
static inline uint16_t field_sprite_word_entry(const GameState *g, uint32_t table, uint16_t index)
{
    return rom_read16(g, (table + (uint32_t)index * FIELD_WORD_ENTRY_BYTES) & 0xFFFFFFu);
}

/* A sprite palette's colours: twelve BGR words a palette, from $E4:0000 (a
 * sprite record's palette times their 24 bytes), which load into a sprite
 * palette's colours 1-12. `colours` is where the palette starts in the bank;
 * a colour past its end wraps in the bank, as the original's block move does. */
enum { FIELD_SPRITE_COLOURS = 12, FIELD_SPRITE_COLOURS_BANK = 0xE4 };
static inline uint16_t field_sprite_palette_place(uint8_t palette)
{
    return (uint16_t)(palette * FIELD_SPRITE_COLOURS * 2);
}
static inline uint16_t field_sprite_colour(const GameState *g, uint16_t colours, unsigned n)
{
    uint32_t bank = (uint32_t)FIELD_SPRITE_COLOURS_BANK << 16;
    return (uint16_t)(rom_read8(g, bank | (uint16_t)(colours + 2 * n)) |
                      rom_read8(g, bank | (uint16_t)(colours + 2 * n + 1)) << 8);
}

/* The wave a waving layer's scroll follows: signed 16-bit horizontal offsets,
 * a word each, at $C0:FF30. `at` is the original's byte index; the line
 * effects read 16 words 4 bytes apart from an even start of at most $3E, so
 * the table runs to $C0:FFAB. */
static inline uint16_t field_wave_offset(const GameState *g, uint16_t at) { return rom_read16(g, 0xC0FF30u + at); }

/* The bit-reversed value of every byte, at $C0:FD00: a sprite tile mirrored
 * left to right, a byte (8 pixels of one bit plane) at a time. */
static inline uint8_t field_mirrored_byte(const GameState *g, uint8_t b) { return rom_read8(g, 0xC0FD00u + b); }

/* The credits the endings roll on BG3 (field/script_layers.c): texts of
 * capital letters, one after another from $FD:A000, each line ended by '/'
 * and each text by a byte with its top bit set. `at` is an offset from the
 * first text's start, carried into the next bank as the original's indexed
 * reads carry. */
static inline uint8_t field_credits_byte(const GameState *g, uint16_t at) { return rom_read8(g, 0xFDA000u + at); }

/* The credits' letters, packed (the unpacker's stream) at $FD:0000: the font
 * the endings' credits are written in, which event $29 loads into BG3's
 * tiles. */
enum { FIELD_CREDITS_LETTERS_PACKED = 0xFD0000 };

/* The blank tile map entries a row of BG3's staging is cleared with before a
 * credits line: 32 bytes at $D2:0000, the start of the sprite graphics' first
 * bank, which hold 0. */
enum { FIELD_BLANK_ENTRIES_BYTES = 32 };
static inline uint8_t field_blank_entries_byte(const GameState *g, unsigned i) { return rom_read8(g, 0xD20000u + i); }

/* Sprite tile graphics, wherever a graphics set's pointer puts them: the ROM's
 * banks $D2-$D5, or graphics built in bank $7F. `at` is a 24-bit address,
 * carried into the next bank as the original's indexed reads carry. */
static inline uint8_t field_sprite_graphics_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

/* An object's frame descriptions (field/frames.c): for each frame, the tiles
 * its sprites show, a word each (four a sprite), and then each sprite's X and
 * Y offset from the object, a byte each. `at` is a 24-bit address, carried
 * into the next bank as the original's indexed reads carry. */
static inline uint16_t field_frame_tile_word(const GameState *g, uint32_t at) { return rom_read16(g, at); }
static inline uint8_t field_frame_offset_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

/* A bank every address of which holds data -- the ROM's high banks and their
 * mirrors, or work RAM -- where a read never finds nothing to answer it and
 * takes what the bus last held. */
static inline bool field_bank_always_answers(uint8_t bank)
{
    return (bank >= 0x40 && bank < 0x80) || bank >= 0xC0;
}

/* The ROM's banks of sprite graphics, which a graphics set's bank names: the
 * original copies from each of the four with a routine of its own, and a bank
 * past them (or before) from the last. */
enum { FIELD_SPRITE_GRAPHICS_FIRST_BANK = 0xD2, FIELD_SPRITE_GRAPHICS_LAST_BANK = 0xD5 };
static inline uint8_t field_sprite_graphics_rom_bank(uint8_t named)
{
    return named >= FIELD_SPRITE_GRAPHICS_FIRST_BANK && named < FIELD_SPRITE_GRAPHICS_LAST_BANK
               ? named
               : (uint8_t)FIELD_SPRITE_GRAPHICS_LAST_BANK;
}
static inline uint16_t field_sprite_graphics_word(const GameState *g, uint32_t at) { return rom_read16(g, at); }

/* An object's animations as the ROM keeps them: four frame numbers an
 * animation, ending in $FF, in the object's frame table for its facing, in
 * the bank the frame builder names; and four frame times an animation,
 * ending in 0, in its table of times in bank $E4. A frame's place in either
 * is the table's offset plus animation * 4 plus the frame, summed in 16
 * bits as the original does. */
enum { FIELD_FRAMES_A_ANIMATION = 4, FIELD_ANIMATION_TIMES_BANK = 0xE4 };
/* The bank the frame builder names for the frame tables (field/frames.c). */
enum { FIELD_FRAME_TABLES_BANK = 0xE4 };

typedef struct {
    uint8_t bank;
    uint16_t offset;
} FieldFrameTable;

static inline uint16_t field_animation_place(uint16_t table, uint8_t animation, uint8_t frame)
{
    return (uint16_t)(table + animation * FIELD_FRAMES_A_ANIMATION + frame);
}

static inline uint8_t field_animation_frame_number(const GameState *g, FieldFrameTable t, uint16_t place)
{
    return rom_read8(g, (uint32_t)t.bank << 16 | place);
}

static inline uint8_t field_animation_frame_time(const GameState *g, uint16_t place)
{
    return rom_read8(g, (uint32_t)FIELD_ANIMATION_TIMES_BANK << 16 | place);
}

/* A direction's angle within its quadrant, at $C0:F300, looked up by the two
 * distances packed into an index (the across distance in the low five bits,
 * the along one above). */
static inline uint8_t field_quadrant_angle(const GameState *g, uint16_t index) { return rom_read8(g, 0xC0F300u + index); }

/* The velocity factor of each of the 256 directions, at $C0:F800: the
 * magnitude on one axis, which the caller signs by the half circle. */
static inline uint8_t field_direction_factor(const GameState *g, uint8_t direction)
{
    return rom_read8(g, 0xC0F800u + direction);
}

/* The facing (0-3) an object moving in each of the 256 directions takes, at
 * $C0:F700. */
static inline uint8_t field_facing_of_direction(const GameState *g, uint8_t direction)
{
    return rom_read8(g, 0xC0F700u + direction);
}

/* The game's 256 random bytes, at $C0:FE00, which it steps through
 * (field_draw_random): a blocked object's new direction, a script's dice. */
static inline uint8_t field_random_byte(const GameState *g, uint8_t index) { return rom_read8(g, 0xC0FE00u + index); }

/* The ground's slopes (field/walk.c): a tile whose shape is one of the 22
 * slopes is parted into its upper and its lower level by a line across it.
 * For each, how far a point's place across the tile is shifted down to rise
 * along the line (mirrored: counted from the tile's other side), where the
 * line starts, and which side of it is the upper level ($C0:9BF5, $C0:9C0B,
 * $C0:9C21, 22 bytes each). */
enum { FIELD_SLOPES = 22 };
typedef struct {
    uint8_t shift;
    bool mirrored;
    uint8_t start;
    uint8_t upper_side;
} FieldSlope;

static inline FieldSlope field_slope(const GameState *g, uint8_t shape)
{
    uint8_t shift = rom_read8(g, 0xC09BF5u + shape);
    uint8_t start = rom_read8(g, 0xC09C0Bu + shape);
    uint8_t side = rom_read8(g, 0xC09C21u + shape);
    return (FieldSlope){ (uint8_t)(shift & 0x7F), (shift & 0x80) != 0, start, side };
}

/* A location's exit (field/walk.c): a tile of the map, and the location the
 * party goes to from it, as a script writes a location (event/locations.c):
 * the map in the low 9 bits of a word whose high byte says how, and where the
 * party stands there. The exits are seven bytes each in bank $E5, from the
 * place field_location_exits says. */
typedef struct {
    uint8_t x, y;
    uint8_t reach;   /* how many tiles past the first the exit spans: down where EXIT_RUNS_DOWN is set, across otherwise */
    uint16_t map_and_how;
    uint8_t to_x, to_y;
} FieldExit;

enum { EXIT_RUNS_DOWN = 0x80, EXIT_MORE_TILES = 0x7F, FIELD_EXIT_BYTES = 7 };

static inline FieldExit field_exit(const GameState *g, uint16_t exits, uint8_t index)
{
    uint32_t at = 0xE50000u + (uint16_t)(exits + index * FIELD_EXIT_BYTES);
    FieldExit e;
    e.x = rom_read8(g, at);
    e.y = rom_read8(g, at + 1);
    e.reach = rom_read8(g, at + 2);
    e.map_and_how = (uint16_t)(rom_read8(g, at + 3) | rom_read8(g, at + 4) << 8);
    e.to_x = rom_read8(g, at + 5);
    e.to_y = rom_read8(g, at + 6);
    return e;
}

/* A chest (event/chests.c): the tile it stands on (row << 8 | column), and
 * what it holds (event/chests.c says how). Four bytes each, one list for the
 * game in bank $F5, a location's a run of it (field_location_chests); a chest's place
 * is where in the bank its record is. */
enum { FIELD_CHEST_BYTES = 4 };
typedef struct {
    uint16_t tile;
    uint16_t contents;
} FieldChest;

static inline uint16_t field_chest_place(uint16_t first, uint8_t index)
{
    return (uint16_t)(first + index * FIELD_CHEST_BYTES);
}

static inline FieldChest field_chest_at(const GameState *g, uint16_t place)
{
    uint32_t at = 0xF50000u + place;
    return (FieldChest){ rom_read16(g, at), rom_read16(g, at + 2) };
}

/* Where the game's chests start, and a location's run of them: a word per
 * location (by the location doubled) from $F5:F000, the run ending where the
 * next location's starts. */
typedef struct {
    uint16_t first, end;
} FieldChestRun;

static inline uint16_t field_games_chests_start(const GameState *g) { return rom_read16(g, 0xF5F000u); }
static inline FieldChestRun field_location_chest_run(const GameState *g, uint16_t location_twice)
{
    uint32_t at = 0xF5F000u + location_twice;
    return (FieldChestRun){ rom_read16(g, at), rom_read16(g, at + 2) };
}

/* ---- The dialog window's looks ------------------------------------------------------------ */

/* The window designs the player chooses among (the settings' second byte,
 * the menus' too), and each one's graphics: a pointer per design at $C0:6EA0
 * to its frame's tiles in bank $FF, $200 bytes, with the interior's pattern
 * after them, $80. And each design's colours, from $FF:B210, eight colours a
 * design apart, of which the window takes fifteen. */
enum { WINDOW_DESIGNS = 8, WINDOW_DESIGN_COLOURS = 15 };
static inline uint32_t field_window_design_graphics(const GameState *g, uint8_t design)
{
    return 0xFF0000u | rom_read16(g, 0xC06EA0u + 2u * (design % WINDOW_DESIGNS));
}
static inline uint16_t field_window_design_colour(const GameState *g, uint8_t design, unsigned n)
{
    return rom_read16(g, 0xFFB210u + 16u * (design % WINDOW_DESIGNS) + 2u * n);
}

/* The choice pointer's four tiles, and the field's two sprites of its own,
 * the tiles of each 16x16 sprite's top and bottom halves, in bank $FF. */
enum { FIELD_CHOICE_POINTER_TILES = 0xFF9260, FIELD_OWN_SPRITES_TILES = 0xFF9CF0 };

/* ---- Locations ------------------------------------------------------------------------------- */

/* A location's record (MD2's location_records: 14 bytes a location from
 * $F6:0000, by the location's nine-bit number), as far as the field's loading
 * reads it here: the song it plays (one of $80 and up plays none), its tile
 * set (BG1's and BG2's tiles: the graphics and how the map's tiles are made
 * of them), and BG3's ($FF: none). The field keeps where the current
 * location's record is (location_record_offset), which the other readers of
 * the record take it from (core/palette_data.h). */
enum { LOCATION_RECORD_BYTES = 14, LOCATION_NUMBER_BITS = 0x01FF, NO_LOCATION_SONG = 0x80, NO_TILE_SET = 0xFF };

typedef struct {
    uint8_t song;
    uint8_t tile_set;
    uint8_t bg3_tile_set;
} LocationDescription;

static inline uint16_t location_record_place(uint16_t location)
{
    return (uint16_t)(LOCATION_RECORD_BYTES * (location & LOCATION_NUMBER_BITS));
}

static inline LocationDescription location_description_at(const GameState *g, uint16_t place)
{
    uint32_t at = 0xF60000u + place;
    return (LocationDescription){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2) };
}

/* The map a location shows (its record's word at +4), and where its screen
 * and its leader stay (four bytes from +10: the left, top, right and bottom
 * tiles; a left with its top bit set lets them have the whole map). */
typedef struct {
    uint8_t left, top, right, bottom;
} MapEdges;
enum { MAP_EDGES_THE_WHOLE_MAP = 0x80 };

static inline uint16_t location_map(const GameState *g, uint16_t place) { return rom_read16(g, 0xF60004u + place); }
static inline MapEdges location_map_edges(const GameState *g, uint16_t place)
{
    uint32_t at = 0xF6000Au + place;
    return (MapEdges){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2), rom_read8(g, at + 3) };
}

/* A location's palette: its record's byte 3 names one of the palettes at
 * $F6:24C0, 210 bytes each: seven rows of fifteen colours, BGR words, for the
 * background palettes 1-7 but each one's colour 0. The place is worked out in
 * 16 bits, as the original's sum is. */
enum { LOCATION_PALETTE_ROWS = 7, LOCATION_PALETTE_ROW_COLOURS = 15 };

static inline uint8_t location_palette(const GameState *g, uint16_t place) { return rom_read8(g, 0xF60003u + place); }
static inline uint16_t location_palette_colour(const GameState *g, uint8_t palette, unsigned row, unsigned colour)
{
    enum { PALETTES = 0x24C0, PALETTE_BYTES = 2 * LOCATION_PALETTE_ROWS * LOCATION_PALETTE_ROW_COLOURS };
    uint16_t at = (uint16_t)(PALETTES + palette * PALETTE_BYTES + 2u * (row * LOCATION_PALETTE_ROW_COLOURS + colour));
    return rom_read16(g, 0xF60000u + at);
}

/* A location's event script: its record's word at +8 names one, and a long
 * pointer per script from $FC:F9F0 says where it is packed (the index tripled
 * in 16 bits as the original adds it, the doubling's carry going into the
 * third, as for the maps). */
static inline uint16_t location_script(const GameState *g, uint16_t place) { return rom_read16(g, 0xF60008u + place); }
static inline uint32_t field_packed_script(const GameState *g, uint16_t script)
{
    uint32_t at = 0xFCF9F0u + (uint16_t)(3u * script + (script >> 15));
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

/* Where a map's packed data is: a long pointer per map from $F6:1E00. */
static inline uint32_t field_packed_map(const GameState *g, uint16_t map)
{
    /* three bytes a map, the index tripled in 16 bits as the original adds
     * it, the doubling's carry going into the third (no map number reaches) */
    uint32_t at = 0xF61E00u + (uint16_t)(3u * map + (map >> 15));
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

/* A location's exits (FieldExit, above): where their run in bank $E5 starts,
 * from a word per location at $E5:0000, and where it ends, where the next
 * location's starts. */
typedef struct {
    uint16_t first, end;
} FieldExitRun;

static inline FieldExitRun field_location_exit_run(const GameState *g, uint16_t location)
{
    uint32_t at = 0xE50000u + (uint16_t)((location & LOCATION_NUMBER_BITS) << 1);
    return (FieldExitRun){ rom_read16(g, at), rom_read16(g, at + 2) };
}

/* A tile set's graphics: eight packed graphics blocks (data/packed_graphics.h),
 * a byte each from $F6:1C00, $FF for none. The first six are BG1's and BG2's
 * tiles in VRAM; the last two are kept in bank $7F. */
enum { TILE_SET_BLOCKS = 8, TILE_SET_BLOCKS_IN_VRAM = 6 };

static inline uint32_t tile_set_blocks_place(uint8_t set) { return 0xF61C00u + (uint16_t)(set * TILE_SET_BLOCKS); }
static inline uint8_t tile_set_block(const GameState *g, uint8_t set, unsigned n)
{
    return rom_read8(g, tile_set_blocks_place(set) + n);
}

/* How a tile set's map tiles are made of its tiles, packed: a long pointer
 * per set, BG1's and BG2's sets from $F6:2100, BG3's from $F6:21C0. */
static inline uint32_t tile_looks_address(const GameState *g, uint8_t set)
{
    uint32_t at = 0xF62100u + 3u * set;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}
static inline uint32_t bg3_tile_looks_address(const GameState *g, uint8_t set)
{
    uint32_t at = 0xF621C0u + 3u * set;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

#endif
