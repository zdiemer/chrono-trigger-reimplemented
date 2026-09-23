/* Scenes: sprites into the OAM buffer at $0700.
 *
 * The scene program builds each frame's sprites in the buffer the NMI uploads
 * (scene_sprite_index is the next free entry's offset, 4 bytes an entry: X, Y,
 * tile, attributes), and packs each sprite's two high-table bits (X's 9th bit,
 * and large size) into the byte scene_sprite_high_pointer points at: each new
 * sprite shifts the byte right two bits and puts its own bits at the top, so
 * four sprites fill a byte and the first ends in the low bits, and after every
 * fourth sprite the pointer moves on. A sprite that leaves both bits clear
 * doesn't touch a byte that is already 0. Every sprite is placed at the
 * scene's offset (scene_sprite_offset_x/_y).
 *
 * The lists are streamed through the work RAM port, and a list can sit over
 * the cells and the buffer it is copied into, so each byte is read where the
 * original reads it and the list's count cell counts down as it goes. What the
 * callers can read afterwards is Y, the next free entry, and (after a list) A,
 * which the scene program's task loop carries into the next task: the lists and
 * the small sprite leave those as the original does, the clipped list its
 * overflow too, and the other registers as they are.
 *
 * A precondition the port assumes and the original doesn't: the entry offset
 * and the high-table pointer stay in low RAM. The original reaches the buffer
 * through a data bank of 0, so a cursor run past $2000 would write I/O
 * registers there; the named table writes bank $7E instead. No game state does
 * this -- the lists always fill the buffer at $0700 -- and the fuzz runs are
 * kept to low RAM for that reason (tools/fuzz_routines.toml). Until a guard
 * refuses such a cursor, this is an accepted divergence (docs/TODO.md). */
#include "scene/scene.h"

#include "cpu_io.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* An entry's bytes, and where the high table starts in the buffer. */
enum { ENTRY_X = 0, ENTRY_Y = 1, ENTRY_TILE = 2, ENTRY_ATTRIBUTES = 3, OAM_ENTRY = 4 };
enum { SPRITE_TABLE = 0x0700 };
enum { ENTRIES_PER_HIGH_BYTE_MASK = 0x0F };   /* the entry offset's low bits, 4 entries of 4 bytes */

/* High-table bits of a sprite, at the top of the byte it shifts in. */
enum { HIGH_LARGE = 0x80, HIGH_X9 = 0x40 };

/* An X at or past the right edge moves the sprite off screen: X 1 with the 9th
 * bit set, written as a word whose high byte the Y overwrites. */
enum { SCREEN_WIDTH = 0x0100, OFFSCREEN_X = 0x9801 };

/* The lists and the sprites' sources are in bank $7E. */
enum { SOURCE_BANK = 0x7E };

/* Byte `field` of the entry at offset `entry`. */
static void set_entry(GameState *g, uint16_t entry, uint8_t field, uint8_t v)
{
    set_scene_sprite_table_byte(g, (unsigned)entry + field, v);
}

/* The high-table byte at address `at`: an address in the buffer's bank, which
 * the pointer holds, rather than an offset in it. */
static unsigned high_byte_at(uint16_t at) { return (unsigned)(at - SPRITE_TABLE); }

/* The scene's offset added to a sprite's X and Y (8-bit, wrapping). */
static uint8_t offset_x8(const GameState *g, uint8_t x) { return (uint8_t)(x + scene_sprite_offset_x(g)); }
static uint8_t offset_y8(const GameState *g, uint8_t y) { return (uint8_t)(y + scene_sprite_offset_y(g)); }

/* The overflow the Y's addition leaves, which the clipped list's callers see. */
static bool y_add_overflows(const GameState *g, uint8_t y)
{
    uint8_t add = scene_sprite_offset_y(g);
    uint8_t sum = (uint8_t)(y + add);
    return (~(y ^ add) & (y ^ sum) & 0x80) != 0;
}

/* The next entry: true when it starts a new high-table byte. */
static bool next_entry(uint16_t *entry)
{
    *entry = (uint16_t)(*entry + OAM_ENTRY);
    return (*entry & ENTRIES_PER_HIGH_BYTE_MASK) == 0;
}

/* A sprite with both high bits clear: the byte shifts, unless it is 0 and
 * isn't written at all. */
static void shift_in_small(GameState *g, unsigned at)
{
    uint8_t high = scene_sprite_table_byte(g, at);
    if (high)
        set_scene_sprite_table_byte(g, at, (uint8_t)(high >> 2));
}

static void shift_in(GameState *g, unsigned at, uint8_t bits)
{
    set_scene_sprite_table_byte(g, at, (uint8_t)(bits | scene_sprite_table_byte(g, at) >> 2));
}

/* A list in $7E: a count byte, then count - 1 entries of tile, attributes, X
 * and Y (a count of 0 is 256). The count goes into its cell, which counts down
 * as the entries are copied: another_entry is the loop's test. */
static uint8_t open_list(GameState *g, uint16_t list)
{
    hw_port_seek(g, list, 0);
    uint8_t count = hw_port_read(g);
    set_scene_sprite_list_left(g, count);
    return count;
}

static bool another_entry(GameState *g)
{
    uint8_t left = (uint8_t)(scene_sprite_list_left(g) - 1);
    set_scene_sprite_list_left(g, left);
    return left != 0;
}

/* Where a list leaves the buffer, and the A it leaves: its low byte the count
 * for a list with no entries, else the last entry's offset within its
 * high-table byte (the loop's test); its high byte what the loop last put
 * there. */
typedef struct {
    uint16_t entry;
    uint16_t a;
} ListEnd;

static uint16_t list_a(uint8_t high, uint8_t count, bool entries, uint16_t entry)
{
    return (uint16_t)(high << 8 | (entries ? (entry & ENTRIES_PER_HIGH_BYTE_MASK) : count));
}

/* The cursors saved back. */
static void close_list(GameState *g, uint16_t entry, uint16_t high)
{
    set_scene_sprite_high_pointer(g, high);
    set_scene_sprite_index(g, entry);
}

// $C3:0D0E
/* Copy the sprite list at $7E:list into the buffer, all small with X's 9th
 * bit clear. */
static ListEnd load_sprite_list(GameState *g, uint16_t list)
{
    uint8_t count = open_list(g, list);
    uint16_t entry = scene_sprite_index(g);
    uint16_t high = scene_sprite_high_pointer(g);
    bool entries = false;
    while (another_entry(g)) {
        entries = true;
        set_entry(g, entry, ENTRY_TILE, hw_port_read(g));
        set_entry(g, entry, ENTRY_ATTRIBUTES, hw_port_read(g));
        set_entry(g, entry, ENTRY_X, offset_x8(g, hw_port_read(g)));
        set_entry(g, entry, ENTRY_Y, offset_y8(g, hw_port_read(g)));
        shift_in_small(g, high_byte_at(high));
        if (next_entry(&entry))
            high++;
    }
    close_list(g, entry, high);
    return (ListEnd){ entry, list_a((uint8_t)(list >> 8), count, entries, entry) };
}

// $C3:0D5E
/* The same with the X offset 16-bit, all sprites large: an X past the right
 * edge moves the sprite off screen, and a negative one sets X's 9th bit.
 * `*overflow` becomes the overflow of the last Y's addition (as it was when
 * there is none); A's high byte is left from the word X shifted left. */
static ListEnd load_sprite_list_clipped(GameState *g, uint16_t list, bool *overflow)
{
    uint8_t count = open_list(g, list);
    uint16_t entry = scene_sprite_index(g);
    uint16_t high = scene_sprite_high_pointer(g);
    uint8_t a_high = (uint8_t)(list >> 8);
    bool entries = false;
    while (another_entry(g)) {
        entries = true;
        set_entry(g, entry, ENTRY_TILE, hw_port_read(g));
        set_entry(g, entry, ENTRY_ATTRIBUTES, hw_port_read(g));
        uint16_t x = (uint16_t)(hw_port_read(g) + scene_sprite_offset_x(g));
        if (!((uint16_t)(x - SCREEN_WIDTH) & 0x8000))
            x = OFFSCREEN_X;
        set_entry(g, entry, ENTRY_X, (uint8_t)x);
        set_entry(g, entry, ENTRY_Y, (uint8_t)(x >> 8));   /* the Y overwrites it below */
        a_high = (uint8_t)((uint16_t)(x << 1) >> 8);
        shift_in(g, high_byte_at(high), (uint8_t)(HIGH_LARGE | ((x & 0x8000) ? HIGH_X9 : 0)));
        uint8_t y = hw_port_read(g);
        *overflow = y_add_overflows(g, y);
        set_entry(g, entry, ENTRY_Y, offset_y8(g, y));
        if (next_entry(&entry))
            high++;
    }
    close_list(g, entry, high);
    return (ListEnd){ entry, list_a(a_high, count, entries, entry) };
}

// $C3:0DBC, $C3:0DF3
/* One sprite from the four bytes at $7E:source (tile, attributes, X, Y), small
 * or large with X's 9th bit clear. The pointer's low byte moves on after every
 * fourth sprite. Returns the next free entry. */
static uint16_t add_sprite(GameState *g, uint16_t source, bool large)
{
    uint16_t entry = scene_sprite_index(g);
    uint32_t from = (uint32_t)SOURCE_BANK << 16 | source;
    uint16_t tile = wram_read16(g, from);   /* and the attributes */
    set_entry(g, entry, ENTRY_TILE, (uint8_t)tile);
    set_entry(g, entry, ENTRY_ATTRIBUTES, (uint8_t)(tile >> 8));
    set_entry(g, entry, ENTRY_X, offset_x8(g, wram_read8(g, from + 2)));
    set_entry(g, entry, ENTRY_Y, offset_y8(g, wram_read8(g, from + 3)));
    unsigned at = high_byte_at(scene_sprite_high_pointer(g));
    if (large)
        shift_in(g, at, HIGH_LARGE);
    else
        shift_in_small(g, at);
    if (next_entry(&entry)) {
        uint16_t pointer = scene_sprite_high_pointer(g);
        set_scene_sprite_high_pointer(g, (uint16_t)((pointer & 0xFF00) | (uint8_t)(pointer + 1)));
    }
    set_scene_sprite_index(g, entry);
    return entry;
}

/* ---- Entries: 16-bit A and index registers, on the scene's page ------------------------- */

/* The list at $7E:A, small sprites. */
void scene_load_sprite_list(Snes *s)
{
    ListEnd end = load_sprite_list(state_of(s), s->cpu.a);
    s->cpu.a = end.a;
    s->cpu.y = end.entry;
    m_rtl(s);
}

/* The list at $7E:A, large sprites clipped at the right edge. */
void scene_load_sprite_list_clipped(Snes *s)
{
    bool overflow = m_flag(s, CPU_FLAG_V);
    ListEnd end = load_sprite_list_clipped(state_of(s), s->cpu.a, &overflow);
    m_set_flag(s, CPU_FLAG_V, overflow);
    s->cpu.a = end.a;
    s->cpu.y = end.entry;
    m_rtl(s);
}

/* These save P while they work (PHP, PLP): the byte is on the stack, where a
 * source can reach it. */

/* One small sprite from $7E:X. */
void scene_add_sprite(Snes *s)
{
    m_push8(s, s->cpu.p);
    s->cpu.y = add_sprite(state_of(s), s->cpu.x, false);
    m_pull8(s);
    m_rtl(s);
}

/* One large sprite from $7E:X, with X's 9th bit clear. */
void scene_add_large_sprite(Snes *s)
{
    m_push8(s, s->cpu.p);
    add_sprite(state_of(s), s->cpu.x, true);
    m_pull8(s);
    m_rtl(s);
}
