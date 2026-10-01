/* Battle: what the combatants' animation and the building of their sprite
 * graphics read from the ROM.
 *
 * Every read of cartridge data battle/animation.c, objects.c, sprites.c and
 * tiles.c make goes through here, named for what the data is (the asset
 * seam, docs/PLAN.md Phase 3), at the index the original uses, 16-bit
 * wraparound and all. The accessors take the game's state (game/rom.h), not
 * the machine, so a build that loads these tables from elsewhere changes this
 * file and nothing that uses it. */
#ifndef CT_GAME_BATTLE_ANIMATION_DATA_H
#define CT_GAME_BATTLE_ANIMATION_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* A long pointer's bank and offset, as the game keeps it in work RAM. The
 * original indexes from the offset in 16 bits within the bank for the lists
 * below, as its [dp],Y reads and its 16-bit sums do. */
static inline uint32_t battle_at(uint8_t bank, uint16_t offset) { return (uint32_t)bank << 16 | offset; }

/* ---- The combatants' poses and frame lists ------------------------------------------------ */

/* The status poses ($CC:F77F), four bytes an entry, ending in an entry whose
 * first byte has bit 7 set: which of the combatant's status bytes to test (by
 * its offset from the condition byte in the stat block), the bits that pick
 * the entry, the pose, and the animation that goes with it ($FF: the
 * combatant's stance). */
typedef struct {
    uint8_t status_byte, bits, pose, animation;
} BattleStatusPose;

enum { BATTLE_STATUS_POSE_BYTES = 4, BATTLE_STATUS_POSES_END = 0x80 };

static inline bool battle_status_pose_ends(const GameState *g, uint16_t entry)
{
    return rom_read8(g, 0xCCF77Fu + entry * BATTLE_STATUS_POSE_BYTES) & BATTLE_STATUS_POSES_END;
}
static inline BattleStatusPose battle_status_pose(const GameState *g, uint16_t entry)
{
    uint32_t at = 0xCCF77Fu + entry * BATTLE_STATUS_POSE_BYTES;
    return (BattleStatusPose){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2), rom_read8(g, at + 3) };
}

/* A byte of an object's frame list or delay list, wherever its pointer puts
 * it (bank $E4). */
static inline uint8_t battle_frame_list_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

/* Which part of an enemy's lists an animation's frames start at ($CC:FB91,
 * by animation), and the frames of time each stored frame time stands for
 * ($CC:FBCE: a fifth of it, 0 ending the list). */
static inline uint8_t battle_enemy_animation_part(const GameState *g, uint8_t animation)
{
    return rom_read8(g, 0xCCFB91u + animation);
}
static inline uint8_t battle_enemy_frame_time(const GameState *g, uint8_t stored)
{
    return rom_read8(g, 0xCCFBCEu + stored);
}

/* Which of the per-animation effects an enemy's animation runs ($CC:FBAB, by
 * animation). */
static inline uint8_t battle_enemy_animation_effect(const GameState *g, uint8_t animation)
{
    return rom_read8(g, 0xCCFBABu + animation);
}

/* The facing a direction between two points gives ($C0:F700, by the
 * direction $C1:0222 finds). */
static inline uint8_t battle_facing_of_direction(const GameState *g, uint16_t direction)
{
    return rom_read8(g, 0xC0F700u + direction);
}

/* ---- The animation scripts ----------------------------------------------------------------- */

/* The subroutines a script calls by number ($CE:0000, a word each, in bank
 * CE); the original doubles the number in a byte, so one of 128 or more names
 * the one 128 below it. */
enum { SCRIPT_SUBROUTINE_BANK = 0xCE };
static inline uint32_t battle_script_subroutine(const GameState *g, uint8_t number)
{
    return (uint32_t)SCRIPT_SUBROUTINE_BANK << 16 | rom_read16(g, 0xCE0000u + (uint8_t)(number * 2));
}

/* The eight speeds the scripts set a mover to by number: its period
 * ($CC:F5BC) and its steps a period ($CC:F4BC). */
static inline uint8_t battle_script_speed_period(const GameState *g, uint8_t speed)
{
    return rom_read8(g, 0xCCF5BCu + speed);
}
static inline uint8_t battle_script_speed_steps(const GameState *g, uint8_t speed)
{
    return rom_read8(g, 0xCCF4BCu + speed);
}

/* ---- Building an object's sprite graphics ------------------------------------------------- */

/* The bytes of one frame's tile list, by the object's graphics kind
 * ($CC:F40A): its tile descriptors, then its part offsets, of which there are
 * as many bytes as $CC:F4FC gives. */
static inline uint16_t battle_frame_tile_list_bytes(const GameState *g, uint8_t kind)
{
    return rom_read16(g, 0xCCF40Au + kind * 2u);
}
static inline uint8_t battle_frame_part_offset_bytes(const GameState *g, uint8_t kind)
{
    return rom_read8(g, 0xCCF4FCu + kind);
}

/* How a frame's tiles are laid out in the object's tile buffer ($CC:F3B6, 14
 * bytes a layout): up to six rows, each at an offset from the buffer's start,
 * of the same number of 32-byte tiles. */
enum { BATTLE_LAYOUT_ROWS = 6, BATTLE_LAYOUT_BYTES = 14 };
typedef struct {
    uint16_t row_offset[BATTLE_LAYOUT_ROWS];
    uint8_t tiles_per_row, rows;
} BattleSpriteLayout;

static inline BattleSpriteLayout battle_sprite_layout(const GameState *g, uint16_t at)
{
    BattleSpriteLayout layout;
    for (unsigned row = 0; row < BATTLE_LAYOUT_ROWS; row++)
        layout.row_offset[row] = rom_read16(g, 0xCCF3B6u + at + row * 2u);
    layout.tiles_per_row = rom_read8(g, 0xCCF3C2u + at);
    layout.rows = rom_read8(g, 0xCCF3C3u + at);
    return layout;
}

/* Where in bank $7E each object's tile buffer starts ($CC:F941, by object). */
static inline uint16_t battle_object_tile_buffer(const GameState *g, unsigned object)
{
    return rom_read16(g, 0xCCF941u + object * 2u);
}

/* A word of a frame's tile list, or a byte of its part offsets, wherever the
 * object's pointer puts it; and a byte of an object's graphics, wherever its
 * graphics set is. */
static inline uint16_t battle_tile_list_word(const GameState *g, uint32_t at) { return rom_read16(g, at); }
static inline uint8_t battle_tile_list_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }
static inline uint16_t battle_sprite_graphics_word(const GameState *g, uint32_t at) { return rom_read16(g, at); }
static inline uint8_t battle_sprite_graphics_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

/* Every byte with its bits in the other order ($C0:FD00): a row of a tile's
 * bit plane mirrored left to right. */
static inline uint8_t battle_mirrored_byte(const GameState *g, uint8_t b) { return rom_read8(g, 0xC0FD00u + b); }

/* ---- The objects' palette animations ------------------------------------------------------ */

/* A palette animation frame ($CD:0000, two bytes a frame from the effect's
 * table): the color set it shows, and where (the quarter of the first sprite
 * palette in its low bits; bit 7 for a set of seven colors rather than
 * three). The frame's place is worked out in 16 bits from the table's. */
typedef struct {
    uint8_t set, place;
} BattlePaletteFrame;

static inline BattlePaletteFrame battle_palette_frame(const GameState *g, uint16_t table, uint16_t frame)
{
    uint16_t at = (uint16_t)(table + frame * 2u);
    return (BattlePaletteFrame){ rom_read8(g, 0xCD0000u + at), rom_read8(g, 0xCD0001u + at) };
}

/* A color of a set of three colors ($D1:4100) or of seven ($D1:4400). */
enum { BATTLE_SMALL_SET_COLORS = 3, BATTLE_LARGE_SET_COLORS = 7 };
static inline uint16_t battle_palette_set_color(const GameState *g, bool seven, uint8_t set, unsigned color)
{
    unsigned colors = seven ? BATTLE_LARGE_SET_COLORS : BATTLE_SMALL_SET_COLORS;
    return rom_read16(g, (seven ? 0xD14400u : 0xD14100u) + (set * colors + color) * 2u);
}

/* A color of the sets a flash puts into the combatants' flash palette
 * ($D1:4D90, twelve colors a set). */
enum { BATTLE_FLASH_SET_COLORS = 12 };
static inline uint16_t battle_flash_color(const GameState *g, uint8_t set, unsigned color)
{
    return rom_read16(g, 0xD14D90u + (set * BATTLE_FLASH_SET_COLORS + color) * 2u);
}

/* ---- Effect records and bitmap sprites ---------------------------------------------------- */

/* The first byte of a four-byte effect animation record ($CE:0000, at the
 * effect's offset), whose top bits mark where the animations divide. */
static inline uint8_t battle_effect_record_marks(const GameState *g, uint16_t at)
{
    return rom_read8(g, 0xCE0000u + at);
}

/* Where in the effect sprite data each effect sprite set starts ($CC:F7C0,
 * by set), and the position of a bitmap shape's column or row ($CC:F820). */
static inline uint16_t battle_sprite_set_start(const GameState *g, uint16_t set)
{
    return rom_read16(g, 0xCCF7C0u + set * 2u);
}
static inline uint8_t battle_bitmap_cell_position(const GameState *g, uint16_t cell)
{
    return rom_read8(g, 0xCCF820u + cell);
}

/* ---- The battlefield's tiles ------------------------------------------------------------- */

/* The battlefield's tile descriptors ($CF:A000, 32 a row set; `at` is the
 * byte offset from the table's start, the sum wrapping in 16 bits as the
 * original's pointer does) and a row set's palette word ($CF:E000). */
static inline uint16_t battle_tile_descriptor(const GameState *g, uint16_t at)
{
    return rom_read16(g, 0xCF0000u | (uint16_t)(0xA000u + at));
}
static inline uint16_t battle_tile_row_palette_word(const GameState *g, uint16_t at)
{
    return rom_read16(g, 0xCFE000u + at);
}

/* A word of the battlefield's tile graphics ($D0:0000: two bit planes, and a
 * third for tiles that have one). */
static inline uint16_t battle_field_graphics_word(const GameState *g, uint32_t at)
{
    return rom_read16(g, 0xD00000u + at);
}

/* The mask that gives a tile's upper two bit planes the palette it takes
 * ($CD:1300, by palette * 2). */
static inline uint16_t battle_palette_plane_mask(const GameState *g, uint8_t palette)
{
    return rom_read16(g, 0xCD1300u + palette * 2u);
}

#endif
