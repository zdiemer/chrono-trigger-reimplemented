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
 * edge or below the picture is put out of sight. */
#include "scene/scene.h"
#include "charge.h"

#include "cpu_io.h"
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
 * place in the table. The checks below keep it inside. */
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

/* A listed sprite's tile changed in place: how a scene animates a piece of a
 * picture it draws from a list. */
void scene_set_listed_tile(GameState *g, uint16_t list, unsigned n, uint8_t tile)
{
    wram_write8(g, list_address(list) + 1 + n * ENTRY_BYTES + LISTED_TILE, tile);
}

static ListedSprite listed_sprite(const GameState *g, uint16_t list, unsigned n)
{
    return (ListedSprite){ listed_byte(g, list, n, LISTED_TILE), listed_byte(g, list, n, LISTED_ATTRIBUTES),
                           listed_byte(g, list, n, LISTED_X), listed_byte(g, list, n, LISTED_Y) };
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
    charge_io_accesses(g, 3);
    charge_accesses(g, 1);
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

/* What a sprite written after one was left out leaves besides itself: its
 * attributes again in the next entry's X, and the high byte of its X, worked
 * out as a word, in its own Y (which a sprite placed there writes over). The
 * original goes on from a sprite it left out working in words, until it
 * places one: its reads of the list then take the stream's byte twice (the
 * same byte, as the stream reads back), and its stores and its add on X are a
 * word each. */
static void leave_word_wide_strays(GameState *g, uint16_t entry, uint8_t attributes, uint8_t list_x)
{
    uint16_t x = (uint16_t)((list_x | list_x << 8) + scene_sprite_offset_x(g));
    set_entry(g, (uint16_t)(entry + ENTRY_BYTES), ENTRY_X, attributes);
    set_entry(g, entry, ENTRY_Y, (uint8_t)(x >> 8));
}

// $7E:A03D
/* Copy the list at `list` into the sprite table as large sprites, each moved
 * by the scene's offset, X's 9th bit clear. The cursor moves on past them.
 *
 * The routine is the program's own, and its callers choose between its two
 * forms by writing its code (scene_large_list_clip): as the program is loaded
 * it leaves out a sprite whose Y comes to the picture's bottom or below
 * (mode 10's picture, $7E:9C58, draws with it); the fireworks write that test
 * out while they draw their bursts, so that every sprite of a burst shows,
 * however low. A sprite left out has had its tile, its attributes and its X
 * written to the free entry already, which the next sprite takes, and the
 * sprites after it until one is placed leave strays (leave_word_wide_strays). */
void scene_draw_large_sprite_list(GameState *g, uint16_t list, SceneLargeList form)
{
    uint8_t sprites = list_sprites(g, list);
    SpriteCursor c = cursor(g);
    bool after_one_left_out = false;
    for (unsigned n = 0; n < sprites; n++) {
        /* a byte at a time, as the original reads them: the list can lie over
         * the tables it fills */
        uint8_t attributes;
        set_entry(g, c.entry, ENTRY_TILE, listed_byte(g, list, n, LISTED_TILE));
        set_entry(g, c.entry, ENTRY_ATTRIBUTES, attributes = listed_byte(g, list, n, LISTED_ATTRIBUTES));
        uint8_t list_x = listed_byte(g, list, n, LISTED_X);
        set_entry(g, c.entry, ENTRY_X, moved_x(g, list_x));
        if (after_one_left_out)
            leave_word_wide_strays(g, c.entry, attributes, list_x);
        uint16_t y = large_list_y(g, listed_byte(g, list, n, LISTED_Y));
        if (form == LIST_LEAVES_OUT_BELOW_THE_PICTURE && y >= BELOW_THE_PICTURE) {
            after_one_left_out = true;
            continue;
        }
        after_one_left_out = false;
        set_entry(g, c.entry, ENTRY_Y, (uint8_t)y);
        shift_in(g, c.high, HIGH_LARGE);
        next_entry(&c);
    }
    set_cursor(g, c);
    charge_io_accesses(g, 3);
    charge_accesses(g, 1);
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
    charge_io_accesses(g, 3);
    charge_accesses(g, 1 + (sprites ? 2 : 0));
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

/* One small sprite from its four bytes in the program, or one the port has as
 * values, each moved by the scene's offset. */
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

/* ---- What the sprites need ------------------------------------------------------------------ */

/* The port writes through the named sprite tables, so it takes only what the
 * game does: a cursor in them, which the sprites it places stay inside. The
 * original writes wherever a cursor points, so a cursor anywhere else would
 * write over the scene's own cells or the console's ports; the game never
 * makes one, and a scene that does goes no further. A list stays clear of
 * the cell the original counts its entries down in, which the port counts in a
 * local; and a clipped list clear of the tables and of all the scene's sprite
 * cells: the original stores its X as a word whose high byte lands in the Y a
 * moment before the Y does, which a list read from the table could see. */
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

/* Whether `sprites` more fit in the tables from the cursor, for the scenes
 * that check before they draw. */
bool scene_sprites_fit(const GameState *g, unsigned sprites)
{
    return fits_in_table(scene_sprite_index(g), scene_sprite_high_pointer(g), sprites);
}

/* Where the original counts a list's entries down, the first of the scene's
 * sprite cells before it, and the end of the high table. */
enum {
    COUNT_CELL = RAM_SCENE_SPRITE_LIST_LEFT & 0xFFFF,
    SPRITE_TABLES_END = SCENE_SPRITE_HIGH_TABLE + SCENE_SPRITE_HIGH_TABLE_BYTES,
};

/* The two forms of the program's list of large sprites, as its code holds
 * them where the callers write it: the test that leaves a sprite out (a
 * branch back to the next sprite where the Y is at the picture's bottom or
 * below, as the program is loaded), or that test written out with two
 * no-operations (the fireworks'). Any other code there is no form the program
 * writes, and a scene that finds one goes no further. */
enum { LEAVES_OUT_BELOW_THE_PICTURE_CODE = 0xD8B0, KEEPS_EVERY_SPRITE_CODE = 0xEAEA };

static bool is_a_large_list_form(uint16_t code)
{
    return code == KEEPS_EVERY_SPRITE_CODE || code == LEAVES_OUT_BELOW_THE_PICTURE_CODE;
}

static SceneLargeList large_list_form(uint16_t code)
{
    return code == KEEPS_EVERY_SPRITE_CODE ? LIST_KEEPS_EVERY_SPRITE : LIST_LEAVES_OUT_BELOW_THE_PICTURE;
}

SceneLargeList scene_large_list_form_written(const GameState *g) { return large_list_form(scene_large_list_clip(g)); }

/* Once the original has left a sprite out it goes on with its accumulator a
 * word wide (leave_word_wide_strays), and takes its count down as a word with
 * the byte after it: the port takes that byte clear, where the count ends
 * with the list. The game's only list drawn in that form is mode 10's
 * picture, which no showing reaches. The strays land in the entry after the
 * sprite's, which must be the table's, and the list must lie clear of the
 * tables they land in, as the port reads it. */
static bool leaves_out_as_ported(const GameState *g, uint16_t list)
{
    uint8_t sprites = list_sprites(g, list);
    unsigned left_out_before_the_last = 0;
    bool any = false;
    for (unsigned n = 0; n < sprites; n++)
        if (large_list_y(g, listed_byte(g, list, n, LISTED_Y)) >= BELOW_THE_PICTURE) {
            any = true;
            left_out_before_the_last += n + 1u < sprites;
        }
    if (!any)
        return true;
    if (scene_sprite_list_left_high(g) != 0)
        return false;
    if (!left_out_before_the_last)
        return true;
    uint32_t end = (uint32_t)list + 1 + sprites * ENTRY_BYTES;
    bool clear_of_the_tables = end <= SCENE_SPRITE_TABLE || list >= SPRITE_TABLES_END;
    return clear_of_the_tables && scene_sprites_fit(g, sprites + 1u);
}

static bool large_list_form_takes(const GameState *g, uint16_t list)
{
    uint16_t code = scene_large_list_clip(g);
    return is_a_large_list_form(code) &&
           (large_list_form(code) == LIST_KEEPS_EVERY_SPRITE || leaves_out_as_ported(g, list));
}

/* The same, for a scene that draws a list in the form the code holds:
 * the list clear of the count the original works in, room for its sprites
 * from the cursor, and a form the port takes. */
bool scene_large_sprite_list_takes(const GameState *g, uint16_t list)
{
    unsigned sprites = list_sprites(g, list);
    uint32_t end = (uint32_t)list + 1 + sprites * ENTRY_BYTES;
    return !(list <= COUNT_CELL && end > COUNT_CELL) && scene_sprites_fit(g, sprites) && large_list_form_takes(g, list);
}
