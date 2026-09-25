/* The typesetter: a string of the game's text set in the proportional font,
 * a few glyphs at a time, into tiles in work RAM.
 *
 * The dialog, the battle's messages and the menus' labels all set their text
 * with it. A caller names the string (its number in a table of strings), the
 * tiles the line goes in and how they are laid out (menu_text), puts the
 * string's numbers and names in the text page's arguments, and starts it
 * ($C2:57DF). Then each call ($C2:5823) sets glyphs until the string asks
 * the caller for something or the call's budget of glyphs runs out, and says
 * why it stopped (menu_text_stop): the string ended, a wait of some frames, a
 * line or page break the caller acts on, or the budget. The dialog calls it
 * every frame with a budget of a glyph or two, which is how its text types
 * itself out; a label is set in one go.
 *
 * A string is bytes: $A0 up a glyph of the font, $01/$02 and a byte a glyph
 * of its two-byte half, $21-$9F a word of the dictionary (the commonest
 * pieces of English, stored once), and below $21 a control code: the end, a
 * wait, the breaks, a number from the arguments, a name (a character, the
 * party's members, an item, a tech, an enemy). What a word, a name or a
 * number inserts is set glyph by glyph from where it is, and a call that runs
 * out of budget partway through one takes it up again next time: where the
 * setting has got to lives in the text page's cells (menu_text_reading),
 * between calls as within one.
 *
 * Numbers are shown in decimal without leading zeros; the one- and two-byte
 * ones in hex if the page's hex setting is on. The glyphs are drawn by
 * glyphs.c. */
#include "menu/typeset.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/glyphs.h"
#include "menu/menu_data.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "vars.h"

/* ---- Where the setting is ------------------------------------------------------------------ */

/* What the next glyph comes from. */
typedef enum { FROM_STRING = 0, FROM_INSERT = 1, FROM_DIGITS = 2, FROM_HEX_DIGITS = 3 } Source;

/* Why a call stopped (menu_text_stop); the breaks are 4-12, the ones from 5
 * on starting a new line. The start leaves "go on". */
enum { STOP_FINISHED = 0, STOP_WAITING = 3, GO_ON = 4, FIRST_NEW_LINE = 5, LAST_BREAK = 12, STOP_BUDGET = 0x10 };

/* How the text is laid out (menu_text_layout). */
enum { LINES_AT_THE_EDGE = 0x80, TILE_KIND_BITS = 0x0F, A_TILE_FURTHER_LEFT = 2 };

/* Where a line starts: a tile in from the edge, or indented. */
enum { MARGIN = 8, INDENT = 0x14, EDGE = 0 };

/* The string's arguments are on the text page from its first byte. */
enum { ARGUMENTS = 0x000200, DIGITS = 0x000240 };

static uint8_t layout(const GameState *g) { return menu_text_layout(g); }

static MenuTextTiles tiles_of_layout(uint8_t l)
{
    return (l & TILE_KIND_BITS) == 0 ? MENU_TILES_4_COLOUR : MENU_TILES_16_COLOUR;
}

/* A 24-bit pointer stepped on a byte within its bank, as the original's word
 * increments do. */
static uint32_t stepped(uint32_t at, uint16_t by) { return (at & 0xFF0000u) | (uint16_t)(at + by); }

static uint8_t next_string_byte(GameState *g)
{
    uint32_t at = menu_text_reading_at(g);
    set_menu_text_reading_at(g, stepped(at, 1));
    return menu_text_byte(g, at);
}

static uint8_t next_insert_byte(GameState *g)
{
    uint32_t at = menu_text_reading_insert(g);
    set_menu_text_reading_insert(g, stepped(at, 1));
    return menu_text_byte(g, at);
}

/* The string's next argument: a byte, a word or three bytes from the text
 * page's arguments, in order. */
static uint32_t next_argument(GameState *g, unsigned bytes)
{
    uint32_t at = menu_text_reading_arguments_at(g);
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; i++)
        value |= (uint32_t)menu_text_byte(g, stepped(at, (uint16_t)i)) << (8 * i);
    set_menu_text_reading_arguments_at(g, stepped(at, (uint16_t)bytes));
    return value;
}

/* ---- Starting a text ----------------------------------------------------------------------- */

// $C2:57DF
/* Start the string the text page names: its first byte from its table, the
 * pen at a line's start, the arguments from the first. A text in sixteen-
 * colour tiles laid out a tile further left, or with its lines at the edge,
 * starts at the edge. */
void menu_text_start(GameState *g)
{
    uint32_t table = menu_text_strings(g);
    uint16_t first = rom_read16(g, (table + 2u * menu_text_string(g)) & 0xFFFFFFu);
    set_menu_text_reading_at(g, (table & 0xFF0000u) | first);
    set_menu_text_reading_source(g, FROM_STRING);
    uint8_t l = layout(g);
    uint8_t pen = (l & LINES_AT_THE_EDGE) || l == A_TILE_FURTHER_LEFT ? EDGE : MARGIN;
    set_menu_text_reading_pen(g, pen);
    set_menu_text_stop(g, GO_ON);
    set_menu_text_reading_arguments_at(g, ARGUMENTS);
    set_menu_text_line_glyphs(g, 0);
}

/* ---- Setting glyphs ------------------------------------------------------------------------ */

// $C2:5DC4
/* Draw a glyph at the pen, and move the pen on by its width. */
enum { FIRST_GLYPH = 0xA0 };

static void set_glyph(GameState *g, uint16_t glyph)
{
    uint8_t pen = menu_text_reading_pen(g);
    set_menu_text_reading_glyph(g, glyph);
    TIMED_AS(g, tiles_of_layout(layout(g)) == MENU_TILES_4_COLOUR ? 0xC25E36 : 0xC25F07,
             menu_put_glyph(g, glyph, pen, menu_text_tiles(g), tiles_of_layout(layout(g))));
    set_menu_text_reading_pen(g, (uint8_t)(pen + menu_glyph_width(g, (uint8_t)(glyph - FIRST_GLYPH))));
    set_menu_text_line_glyphs(g, (uint8_t)(menu_text_line_glyphs(g) + 1));
}

/* One glyph of the call's budget spent: true while some is left. */
static bool spend_budget(GameState *g)
{
    uint8_t left = (uint8_t)(menu_text_budget(g) - 1);
    set_menu_text_budget(g, left);
    return left != 0;
}

static void out_of_budget(GameState *g) { set_menu_text_stop(g, STOP_BUDGET); }

/* How a call ended, which is what the original leaves in its index
 * registers: at a control code (the code), out of budget in the string or in
 * an insert (whether some of the insert was left), or not begun at all. */
typedef enum { ENDED_AT_CODE, ENDED_IN_STRING, ENDED_IN_INSERT, NOT_BEGUN } HowItEnded;
typedef struct {
    HowItEnded how;
    uint8_t code;
    bool insert_left;
} Ending;

/* ---- Inserts ------------------------------------------------------------------------------- */

/* A glyph of an insert: a byte, or $01/$02 and the byte after it. */
static uint16_t insert_glyph(GameState *g, Source from)
{
    uint8_t b = next_insert_byte(g);
    if (from == FROM_DIGITS)
        return (uint8_t)(b + MENU_FIRST_DIGIT_GLYPH);
    if (b == 1 || b == 2)
        return (uint16_t)(b << 8 | next_insert_byte(g));
    return b;
}

// $C2:5BF5, $C2:5C3E, $C2:5C77
/* Set the insert's glyphs until it or the budget runs out. True if the
 * string goes on after it this call. */
static bool set_insert(GameState *g, Ending *end)
{
    Source from = (Source)menu_text_reading_source(g);
    for (;;) {
        set_glyph(g, insert_glyph(g, from));
        uint8_t left = (uint8_t)(menu_text_reading_insert_left(g) - 1);
        set_menu_text_reading_insert_left(g, left);
        bool budget = spend_budget(g);
        if (left == 0)
            set_menu_text_reading_source(g, FROM_STRING);
        if (!budget) {
            out_of_budget(g);
            *end = (Ending){ ENDED_IN_INSERT, 0, left != 0 };
            return false;
        }
        if (left == 0)
            return true;
    }
}

static void insert(GameState *g, Source from, uint32_t at, uint8_t glyphs)
{
    set_menu_text_reading_insert(g, at);
    set_menu_text_reading_insert_left(g, glyphs);
    set_menu_text_reading_source(g, from);
}

/* How many glyphs a name has: up to its end mark, at most `most`. */
static uint8_t glyphs_before(const GameState *g, uint32_t at, uint8_t end, uint8_t most)
{
    uint8_t n = 0;
    while (n < most && menu_text_byte(g, stepped(at, n)) != end)
        n++;
    return n;
}

/* The dictionary: a word's glyphs, their count first ($DE:FA00). */
static void insert_word(GameState *g, uint8_t code, uint16_t entry)
{
    uint32_t at = menu_dictionary_word(g, entry);
    set_menu_text_reading_code(g, code);
    insert(g, FROM_INSERT, stepped(at, 1), menu_text_byte(g, at));
}

/* A character's name (work RAM, the table of places at $C2:5FD8), up to its
 * end mark or five glyphs. The original counts the name's glyphs with the
 * insert already begun, pointer and source set: a place past the table's
 * seven can be the text page's own cells, which the count then sees so. */
enum { NAME_END = 0x00, NAME_GLYPHS = 5, MARLE_REAL_NAME_GLYPHS = 5 };

static void insert_name_at(GameState *g, uint16_t place)
{
    uint32_t at = MENU_NAMES_BANK | place;
    set_menu_text_reading_insert(g, at);
    set_menu_text_reading_source(g, FROM_INSERT);
    set_menu_text_reading_insert_left(g, glyphs_before(g, at, NAME_END, NAME_GLYPHS));
}

static void insert_character_name(GameState *g, unsigned index) { insert_name_at(g, menu_character_name_place(g, index)); }

/* An item's or a tech's name ($CC:0B5E, $CC:15C4, eleven bytes a name, an
 * icon first where it has one) up to its first space; an enemy's all eleven.
 * The original finds a name with the multiplier, which it leaves so. */
enum { SPACE = 0xEF, NAME_BYTES = MENU_NAME_TABLE_BYTES, NO_ICON = 0x2F };

static void leave_name_found(GameState *g, uint8_t which)
{
    hw_set_multiplicand(g, which);
    (void)hw_multiply_by(g, NAME_BYTES);
}

static void insert_item_name(GameState *g, uint8_t item)
{
    ORIGINAL_EFFECT(leave_name_found(g, item));
    uint32_t at = stepped(menu_item_name(g, item), 1);   /* past the icon */
    insert(g, FROM_INSERT, at, glyphs_before(g, at, SPACE, NAME_BYTES - 1));
}

static void insert_tech_name(GameState *g, uint8_t tech)
{
    ORIGINAL_EFFECT(leave_name_found(g, tech));
    uint32_t at = menu_tech_name(g, tech);
    uint8_t glyphs = glyphs_before(g, at, SPACE, NAME_BYTES);
    if (menu_text_byte(g, at) == NO_ICON) {
        at = stepped(at, 1);
        glyphs = glyphs_before(g, at, SPACE, NAME_BYTES - 1);
    }
    insert(g, FROM_INSERT, at, glyphs);
}

static void insert_enemy_name(GameState *g, uint8_t enemy)
{
    ORIGINAL_EFFECT(leave_name_found(g, enemy));
    insert(g, FROM_INSERT, menu_enemy_name(g, enemy), NAME_BYTES);
}

/* ---- Numbers ------------------------------------------------------------------------------- */

/* A number's decimal digits, the first first, without its leading zeros (a
 * zero keeps one digit). The original shifts its digit cells down over the
 * zeros, which leaves the cells past the number's last digit as they were. */
enum { MOST_DIGITS = 8 };

static uint8_t decimal_digits(GameState *g, uint32_t value, uint8_t width)
{
    uint8_t digits[MOST_DIGITS];
    for (int i = width - 1; i >= 0; i--) {
        digits[i] = (uint8_t)(value % 10);
        value /= 10;
    }
    uint8_t count = width;
    while (count > 1 && digits[0] == 0) {
        for (uint8_t i = 0; i + 1 < count; i++)
            digits[i] = digits[i + 1];
        count--;
    }
    for (uint8_t i = 0; i < width; i++)
        set_menu_text_digits(g, i, digits[i]);
    return count;
}

/* Its hex digits as glyphs ($C2:5D03), the high byte first. */
static uint8_t hex_digits(GameState *g, uint16_t value, uint8_t bytes)
{
    uint8_t n = 0;
    for (int b = bytes - 1; b >= 0; b--) {
        uint8_t byte = (uint8_t)(value >> (8 * b));
        set_menu_text_digits(g, n++, (uint8_t)menu_hex_digit_glyph(g, byte >> 4));
        set_menu_text_digits(g, n++, (uint8_t)menu_hex_digit_glyph(g, byte & 0x0F));
    }
    set_menu_text_digits(g, n, 0);
    return n;
}

/* What the original's conversion leaves on the text page past the glyph
 * drawer's cells, which the first digit's drawing covers all the rest of:
 * its last quotient cell, the eight-digit number's units, and the number's
 * second byte where it read one. */
static void leave_conversion(GameState *g, uint32_t value, uint8_t bytes, bool hex)
{
    if (bytes >= 2)
        set_menu_number_second_byte(g, (uint8_t)(value >> 8));
    if (hex)
        return;
    if (bytes == 1)
        set_menu_number_conversion_quotient(g, (uint16_t)((menu_number_conversion_quotient(g) & 0xFF00) | (value / 10 % 10)));
    else if (bytes == 2)
        set_menu_number_conversion_quotient(g, 0);
    else {
        set_menu_number_conversion_quotient(g, (uint16_t)((value / 100 % 10) | (value / 10 % 10) << 8));
        set_menu_number_conversion_last(g, (uint8_t)(value % 10));
        set_menu_number_second_byte(g, 0);
    }
}

// $C2:5953, $C2:5985, $C2:59C8
/* A number from the arguments, one to three bytes, as digits to set. */
static void insert_number(GameState *g, uint8_t bytes)
{
    static const uint8_t WIDTH[4] = { 0, 3, 5, 8 };
    uint32_t value = next_argument(g, bytes);
    bool hex = bytes < 3 && menu_text_hex(g) != 0;
    uint8_t count = hex ? hex_digits(g, (uint16_t)value, bytes) : decimal_digits(g, value, WIDTH[bytes]);
    ORIGINAL_EFFECT(leave_conversion(g, value, bytes, hex));
    insert(g, hex ? FROM_HEX_DIGITS : FROM_DIGITS, DIGITS, count);
}

/* ---- The string ---------------------------------------------------------------------------- */

/* The control codes. */
enum {
    CODE_END = 0x00, CODE_WAIT = 0x03, CODE_BREAKS_FROM = 0x04, CODE_BREAKS_TO = 0x0C,
    CODE_NUMBER_BYTE = 0x0D, CODE_NUMBER_WORD = 0x0E, CODE_NUMBER_LONG = 0x0F, CODE_PAUSE = 0x10,
    CODE_CHARACTER = 0x11, CODE_NAMED = 0x12, CODE_CRONO = 0x13, CODE_MAGUS = 0x19, CODE_LEADER = 0x1A,
    CODE_MEMBER_1 = 0x1B, CODE_MEMBER_3 = 0x1D, CODE_NADIA = 0x1E, CODE_ITEM = 0x1F, CODE_EPOCH = 0x20,
    FIRST_WORD = 0x21,
};

/* After $12: 0 a tech, 1 an enemy, 8 up a word of the dictionary's second
 * part. 2-7 jump into code in the original; no string has them. */
enum { NAMED_TECH = 0, NAMED_ENEMY = 1, NAMED_WORDS = 8, SECOND_WORDS = 0x1F };

/* The Epoch's name is the eighth of the names the text page reaches. */
enum { EPOCH_NAME = 7 };

/* What reading a control code does: true if the string goes on this call. */
static bool control_code(GameState *g, uint8_t code, Ending *end)
{
    *end = (Ending){ ENDED_AT_CODE, code, false };
    if (code == CODE_END || (code >= CODE_BREAKS_FROM && code <= CODE_BREAKS_TO)) {
        set_menu_text_stop(g, code);
        return false;
    }
    if (code == CODE_WAIT) {
        set_menu_text_wait(g, next_string_byte(g));
        set_menu_text_stop(g, STOP_WAITING);
        return false;
    }
    if (code == CODE_PAUSE)
        return false;
    if (code >= CODE_NUMBER_BYTE && code <= CODE_NUMBER_LONG)
        insert_number(g, (uint8_t)(code - CODE_NUMBER_BYTE + 1));
    else if (code == CODE_CHARACTER)
        insert_character_name(g, (unsigned)next_argument(g, 1));
    else if (code >= CODE_CRONO && code <= CODE_MAGUS)
        insert_character_name(g, code - CODE_CRONO);
    else if (code == CODE_LEADER)
        insert_name_at(g, MENU_FIRST_NAME);
    else if (code >= CODE_MEMBER_1 && code <= CODE_MEMBER_3)
        insert_character_name(g, (uint8_t)(party_members(g, code - CODE_MEMBER_1) * 2) / 2);
    else if (code == CODE_NADIA)
        insert(g, FROM_INSERT, MENU_MARLE_REAL_NAME, MARLE_REAL_NAME_GLYPHS);
    else if (code == CODE_ITEM)
        insert_item_name(g, (uint8_t)event_variables(g, 0));
    else if (code == CODE_EPOCH)
        insert_name_at(g, (uint16_t)(MENU_FIRST_NAME + EPOCH_NAME * MENU_NAME_BYTES));
    else {   /* CODE_NAMED */
        uint8_t what = next_string_byte(g);
        if (what >= NAMED_WORDS)
            insert_word(g, what, (uint16_t)(what - NAMED_WORDS + SECOND_WORDS));
        else if (what == NAMED_TECH)
            insert_tech_name(g, (uint8_t)next_argument(g, 1));
        else if (what == NAMED_ENEMY)
            insert_enemy_name(g, (uint8_t)next_argument(g, 1));
        else
            return false;   /* 2-7: the original jumps into code; no string has them */
    }
    return set_insert(g, end);
}

// $C2:58B2
/* Read the string on: glyphs set, words and codes acted on, until something
 * stops the call. */
static void read_string(GameState *g, Ending *end)
{
    for (;;) {
        uint8_t b = next_string_byte(g);
        if (b >= FIRST_GLYPH || b == 1 || b == 2) {
            uint16_t glyph = b >= FIRST_GLYPH ? b : (uint16_t)(b << 8 | next_string_byte(g));
            set_glyph(g, glyph);
            if (!spend_budget(g)) {
                out_of_budget(g);
                *end = (Ending){ ENDED_IN_STRING, 0, false };
                return;
            }
        } else if (b >= FIRST_WORD) {
            insert_word(g, b, (uint16_t)(b - FIRST_WORD));
            if (!set_insert(g, end))
                return;
        } else if (!control_code(g, b, end)) {
            return;
        }
    }
}

/* ---- Going on ------------------------------------------------------------------------------ */

// $C2:584A
/* Take the text up where the last call stopped: not at all once it has
 * ended, and at the start of a new line after a break that asks for one. */
static bool ready_to_go_on(GameState *g)
{
    uint8_t stop = menu_text_stop(g);
    if (stop == STOP_FINISHED)
        return false;
    if (stop >= FIRST_NEW_LINE && stop <= LAST_BREAK) {
        uint8_t pen = (stop & 1) ? MARGIN : INDENT;
        if ((layout(g) & TILE_KIND_BITS) == A_TILE_FURTHER_LEFT)
            pen = (uint8_t)(pen - MARGIN);
        set_menu_text_reading_pen(g, pen);
        set_menu_text_line_glyphs(g, 0);
    }
    return true;
}

// $C2:5823
/* Set glyphs until the string stops the call or the budget runs out. */
static Ending go_on(GameState *g)
{
    Ending end = { NOT_BEGUN, 0, false };
    if (!ready_to_go_on(g))
        return end;
    if (menu_text_reading_source(g) == FROM_STRING || set_insert(g, &end))
        read_string(g, &end);
    return end;
}

void menu_text_go_on(GameState *g) { (void)go_on(g); }

/* ---- The menus' labels ----------------------------------------------------------------------- */

// $C2:F90C
/* A label of the menus' table: set a line at a time, each line's glyphs
 * $400 bytes below the line before, until the string ends or a line fills
 * its 32 glyphs. The tiles are in work RAM's bank, the lines from the tiles'
 * edge. */
enum { LABEL_GLYPHS_A_LINE = 0x20, NEW_LINE = FIRST_NEW_LINE, LABEL_LINE_BYTES = 0x400, TILES_BANK = 0x7E0000u };

static uint32_t next_label_line(uint32_t at)
{
    uint8_t high = (uint8_t)((at >> 8) + (LABEL_LINE_BYTES >> 8));   /* the address's middle byte, stepped on its own */
    return (at & 0xFF00FFu) | (uint32_t)high << 8;
}

static Ending set_label(GameState *g, uint16_t tiles)
{
    uint32_t at = TILES_BANK | tiles;
    Ending end;
    set_menu_text_layout(g, LINES_AT_THE_EDGE);
    set_menu_text_tiles(g, at);
    TIMED_AS(g, 0xC257DF, menu_text_start(g));
    for (;;) {
        set_menu_text_budget(g, LABEL_GLYPHS_A_LINE);
        TIMED_AS(g, 0xC25823, end = go_on(g));
        if (menu_text_stop(g) != NEW_LINE)
            return end;
        at = next_label_line(at);
        set_menu_text_tiles(g, at);
    }
}

void menu_set_label(GameState *g, uint16_t tiles) { (void)set_label(g, tiles); }

/* ---- The entries ------------------------------------------------------------------------- */

/* What the start leaves in the registers: X the arguments' place, Y the
 * string's offset in its table. */
static void leave_started(Snes *s)
{
    s->cpu.a = 0;
    s->cpu.x = ARGUMENTS;
    s->cpu.y = (uint16_t)(2u * menu_text_string(state_of(s)));
}

// $C2:57DF
void menu_text_start_entry(Snes *s)
{
    menu_text_start(state_of(s));
    ORIGINAL_EFFECT(leave_started(s));
    m_rtl(s);
}

/* What a call leaves in the registers: A clear; X and Y as its last steps
 * leave them. A control code is read into Y and dispatched on doubled in X;
 * a glyph's drawing leaves Y at the pen's own tile's column offset and X at
 * its width's index, and a call out of budget in an insert the choice it
 * dispatched on, doubled, in X; a text already ended the stop doubled in X. */
static void leave_ending(Snes *s, Ending end)
{
    const GameState *g = state_of(s);
    s->cpu.a = 0;
    uint16_t glyph = menu_text_reading_glyph(g);
    uint8_t width = menu_glyph_width(g, (uint8_t)(glyph - FIRST_GLYPH));
    unsigned column = (uint8_t)(menu_text_reading_pen(g) - width) / 8u;
    uint16_t column_offset = tiles_of_layout(layout(g)) == MENU_TILES_4_COLOUR
                                 ? menu_text_column_offset(g, column)
                                 : menu_text_column_offset_16_colour(g, column);
    switch (end.how) {
    case ENDED_AT_CODE:
        s->cpu.x = (uint16_t)(end.code * 2);
        s->cpu.y = end.code;
        break;
    case ENDED_IN_STRING:
        s->cpu.x = (uint8_t)(glyph - FIRST_GLYPH);
        s->cpu.y = column_offset;
        break;
    case ENDED_IN_INSERT:
        s->cpu.x = end.insert_left ? 2 : 0;
        s->cpu.y = column_offset;
        break;
    case NOT_BEGUN:
        s->cpu.x = 0;
        break;
    }
}

// $C2:5823
void menu_text_go_on_entry(Snes *s)
{
    Ending end = go_on(state_of(s));
    (void)end;
    ORIGINAL_EFFECT(leave_ending(s, end));
    m_rtl(s);
}

/* The label's setting leaves its last call's registers, and that call's
 * stop in A. */
static void leave_label(Snes *s, Ending end)
{
    leave_ending(s, end);
    s->cpu.a = menu_text_stop(state_of(s));
}

// $C2:F90C
void menu_set_label_entry(Snes *s)
{
    GameState *g = state_of(s);
    Ending end = set_label(g, menu_load_destination(g));
    (void)end;
    ORIGINAL_EFFECT(leave_label(s, end));
    m_rts(s);
}

/* ---- The guards -------------------------------------------------------------------------- */

/* What the game sets text from is the ROM or work RAM, never the I/O
 * registers or open bus, where a read changes what it reads. */
static bool readable(uint32_t at)
{
    uint8_t bank = (uint8_t)(at >> 16);
    uint16_t address = (uint16_t)at;
    if (bank == 0x7E || bank == 0x7F || (bank & 0x7F) >= 0x40)
        return true;
    return address < 0x2000 || address >= 0x8000;
}

/* The tiles the game sets text in are in work RAM, clear of the text page
 * (the setting reads its cells as it goes) and of the bank's end: a line's
 * glyphs reach under $1000 bytes past its start. */
enum { TEXT_PAGE = 0x0200, TEXT_PAGE_END = 0x0300, LINE_REACH = 0x1000 };

static bool tiles_are_writable(uint32_t tiles)
{
    uint8_t bank = (uint8_t)(tiles >> 16);
    uint16_t at = (uint16_t)tiles;
    return (bank == 0x7E || bank == 0x7F) && (uint32_t)at + LINE_REACH <= 0x10000
           && (at >= TEXT_PAGE_END || (uint32_t)at + LINE_REACH <= TEXT_PAGE || bank == 0x7F);
}

bool menu_text_is_startable(Snes *s)
{
    vars_peek(true);
    bool ok = readable(menu_text_strings(state_of(s)));
    vars_peek(false);
    return ok;
}

/* Taking a text up: it is at one of the stops a call leaves, reading from
 * one of the four sources, both from memory; the arguments on the text page. */
enum { MOST_STOP = 0x20 };

static bool can_go_on(const GameState *g)
{
    uint8_t source = menu_text_reading_source(g);
    return menu_text_stop(g) <= MOST_STOP && source <= FROM_HEX_DIGITS && readable(menu_text_reading_at(g))
           && (source == FROM_STRING || readable(menu_text_reading_insert(g)))
           && readable(menu_text_reading_arguments_at(g)) && tiles_are_writable(menu_text_tiles(g));
}

bool menu_text_can_go_on(Snes *s)
{
    vars_peek(true);
    bool ok = can_go_on(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_label_is_settable(Snes *s)
{
    const GameState *g = state_of(s);
    vars_peek(true);
    bool ok = readable(menu_text_strings(g)) && tiles_are_writable(TILES_BANK | menu_load_destination(g));
    vars_peek(false);
    return ok;
}
