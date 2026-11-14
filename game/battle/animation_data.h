/* Battle: what the combatants' animation and the building of their sprite
 * graphics read from the ROM.
 *
 * Every read of cartridge data battle/animation.c, objects.c, sprites.c,
 * tiles.c and beaten_enemies.c make goes through here, named for what the data is (the asset
 * seam, docs/PLAN.md Phase 3), at the index the original uses, 16-bit
 * wraparound and all. The accessors take the game's state (game/rom.h), so a
 * build that loads these tables from elsewhere changes this file and nothing
 * that uses it. */
#ifndef CT_GAME_BATTLE_ANIMATION_DATA_H
#define CT_GAME_BATTLE_ANIMATION_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

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

/* ---- The animation scripts ----------------------------------------------------------------- */

/* The subroutines a script calls by number ($CE:0000, a word each, in bank
 * CE); the original doubles the number in a byte, so one of 128 or more names
 * the one 128 below it. */

/* How a frame's tiles are laid out in the object's tile buffer ($CC:F3B6, 14
 * bytes a layout): up to six rows, each at an offset from the buffer's start,
 * of the same number of 32-byte tiles. */
enum { BATTLE_LAYOUT_ROWS = 6, BATTLE_LAYOUT_BYTES = 14 };
typedef struct {
    uint16_t row_offset[BATTLE_LAYOUT_ROWS];
    uint8_t tiles_per_row, rows;
} BattleSpriteLayout;

/* ---- The objects' palette animations ------------------------------------------------------ */

/* A palette animation frame ($CD:0000, two bytes a frame from the effect's
 * table): the color set it shows, and where (the quarter of the first sprite
 * palette in its low bits; bit 7 for a set of seven colors rather than
 * three). The frame's place is worked out in 16 bits from the table's. */
typedef struct {
    uint8_t set, place;
} BattlePaletteFrame;

/* A color of a set of three colors ($D1:4100) or of seven ($D1:4400). */
enum { BATTLE_SMALL_SET_COLORS = 3, BATTLE_LARGE_SET_COLORS = 7 };

/* A color of the sets a flash puts into the combatants' flash palette
 * ($D1:4D90, twelve colors a set). */
enum { BATTLE_FLASH_SET_COLORS = 12 };

/* The whole record, as a frame of the effect's animation: its marks (any of
 * them, BATTLE_EFFECT_FRAME_ENDS, makes it the animation's last) over the
 * low bits of its sprite set, its offset from the anchor, and how many frames
 * it shows, whose top bit is the sprite set's high bit. The record is read
 * on past the bank's end as the original's long reads are. */
enum { BATTLE_EFFECT_FRAME_BYTES = 4, BATTLE_EFFECT_FRAME_ENDS = 0xE0 };
typedef struct {
    uint8_t marks;
    int8_t x, y;
    uint8_t time;
    bool high_set;
} BattleEffectFrame;

/* The runs of an enemy's tiles a dissolve goes through, two a layout ($CE:F192,
 * eight bytes a layout): each run's first tile's place in the object's tile
 * buffer, and how many tiles follow it a tile apart. The original's index is a
 * byte of the layout times eight. */
enum { BATTLE_DISSOLVE_RUNS = 2, BATTLE_DISSOLVE_LAYOUTS = 6 };
typedef struct {
    uint16_t first, tiles;
} BattleTileRun;

/* A long pointer's bank and offset, as the game keeps it in work RAM. The
 * original indexes from the offset in 16 bits within the bank for the lists
 * below, wrapping within the bank. */
static inline uint32_t battle_at(uint8_t bank, uint16_t offset) { return (uint32_t)bank << 16 | offset; }

static inline bool battle_status_pose_ends(const GameState *g, uint16_t entry)
{
    return asset_read8(g, ASSET_BATTLE_STATUS_POSE_ENDS, entry * BATTLE_STATUS_POSE_BYTES) & BATTLE_STATUS_POSES_END;
}

static inline BattleStatusPose battle_status_pose(const GameState *g, uint16_t entry)
{
    uint32_t at = entry * BATTLE_STATUS_POSE_BYTES;
    return (BattleStatusPose){ asset_read8(g, ASSET_BATTLE_STATUS_POSE_ENDS, at),
                               asset_read8(g, ASSET_BATTLE_STATUS_POSE_ENDS, at + 1),
                               asset_read8(g, ASSET_BATTLE_STATUS_POSE_ENDS, at + 2),
                               asset_read8(g, ASSET_BATTLE_STATUS_POSE_ENDS, at + 3) };
}

/* A byte of an object's frame list or delay list, wherever its pointer puts
 * it (bank $E4). */
static inline uint8_t battle_frame_list_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

/* Which part of an enemy's lists an animation's frames start at ($CC:FB91,
 * by animation), and the frames of time each stored frame time stands for
 * ($CC:FBCE: a fifth of it, 0 ending the list). */
static inline uint8_t battle_enemy_animation_part(const GameState *g, uint8_t animation)
{
    return asset_read8(g, ASSET_BATTLE_ENEMY_ANIMATION_PART, animation);
}

static inline uint8_t battle_enemy_frame_time(const GameState *g, uint8_t stored)
{
    return asset_read8(g, ASSET_BATTLE_ENEMY_FRAME_TIME, stored);
}

/* Which of the per-animation effects an enemy's animation runs ($CC:FBAB, by
 * animation). */
static inline uint8_t battle_enemy_animation_effect(const GameState *g, uint8_t animation)
{
    return asset_read8(g, ASSET_BATTLE_ENEMY_ANIMATION_EFFECT, animation);
}

/* The facing a direction between two points gives ($C0:F700, by the
 * direction $C1:0222 finds). */
static inline uint8_t battle_facing_of_direction(const GameState *g, uint16_t direction)
{
    return asset_read8(g, ASSET_DIRECTION_FACINGS, direction);
}

static inline uint32_t battle_script_subroutine(const GameState *g, uint8_t number)
{
    return asset_place_address(ASSET_BATTLE_ANIMATION_CALLS,
                               asset_read16(g, ASSET_BATTLE_ANIMATION_CALLS, (uint8_t)(number * 2)));
}

/* The eight speeds the scripts set a mover to by number: its period
 * ($CC:F5BC) and its steps a period ($CC:F4BC). */
static inline uint8_t battle_script_speed_period(const GameState *g, uint8_t speed)
{
    return asset_read8(g, ASSET_BATTLE_SCRIPT_SPEED_PERIOD, speed);
}

static inline uint8_t battle_script_speed_steps(const GameState *g, uint8_t speed)
{
    return asset_read8(g, ASSET_BATTLE_SCRIPT_SPEED_STEPS, speed);
}

/* ---- Building an object's sprite graphics ------------------------------------------------- */

/* The field supplies creature graphics unpacked in its second work-RAM
 * bank; the party and uncompressed creatures use cartridge resources. A
 * source bank must stay away from the battle's low work RAM even when
 * a tile's offset wraps. The lower cartridge mirrors also satisfy this. */
static inline bool battle_sprite_graphics_storage(uint32_t source)
{
    uint8_t bank = (uint8_t)(source >> 16);
    return bank == 0x7F || bank >= 0xC0 || (bank >= 0x40 && bank <= 0x7D);
}

/* The bytes of one frame's tile list, by the object's graphics kind
 * ($CC:F40A): its tile descriptors, then its part offsets, of which there are
 * as many bytes as $CC:F4FC gives. */
static inline uint16_t battle_frame_tile_list_bytes(const GameState *g, uint8_t kind)
{
    return asset_read16(g, ASSET_BATTLE_FRAME_TILE_LIST_BYTES, kind * 2u);
}

static inline uint8_t battle_frame_part_offset_bytes(const GameState *g, uint8_t kind)
{
    return asset_read8(g, ASSET_BATTLE_FRAME_PART_OFFSET_BYTES, kind);
}

static inline BattleSpriteLayout battle_sprite_layout(const GameState *g, uint16_t at)
{
    BattleSpriteLayout layout;
    for (unsigned row = 0; row < BATTLE_LAYOUT_ROWS; row++)
        layout.row_offset[row] = asset_read16(g, ASSET_BATTLE_SPRITE_LAYOUT, at + row * 2u);
    layout.tiles_per_row = asset_read8(g, ASSET_BATTLE_SPRITE_LAYOUT_F3C2, at);
    layout.rows = asset_read8(g, ASSET_BATTLE_SPRITE_LAYOUT_F3C2, 0x1u + at);
    return layout;
}

/* Where in bank $7E each object's tile buffer starts ($CC:F941, by object). */
static inline uint16_t battle_object_tile_buffer(const GameState *g, unsigned object)
{
    return asset_read16(g, ASSET_BATTLE_OBJECT_TILE_BUFFER, object * 2u);
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
static inline uint8_t battle_mirrored_byte(const GameState *g, uint8_t b)
{
    return asset_read8(g, ASSET_MIRRORED_BYTES, b);
}

static inline BattlePaletteFrame battle_palette_frame(const GameState *g, uint16_t table, uint16_t frame)
{
    uint16_t at = (uint16_t)(table + frame * 2u);
    return (BattlePaletteFrame){ asset_read8_at_place(g, ASSET_BATTLE_PALETTE_FRAMES, at),
                                 asset_read8_at_place(g, ASSET_BATTLE_PALETTE_FRAMES, (uint16_t)(at + 1)) };
}

static inline uint16_t battle_palette_set_color(const GameState *g, bool seven, uint8_t set, unsigned color)
{
    unsigned colors = seven ? BATTLE_LARGE_SET_COLORS : BATTLE_SMALL_SET_COLORS;
    return asset_read16(g, seven ? ASSET_BATTLE_SPRITE_LARGE_COLOR_SETS : ASSET_BATTLE_SPRITE_COLOR_SETS,
                        (set * colors + color) * 2u);
}

static inline uint16_t battle_flash_color(const GameState *g, uint8_t set, unsigned color)
{
    return asset_read16(g, ASSET_BATTLE_FLASH_COLORS, (set * BATTLE_FLASH_SET_COLORS + color) * 2u);
}

/* ---- Effect records and bitmap sprites ---------------------------------------------------- */

/* The first byte of a four-byte effect animation record ($CE:0000, at the
 * effect's offset), whose top bits mark where the animations divide. */
static inline uint8_t battle_effect_record_marks(const GameState *g, uint16_t at)
{
    return asset_read8_at_place(g, ASSET_BATTLE_EFFECT_FRAMES, at);
}

static inline BattleEffectFrame battle_effect_frame_record(const GameState *g, uint16_t at)
{
    uint8_t time = asset_read8_at_place(g, ASSET_BATTLE_EFFECT_FRAMES, (uint16_t)(at + 3));
    return (BattleEffectFrame){ asset_read8_at_place(g, ASSET_BATTLE_EFFECT_FRAMES, at),
                                (int8_t)asset_read8_at_place(g, ASSET_BATTLE_EFFECT_FRAMES, (uint16_t)(at + 1)),
                                (int8_t)asset_read8_at_place(g, ASSET_BATTLE_EFFECT_FRAMES, (uint16_t)(at + 2)),
                                (uint8_t)(time & 0x7F), (time & 0x80) != 0 };
}

/* Where in the effect sprite data each effect sprite set starts ($CC:F7C0,
 * by set), and the position of a bitmap shape's column or row ($CC:F820). */
static inline uint16_t battle_sprite_set_start(const GameState *g, uint16_t set)
{
    return asset_read16(g, ASSET_BATTLE_SPRITE_SET_START, set * 2u);
}

static inline uint8_t battle_bitmap_cell_position(const GameState *g, uint16_t cell)
{
    return asset_read8(g, ASSET_BATTLE_BITMAP_CELL_POSITION, cell);
}

/* ---- The battlefield's tiles ------------------------------------------------------------- */

/* The battlefield's tile descriptors ($CF:A000, 32 a row set; `at` is the
 * byte offset from the table's start, the sum wrapping in 16 bits as the
 * original's pointer does) and a row set's palette word ($CF:E000). */
static inline uint16_t battle_tile_descriptors_place(void) { return (uint16_t)asset_address(ASSET_BATTLE_TILE_DESCRIPTORS); }
static inline uint8_t battle_tile_descriptors_bank(void) { return (uint8_t)(asset_address(ASSET_BATTLE_TILE_DESCRIPTORS) >> 16); }

static inline uint16_t battle_tile_descriptor(const GameState *g, uint16_t at)
{
    /* the table's place in its bank wraps round the bank, as the original's word does */
    return asset_read16_at_place(g, ASSET_BATTLE_TILE_DESCRIPTORS, (uint16_t)(battle_tile_descriptors_place() + at));
}

static inline uint16_t battle_tile_row_palette_word(const GameState *g, uint16_t at)
{
    return asset_read16(g, ASSET_BATTLE_TILE_ROW_PALETTES, at);
}

/* A word of the battlefield's tile graphics ($D0:0000: two bit planes, and a
 * third for tiles that have one). */
static inline uint16_t battle_field_graphics_word(const GameState *g, uint32_t at)
{
    return asset_read16(g, ASSET_BATTLEFIELD_TILE_POOL, at);
}

/* The mask that gives a tile's upper two bit planes the palette it takes
 * ($CD:1300, by palette * 2). */
static inline uint16_t battle_palette_plane_mask(const GameState *g, uint8_t palette)
{
    return asset_read16(g, ASSET_BATTLE_PALETTE_PLANE_MASK, palette * 2u);
}

/* ---- A beaten enemy's dissolve ------------------------------------------------------------- */

/* How far a dissolve moves on through the 64 places of a tile at each step,
 * by its pace ($CE:EFEA, a byte a pace, by the pace's low three bits). */
static inline uint8_t battle_dissolve_stride(const GameState *g, uint8_t pace)
{
    return asset_read8(g, ASSET_BATTLE_DISSOLVE_STRIDE, pace & 7u);
}

static inline BattleTileRun battle_dissolve_run(const GameState *g, uint8_t layout, unsigned run)
{
    uint16_t at = (uint16_t)((uint8_t)(layout * 8u) + run * 4u);
    return (BattleTileRun){ asset_read16(g, ASSET_BATTLE_DISSOLVE_RUN, at),
                            asset_read16(g, ASSET_BATTLE_DISSOLVE_RUN_F194, at) };
}

/* How much of the object's tile buffer a dissolve sends to VRAM a frame, in
 * bytes, by the object's graphics kind ($CE:F156, a word a kind). */
static inline uint16_t battle_dissolve_bytes(const GameState *g, uint8_t kind)
{
    return asset_read16(g, ASSET_BATTLE_DISSOLVE_BYTES, kind * 2u);
}

/* Commands selected by the running battle animation. */
static inline uint8_t battle_animation_script_byte(const GameState *g, uint32_t at) { return rom_read8(g, at); }

#endif
