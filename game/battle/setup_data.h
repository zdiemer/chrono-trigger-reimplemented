/* Battle: what the battle's start reads from the ROM.
 *
 * Every read of cartridge data the battle's setup makes (battle/setup*.c)
 * goes through here, by what the data is (the asset seam, docs/PLAN.md Phase
 * 3), so a build that loads these tables from somewhere else maps them in this
 * one file. Each takes the index the game keeps and does the original's index
 * arithmetic, wraparound included: a long address plus an index runs on into
 * the next bank, as the 65816's does. */
#ifndef CT_GAME_BATTLE_SETUP_DATA_H
#define CT_GAME_BATTLE_SETUP_DATA_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

static inline uint32_t rom_read24(const GameState *g, uint32_t at)
{
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

/* ---- The window and the screen ----------------------------------------------------------- */

/* How the battle window is laid out, by the battle gauge setting ($CC:FBC5). */
static inline uint8_t setup_window_layout_for_gauge(const GameState *g, uint8_t setting)
{
    return rom_read8(g, 0xCCFBC5u + setting);
}

/* A sprite's X high bit in its high-table byte, and the mask that clears it,
 * by its place among the four sprites a byte holds ($CC:F4C4, $CC:F4CC). */
static inline uint8_t setup_sprite_x_high_bit(const GameState *g, uint8_t place) { return rom_read8(g, 0xCCF4C4u + place); }
static inline uint8_t setup_sprite_x_high_clear(const GameState *g, uint8_t place)
{
    return rom_read8(g, 0xCCF4CCu + place);
}

/* The battlefield grid's rows as they start: the rows along the top and the
 * bottom edges (a table of three rows, $CC:F694), and the one row every row
 * between them starts as ($CC:F644). */
static inline uint8_t setup_grid_edge_cell(const GameState *g, uint16_t at) { return rom_read8(g, 0xCCF694u + at); }
static inline uint8_t setup_grid_middle_cell(const GameState *g, uint8_t column) { return rom_read8(g, 0xCCF644u + column); }

/* The window frames' tilemap ($D1:5800), and the start of the tilemap the
 * graphics' second load step uploads ($D1:5D50). */
static inline uint8_t setup_window_frames_tilemap(const GameState *g, uint16_t at) { return rom_read8(g, 0xD15800u + at); }
static inline uint8_t setup_load_tilemap_start(const GameState *g, uint16_t at) { return rom_read8(g, 0xD15D50u + at); }

/* ---- Names -------------------------------------------------------------------------------- */

/* Where the n-th five-character name is in a table of names ($CC:F83F), and
 * where its two converted rows of five tiles go ($CC:F837). */
static inline uint8_t setup_name_at(const GameState *g, uint8_t n) { return rom_read8(g, 0xCCF83Fu + n); }
static inline uint8_t setup_name_tiles_at(const GameState *g, uint8_t n) { return rom_read8(g, 0xCCF837u + n); }

/* Where a character's name is among the characters' six-byte records
 * ($CC:F846, by character). */
static inline uint8_t setup_character_name_at(const GameState *g, uint8_t character)
{
    return rom_read8(g, 0xCCF846u + character);
}

/* A character of an enemy's name, eleven a name by enemy number ($CC:6500). */
static inline uint8_t setup_enemy_name_char(const GameState *g, uint16_t at) { return rom_read8(g, 0xCC6500u + at); }

/* ---- Colors -------------------------------------------------------------------------------- */

/* The colors every battle starts with in three runs of the palette ($FF:9C68,
 * $FF:9C70, $FF:9E0A), by run and color. */
static inline uint16_t setup_fixed_color(const GameState *g, unsigned run, unsigned color)
{
    static const uint32_t runs[3] = { 0xFF9C68u, 0xFF9C70u, 0xFF9E0Au };
    return rom_read16(g, runs[run] + 2 * color);
}

/* A party member's color set, by the byte the battle carries for it
 * ($CD:6CEC), and a set's colors (three to a set, $D1:4B00). */
static inline uint8_t setup_member_color_set(const GameState *g, uint8_t carried) { return rom_read8(g, 0xCD6CECu + carried); }
static inline uint16_t setup_member_color(const GameState *g, uint16_t at) { return rom_read16(g, 0xD14B00u + at); }

/* The colors the cursors' sprite palettes start with ($D1:38F2, $D1:38FA),
 * and the number sprites' colors ($D1:4D80). */
static inline uint16_t setup_cursor_color(const GameState *g, unsigned palette, unsigned color)
{
    return rom_read16(g, (palette ? 0xD138FAu : 0xD138F2u) + 2 * color);
}
static inline uint16_t setup_number_color(const GameState *g, unsigned color) { return rom_read16(g, 0xD14D80u + 2 * color); }

/* ---- The party's techs ---------------------------------------------------------------------- */

/* Where a party member's tech records start, in bytes into the records
 * ($CC:F395, a word a member), and where its tech list starts ($CC:F38C). */
static inline uint16_t setup_member_tech_records_at(const GameState *g, uint8_t member)
{
    return rom_read16(g, 0xCCF395u + 2u * member);
}
static inline uint8_t setup_member_tech_list_start(const GameState *g, uint8_t member) { return rom_read8(g, 0xCCF38Cu + member); }

/* ---- Objects' graphics ---------------------------------------------------------------------- */

/* A character's graphics set and tile lists, long pointers three bytes a
 * character ($E4:2000, $E4:2300; the tile lists are also an enemy's, by its
 * graphics set); and an animation set's frame lists and frame delay lists,
 * in bank $E4, a word a set ($E4:2600, $E4:2800). */
enum { SETUP_ANIMATION_BANK = 0xE4 };

static inline uint32_t setup_character_graphics(const GameState *g, uint8_t at) { return rom_read24(g, 0xE42000u + at); }
static inline uint32_t setup_tile_lists(const GameState *g, uint16_t at) { return rom_read24(g, 0xE42300u + at); }
static inline uint16_t setup_frame_lists(const GameState *g, uint16_t at) { return rom_read16(g, 0xE42600u + at); }
static inline uint16_t setup_delay_lists(const GameState *g, uint16_t at) { return rom_read16(g, 0xE42800u + at); }

/* How many sprites an object of a graphics kind is drawn with ($CC:F505). */
static inline uint8_t setup_kind_sprites(const GameState *g, uint8_t kind) { return rom_read8(g, 0xCCF505u + kind); }

/* An object's sprite entries' tiles and attributes as they start, two bytes
 * an entry ($CC:FAF3). */
static inline uint8_t setup_sprite_entry_byte(const GameState *g, uint16_t at) { return rom_read8(g, 0xCCFAF3u + at); }

/* The tiles the battle's start puts in the decoded tiles: sixteen tiles of two
 * bitplanes ($D1:4C80) and 64 of four ($D1:3900). */
static inline uint8_t setup_two_plane_tiles(const GameState *g, uint16_t at) { return rom_read8(g, 0xD14C80u + at); }
static inline uint8_t setup_four_plane_tiles(const GameState *g, uint16_t at) { return rom_read8(g, 0xD13900u + at); }

/* Each object's two graphics uploads: their sizes by graphics kind ($CC:F9DF,
 * $CC:F9E9) and their VRAM addresses and sources by battle slot ($CC:F9B3,
 * $CC:F9C9; $CC:F987, $CC:F99D), a word each. */
static inline uint16_t setup_upload_size(const GameState *g, unsigned upload, uint8_t kind)
{
    return rom_read16(g, (upload ? 0xCCF9E9u : 0xCCF9DFu) + (uint8_t)(kind << 1));
}
static inline uint16_t setup_upload_vram(const GameState *g, unsigned upload, uint8_t slot)
{
    return rom_read16(g, (upload ? 0xCCF9C9u : 0xCCF9B3u) + 2u * slot);
}
static inline uint16_t setup_upload_source(const GameState *g, unsigned upload, uint8_t slot)
{
    return rom_read16(g, (upload ? 0xCCF99Du : 0xCCF987u) + 2u * slot);
}

/* ---- Enemies -------------------------------------------------------------------------------- */

/* Where the n-th enemy's record is among the battle's enemy records, in
 * bytes ($CC:F939). */
static inline uint8_t setup_enemy_record_at(const GameState *g, uint8_t n) { return rom_read8(g, 0xCCF939u + n); }

/* An enemy's header, ten bytes an enemy number ($E4:F600): its graphics set
 * and animation set, its flags, where its cursor points and how big it is,
 * and how it moves (its orbit's radius step in bits 0-2, its period in bits
 * 4-7). */
typedef struct {
    uint8_t graphics_set, animation_set, flags;
    uint8_t cursor_x, cursor_y, half_width, half_height;
    uint8_t motion;
} EnemyHeader;

static inline EnemyHeader setup_enemy_header(const GameState *g, uint16_t number)
{
    uint32_t at = 0xE4F600u + (uint16_t)(number * 10);
    return (EnemyHeader){ rom_read8(g, at + 1), rom_read8(g, at + 3), rom_read8(g, at + 4), rom_read8(g, at + 5),
                          rom_read8(g, at + 6), rom_read8(g, at + 7), rom_read8(g, at + 8), rom_read8(g, at + 9) };
}

/* How often an enemy's effects step, by its orbit's period and radius step
 * ($CC:F544). */
static inline uint8_t setup_enemy_effect_period(const GameState *g, uint8_t at) { return rom_read8(g, 0xCCF544u + at); }

/* The enemies that bring a rule into the battle ($E1:DE00): an enemy number
 * and a rule, a word each, until an enemy number with its top bit set. */
enum { SETUP_RULE_ENTRY_BYTES = 4 };

static inline uint16_t setup_rule_enemy(const GameState *g, uint16_t at) { return rom_read16(g, 0xE1DE00u + at); }
static inline uint16_t setup_rule(const GameState *g, uint16_t at) { return rom_read16(g, 0xE1DE02u + at); }

/* ---- The fight's start (battle/fight_*.c) ----------------------------------------------------- */

/* An enemy's record, 23 bytes by its id ($CC:4700): its HP, its level, the
 * five bytes of what it shrugs off and keeps (the stat block's from
 * STAT_SHRUGS_OFF), its twelve stats from its stamina to its affinities, and
 * its kind, family and attack's detail. */
enum { ENEMY_DEFENCES = 5, ENEMY_STATS = 12, ENEMY_KIND_BYTES = 3 };

typedef struct {
    uint16_t hp;
    uint8_t level;
    uint8_t defences[ENEMY_DEFENCES];
    uint8_t stats[ENEMY_STATS];
    uint8_t kind[ENEMY_KIND_BYTES];
} EnemyStatsRecord;

static inline EnemyStatsRecord enemy_stats_record(const GameState *g, uint8_t enemy)
{
    uint32_t at = 0xCC4700u + (uint16_t)(enemy * 23);
    EnemyStatsRecord r;
    r.hp = rom_read16(g, at);
    r.level = rom_read8(g, at + 2);
    for (unsigned n = 0; n < ENEMY_DEFENCES; n++)
        r.defences[n] = rom_read8(g, at + 3 + n);
    for (unsigned n = 0; n < ENEMY_STATS; n++)
        r.stats[n] = rom_read8(g, at + 8 + n);
    for (unsigned n = 0; n < ENEMY_KIND_BYTES; n++)
        r.kind[n] = rom_read8(g, at + 20 + n);
    return r;
}

/* The characters' records as a new game starts them ($CC:0000, laid out as
 * character_records), which a demo battle's party starts from. */
static inline uint8_t character_start_record_byte(const GameState *g, uint16_t at) { return rom_read8(g, 0xCC0000u + at); }

/* A demo battle's party: each character's HP, its most, its MP and its most,
 * eight bytes a character, from where the demo's word says in bank FD
 * ($FD:B99C, a word a demo). */
typedef struct {
    uint16_t hp, max_hp, mp, max_mp;
} DemoMember;

static inline DemoMember demo_member(const GameState *g, uint8_t demo, unsigned character)
{
    uint16_t at = (uint16_t)(rom_read16(g, 0xFDB99Cu + (uint8_t)(demo << 1)) + character * 8);
    return (DemoMember){ rom_read16(g, 0xFD0000u | at), rom_read16(g, 0xFD0000u | (uint16_t)(at + 2)),
                         rom_read16(g, 0xFD0000u | (uint16_t)(at + 4)), rom_read16(g, 0xFD0000u | (uint16_t)(at + 6)) };
}

/* A demo battle's party's chosen actions, every turn: the three members'
 * action records (battle_member_actions) a byte at a time, from where the
 * demo's word says in bank FD ($FD:B957, a word a demo). */
static inline uint8_t demo_party_actions_byte(const GameState *g, uint8_t demo, unsigned at)
{
    uint16_t from = rom_read16(g, 0xFDB957u + (uint8_t)(demo << 1));
    return rom_read8(g, 0xFD0000u | (uint16_t)(from + at));
}

/* How long a combatant's first turn waits, before its speed takes its part
 * off, by the battle speed setting and its speed ($CC:2E31, sixteen a
 * setting from speed 1; the place is taken in a byte, so a speed of 0 reads
 * the previous setting's last). */
enum { SPEEDS_A_SETTING = 16 };

static inline uint8_t first_turn_wait(const GameState *g, uint8_t setting, uint8_t speed)
{
    return rom_read8(g, 0xCC2E31u + (uint8_t)(speed - 1 + setting * SPEEDS_A_SETTING));
}

#endif
