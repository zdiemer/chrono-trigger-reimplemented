/* Scenes: sprites into the frame's sprite table.
 *
 * The scene programs build each frame's sprites in the sprite table the frame
 * work uploads (scene_sprite_table: 128 four-byte entries -- X, Y, tile,
 * attributes -- and a high table of two bits a sprite, X's 9th bit and the
 * large size). A scene places its sprites through a cursor: the next free
 * entry, and the high-table byte the next sprite's bits go into. Each sprite
 * shifts that byte right two bits and puts its own bits at the top, so four
 * sprites fill a byte and the first ends in the low bits; after every fourth
 * the cursor moves to the next byte. A small sprite with X's 9th bit clear
 * leaves a byte that is already 0 alone. Every sprite is moved by the scene's
 * offset (scene_sprite_offset_x/_y), which is how a scene scrolls its sprites.
 *
 * Sprites come one at a time, or as a list in the program: a count byte (one
 * more than the entries; 0 is 256) and four bytes an entry -- tile, attributes,
 * X and Y -- streamed through the work RAM port. A list is placed small, or large
 * and clipped at the right edge, where a sprite that would start past it is put
 * off screen (the race's racers leaving the picture).
 *
 * The original's registers leave the callers the next free entry, and after a
 * list the A its loop ends with and the overflow of its last Y; the entries'
 * wrappers give them back. */
#include "scene/scene.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* An entry's bytes, and where the table and its high table are. */
enum { ENTRY_X = 0, ENTRY_Y = 1, ENTRY_TILE = 2, ENTRY_ATTRIBUTES = 3, ENTRY_BYTES = 4 };
enum { SPRITE_TABLE = 0x0700, SPRITE_ENTRIES_BYTES = 0x200, HIGH_TABLE = 0x0900, HIGH_TABLE_END = 0x0920 };
enum { SPRITES_PER_HIGH_BYTE = 4 };

/* A sprite's two high-table bits, at the top of the byte it shifts in. */
enum { HIGH_LARGE = 0x80, HIGH_X9 = 0x40 };

/* The lists and the single sprites' sources are in the first bank of work RAM. */
enum { SOURCE_BANK = 0x7E };

/* A clipped sprite whose X is at or past the right edge is put off screen: at
 * X 257, one past the last X the 9-bit position can show whole. */
enum { SCREEN_RIGHT = 0x100, OFF_THE_RIGHT = 0x101 };

typedef struct {
    uint16_t entry;   /* the next free entry, a byte offset in the table */
    uint16_t high;    /* the address of the high-table byte its bits go into */
} SpriteCursor;

static SpriteCursor cursor(const GameState *g)
{
    return (SpriteCursor){ scene_sprite_index(g), scene_sprite_high_pointer(g) };
}

static void set_entry(GameState *g, uint16_t entry, uint8_t field, uint8_t v)
{
    set_scene_sprite_table_byte(g, (unsigned)entry + field, v);
}

/* The high-table byte the cursor holds the address of. */
static unsigned high_byte(uint16_t high) { return (unsigned)(high - SPRITE_TABLE); }

/* A sprite that is small with X's 9th bit clear: its bits are 0, so a byte
 * that is 0 already stays as it is. */
static void shift_in_small(GameState *g, uint16_t high)
{
    uint8_t bits = scene_sprite_table_byte(g, high_byte(high));
    if (bits)
        set_scene_sprite_table_byte(g, high_byte(high), (uint8_t)(bits >> 2));
}

static void shift_in(GameState *g, uint16_t high, uint8_t sprite_bits)
{
    set_scene_sprite_table_byte(g, high_byte(high),
                                (uint8_t)(sprite_bits | scene_sprite_table_byte(g, high_byte(high)) >> 2));
}

/* On to the next entry: true when it starts a new high-table byte. */
static bool next_entry(uint16_t *entry)
{
    *entry = (uint16_t)(*entry + ENTRY_BYTES);
    return *entry % (ENTRY_BYTES * SPRITES_PER_HIGH_BYTE) == 0;
}

/* The scene's offset added to a sprite's X and Y (8-bit, wrapping). */
static uint8_t moved_x(const GameState *g, uint8_t x) { return (uint8_t)(x + scene_sprite_offset_x(g)); }
static uint8_t moved_y(const GameState *g, uint8_t y) { return (uint8_t)(y + scene_sprite_offset_y(g)); }

/* ---- What a list leaves in the original's registers, for the entries' wrappers -------------- */

typedef struct {
    uint8_t a_high, a_low;
    bool overflow;
} SpriteLeaves;

static SpriteLeaves *sprite_leaves;

/* A: its low byte the count for a list with no entries, else the last entry's
 * place in its high-table byte (the loop's test); its high byte the list's
 * address's for a small list, and for a clipped one the last sprite's X shifted
 * left -- where an X past the right edge is the word $9801, as the original
 * stores it (its high byte overwritten by the Y). */
static void leave_a(uint8_t high, uint8_t low)
{
    sprite_leaves->a_high = high;
    sprite_leaves->a_low = low;
}

static void leave_a_low(uint8_t low) { sprite_leaves->a_low = low; }

static void leave_a_high_from_x(uint16_t x, bool off_the_right)
{
    sprite_leaves->a_high = (uint8_t)((uint16_t)((off_the_right ? 0x9801 : x) << 1) >> 8);
}

/* The overflow of adding the scene's offset to a sprite's Y. */
static void leave_overflow_of_y(uint8_t y, uint8_t offset)
{
    uint8_t sum = (uint8_t)(y + offset);
    sprite_leaves->overflow = (~(y ^ offset) & (y ^ sum) & 0x80) != 0;
}

/* ---- Lists ------------------------------------------------------------------------------------ */

/* A clipped sprite's X: the list's X plus the scene's 16-bit offset. At or past
 * the right edge -- compared by the sign of the difference, as the original
 * does, so $8000-$80FF counts as past it -- it goes off the right. Left of the
 * screen, X's 9th bit is set and the sprite shows as far in from the left edge
 * as the low byte says. */
static void place_clipped_x(GameState *g, uint16_t entry, uint16_t high, uint8_t list_x)
{
    uint16_t x = (uint16_t)(list_x + scene_sprite_offset_x(g));
    bool off_the_right = (int16_t)(x - SCREEN_RIGHT) >= 0;
    uint16_t placed = off_the_right ? OFF_THE_RIGHT : x;
    set_entry(g, entry, ENTRY_X, (uint8_t)placed);
    shift_in(g, high, (uint8_t)(HIGH_LARGE | ((off_the_right || (x & 0x8000)) ? HIGH_X9 : 0)));
    ORIGINAL_EFFECT(leave_a_high_from_x(x, off_the_right));
}

// $C3:0D0E, $C3:0D5E
/* Copy the list at `list` into the sprite table: small sprites with X's 9th bit
 * clear, or large ones clipped at the right edge. The cursor moves on past
 * them. */
static void load_sprite_list(GameState *g, uint16_t list, bool clipped)
{
    hw_port_seek(g, list, 0);
    uint8_t count = hw_port_read(g);
    SpriteCursor c = cursor(g);
    ORIGINAL_EFFECT(leave_a((uint8_t)(list >> 8), count));
    for (uint8_t left = (uint8_t)(count - 1); left != 0; left--) {
        set_entry(g, c.entry, ENTRY_TILE, hw_port_read(g));
        set_entry(g, c.entry, ENTRY_ATTRIBUTES, hw_port_read(g));
        if (clipped) {
            place_clipped_x(g, c.entry, c.high, hw_port_read(g));
            uint8_t y = hw_port_read(g);
            ORIGINAL_EFFECT(leave_overflow_of_y(y, scene_sprite_offset_y(g)));
            set_entry(g, c.entry, ENTRY_Y, moved_y(g, y));
        } else {
            set_entry(g, c.entry, ENTRY_X, moved_x(g, hw_port_read(g)));
            set_entry(g, c.entry, ENTRY_Y, moved_y(g, hw_port_read(g)));
            shift_in_small(g, c.high);
        }
        if (next_entry(&c.entry))
            c.high++;
        ORIGINAL_EFFECT(leave_a_low((uint8_t)(c.entry % (ENTRY_BYTES * SPRITES_PER_HIGH_BYTE))));
    }
    set_scene_sprite_high_pointer(g, c.high);
    set_scene_sprite_index(g, c.entry);
    ORIGINAL_EFFECT(set_scene_sprite_list_left(g, 0));
}

// $C3:0DBC, $C3:0DF3
/* One sprite from the four bytes at `source` (tile, attributes, X, Y), small
 * or large, with X's 9th bit clear. The high-table cursor moves on within its
 * page. */
static void add_sprite(GameState *g, uint16_t source, bool large)
{
    SpriteCursor c = cursor(g);
    uint32_t from = (uint32_t)SOURCE_BANK << 16 | source;
    uint16_t looks = wram_read16(g, from);   /* the tile and the attributes */
    set_entry(g, c.entry, ENTRY_TILE, (uint8_t)looks);
    set_entry(g, c.entry, ENTRY_ATTRIBUTES, (uint8_t)(looks >> 8));
    set_entry(g, c.entry, ENTRY_X, moved_x(g, wram_read8(g, from + 2)));
    set_entry(g, c.entry, ENTRY_Y, moved_y(g, wram_read8(g, from + 3)));
    if (large)
        shift_in(g, c.high, HIGH_LARGE);
    else
        shift_in_small(g, c.high);
    if (next_entry(&c.entry))
        set_scene_sprite_high_pointer(g, (uint16_t)((c.high & 0xFF00) | (uint8_t)(c.high + 1)));
    set_scene_sprite_index(g, c.entry);
}

/* ---- Guards --------------------------------------------------------------------------------- */

/* The port writes through the named sprite table, so it takes only what the
 * game does: a cursor in the table, which the sprites it places stay inside
 * (the entries in its first $200 bytes, the high bytes in its high table). The
 * original reaches the table through a data bank of 0, so a cursor anywhere else
 * writes wherever it points, the scene's own cells and the I/O registers past
 * $2000 among them; those calls are the original's. A clipped list also stays
 * clear of the table: the original stores its X as a word whose high byte lands
 * in the Y a moment before the Y does, which a list read from the table could
 * see. */
static bool fits_in_table(uint16_t entry, uint16_t high, unsigned sprites)
{
    if (sprites == 0)
        return true;
    /* the cursor moves to the next high byte only from an entry at a whole byte's start */
    unsigned last_high = high;
    if (entry % ENTRY_BYTES == 0)
        last_high += (entry / ENTRY_BYTES % SPRITES_PER_HIGH_BYTE + sprites - 1) / SPRITES_PER_HIGH_BYTE;
    return (unsigned)entry + sprites * ENTRY_BYTES <= SPRITE_ENTRIES_BYTES && high >= HIGH_TABLE &&
           last_high < HIGH_TABLE_END;
}

static bool cursor_fits(Snes *s, unsigned sprites)
{
    vars_peek(true);
    SpriteCursor c = cursor(state_of(s));
    vars_peek(false);
    return scene_on_scene_page_io_bank(s) && fits_in_table(c.entry, c.high, sprites);
}

static bool list_fits(Snes *s, bool clipped)
{
    uint16_t list = s->cpu.a;
    uint8_t count = snes_bus_peek(&s->bus, (uint32_t)SOURCE_BANK << 16 | list);
    unsigned sprites = (uint8_t)(count - 1);
    if (!cursor_fits(s, sprites))
        return false;
    if (!clipped)
        return true;
    uint32_t end = (uint32_t)list + 1 + sprites * ENTRY_BYTES;
    return end <= SPRITE_TABLE || list >= HIGH_TABLE_END;
}

bool scene_sprite_list_fits(Snes *s) { return list_fits(s, false); }
bool scene_clipped_sprite_list_fits(Snes *s) { return list_fits(s, true); }
bool scene_sprite_fits(Snes *s) { return cursor_fits(s, 1); }

/* ---- Entries: 16-bit A and index registers, on the scene's page ------------------------- */

/* The list at A, small sprites; back, Y is the next free entry. */
void scene_load_sprite_list(Snes *s)
{
    SpriteLeaves leaves = { 0, 0, m_flag(s, CPU_FLAG_V) };
    sprite_leaves = &leaves;
    load_sprite_list(state_of(s), s->cpu.a, false);
    s->cpu.a = (uint16_t)(leaves.a_high << 8 | leaves.a_low);
    s->cpu.y = scene_sprite_index(state_of(s));
    m_rtl(s);
}

/* The list at A, large sprites clipped at the right edge; back, Y is the next
 * free entry, and the overflow the last Y's. */
void scene_load_sprite_list_clipped(Snes *s)
{
    SpriteLeaves leaves = { 0, 0, m_flag(s, CPU_FLAG_V) };
    sprite_leaves = &leaves;
    load_sprite_list(state_of(s), s->cpu.a, true);
    s->cpu.a = (uint16_t)(leaves.a_high << 8 | leaves.a_low);
    m_set_flag(s, CPU_FLAG_V, leaves.overflow);
    s->cpu.y = scene_sprite_index(state_of(s));
    m_rtl(s);
}

/* These save P while they work (PHP, PLP): the byte is on the stack, where a
 * source can reach it. */

/* One small sprite from X; back, Y is the next free entry. */
void scene_add_sprite(Snes *s)
{
    m_push8(s, s->cpu.p);
    add_sprite(state_of(s), s->cpu.x, false);
    s->cpu.y = scene_sprite_index(state_of(s));
    m_pull8(s);
    m_rtl(s);
}

/* One large sprite from X. */
void scene_add_large_sprite(Snes *s)
{
    m_push8(s, s->cpu.p);
    add_sprite(state_of(s), s->cpu.x, true);
    m_pull8(s);
    m_rtl(s);
}
