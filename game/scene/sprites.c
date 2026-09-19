/* Scenes: sprites into the OAM buffer at $0700.
 *
 * The scene program builds each frame's sprites in the buffer the NMI uploads
 * (RAM_SCENE_SPRITE_INDEX is the next free entry, 4 bytes each: X, Y, tile,
 * attributes), and packs each sprite's two high-table bits (X's 9th bit, and
 * large size) into the byte RAM_SCENE_SPRITE_HIGH_POINTER points at: each new
 * sprite shifts the byte right two bits and puts its own bits at the top, so
 * four sprites fill a byte and the first ends in the low bits, and after every
 * fourth sprite the pointer moves on. A sprite that leaves both bits clear
 * doesn't touch a byte that is already 0. Every sprite is placed at the
 * scene's offset (RAM_SCENE_SPRITE_OFFSET_X/_Y).
 *
 * The buffer and the high table are reached through the data bank, as the
 * original does; the lists are read through the work RAM port. No caller reads
 * a register afterwards (the lists' callers the overflow excepted, below), so
 * the entries leave them as they are. */
#include "scene/scene.h"

#include "port/machine.h"
#include "ram.h"

enum { OAM_ENTRY = RAM_OAM_BUFFER_STRIDE };
enum { ENTRIES_PER_HIGH_BYTE_MASK = 0x0F };   /* the entry offset's low bits, 4 entries of 4 bytes */

/* High-table bits of a sprite, at the top of the byte it shifts in. */
enum { HIGH_LARGE = 0x80, HIGH_X9 = 0x40 };

/* An X at or past the right edge moves the sprite off screen: X 1 with the 9th
 * bit set, written as a word whose high byte the Y overwrites. */
enum { SCREEN_WIDTH = 0x0100, OFFSCREEN_X = 0x9801 };

/* The work RAM port: memory access, not video (docs/PLATFORM.md). */
enum { WRAM_PORT = 0x2180, WRAM_PORT_ADDRESS = 0x2181, WRAM_PORT_BANK = 0x2183 };

static uint16_t cell16(Snes *s, uint8_t dp) { return m_read16(s, m_dp(s, dp)); }
static void set_cell16(Snes *s, uint8_t dp, uint16_t v) { m_write16(s, m_dp(s, dp), v); }
static uint8_t cell8(Snes *s, uint8_t dp) { return m_read8(s, m_dp(s, dp)); }

/* Byte `field` (RAM_OAM_BUFFER_*) of the OAM buffer entry at `entry`, through
 * the data bank. */
static void set_oam(Snes *s, uint16_t entry, uint32_t field, uint8_t v)
{
    m_write8(s, m_abs(s, RAM_ABS(field)) + entry, v);
}

/* Two bytes from `field` on, where the original stores a word. */
static void set_oam16(Snes *s, uint16_t entry, uint32_t field, uint16_t v)
{
    m_write16(s, m_abs(s, RAM_ABS(field)) + entry, v);
}

/* The scene's offset added to a sprite's X and Y (8-bit, wrapping). */
static uint8_t offset_x8(Snes *s, uint8_t x) { return (uint8_t)(x + cell8(s, RAM_SCENE_SPRITE_OFFSET_X_DP)); }
static uint8_t offset_y8(Snes *s, uint8_t y) { return (uint8_t)(y + cell8(s, RAM_SCENE_SPRITE_OFFSET_Y_DP)); }

/* The overflow the Y's addition leaves, which the clipped list's callers see. */
static bool y_add_overflows(Snes *s, uint8_t y)
{
    uint8_t add = cell8(s, RAM_SCENE_SPRITE_OFFSET_Y_DP);
    uint8_t sum = (uint8_t)(y + add);
    return (~(y ^ add) & (y ^ sum) & 0x80) != 0;
}

/* The next entry: true when it starts a new high-table byte. */
static bool next_entry(uint16_t *oam)
{
    *oam = (uint16_t)(*oam + OAM_ENTRY);
    return (*oam & ENTRIES_PER_HIGH_BYTE_MASK) == 0;
}

/* A sprite with both high bits clear: the byte shifts, unless it is 0 and
 * isn't written at all. */
static uint8_t shift_in_small(uint8_t high) { return (uint8_t)(high >> 2); }
static uint8_t shift_in_large(uint8_t high) { return (uint8_t)(HIGH_LARGE | high >> 2); }

static void shift_in(Snes *s, uint32_t at, uint8_t (*add)(uint8_t))
{
    uint8_t high = m_read8(s, at);
    if (high || add != shift_in_small)
        m_write8(s, at, add(high));
}

/* A list in $7E: a count byte, then count - 1 entries of tile, attributes, X
 * and Y (a count of 0 is 256). The count goes into its cell, which counts down
 * as the entries are copied: another_entry is the loop's test. */
static void open_list(Snes *s, uint16_t list)
{
    m_write16(s, m_abs(s, WRAM_PORT_ADDRESS), list);
    m_write8(s, m_abs(s, WRAM_PORT_BANK), 0);
    m_write8(s, m_dp(s, RAM_SCENE_SPRITE_LIST_LEFT_DP), m_read8(s, m_abs(s, WRAM_PORT)));
}

static bool another_entry(Snes *s)
{
    uint32_t left = m_dp(s, RAM_SCENE_SPRITE_LIST_LEFT_DP);
    uint8_t n = (uint8_t)(m_read8(s, left) - 1);
    m_write8(s, left, n);
    return n != 0;
}

static uint8_t list_byte(Snes *s) { return m_read8(s, m_abs(s, WRAM_PORT)); }

/* The cursor saved back. */
static void close_list(Snes *s, uint16_t oam, uint16_t high)
{
    set_cell16(s, RAM_SCENE_SPRITE_HIGH_POINTER_DP, high);
    set_cell16(s, RAM_SCENE_SPRITE_INDEX_DP, oam);
}

/* Copy the sprite list at $7E:list into the buffer, all small with X's 9th
 * bit clear. The high-table pointer is used as an index, whole. */
static void load_sprite_list(Snes *s, uint16_t list)
{
    open_list(s, list);
    uint16_t oam = cell16(s, RAM_SCENE_SPRITE_INDEX_DP);
    uint16_t high = cell16(s, RAM_SCENE_SPRITE_HIGH_POINTER_DP);
    while (another_entry(s)) {
        set_oam(s, oam, RAM_OAM_BUFFER_TILE, list_byte(s));
        set_oam(s, oam, RAM_OAM_BUFFER_ATTRIBUTES, list_byte(s));
        set_oam(s, oam, RAM_OAM_BUFFER_X, offset_x8(s, list_byte(s)));
        set_oam(s, oam, RAM_OAM_BUFFER_Y, offset_y8(s, list_byte(s)));
        shift_in(s, m_abs(s, 0) + high, shift_in_small);
        if (next_entry(&oam))
            high++;
    }
    close_list(s, oam, high);
}

/* The same with the X offset 16-bit, all sprites large: an X past the right
 * edge moves the sprite off screen, and a negative one sets X's 9th bit.
 * Returns the overflow of the last Y's addition (`overflow` when there is
 * none). */
static bool load_sprite_list_clipped(Snes *s, uint16_t list, bool overflow)
{
    open_list(s, list);
    uint16_t oam = cell16(s, RAM_SCENE_SPRITE_INDEX_DP);
    uint16_t high = cell16(s, RAM_SCENE_SPRITE_HIGH_POINTER_DP);
    while (another_entry(s)) {
        set_oam(s, oam, RAM_OAM_BUFFER_TILE, list_byte(s));
        set_oam(s, oam, RAM_OAM_BUFFER_ATTRIBUTES, list_byte(s));
        uint16_t x = (uint16_t)(list_byte(s) + cell16(s, RAM_SCENE_SPRITE_OFFSET_X_DP));
        if (!((uint16_t)(x - SCREEN_WIDTH) & 0x8000))
            x = OFFSCREEN_X;
        set_oam16(s, oam, RAM_OAM_BUFFER_X, x);   /* the Y overwrites the high byte */
        uint32_t at = m_abs(s, 0) + high;
        uint8_t bits = HIGH_LARGE | ((x & 0x8000) ? HIGH_X9 : 0);
        m_write8(s, at, (uint8_t)(bits | m_read8(s, at) >> 2));
        uint8_t y = list_byte(s);
        overflow = y_add_overflows(s, y);
        set_oam(s, oam, RAM_OAM_BUFFER_Y, offset_y8(s, y));
        if (next_entry(&oam))
            high++;
    }
    close_list(s, oam, high);
    return overflow;
}

/* One sprite from the four bytes at $7E:source (tile, attributes, X, Y), whose
 * high bits `add` shifts in. The pointer's low byte moves on after every
 * fourth sprite. */
static void add_sprite(Snes *s, uint16_t source, uint8_t (*add)(uint8_t))
{
    uint16_t oam = cell16(s, RAM_SCENE_SPRITE_INDEX_DP);
    uint32_t from = 0x7E0000u + source;
    set_oam16(s, oam, RAM_OAM_BUFFER_TILE, m_read16(s, from));   /* and the attributes */
    set_oam(s, oam, RAM_OAM_BUFFER_X, offset_x8(s, m_read8(s, from + 2)));
    set_oam(s, oam, RAM_OAM_BUFFER_Y, offset_y8(s, m_read8(s, from + 3)));
    shift_in(s, m_abs(s, cell16(s, RAM_SCENE_SPRITE_HIGH_POINTER_DP)), add);
    if (next_entry(&oam)) {
        uint32_t pointer_low = m_dp(s, RAM_SCENE_SPRITE_HIGH_POINTER_DP);
        m_write8(s, pointer_low, (uint8_t)(m_read8(s, pointer_low) + 1));
    }
    set_cell16(s, RAM_SCENE_SPRITE_INDEX_DP, oam);
}

/* ---- Entries: 16-bit A and index registers ------------------------------------------ */

// $C3:0D0E
/* The list at $7E:A, small sprites. */
void scene_load_sprite_list(Snes *s)
{
    load_sprite_list(s, s->cpu.a);
    m_rtl(s);
}

// $C3:0D5E
/* The list at $7E:A, large sprites clipped at the right edge. */
void scene_load_sprite_list_clipped(Snes *s)
{
    bool overflow = load_sprite_list_clipped(s, s->cpu.a, m_flag(s, CPU_FLAG_V));
    m_set_flag(s, CPU_FLAG_V, overflow);
    m_rtl(s);
}

/* These save P while they work (PHP, PLP): the byte is on the stack, where a
 * source can reach it. */

// $C3:0DBC
/* One small sprite from $7E:X. */
void scene_add_sprite(Snes *s)
{
    m_push8(s, s->cpu.p);
    add_sprite(s, s->cpu.x, shift_in_small);
    m_pull8(s);
    m_rtl(s);
}

// $C3:0DF3
/* One large sprite from $7E:X, with X's 9th bit clear. */
void scene_add_large_sprite(Snes *s)
{
    m_push8(s, s->cpu.p);
    add_sprite(s, s->cpu.x, shift_in_large);
    m_pull8(s);
    m_rtl(s);
}
