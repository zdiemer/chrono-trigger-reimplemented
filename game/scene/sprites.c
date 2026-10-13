/* Scenes: sprites into the frame's sprite table.
 *
 * The scene programs build each frame's sprites in the sprite table the frame
 * work uploads: 128 four-byte entries -- X, Y, tile, attributes
 * (scene_sprite_table) -- and a high table of two bits a sprite, X's 9th bit
 * and the large size (scene_sprite_high_table). A scene places its sprites
 * through a cursor: the next free entry, and the high-table byte the next
 * sprite's bits go into. Each sprite shifts that byte right two bits and puts
 * its own bits at the top, so four sprites fill a byte and the first ends in
 * the low bits; after every fourth the cursor moves to the next byte. A small
 * sprite with X's 9th bit clear leaves a byte that is already 0 alone. Every
 * sprite is moved by the scene's offset (scene_sprite_offset_x/_y), which is
 * how a scene scrolls its sprites.
 *
 * Sprites come one at a time, or as a list in the program: a count byte (one
 * more than the entries; 0 is 256) and four bytes an entry -- tile, attributes,
 * X and Y -- which the original streams through the work RAM port. A list is
 * placed small, or large and clipped at the right edge, where a sprite that
 * would start past it is put off screen (the race's racers leaving the picture).
 *
 * The original's registers leave the callers the next free entry, and after a
 * clipped list the A its loop ends with and the overflow of its last Y; the
 * entries' wrappers give them back, working the last two out from the list's
 * last entry, which stays where it is. */
#include "scene/scene.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* An entry's bytes, and how the entries share the high table's bytes. */
enum { ENTRY_X = 0, ENTRY_Y = 1, ENTRY_TILE = 2, ENTRY_ATTRIBUTES = 3, ENTRY_BYTES = 4 };
enum { SPRITES_PER_HIGH_BYTE = 4 };

/* A sprite's two high-table bits, at the top of the byte it shifts in. */
enum { HIGH_LARGE = 0x80, HIGH_X9 = 0x40 };

/* The lists and the single sprites' sources are in the first bank of work RAM. */
enum { SOURCE_BANK = SCENE_LOWER_WORK_RAM };

/* A clipped sprite whose X is at or past the right edge is put off screen: at
 * X 257, one past the last X the 9-bit position can show whole. */
enum { SCREEN_RIGHT = 0x100, OFF_THE_RIGHT = 0x101 };

typedef struct {
    uint16_t entry;   /* the next free entry, a byte offset in the table */
    uint8_t high;     /* the high-table byte its bits go into, 0-31 */
} SpriteCursor;

/* The game keeps the high-table cursor as an address; the port, as the byte's
 * place in the table. The guards keep it inside. */
static SpriteCursor cursor(const GameState *g)
{
    return (SpriteCursor){ scene_sprite_index(g), (uint8_t)(scene_sprite_high_pointer(g) - SCENE_SPRITE_HIGH_TABLE) };
}

static void set_cursor(GameState *g, SpriteCursor c)
{
    set_scene_sprite_index(g, c.entry);
    set_scene_sprite_high_pointer(g, (uint16_t)(SCENE_SPRITE_HIGH_TABLE + c.high));
}

static void set_entry(GameState *g, uint16_t entry, uint8_t field, uint8_t v)
{
    set_scene_sprite_table_byte(g, (unsigned)entry + field, v);
}

/* A sprite that is small with X's 9th bit clear: its bits are 0, so a byte
 * that is 0 already stays as it is. */
static void shift_in_small(GameState *g, uint8_t high)
{
    uint8_t bits = scene_sprite_high_table_byte(g, high);
    if (bits)
        set_scene_sprite_high_table_byte(g, high, (uint8_t)(bits >> 2));
}

static void shift_in(GameState *g, uint8_t high, uint8_t sprite_bits)
{
    set_scene_sprite_high_table_byte(g, high, (uint8_t)(sprite_bits | scene_sprite_high_table_byte(g, high) >> 2));
}

/* On to the next entry, and the next high-table byte after every fourth. */
static void next_entry(SpriteCursor *c)
{
    c->entry = (uint16_t)(c->entry + ENTRY_BYTES);
    if (c->entry % (ENTRY_BYTES * SPRITES_PER_HIGH_BYTE) == 0)
        c->high++;
}

/* The scene's offset added to a sprite's X and Y (8-bit, wrapping). */
static uint8_t moved_x(const GameState *g, uint8_t x) { return (uint8_t)(x + scene_sprite_offset_x(g)); }
static uint8_t moved_y(const GameState *g, uint8_t y) { return (uint8_t)(y + scene_sprite_offset_y(g)); }

void scene_place_sprite(GameState *g, uint16_t entry, SceneSprite sprite)
{
    set_entry(g, entry, ENTRY_X, sprite.x);
    set_entry(g, entry, ENTRY_Y, sprite.y);
    set_entry(g, entry, ENTRY_TILE, sprite.tile);
    set_entry(g, entry, ENTRY_ATTRIBUTES, sprite.attributes);
}

/* ---- Lists ------------------------------------------------------------------------------------ */

/* A list: its count byte, then its entries. */
static uint32_t list_address(uint16_t list) { return (uint32_t)SOURCE_BANK << 16 | list; }
static uint32_t list_entry(uint16_t list, unsigned n) { return list_address(list) + 1 + n * ENTRY_BYTES; }
static uint8_t list_sprites(const GameState *g, uint16_t list) { return (uint8_t)(wram_read8(g, list_address(list)) - 1); }

/* The original reads a list through the work RAM port, which it leaves just
 * past the list's last byte (in the port's second bank, if the list runs over). */
static void leave_port_past(GameState *g, uint16_t list, uint8_t sprites)
{
    uint32_t past = (uint32_t)list + 1 + (uint32_t)sprites * ENTRY_BYTES;
    hw_port_seek(g, (uint16_t)past, (uint8_t)(past >> 16));
}

/* A clipped sprite's X: the list's X plus the scene's 16-bit offset. At or past
 * the right edge -- compared by the sign of the difference, as the original
 * does, so $8000-$80FF counts as past it -- it goes off the right. Left of the
 * screen, X's 9th bit is set and the sprite shows as far in from the left edge
 * as the low byte says. */
static uint16_t clipped_x(const GameState *g, uint8_t list_x) { return (uint16_t)(list_x + scene_sprite_offset_x(g)); }
static bool past_the_right(uint16_t x) { return (int16_t)(x - SCREEN_RIGHT) >= 0; }

static void place_clipped_x(GameState *g, SpriteCursor c, uint8_t list_x)
{
    uint16_t x = clipped_x(g, list_x);
    bool off_the_right = past_the_right(x);
    set_entry(g, c.entry, ENTRY_X, (uint8_t)(off_the_right ? OFF_THE_RIGHT : x));
    shift_in(g, c.high, (uint8_t)(HIGH_LARGE | ((off_the_right || (x & 0x8000)) ? HIGH_X9 : 0)));
}

// $C3:0D0E, $C3:0D5E
/* Copy the list at `list` into the sprite table: small sprites with X's 9th bit
 * clear, or large ones clipped at the right edge. The cursor moves on past
 * them. */
static void load_sprite_list(GameState *g, uint16_t list, bool clipped)
{
    uint8_t sprites = list_sprites(g, list);
    SpriteCursor c = cursor(g);
    for (unsigned n = 0; n < sprites; n++) {
        uint32_t at = list_entry(list, n);
        set_entry(g, c.entry, ENTRY_TILE, wram_read8(g, at));
        set_entry(g, c.entry, ENTRY_ATTRIBUTES, wram_read8(g, at + 1));
        /* in the order the original reads and writes them, since a list can
         * lie over the tables it fills */
        if (clipped) {
            place_clipped_x(g, c, wram_read8(g, at + 2));
            set_entry(g, c.entry, ENTRY_Y, moved_y(g, wram_read8(g, at + 3)));
        } else {
            set_entry(g, c.entry, ENTRY_X, moved_x(g, wram_read8(g, at + 2)));
            set_entry(g, c.entry, ENTRY_Y, moved_y(g, wram_read8(g, at + 3)));
            shift_in_small(g, c.high);
        }
        next_entry(&c);
    }
    set_cursor(g, c);
    ORIGINAL_EFFECT(leave_port_past(g, list, sprites));
    ORIGINAL_EFFECT(set_scene_sprite_list_left(g, 0));
}

// $C3:0DBC, $C3:0DF3
/* One sprite from the four bytes at `source` (tile, attributes, X, Y), small
 * or large, with X's 9th bit clear. */
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
    next_entry(&c);
    set_cursor(g, c);
}

/* A sprite added at the cursor as it is, small or large (the captions'
 * letters). */
void scene_add_sprite_at_cursor(GameState *g, SceneSprite sprite, bool large)
{
    SpriteCursor c = cursor(g);
    scene_place_sprite(g, c.entry, sprite);
    if (large)
        shift_in(g, c.high, HIGH_LARGE);
    else
        shift_in_small(g, c.high);
    next_entry(&c);
    set_cursor(g, c);
}

// $7E:3870
/* The frame's sprites done: the entries left in the group of four the cursor
 * is in are put past the screen's right edge (X's 9th bit). With the cursor
 * at a group's start, that is the whole of the next group. */
void scene_hide_rest_of_group(GameState *g)
{
    SpriteCursor c = cursor(g);
    uint8_t bits = scene_sprite_high_table_byte(g, c.high);
    for (unsigned n = (c.entry / ENTRY_BYTES) % SPRITES_PER_HIGH_BYTE; n < SPRITES_PER_HIGH_BYTE; n++)
        bits = (uint8_t)(HIGH_X9 | bits >> 2);
    set_scene_sprite_high_table_byte(g, c.high, bits);
}

/* How many sprites a list in the program holds. */
uint8_t scene_sprite_list_length(const GameState *g, uint16_t list) { return list_sprites(g, list); }

/* Where the scenes' sprite lists are drawn from next: X and Y. X's 9th bit,
 * which only the clipped lists use, stays as it was. */
void scene_move_sprite_offset(GameState *g, uint8_t x, uint8_t y)
{
    set_scene_sprite_offset_x(g, (uint16_t)((scene_sprite_offset_x(g) >> 8) << 8 | x));
    set_scene_sprite_offset_y(g, y);
}

/* Where the scene's sprite lists are drawn from, as the program stores it: X
 * and Y each a word, Y's high byte in the byte after its offset. */
void scene_draw_lists_at(GameState *g, uint16_t x, uint16_t y)
{
    set_scene_sprite_offset_x(g, x);
    set_scene_sprite_offset_y(g, (uint8_t)y);
    set_scene_sprite_offset_y_high(g, (uint8_t)(y >> 8));
}

/* As C calls, for the program's routines ported as C. */
void scene_draw_sprite(GameState *g, uint16_t source) { add_sprite(g, source, false); }
void scene_draw_sprite_list(GameState *g, uint16_t list, bool clipped) { load_sprite_list(g, list, clipped); }

/* ---- Guards --------------------------------------------------------------------------------- */

/* The port writes through the named sprite tables, so it takes only what the
 * game does: a cursor in them, which the sprites it places stay inside. The
 * original reaches the tables through a data bank of 0, so a cursor anywhere
 * else writes wherever it points, the scene's own cells and the I/O registers
 * past $2000 among them; those calls are the original's. A list stays clear of
 * the cell the original counts its entries down in, which the port counts in a
 * local; and a clipped list clear of the tables and of all the scene's sprite
 * cells: the original stores its X as a word whose high byte lands in the Y a
 * moment before the Y does, which a list read from the table could see, and the
 * entry's wrapper reads the list's last entry back after the call. */
static bool fits_in_table(uint16_t entry, uint16_t high, unsigned sprites)
{
    if (sprites == 0)
        return true;
    /* the cursor moves to the next high byte only from an entry at a whole byte's start */
    unsigned last_high = high;
    if (entry % ENTRY_BYTES == 0)
        last_high += (entry / ENTRY_BYTES % SPRITES_PER_HIGH_BYTE + sprites - 1) / SPRITES_PER_HIGH_BYTE;
    return (unsigned)entry + sprites * ENTRY_BYTES <= SCENE_SPRITE_TABLE_BYTES && high >= SCENE_SPRITE_HIGH_TABLE &&
           last_high < SCENE_SPRITE_HIGH_TABLE + SCENE_SPRITE_HIGH_TABLE_BYTES;
}

/* Whether `sprites` more fit in the tables from the cursor, for the guards of
 * ports that draw through these as C. */
bool scene_sprites_fit(const GameState *g, unsigned sprites)
{
    return fits_in_table(scene_sprite_index(g), scene_sprite_high_pointer(g), sprites);
}

static bool cursor_fits(Snes *s, unsigned sprites)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = fits_in_table(scene_sprite_index(g), scene_sprite_high_pointer(g), sprites);
    vars_peek(false);
    return scene_on_scene_page_io_bank(s) && fits;
}

/* Where the original counts a list's entries down, the first of the scene's
 * sprite cells before it, and the end of the high table. */
enum {
    COUNT_CELL = RAM_SCENE_SPRITE_LIST_LEFT & 0xFFFF,
    FIRST_SPRITE_CELL = RAM_SCENE_SPRITE_INDEX & 0xFFFF,
    SPRITE_TABLES_END = SCENE_SPRITE_HIGH_TABLE + SCENE_SPRITE_HIGH_TABLE_BYTES,
};

static bool list_fits(Snes *s, bool clipped)
{
    uint16_t list = s->cpu.a;
    unsigned sprites = (uint8_t)(snes_bus_peek(&s->bus, list_address(list)) - 1);
    if (!cursor_fits(s, sprites))
        return false;
    uint32_t end = (uint32_t)list + 1 + sprites * ENTRY_BYTES;
    uint16_t first_kept_clear = clipped ? FIRST_SPRITE_CELL : COUNT_CELL;
    if (list <= COUNT_CELL && end > first_kept_clear)
        return false;
    return !clipped || end <= SCENE_SPRITE_TABLE || list >= SPRITE_TABLES_END;
}

/* The cursor's high-table byte inside the table. */
static bool high_cursor_in_table(Snes *s)
{
    vars_peek(true);
    uint16_t high = scene_sprite_high_pointer(state_of(s));
    vars_peek(false);
    return high >= SCENE_SPRITE_HIGH_TABLE && high < SPRITE_TABLES_END;
}

bool scene_sprite_list_fits(Snes *s) { return list_fits(s, false); }
bool scene_clipped_sprite_list_fits(Snes *s) { return list_fits(s, true); }
bool scene_sprite_fits(Snes *s) { return cursor_fits(s, 1); }

/* ---- Entries: 16-bit A and index registers, on the scene's page ------------------------- */

bool scene_hide_rest_of_group_present(Snes *s)
{
    return m_code_matches(s, 0x7E3870, 0x1B, 0xB778A96EC9EEDB9Aull) && scene_on_scene_page_low_bank(s) &&
           high_cursor_in_table(s);
}

/* Back with 8-bit A, holding the byte it left, and the count it reached and
 * the carry of its last compare, as the original leaves them. */
void scene_hide_rest_of_group_entry(Snes *s)
{
    GameState *g = state_of(s);
    scene_hide_rest_of_group(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_sprite_high_table_byte(g, cursor(g).high));
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_C, true));   /* the count of the group's sprites reached, */
    ORIGINAL_EFFECT(s->cpu.x = SPRITES_PER_HIGH_BYTE);  /* which stays in X */
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* The list at A, small sprites; back, Y is the next free entry. The original's
 * A ends with the list's high byte, which it already has (its low byte isn't
 * read). */
void scene_load_sprite_list(Snes *s)
{
    load_sprite_list(state_of(s), s->cpu.a, false);
    s->cpu.y = scene_sprite_index(state_of(s));
    m_rtl(s);
}

/* What a clipped list leaves, from its last entry: A's high byte is that
 * sprite's X shifted left -- where an X past the right edge is the word $9801,
 * as the original stores it (its high byte overwritten by the Y) -- and V the
 * overflow of adding the scene's offset to its Y. An empty list leaves both. */
typedef struct {
    uint16_t a;
    bool overflow;
} ClippedListLeaves;

static ClippedListLeaves clipped_list_leaves(const GameState *g, uint16_t list, uint16_t a, bool overflow)
{
    uint8_t sprites = list_sprites(g, list);
    if (sprites) {
        uint32_t last = list_entry(list, sprites - 1u);
        uint16_t x = clipped_x(g, wram_read8(g, last + 2));
        uint16_t stored = past_the_right(x) ? 0x9801 : x;
        uint8_t y = wram_read8(g, last + 3), offset = (uint8_t)scene_sprite_offset_y(g);
        uint8_t sum = (uint8_t)(y + offset);
        a = (uint16_t)(((uint16_t)(stored << 1) & 0xFF00) | (a & 0xFF));
        overflow = (~(y ^ offset) & (y ^ sum) & 0x80) != 0;
    }
    return (ClippedListLeaves){ a, overflow };
}

/* The A a clipped list leaves, from the A it was called with, for the ports
 * that call it as C and hand A on as the original does. */
uint16_t scene_clipped_list_leaves_a(const GameState *g, uint16_t list, uint16_t a)
{
    return clipped_list_leaves(g, list, a, false).a;
}

static void leave_after_clipped_list(Snes *s, uint16_t list)
{
    vars_peek(true);
    ClippedListLeaves left = clipped_list_leaves(state_of(s), list, s->cpu.a, m_flag(s, CPU_FLAG_V));
    vars_peek(false);
    s->cpu.a = left.a;
    m_set_flag(s, CPU_FLAG_V, left.overflow);
}

/* The list at A, large sprites clipped at the right edge; back, Y is the next
 * free entry, and A and V as above. */
void scene_load_sprite_list_clipped(Snes *s)
{
    uint16_t list = s->cpu.a;
    load_sprite_list(state_of(s), list, true);
    leave_after_clipped_list(s, list);
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
