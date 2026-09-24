/* The menus' text: strings and numbers drawn as tiles into the tilemaps the
 * menus build in work RAM (bank $7E), which the VRAM queue then sends.
 *
 * A string is a run of bytes ending in 0. A byte of $10 or more is a tile,
 * written with the current attribute (its palette, priority and flips); the
 * bytes below are control codes, which take operands from the bytes after
 * them: a new line, a move to a place in the window, a number, another
 * string or a character's name drawn in the middle of this one, a run of
 * blanks, a tile copied from elsewhere, the attribute or its palette, the
 * text's flags. A string draws at most a set number of tiles.
 *
 * The menus' text is two tile rows a line -- the row above a line held the
 * Japanese text's marks, and gets a blank wherever the line has one -- unless
 * the flags say one row. Numbers are drawn in decimal, right-aligned in a set
 * number of digits with blanks for the leading zeros (or zeros, where the
 * format says), or in hex; a limited number is drawn only below its limit,
 * and as dashes from it on.
 *
 * The text's state is the menus' own cells: the attribute, the flags, the
 * tiles left, where the line starts, and the number being drawn, which the
 * menus' other code reads. Where the pen is and where the string is being read
 * are the drawer's. */
#include "menu/text.h"

#include "charge.h"
#include "menu/menu_data.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { END_OF_STRING = 0x00, FIRST_TILE = 0x10 };
enum { BLANK = 0xFF, DASH = 0x2F, FIRST_DIGIT = 0xD4 };

/* The tilemaps are in bank $7E, 32 tiles to a row, a tile a word: its number,
 * then its attribute. */
enum { TILEMAP_BANK = 0x7E, TILE_BYTES = 2, ROW_BYTES = 0x40 };

/* The text's flags (menu_text_flags). */
enum { ONE_ROW = 0x80 };

/* The attribute's palette bits, and what of the attribute a string leaves for
 * the next: the tile's high bits and its priority, not its flips or its
 * palette. */
enum { PALETTE_BITS = 0x1C, KEPT_AFTER_A_STRING = 0x23 };

/* A name is drawn in five tiles on one row, padded with blanks. */
enum { NAME_TILES = 5, NAME_FLAGS = ONE_ROW };

/* The control codes. */
enum {
    CODE_NEW_LINE = 0x01,
    CODE_MOVE = 0x02,
    CODE_NUMBER = 0x03,
    CODE_STRING = 0x04,
    CODE_NAME = 0x05,
    CODE_PALETTE_FROM = 0x06,
    CODE_COPY_TILE = 0x07,
    CODE_BLANKS = 0x08,
    CODE_FLAGS = 0x09,
    CODE_ATTRIBUTE = 0x0A,
    CODE_HEX_NUMBER = 0x0B,
    CODE_LIMITED_NUMBER = 0x0C,
};

/* ---- Where the text is ------------------------------------------------------------------- */

/* A place in a string: its bank and where in it. Reading on past the bank's
 * end carries into the next, while the place itself wraps in its bank, as the
 * original's index register does. */
typedef struct {
    uint8_t bank;
    uint16_t at;
} TextPlace;

static uint32_t address_of(TextPlace p, uint16_t k) { return ((uint32_t)p.bank << 16 | p.at) + k; }
static uint8_t text_byte(const GameState *g, TextPlace p, uint16_t k) { return menu_text_byte(g, address_of(p, k)); }
static uint16_t text_word(const GameState *g, TextPlace p, uint16_t k)
{
    return (uint16_t)(text_byte(g, p, k) | text_byte(g, p, (uint16_t)(k + 1)) << 8);
}
static TextPlace advanced(TextPlace p, uint16_t by) { return (TextPlace){ p.bank, (uint16_t)(p.at + by) }; }

/* A place in a tilemap: the tiles are in bank $7E, and the row above a tile
 * at the tilemap's start is before the bank, where the original's store goes
 * into the ROM and does nothing. */
static uint32_t in_tilemap(uint16_t pen, int16_t offset) { return (((uint32_t)TILEMAP_BANK << 16) + pen + offset) & 0xFFFFFFu; }

/* ---- Tiles ----------------------------------------------------------------------------------- */

static bool one_row(const GameState *g) { return (menu_text_flags(g) & ONE_ROW) != 0; }

static void put_tile_word(GameState *g, uint32_t at, uint8_t tile)
{
    wram_write8(g, at, tile);
    wram_write8(g, at + 1, menu_text_attribute(g));
}

/* Draw a tile at the pen, and a blank above a blank on two-row text. Returns
 * where the pen goes on. */
static uint16_t draw_tile(GameState *g, uint16_t pen, uint8_t tile)
{
    if (tile == BLANK && !one_row(g))
        put_tile_word(g, in_tilemap(pen, -ROW_BYTES), BLANK);
    put_tile_word(g, in_tilemap(pen, 0), tile);
    return (uint16_t)(pen + TILE_BYTES);
}

static uint16_t draw_blanks(GameState *g, uint16_t pen, uint16_t count)
{
    uint16_t left = count;   /* 0 is 65536: the original counts down before it tests */
    do
        pen = draw_tile(g, pen, BLANK);
    while (--left != 0);
    return pen;
}

/* ---- Numbers ------------------------------------------------------------------------------- */

/* How a number is drawn, from its format byte: how many bytes it is (1-3), in
 * how many digits (1-8), whether its leading zeros show as blanks, and whether
 * even its units digit does. */
typedef struct {
    uint8_t bytes;
    uint8_t digits;
    bool blank_leading_zeros;
    bool blank_a_zero_units;
} NumberFormat;

static NumberFormat number_format(uint8_t format)
{
    return (NumberFormat){ (uint8_t)(format & 0x03), (uint8_t)(((format >> 4) & 0x07) + 1),
                           (format & 0x80) == 0, (format & 0x08) != 0 };
}

/* The number of `bytes` bytes at `at`, low byte first. */
static uint32_t number_at(const GameState *g, uint32_t at, uint8_t bytes)
{
    uint32_t value = 0;
    for (uint8_t i = 0; i < bytes; i++)
        value |= (uint32_t)menu_text_byte(g, at + i) << (8 * i);
    return value;
}

/* A number as eight decimal digits, four bits each, the units lowest. */
static uint32_t decimal_digits(uint32_t value)
{
    uint32_t digits = 0;
    for (unsigned place = 0; place < 8; place++) {
        digits |= (value % 10) << (4 * place);
        value /= 10;
    }
    return digits;
}

/* Draw the last `digits` of `number` (four bits a digit) at the pen, most
 * significant first; a zero before the first other digit is a blank while
 * leading zeros are blanked, and so is the units digit where the format says.
 * Returns where the pen goes on. */
static uint16_t draw_digits(GameState *g, uint16_t pen, uint32_t number, NumberFormat f)
{
    bool blanking = f.blank_leading_zeros;
    for (int place = f.digits - 1; place >= 0; place--) {
        if (place == 0 && !f.blank_a_zero_units)
            blanking = false;
        uint8_t digit = (uint8_t)(number >> (4 * place) & 0x0F);
        bool blank = digit == 0 && blanking;
        if (!blank)
            blanking = false;
        put_tile_word(g, in_tilemap(pen, 0), blank ? BLANK : (uint8_t)(FIRST_DIGIT + digit));
        pen = (uint16_t)(pen + TILE_BYTES);
    }
    return pen;
}

/* What the number drawer leaves in its cells: the digits less one, the
 * blanking flags (the format's, flipped, until a digit stops them: they last
 * only where every digit was a blank), where it drew and the bytes it had
 * left to convert. */
static void leave_number_drawn(GameState *g, uint8_t format, NumberFormat f, uint32_t number, uint16_t pen)
{
    uint32_t shown = f.digits == 8 ? number : number & ((1u << (4 * f.digits)) - 1);
    bool still_blanking = f.blank_leading_zeros && f.blank_a_zero_units && shown == 0;
    set_menu_number_digits(g, (uint8_t)(f.digits - 1));
    set_menu_number_blanking(g, still_blanking ? (uint8_t)(format ^ 0x80) : 0);
    set_menu_number_dest(g, pen);
    set_menu_number_size(g, 0);
}

/* The conversion's own leftovers: the byte it shifted out (a word, over the
 * digits and blanking cells), and the bytes left. */
static void leave_converted(GameState *g)
{
    set_menu_number_digits(g, 0);
    set_menu_number_blanking(g, 0);
    set_menu_number_size(g, 0);
}

/* The original converts bit by bit, adding each set bit's decimal value: its
 * time is a pass over each byte's eight bits and an add for each bit set. */
enum { NUMBER_TO_BCD = 0xC2F13F, ACCESSES_PER_BYTE = 56, ACCESSES_PER_SET_BIT = 10 };

static void charge_conversion(GameState *g, uint32_t value, uint8_t bytes)
{
    unsigned set = 0;
    for (uint32_t v = value; v != 0; v >>= 1)
        set += v & 1;
    charge_as(g, NUMBER_TO_BCD, bytes * ACCESSES_PER_BYTE + set * ACCESSES_PER_SET_BIT);
}

/* The number of `bytes` bytes at `at` as decimal digits. */
static uint32_t converted(GameState *g, uint32_t at, uint8_t bytes)
{
    uint32_t value = number_at(g, at, bytes);
    ORIGINAL_EFFECT(charge_conversion(g, value, bytes));
    return decimal_digits(value);
}

// $C2:F13F
/* Convert the number of `bytes` bytes at `at` to decimal digits, for the menus
 * to show. */
static void convert_number(GameState *g, uint32_t at, uint8_t bytes)
{
    set_menu_text_work_number(g, converted(g, at, bytes));
    ORIGINAL_EFFECT(leave_converted(g));
}

// $C2:F114
/* Draw the number at `at` as its format says, at the pen. Returns where the
 * pen goes on. */
static uint16_t draw_number(GameState *g, uint32_t at, uint8_t format, uint16_t pen)
{
    NumberFormat f = number_format(format);
    uint32_t digits = converted(g, at, f.bytes);
    set_menu_text_work_number(g, digits);
    uint16_t after = draw_digits(g, pen, digits, f);
    ORIGINAL_EFFECT(leave_number_drawn(g, format, f, digits, pen));
    return after;
}

/* Where the number's four bytes are on the menus' page, as the original
 * reads a hex number's digits. */
enum { NUMBER_CELLS = 0x7E008A };

/* The hex number's digit `place` (from the units): four bytes' worth are the
 * number's; past them the original reads on into the drawer's own cells --
 * the count, the bank, where the digits go -- and on up the page, which no
 * string of the game's asks for, and the port reads them too (a stray read,
 * explicitly past menu_text_work's four bytes). */
static uint8_t hex_byte(const GameState *g, uint32_t bytes, unsigned place)
{
    unsigned i = place / 2;
    if (i < 4)
        return (uint8_t)(bytes >> (8 * i));
    return wram_read8(g, NUMBER_CELLS + i);
}

// $C2:F227
/* Draw the four bytes at `at` as a hex number of `count` + 1 digits. */
static uint16_t draw_hex_number(GameState *g, uint32_t at, uint16_t format, uint16_t pen)
{
    uint32_t bytes = number_at(g, at, 4);
    set_menu_text_work_number(g, bytes);
    uint8_t count = (uint8_t)format;
    /* written first: the digits past the number's bytes are read from here */
    set_menu_number_digits(g, count);
    set_menu_number_blanking(g, (uint8_t)(format >> 8));
    set_menu_number_dest(g, pen);
    for (int place = count; place >= 0; place--) {
        uint8_t b = hex_byte(g, bytes, (unsigned)place);
        uint8_t digit = (place & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
        put_tile_word(g, in_tilemap(pen, 0), menu_hex_digit_tile(g, digit));
        pen = (uint16_t)(pen + TILE_BYTES);
    }
    return pen;
}

// $C2:F0D1
/* A value shown only while it is under its limit (a stat's cap), and as
 * dashes from it on: one byte in work RAM, two digits with a blank for a
 * leading zero. */
static uint16_t draw_limited_number(GameState *g, uint8_t which, uint16_t value_at, uint16_t pen)
{
    enum { TWO_DIGITS_OF_A_BYTE = 0x11 };
    uint32_t at = (uint32_t)TILEMAP_BANK << 16 | value_at;
    if (menu_text_byte(g, at) < menu_number_limit(g, which))
        return draw_number(g, at, TWO_DIGITS_OF_A_BYTE, pen);
    wram_write8(g, in_tilemap(pen, 0), DASH);
    wram_write8(g, in_tilemap(pen, 2), DASH);
    uint8_t attribute = menu_text_attribute(g);
    wram_write8(g, in_tilemap(pen, 1), attribute);
    wram_write8(g, in_tilemap(pen, 3), attribute);
    return (uint16_t)(pen + 2 * TILE_BYTES);
}

/* ---- Strings ------------------------------------------------------------------------------- */

/* The tiles a string may still draw: when a drawn tile uses the last, the
 * string ends there. A tile a string copies counts too, but doesn't end it. */
static bool count_a_tile(GameState *g)
{
    uint8_t left = (uint8_t)(menu_text_tile_limit(g) - 1);
    set_menu_text_tile_limit(g, left);
    return left != 0;
}

/* A new line: back to where the line started, two rows down, or one on
 * one-row text. */
static uint16_t new_line(GameState *g)
{
    uint16_t start = (uint16_t)(menu_text_line_start(g) + (one_row(g) ? ROW_BYTES : 2 * ROW_BYTES));
    set_menu_text_line_start(g, start);
    return start;
}

/* A move: a new line starting at `offset` from the window's origin. */
static uint16_t move_to(GameState *g, uint16_t offset)
{
    uint16_t start = (uint16_t)(offset + menu_draw_origin(g));
    set_menu_text_line_start(g, start);
    return start;
}

/* The attribute's palette, taken from another tile's attribute in work RAM. */
static void palette_from(GameState *g, uint16_t attribute_at)
{
    set_menu_text_attribute(g, (uint8_t)(menu_text_attribute(g) & ~PALETTE_BITS));
    uint8_t other = wram_read8(g, (uint32_t)TILEMAP_BANK << 16 | attribute_at);
    set_menu_text_attribute(g, (uint8_t)(menu_text_attribute(g) | (other & PALETTE_BITS)));
}

/* A tile word copied from elsewhere in work RAM. */
static uint16_t copy_tile(GameState *g, uint16_t from, uint16_t pen)
{
    uint8_t tile = wram_read8(g, in_tilemap(from, 0));
    uint8_t attribute = wram_read8(g, in_tilemap(from, 1));
    wram_write8(g, in_tilemap(pen, 0), tile);
    wram_write8(g, in_tilemap(pen, 1), attribute);
    (void)count_a_tile(g);
    return (uint16_t)(pen + TILE_BYTES);
}

/* A string drawn inside another: another string, or a character's name, which
 * is drawn in five tiles on one row whatever its length and then puts the
 * tile limit and flags back as they were. The original draws them by calling
 * itself; the port keeps what it needs to go back to the string that
 * included them, as deep as the game's strings go. */
typedef struct {
    TextPlace resume;
    bool name;
    uint16_t pen_after;
    uint8_t limit, flags;
} Included;
enum { INCLUDED_DEPTH = 4 };

typedef struct {
    Included stack[INCLUDED_DEPTH];
    unsigned depth;
} Inclusions;

/* The number of bytes of operands after each control code. */
static uint8_t operand_bytes(uint8_t code)
{
    switch (code) {
    case CODE_MOVE: case CODE_PALETTE_FROM: case CODE_COPY_TILE: case CODE_NAME: return 2;
    case CODE_NUMBER: case CODE_HEX_NUMBER: return 4;
    case CODE_STRING: case CODE_LIMITED_NUMBER: return 3;
    case CODE_BLANKS: case CODE_FLAGS: case CODE_ATTRIBUTE: return 1;
    default: return 0;
    }
}

/* Carry out the control code whose operands start at `p`: the pen may move,
 * and a string or a name included starts being read (`*p` is where the text
 * goes on). */
static uint16_t control_code(GameState *g, uint8_t code, TextPlace *p, uint16_t pen, Inclusions *in)
{
    TextPlace o = *p;
    *p = advanced(o, operand_bytes(code));
    switch (code) {
    case CODE_NEW_LINE: return new_line(g);
    case CODE_MOVE: return move_to(g, text_word(g, o, 0));
    case CODE_NUMBER: {
        uint16_t format = text_word(g, o, 2);
        return draw_number(g, (uint32_t)(format >> 8) << 16 | text_word(g, o, 0), (uint8_t)format, pen);
    }
    case CODE_HEX_NUMBER: {
        uint16_t format = text_word(g, o, 2);
        return draw_hex_number(g, (uint32_t)(format >> 8) << 16 | text_word(g, o, 0), format, pen);
    }
    case CODE_LIMITED_NUMBER: return draw_limited_number(g, text_byte(g, o, 0), text_word(g, o, 1), pen);
    case CODE_PALETTE_FROM: palette_from(g, text_word(g, o, 0)); return pen;
    case CODE_COPY_TILE: return copy_tile(g, text_word(g, o, 0), pen);
    case CODE_BLANKS: return draw_blanks(g, pen, text_byte(g, o, 0));
    case CODE_FLAGS: set_menu_text_flags(g, text_byte(g, o, 0)); return pen;
    case CODE_ATTRIBUTE: set_menu_text_attribute(g, text_byte(g, o, 0)); return pen;
    case CODE_STRING:
    case CODE_NAME:
        if (in->depth < INCLUDED_DEPTH) {
            Included *i = &in->stack[in->depth++];
            *i = (Included){ *p, code == CODE_NAME, (uint16_t)(pen + NAME_TILES * TILE_BYTES),
                             menu_text_tile_limit(g), menu_text_flags(g) };
            if (i->name) {
                set_menu_text_tile_limit(g, NAME_TILES);
                set_menu_text_flags(g, NAME_FLAGS);
                *p = (TextPlace){ TILEMAP_BANK, text_word(g, o, 0) };
            } else {
                *p = (TextPlace){ text_byte(g, o, 2), text_word(g, o, 0) };
            }
        }
        return pen;
    default: return pen;
    }
}

/* Back to the string that included the one just ended: a name is padded to
 * its five tiles with blanks, and the pen goes on past them. */
static uint16_t back_to_including(GameState *g, Included *i, uint16_t pen, TextPlace *p)
{
    if (i->name) {
        for (uint8_t left = menu_text_tile_limit(g); left != 0; left = menu_text_tile_limit(g)) {
            pen = draw_tile(g, pen, BLANK);
            set_menu_text_tile_limit(g, (uint8_t)(left - 1));
        }
        pen = i->pen_after;
        set_menu_text_tile_limit(g, i->limit);
        set_menu_text_flags(g, i->flags);
    }
    *p = i->resume;
    return pen;
}

// $C2:EF7E
/* Draw the string at `p` at the pen, until its end or until it draws its last
 * tile. Returns where the string stopped; the pen is left where it got to. */
static TextPlace draw_string(GameState *g, TextPlace p, uint16_t *pen)
{
    Inclusions in = { .depth = 0 };
    for (;;) {
        uint8_t byte = text_byte(g, p, 0);
        bool ended = byte == END_OF_STRING;
        if (!ended) {
            p = advanced(p, 1);
            if (byte < FIRST_TILE) {
                ORIGINAL_EFFECT(set_menu_text_work_code_handler(g, menu_text_code_handler(g, byte)));
                *pen = control_code(g, byte, &p, *pen, &in);
                continue;
            }
            *pen = draw_tile(g, *pen, byte);
            ended = !count_a_tile(g);
        }
        if (ended) {
            if (in.depth == 0)
                return p;
            *pen = back_to_including(g, &in.stack[--in.depth], *pen, &p);
        }
    }
}

// $C2:EF65
/* Draw a string at the pen: `settings` is the text's flags, and in its low
 * seven bits the most tiles it may draw (0 for 256: the count is a byte that
 * counts down before it is tested).
 *
 * Two things no string of the game's does, where the port and the original
 * part: strings include strings and names as deep as INCLUDED_DEPTH, and one
 * deeper the original would draw and the port leaves out; and a number of no
 * bytes (a format whose low bits are 0) the original converts from 65536
 * bytes and its table far past its end, and the port as 0. */
static TextPlace draw_text(GameState *g, TextPlace p, uint16_t *pen, uint8_t settings)
{
    set_menu_text_flags(g, settings);
    set_menu_text_tile_limit(g, (uint8_t)(settings & 0x7F));
    set_menu_text_line_start(g, *pen);
    TextPlace stopped = draw_string(g, p, pen);
    set_menu_text_attribute(g, (uint8_t)(menu_text_attribute(g) & KEPT_AFTER_A_STRING));
    return stopped;
}

/* ---- The entries ------------------------------------------------------------------------- */

/* All four run on the menus' page, and the conversion starts from the first
 * power of two: the word it starts its table from ($51) is 0 whenever the
 * menus draw a number. */
static bool on_the_menus_page(Snes *s) { return m_direct_page(s) == 0x0000; }
static bool converts_from_one(Snes *s)
{
    return snes_bus_peek(&s->bus, 0x000051) == 0 && snes_bus_peek(&s->bus, 0x000052) == 0;
}

/* What the menus read strings and numbers from is work RAM or the ROM: no
 * string or number of the game's is in the I/O registers or open bus, where
 * a read can change what it reads. */
static bool reads_memory(uint32_t at)
{
    uint8_t bank = (uint8_t)(at >> 16);
    uint16_t address = (uint16_t)at;
    if (bank == 0x7E || bank == 0x7F || (bank & 0x7F) >= 0x40)
        return true;
    return address < 0x2000 || address >= 0x8000;
}

/* The number drawer keeps its digits and its place in cells of the menus'
 * page ($8A-$93); a number the game draws is neither read from them nor drawn
 * over them. */
static bool clear_of_the_drawers_cells(uint32_t number_at, uint16_t pen)
{
    enum { CELLS = 0x8A, CELLS_END = 0x94, NUMBER_BYTES = 3, BELOW_THE_PEN = 2 * ROW_BYTES };
    uint8_t bank = (uint8_t)(number_at >> 16);
    uint16_t address = (uint16_t)number_at;
    bool low_ram = bank == 0x7E || ((bank & 0x7F) < 0x40 && address < 0x2000);
    bool reads_them = low_ram && address < CELLS_END && address + NUMBER_BYTES > CELLS;
    return !reads_them && pen >= CELLS_END + BELOW_THE_PEN;
}

/* A number the game draws isn't on the stack, where the conversion pushes
 * its place in the number as it reads each byte: the reach of its pushes and
 * the drawer's call below the stack pointer. */
static bool clear_of_the_stack(uint32_t number_at, uint16_t sp)
{
    enum { NUMBER_BYTES = 3, PUSHES = 8 };
    uint8_t bank = (uint8_t)(number_at >> 16);
    uint16_t address = (uint16_t)number_at;
    bool low_ram = bank == 0x7E || ((bank & 0x7F) < 0x40 && address < 0x2000);
    return !low_ram || address > sp || address + NUMBER_BYTES + PUSHES <= sp;
}

bool menu_tile_on_page(Snes *s) { return on_the_menus_page(s); }

bool menu_text_on_page(Snes *s)
{
    return on_the_menus_page(s) && converts_from_one(s) && reads_memory((uint32_t)(s->cpu.a >> 8) << 16 | s->cpu.y);
}

/* A number the game draws is one to three bytes. */
bool menu_number_is_drawable(Snes *s)
{
    uint8_t bytes = number_format((uint8_t)s->cpu.a).bytes;
    uint32_t at = (uint32_t)(s->cpu.a >> 8) << 16 | s->cpu.y;
    return on_the_menus_page(s) && converts_from_one(s) && bytes != 0 && reads_memory(at)
           && clear_of_the_drawers_cells(at, s->cpu.x) && clear_of_the_stack(at, s->cpu.s);
}

bool menu_number_is_convertible(Snes *s)
{
    vars_peek(true);
    uint16_t bytes = menu_number_size(state_of(s));
    vars_peek(false);
    uint32_t at = (uint32_t)s->cpu.db << 16 | s->cpu.y;
    return on_the_menus_page(s) && converts_from_one(s) && bytes >= 1 && bytes <= 3 && reads_memory(at)
           && clear_of_the_drawers_cells(at, 0xFFFF) && clear_of_the_stack(at, s->cpu.s);
}

/* The number drawer leaves A's high byte clear (its digits are drawn with it
 * so), which its callers can't be shown not to read. */
static void clear_a_high(Snes *s) { s->cpu.a &= 0x00FF; }

// $C2:EF65
void menu_draw_text(Snes *s)
{
    uint16_t pen = s->cpu.x;
    TextPlace stopped = draw_text(state_of(s), (TextPlace){ (uint8_t)(s->cpu.a >> 8), s->cpu.y }, &pen,
                                  (uint8_t)s->cpu.a);
    s->cpu.x = pen;
    s->cpu.y = stopped.at;
    m_rts(s);
}

// $C2:EF97
void menu_draw_tile(Snes *s)
{
    s->cpu.x = draw_tile(state_of(s), s->cpu.x, (uint8_t)s->cpu.a);
    m_rts(s);
}

// $C2:F114
void menu_draw_number(Snes *s)
{
    uint8_t bytes = number_format((uint8_t)s->cpu.a).bytes;
    s->cpu.x = draw_number(state_of(s), (uint32_t)(s->cpu.a >> 8) << 16 | s->cpu.y, (uint8_t)s->cpu.a, s->cpu.x);
    ORIGINAL_EFFECT(clear_a_high(s));
    s->cpu.y = (uint16_t)(s->cpu.y + bytes);
    m_rts(s);
}

// $C2:F13F
void menu_number_to_bcd(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    uint8_t bytes = (uint8_t)menu_number_size(g);
    vars_peek(false);
    convert_number(g, m_abs(s, s->cpu.y), bytes);
    s->cpu.y = (uint16_t)(s->cpu.y + bytes);
    m_rts(s);
}
