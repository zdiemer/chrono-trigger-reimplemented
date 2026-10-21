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
 * would start past it is put off screen (the race's racers leaving the
 * picture), or large through the program's own list routine ($7E:A03D, which
 * the fireworks draw their bursts with), which works each Y out as a word and
 * can leave out a sprite at the picture's bottom or below; or with signed
 * offsets, each sprite's size in its attributes, through another of the
 * program's own ($7E:3A97, the endings' rider of the swept window), where a
 * sprite left of the picture shows with X's 9th bit and one past the right
 * edge or below the picture is put out of sight.
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

/* A list: its count byte, then its entries, each a sprite's tile,
 * attributes, X and Y. */
enum { LISTED_TILE = 0, LISTED_ATTRIBUTES = 1, LISTED_X = 2, LISTED_Y = 3 };

typedef struct {
    uint8_t tile, attributes, x, y;
} ListedSprite;

static uint32_t list_address(uint16_t list) { return (uint32_t)SOURCE_BANK << 16 | list; }
static uint8_t list_sprites(const GameState *g, uint16_t list) { return (uint8_t)(wram_read8(g, list_address(list)) - 1); }

/* One of the `n`th sprite's bytes: what reads a single one of them, and the
 * lists that may lie over the tables they fill and so are read a byte at a
 * time, as the original reads them. */
static uint8_t listed_byte(const GameState *g, uint16_t list, unsigned n, uint8_t field)
{
    return wram_read8(g, list_address(list) + 1 + n * ENTRY_BYTES + field);
}

static ListedSprite listed_sprite(const GameState *g, uint16_t list, unsigned n)
{
    return (ListedSprite){ listed_byte(g, list, n, LISTED_TILE), listed_byte(g, list, n, LISTED_ATTRIBUTES),
                           listed_byte(g, list, n, LISTED_X), listed_byte(g, list, n, LISTED_Y) };
}

/* The original reads a list through the work RAM port, which it leaves just
 * past the list's last byte (in the port's second bank, if the list runs
 * over), the port's bank byte as the routine set it: 0, or for the signed
 * list's routine the bank's own number. */
enum { PORT_BANK_ZERO = 0x00, PORT_BANK_NAMED = SOURCE_BANK };

static void leave_port_past(GameState *g, uint8_t bank, uint16_t list, uint8_t sprites)
{
    uint32_t past = ((uint32_t)bank << 16 | list) + 1 + (uint32_t)sprites * ENTRY_BYTES;
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
        /* a byte at a time, in the order the original reads and writes them,
         * since a list of small sprites can lie over the tables it fills */
        set_entry(g, c.entry, ENTRY_TILE, listed_byte(g, list, n, LISTED_TILE));
        set_entry(g, c.entry, ENTRY_ATTRIBUTES, listed_byte(g, list, n, LISTED_ATTRIBUTES));
        if (clipped) {
            place_clipped_x(g, c, listed_byte(g, list, n, LISTED_X));
            set_entry(g, c.entry, ENTRY_Y, moved_y(g, listed_byte(g, list, n, LISTED_Y)));
        } else {
            set_entry(g, c.entry, ENTRY_X, moved_x(g, listed_byte(g, list, n, LISTED_X)));
            set_entry(g, c.entry, ENTRY_Y, moved_y(g, listed_byte(g, list, n, LISTED_Y)));
            shift_in_small(g, c.high);
        }
        next_entry(&c);
    }
    set_cursor(g, c);
    ORIGINAL_EFFECT(leave_port_past(g, PORT_BANK_ZERO, list, sprites));
    ORIGINAL_EFFECT(set_scene_sprite_list_left(g, 0));
}

/* The lists' offset down as a whole word, which the program's own list of
 * large sprites works its Ys out on before it keeps their low bytes. */
static uint16_t offset_y_word(const GameState *g)
{
    return (uint16_t)(scene_sprite_offset_y(g) | scene_sprite_offset_y_high(g) << 8);
}

static uint16_t large_list_y(const GameState *g, uint8_t list_y) { return (uint16_t)(list_y + offset_y_word(g)); }

/* The first line below the picture. */
enum { BELOW_THE_PICTURE = 0xE0 };

// $7E:A03D
/* Copy the list at `list` into the sprite table as large sprites, each moved
 * by the scene's offset, X's 9th bit clear. The cursor moves on past them.
 *
 * The routine is the program's own, and its callers choose between its two
 * forms by writing its code (scene_large_list_clip): as the program is loaded
 * it leaves out a sprite whose Y comes to the picture's bottom or below
 * (another ending's picture, $7E:9C58, draws with it); the fireworks write
 * that test out while they draw their bursts, so that every sprite of a burst
 * shows, however low. A sprite left out has had its tile, its attributes and
 * its X written to the free entry already, which the next sprite takes. */
void scene_draw_large_sprite_list(GameState *g, uint16_t list, SceneLargeList form)
{
    uint8_t sprites = list_sprites(g, list);
    SpriteCursor c = cursor(g);
    for (unsigned n = 0; n < sprites; n++) {
        /* a byte at a time, as the original reads them: the list can lie over
         * the tables it fills */
        set_entry(g, c.entry, ENTRY_TILE, listed_byte(g, list, n, LISTED_TILE));
        set_entry(g, c.entry, ENTRY_ATTRIBUTES, listed_byte(g, list, n, LISTED_ATTRIBUTES));
        set_entry(g, c.entry, ENTRY_X, moved_x(g, listed_byte(g, list, n, LISTED_X)));
        uint16_t y = large_list_y(g, listed_byte(g, list, n, LISTED_Y));
        if (form == LIST_LEAVES_OUT_BELOW_THE_PICTURE && y >= BELOW_THE_PICTURE)
            continue;
        set_entry(g, c.entry, ENTRY_Y, (uint8_t)y);
        shift_in(g, c.high, HIGH_LARGE);
        next_entry(&c);
    }
    set_cursor(g, c);
    ORIGINAL_EFFECT(leave_port_past(g, PORT_BANK_ZERO, list, sprites));
    ORIGINAL_EFFECT(set_scene_sprite_list_left(g, 0));
}

/* A list with signed offsets: an entry's X and Y count from the scene's
 * offsets, both whole words, either way; its attributes carry its size, a
 * bit that isn't kept in the table. */
enum { SIGNED_LIST_LARGE = 0x10 };

static int16_t signed_list_x(const GameState *g, uint8_t list_x)
{
    return (int16_t)(uint16_t)((int8_t)list_x + scene_sprite_offset_x(g));
}

static int16_t signed_list_y(const GameState *g, uint8_t list_y)
{
    return (int16_t)(uint16_t)((int8_t)list_y + offset_y_word(g));
}

/* A sprite at or past the right edge, or above or below the picture, shows
 * nowhere: it goes below the picture. */
static bool past_the_right_edge(int16_t x) { return x >= SCREEN_RIGHT; }
static uint8_t shown_y(int16_t y) { return y < 0 || y >= BELOW_THE_PICTURE ? BELOW_THE_PICTURE : (uint8_t)y; }

/* What the original's drawing leaves in its cells: the work RAM port past the
 * list, the count run down, and the last sprite's attributes, which it keeps
 * to test the sprite's size. */
static void leave_signed_list_drawn(GameState *g, uint16_t list, uint8_t sprites)
{
    leave_port_past(g, PORT_BANK_NAMED, list, sprites);
    set_scene_sprite_list_left(g, 0);
    if (sprites)
        set_scene_sprite_list_attributes(g, listed_byte(g, list, sprites - 1u, LISTED_ATTRIBUTES));
}

// $7E:3A97
/* Copy the list at `list` into the sprite table, each sprite placed by its
 * signed offsets from the scene's and as large as its attributes say. A sprite
 * left of the picture shows as far in from its left edge as X's low byte says
 * (X's 9th bit); one that would start past the right edge goes below the
 * picture, small, its X in the table left as it was. The cursor moves on past
 * them. */
void scene_draw_signed_sprite_list(GameState *g, uint16_t list)
{
    uint8_t sprites = list_sprites(g, list);
    SpriteCursor c = cursor(g);
    for (unsigned n = 0; n < sprites; n++) {
        ListedSprite sprite = listed_sprite(g, list, n);
        set_entry(g, c.entry, ENTRY_TILE, sprite.tile);
        set_entry(g, c.entry, ENTRY_ATTRIBUTES, (uint8_t)(sprite.attributes & ~SIGNED_LIST_LARGE));
        int16_t x = signed_list_x(g, sprite.x);
        if (past_the_right_edge(x)) {
            shift_in(g, c.high, 0);
            set_entry(g, c.entry, ENTRY_Y, BELOW_THE_PICTURE);
        } else {
            set_entry(g, c.entry, ENTRY_X, (uint8_t)x);
            shift_in(g, c.high, (uint8_t)((sprite.attributes & SIGNED_LIST_LARGE ? HIGH_LARGE : 0) | (x < 0 ? HIGH_X9 : 0)));
            set_entry(g, c.entry, ENTRY_Y, shown_y(signed_list_y(g, sprite.y)));
        }
        next_entry(&c);
    }
    set_cursor(g, c);
    ORIGINAL_EFFECT(leave_signed_list_drawn(g, list, sprites));
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

/* As C calls, for the program's routines ported as C: one small sprite from
 * its four bytes in the program, or one the port has as values, each moved by
 * the scene's offset. */
void scene_draw_sprite(GameState *g, uint16_t source) { add_sprite(g, source, false); }
void scene_draw_sprite_moved(GameState *g, SceneSprite sprite)
{
    sprite.x = moved_x(g, sprite.x);
    sprite.y = moved_y(g, sprite.y);
    scene_add_sprite_at_cursor(g, sprite, false);
}
void scene_draw_sprite_list(GameState *g, uint16_t list, bool clipped) { load_sprite_list(g, list, clipped); }

/* Where the sprite table's cursor stands in its group of four. */
uint8_t scene_sprite_place_in_group(const GameState *g)
{
    return (uint8_t)(scene_sprite_index(g) % (ENTRY_BYTES * SPRITES_PER_HIGH_BYTE));
}

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

/* ---- What the lists leave, for the wrappers -------------------------------------------------- */

/* What the program's list routines leave in the registers and flags as they
 * return, worked out from the list and the sprite table: for the entries
 * below, and for the ports that draw a list as C where the original called
 * the routine and hand on what it left (scene.h). None of it is the game's. */

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
        uint16_t x = clipped_x(g, listed_byte(g, list, sprites - 1u, LISTED_X));
        uint16_t stored = past_the_right(x) ? 0x9801 : x;
        uint8_t y = listed_byte(g, list, sprites - 1u, LISTED_Y), offset = (uint8_t)scene_sprite_offset_y(g);
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

bool scene_clipped_list_leaves_overflow(const GameState *g, uint16_t list)
{
    return clipped_list_leaves(g, list, 0, false).overflow;
}

/* What the program's list of large sprites leaves in A: its last sprite's Y
 * word, where that sprite was left out; otherwise that word's high byte over
 * the next free entry's place in its group of four; with no sprite, the
 * list's high byte over its count. */
static uint16_t large_list_leaves_a(const GameState *g, uint16_t list, SceneLargeList form)
{
    uint8_t sprites = list_sprites(g, list);
    if (!sprites)
        return (uint16_t)((list & 0xFF00) | wram_read8(g, list_address(list)));
    uint16_t y = large_list_y(g, listed_byte(g, list, sprites - 1u, LISTED_Y));
    if (form == LIST_LEAVES_OUT_BELOW_THE_PICTURE && y >= BELOW_THE_PICTURE)
        return y;
    return (uint16_t)((y & 0xFF00) | (scene_sprite_index(g) & (ENTRY_BYTES * SPRITES_PER_HIGH_BYTE - 1)));
}

/* What the signed list leaves in A: the next free entry's place in its group
 * of four, over the high byte of its last sprite's X where that sprite went
 * past the right edge (0 otherwise); with no sprite, its count. */
uint16_t scene_signed_list_leaves_a(const GameState *g, uint16_t list)
{
    uint8_t sprites = list_sprites(g, list);
    if (!sprites)
        return wram_read8(g, list_address(list));
    int16_t x = signed_list_x(g, listed_byte(g, list, sprites - 1u, LISTED_X));
    uint16_t high = past_the_right_edge(x) ? (uint16_t)((uint16_t)x & 0xFF00) : 0;
    return (uint16_t)(high | scene_sprite_place_in_group(g));
}

/* The bit of the last sprite's high-table byte, as it is before the list is
 * drawn, that the routine's shifts bring down to the byte's second bit by that
 * sprite: a shift of two for each of the list's sprites before it in the
 * byte. */
static bool last_high_bit(const GameState *g, uint16_t list)
{
    uint8_t sprites = list_sprites(g, list);
    if (!sprites)
        return false;
    SpriteCursor c = cursor(g);
    unsigned before_in_the_byte = 0;
    for (unsigned n = 1; n < sprites; n++) {
        next_entry(&c);
        before_in_the_byte = c.entry % (ENTRY_BYTES * SPRITES_PER_HIGH_BYTE) == 0 ? 0 : before_in_the_byte + 1;
    }
    return (scene_sprite_high_table_byte(g, c.high) >> (2 * before_in_the_byte + 1)) & 1;
}

/* Looked at without the port's time charged for it, as the wrapper's leaves
 * are. */
static bool last_high_bit_before(const GameState *g, uint16_t list)
{
    vars_peek(true);
    bool bit = last_high_bit(g, list);
    vars_peek(false);
    return bit;
}

/* The flags the routine leaves, which a caller's random draw mixes in (the
 * Epoch's dive): the count run down (zero, not negative); the overflow of the
 * last sprite's last sum and the carry of its last test. For a sprite that
 * would start past the right edge, those of its X's sum and of the shift of
 * its high-table byte, which pushes out the byte's bit from before the list
 * was drawn (`high_bit_before`); for any other, its Y's sum's overflow, and
 * the carry of the sum where the sprite is above the top, or of its compare
 * with the bottom of the picture. With no sprite, both as it was called. */
static bool word_sum_overflows(uint16_t a, uint16_t b, uint16_t sum) { return (~(a ^ b) & (a ^ sum) & 0x8000) != 0; }

static void leave_the_last_sprite_s_flags(Snes *s, const GameState *g, uint16_t list, uint8_t sprites,
                                          bool high_bit_before)
{
    uint16_t listed_x = (uint16_t)(int8_t)listed_byte(g, list, sprites - 1u, LISTED_X);
    uint16_t x = (uint16_t)signed_list_x(g, listed_byte(g, list, sprites - 1u, LISTED_X));
    if (past_the_right_edge((int16_t)x)) {
        m_set_flag(s, CPU_FLAG_V, word_sum_overflows(listed_x, scene_sprite_offset_x(g), x));
        m_set_flag(s, CPU_FLAG_C, high_bit_before);
        return;
    }
    uint16_t listed_y = (uint16_t)(int8_t)listed_byte(g, list, sprites - 1u, LISTED_Y);
    uint16_t y = (uint16_t)signed_list_y(g, listed_byte(g, list, sprites - 1u, LISTED_Y));
    m_set_flag(s, CPU_FLAG_V, word_sum_overflows(listed_y, offset_y_word(g), y));
    m_set_flag(s, CPU_FLAG_C, (int16_t)y < 0 ? (uint32_t)listed_y + offset_y_word(g) > 0xFFFF : y >= BELOW_THE_PICTURE);
}

static void leave_signed_list_flags(Snes *s, uint16_t list, bool high_bit_before)
{
    const GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    vars_peek(true);
    uint8_t sprites = list_sprites(g, list);
    if (sprites)
        leave_the_last_sprite_s_flags(s, g, list, sprites, high_bit_before);
    vars_peek(false);
}

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

/* The two forms of the program's list of large sprites, as its code holds
 * them where the callers write it: the test that leaves a sprite out (a
 * branch back to the next sprite where the Y is at the picture's bottom or
 * below, as the program is loaded), or that test written out with two
 * no-operations (the fireworks'). Any other code there is no form the program
 * writes, and the guard hands it to the original. */
enum { LEAVES_OUT_BELOW_THE_PICTURE_CODE = 0xD8B0, KEEPS_EVERY_SPRITE_CODE = 0xEAEA };

static bool is_a_large_list_form(uint16_t code)
{
    return code == KEEPS_EVERY_SPRITE_CODE || code == LEAVES_OUT_BELOW_THE_PICTURE_CODE;
}

static SceneLargeList large_list_form(uint16_t code)
{
    return code == KEEPS_EVERY_SPRITE_CODE ? LIST_KEEPS_EVERY_SPRITE : LIST_LEAVES_OUT_BELOW_THE_PICTURE;
}

/* The list at A, in the form the routine's code holds; back, Y is the next
 * free entry, as the routine keeps it in its cells. */
void scene_draw_large_sprite_list_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t list = s->cpu.a;
    SceneLargeList form = large_list_form(scene_large_list_clip(g));
    scene_draw_large_sprite_list(g, list, form);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = large_list_leaves_a(g, list, form));
    m_rts(s);
}

/* The list at A; back, Y is the next free entry, as the routine keeps it in
 * its cells, with 16-bit A, and X as it was. */
void scene_draw_signed_sprite_list_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t list = s->cpu.a;
    bool high_bit_before = false;
    ORIGINAL_EFFECT(high_bit_before = last_high_bit_before(g, list));
    scene_draw_signed_sprite_list(g, list);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_signed_list_leaves_a(g, list));
    ORIGINAL_EFFECT(leave_signed_list_flags(s, list, high_bit_before));
    m_rtl(s);
}

/* Where the original keeps a sprite's attributes while it places it. */
enum { ATTRIBUTES_CELL = RAM_SCENE_SPRITE_LIST_ATTRIBUTES & 0xFFFF };

/* The routine's code, on the scene's page; a list clear of the sprite tables
 * and of the cells the original works in, which it reads back as it goes. */
bool scene_signed_sprite_list_code_present(Snes *s) { return m_code_matches(s, 0x7E3A92, 0xB6, 0x25F432D70AF1A949ull); }

bool scene_signed_sprite_list_fits(Snes *s)
{
    if (!scene_signed_sprite_list_code_present(s) || !list_fits(s, true))
        return false;
    uint16_t list = s->cpu.a;
    unsigned sprites = (uint8_t)(snes_bus_peek(&s->bus, list_address(list)) - 1);
    uint32_t end = (uint32_t)list + 1 + sprites * ENTRY_BYTES;
    return !(list <= ATTRIBUTES_CELL && end > ATTRIBUTES_CELL);
}

/* The routine's code, but for the two words of it a caller writes: the test
 * that leaves a sprite out, and two bytes no path runs. */
static bool large_list_code_present(Snes *s)
{
    return m_code_matches(s, 0x7EA03D, 0x37, 0x3766BB960571A2ECull) && m_code_matches(s, 0x7EA076, 0x1A, 0xF794154F7C7B7E59ull) &&
           m_code_matches(s, 0x7EA092, 0x07, 0x35C70EE850957A8Dull);
}

/* Once the original has left a sprite out it goes on with its accumulator a
 * word wide: its next sprite's bytes are stored as words, spilling into the
 * next entry, and the count is taken down as a word with the byte after it.
 * The game's only list drawn in that form is another ending's picture, which
 * no showing reaches; a list that leaves out nothing, or only its last
 * sprite with the count's next byte clear, comes to the same as the port, and
 * any other is the original's. */
static bool leaves_out_no_more_than_its_last(const GameState *g, uint16_t list)
{
    uint8_t sprites = list_sprites(g, list);
    for (unsigned n = 0; n < sprites; n++)
        if (large_list_y(g, listed_byte(g, list, n, LISTED_Y)) >= BELOW_THE_PICTURE)
            return n + 1u == sprites && scene_sprite_list_left_high(g) == 0;
    return true;
}

bool scene_large_sprite_list_fits(Snes *s)
{
    if (!large_list_code_present(s) || !list_fits(s, false))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t code = scene_large_list_clip(g);
    bool fits = is_a_large_list_form(code) && (large_list_form(code) == LIST_KEEPS_EVERY_SPRITE ||
                                                leaves_out_no_more_than_its_last(g, s->cpu.a));
    vars_peek(false);
    return fits;
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
