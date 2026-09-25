/* The menus' windows: the command lists every menu screen is drawn from.
 *
 * A menu screen is laid out by a list of commands in the ROM (bank $FF), which
 * the screen's code hands to the interpreter here: draw a window, write one of
 * the menus' strings, fill a stretch of tilemap with a tile, copy in a run of
 * tilemap bytes the ROM holds ready, copy or swap a rectangle of tiles, move
 * the place the rest is drawn from, set a per-line effect up and draw its
 * table. Everything goes into the tilemap buffers the menus keep in work RAM
 * (bank $7E), which the screen then queues for VRAM (menu/vram.h). A place in
 * a command is a tile's column and row from the drawing origin, 32 tiles a
 * row, two bytes a tile.
 *
 * A window is a frame and a patterned inside, drawn from one of the styles'
 * twenty tiles: the four corners, the edges and sides (each two tiles taken by
 * turns), and a pattern of four rows of four, repeated across the inside. The
 * style is the player's chosen window design unless the list names one, and
 * the window's attribute (its palette and priority) is the command's; the
 * design's colours go into the upper half of that palette row
 * (menu_load_window_colours).
 *
 * The interpreter runs a list as C, through a table of the commands; each
 * command's handler stays an entry of its own, so the original's calls and
 * fuzzing still find it. */
#include "menu/windows.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- Places ---------------------------------------------------------------------------------- */

/* A tile's place in a command: its column (0-31) and row from the drawing
 * origin. */
typedef struct {
    uint8_t column, row;
} TilePlace;

enum { COLUMN_BITS = 0x1F };

/* Where the tile at `p` is in bank $7E. */
static uint16_t place_of(const GameState *g, TilePlace p)
{
    return (uint16_t)(menu_draw_origin(g) + p.row * MENU_TILEMAP_ROW_BYTES + (p.column & COLUMN_BITS) * MENU_TILE_BYTES);
}

static uint16_t rows_down(uint16_t at, unsigned rows) { return (uint16_t)(at + rows * MENU_TILEMAP_ROW_BYTES); }
static uint16_t across(uint16_t at, unsigned columns) { return (uint16_t)(at + columns * MENU_TILE_BYTES); }

/* A tile in bank $7E. The tilemaps don't reach its end; a word that did would
 * put its second byte in $7F, as the original's indexed stores do. */
static void put_tile(GameState *g, uint16_t at, uint16_t tile) { wram_write16(g, 0x7E0000u + at, tile); }
static uint16_t tile_at(const GameState *g, uint16_t at) { return wram_read16(g, 0x7E0000u + at); }

/* ---- The window styles ------------------------------------------------------------------------ */

/* A style's twenty tiles, from its first: a corner, the top edge's two and
 * the other corner; the sides' two pairs; the bottom's four like the top's;
 * the inside's eight. */
enum { TILES_A_STYLE = 20, LAST_INSIDE_TILE = 0x13, INSIDE_PATTERN = 4 };

/* A style's first tile, by its number. The original multiplies by twenty in
 * a byte, with shifts and an add that carries the number's bit 4 into the
 * sum; the game's styles (the eight designs, and $8B) have it clear. */
static uint8_t style_first_tile(uint8_t number)
{
    return (uint8_t)(number * TILES_A_STYLE + ((number >> 4) & 1));
}

/* A list names a style of its own with the top bit set; otherwise the window
 * is drawn in the player's design. */
enum { NAMED_STYLE = 0x80, DESIGNS = 8 };

static uint8_t design_chosen(const GameState *g) { return (uint8_t)(menu_window_design(g) & (DESIGNS - 1)); }

static uint8_t style_number(const GameState *g, uint8_t style)
{
    return (style & NAMED_STYLE) ? style : design_chosen(g);
}

static MenuWindowStyle style_of(uint8_t first_tile, uint8_t attribute)
{
    return (MenuWindowStyle)(attribute << 8 | first_tile);
}

MenuWindowStyle menu_design_style(const GameState *g, uint8_t attribute)
{
    return style_of(style_first_tile(menu_window_design(g)), attribute);
}

/* ---- Windows ---------------------------------------------------------------------------------- */

/* The frame's parts follow each other in the style's tiles: each starts at
 * the next multiple of four past where the one before it got to. */
static uint16_t past_the_part(uint16_t tile) { return (uint16_t)((tile | 3) + 1); }

/* The sides' pairs go by turns: the next pair's left tile is the right tile
 * before it with its low two bits flipped. */
static uint16_t next_side_pair(uint16_t left) { return (uint16_t)((left + 1) ^ 3); }

static uint16_t side_pair_after(uint16_t first, uint16_t rows)
{
    uint16_t left = first;
    for (uint16_t r = rows; r != 0; r--)
        left = next_side_pair(left);
    return left;
}

/* A frame's top or bottom row, from `first` of its four tiles: a corner, the
 * edge's two tiles by turns, the other corner. A frame narrower than four
 * tiles still has its right corner four in, where the original's copy of the
 * edge starts. */
static void draw_edge_row(GameState *g, uint16_t row, uint8_t columns, uint16_t first)
{
    unsigned last = columns > 3 ? columns - 1u : 3u;
    put_tile(g, row, first);
    for (unsigned c = 1; c < last; c++)
        put_tile(g, across(row, c), (uint16_t)(first + 1 + ((c - 1) & 1)));
    put_tile(g, across(row, last), (uint16_t)(first | 3));
}

/* The frame's sides down `rows` rows, a pair a row. The right side is reached
 * from the left through a pointer, so it alone runs on into bank $7F. */
static void draw_sides(GameState *g, uint16_t row, uint8_t columns, uint16_t rows, uint16_t first)
{
    uint16_t right = (uint16_t)((columns - 1) * MENU_TILE_BYTES);
    uint16_t left = first;
    for (uint16_t r = rows; r != 0; r--, row = rows_down(row, 1)) {
        put_tile(g, row, left);
        wram_write16(g, 0x7E0000u + right + row, (uint16_t)(left + 1));
        left = next_side_pair(left);
    }
}

/* A frame of `rows` rows has that many less two inside, and as many columns
 * less two. */
static uint16_t inside_of(uint8_t across_the_frame) { return (uint16_t)(across_the_frame - 2); }

void menu_draw_window_frame(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style)
{
    uint16_t sides = inside_of(size.rows);
    uint16_t side_pairs = past_the_part(style);
    uint16_t bottom = past_the_part(side_pair_after(side_pairs, sides));
    draw_edge_row(g, at, size.columns, style);
    draw_sides(g, rows_down(at, 1), size.columns, sides, side_pairs);
    draw_edge_row(g, rows_down(at, 1u + sides), size.columns, bottom);
}

/* The inside's pattern, four tiles by four, repeated across and down from a
 * tile in from the window's corner: each tile the style's first plus the
 * pattern's, in the style's attribute. The original draws the first row a
 * pattern's width and repeats it across with a block move, draws the next
 * three tile by tile, and repeats the four rows down with block moves; the
 * port keeps that division so that the block moves' time is charged where
 * they are. */
static uint16_t inside_corner(uint16_t at) { return rows_down(across(at, 1), 1); }

static void repeat_tiles(GameState *g, uint16_t from, uint16_t to, uint16_t tiles)
{
    uint32_t bytes = (uint32_t)tiles * MENU_TILE_BYTES;
    wram_copy(g, 0x7E0000u + from, 0x7E0000u + to, bytes);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
}

static void draw_pattern_tiles(GameState *g, uint16_t row_at, uint16_t row, uint16_t columns, MenuWindowStyle style)
{
    for (uint16_t c = 0; c < columns; c++) {
        uint8_t tile = (uint8_t)(style + menu_window_inside_tile(g, row, c % INSIDE_PATTERN));
        put_tile(g, across(row_at, c), (uint16_t)((style & 0xFF00) | tile));
    }
}

static void fill_inside(GameState *g, uint16_t at, uint16_t columns, uint16_t rows, MenuWindowStyle style)
{
    uint16_t corner = inside_corner(at);
    if (columns > INSIDE_PATTERN) {
        draw_pattern_tiles(g, corner, 0, INSIDE_PATTERN, style);
        repeat_tiles(g, corner, across(corner, INSIDE_PATTERN), (uint16_t)(columns - INSIDE_PATTERN));
    } else {
        draw_pattern_tiles(g, corner, 0, columns, style);
    }
    for (uint16_t r = 1; r < rows && r < INSIDE_PATTERN; r++)
        draw_pattern_tiles(g, rows_down(corner, r), r, columns, style);
    for (uint16_t r = INSIDE_PATTERN; r < rows; r++)
        repeat_tiles(g, rows_down(corner, r - INSIDE_PATTERN), rows_down(corner, r), columns);
}

void menu_fill_window_inside(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style)
{
    fill_inside(g, at, inside_of(size.columns), inside_of(size.rows), style);
}

// $C2:ED08
/* The design's colours into the window's palette row, whose upper half they
 * are, and the palette sent at the next vblank. */
enum { PALETTE_ROW_COLOURS = 16, PALETTE_BITS = 0x1C, PALETTE_SHIFT = 2 };

void menu_load_window_colours(GameState *g, MenuWindowStyle style)
{
    uint8_t row = (uint8_t)(((style >> 8) & PALETTE_BITS) >> PALETTE_SHIFT);
    uint8_t design = design_chosen(g);
    for (unsigned i = 0; i < MENU_WINDOW_DESIGN_COLOURS; i++)
        set_menu_palette(g, row * PALETTE_ROW_COLOURS + MENU_WINDOW_DESIGN_COLOURS + i,
                         menu_window_design_colour(g, design, i));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
}

// $C2:EDAA, $C2:EDB0, $C2:EDB6 (the style $C2:EDD3, the frame $C2:EDF6, the inside $C2:EE7F)
/* A window a list draws: its frame, its inside or both, at a place, in the
 * style the list names or the player's design; the style is the one the
 * colours are loaded for (menu_window_style). */
static void draw_window(GameState *g, TilePlace at, uint8_t style, uint8_t attribute, MenuWindowSize size,
                        bool frame, bool inside)
{
    uint16_t place = place_of(g, at);
    MenuWindowStyle chosen = style_of(style_first_tile(style_number(g, style)), attribute);
    set_menu_window_place(g, place);
    set_menu_window_style(g, chosen);
    if (frame)
        menu_draw_window_frame(g, place, size, chosen);
    if (inside)
        menu_fill_window_inside(g, place, size, chosen);
}

/* ---- The other commands ------------------------------------------------------------------------ */

// $C2:ED77
/* A stretch of `bytes` bytes of tilemap from the origin filled with a tile,
 * which with its attribute becomes the tile and attribute the strings drawn
 * after it take. */
static void fill_stretch(GameState *g, uint16_t bytes, uint16_t tile)
{
    uint16_t at = menu_draw_origin(g);
    set_menu_fill_tile(g, (uint8_t)tile);
    set_menu_text_attribute(g, (uint8_t)(tile >> 8));
    put_tile(g, at, tile);
    for (uint16_t i = MENU_TILE_BYTES; i < bytes; i++)
        wram_write8(g, 0x7E0000u | (uint16_t)(at + i), (uint8_t)(i & 1 ? tile >> 8 : tile));
}

// $C2:EF05
/* A rectangle of tiles copied from one place to another, a row at a time and
 * each row from its left, so a rectangle copied over itself reads what it has
 * already written. */
static void copy_area(GameState *g, TilePlace from, TilePlace to, uint8_t columns, uint8_t rows)
{
    uint16_t source = place_of(g, from), dest = place_of(g, to);
    for (unsigned r = 0; r < rows; r++)
        for (unsigned i = 0; i < columns * MENU_TILE_BYTES; i++)
            wram_write8(g, 0x7E0000u | (uint16_t)(rows_down(dest, r) + i),
                        wram_read8(g, 0x7E0000u | (uint16_t)(rows_down(source, r) + i)));
}

// $C2:F378
/* Two rectangles of tiles exchanged. */
static void swap_areas(GameState *g, TilePlace one, TilePlace other, MenuWindowSize size)
{
    uint16_t a = place_of(g, one), b = place_of(g, other);
    for (unsigned r = 0; r < size.rows; r++) {
        for (unsigned c = 0; c < size.columns; c++) {
            uint16_t at_a = across(rows_down(a, r), c), at_b = across(rows_down(b, r), c);
            uint16_t was = tile_at(g, at_a);
            put_tile(g, at_a, tile_at(g, at_b));
            put_tile(g, at_b, was);
        }
    }
}

// $C2:EF48
/* One of the menus' strings, by its number, drawn at a place (menu/text.c):
 * `settings` are the text's flags and the most tiles it may draw. */
enum { STRINGS_BANK = 0xFF };

static void draw_string(GameState *g, uint16_t number, TilePlace at, uint8_t settings)
{
    uint16_t pen = place_of(g, at);
    set_menu_window_place(g, pen);
    uint32_t string = (uint32_t)STRINGS_BANK << 16 | menu_window_string(g, number);
    TIMED_AS(g, 0xC2EF65, (void)menu_draw_string(g, string, pen, settings));
}

// $C2:F332
/* Where the commands after it draw from: a tilemap buffer, or a per-line
 * effect's table. */
static void set_origin(GameState *g, uint16_t origin) { set_menu_draw_origin(g, origin); }

// $C2:F364
/* A run of tilemap bytes the ROM holds ready, copied in at the origin: its
 * first byte says how many follow (0 for 256). */
static unsigned run_length(uint8_t count) { return count ? count : 256u; }

static void copy_bytes(GameState *g, uint16_t run)
{
    unsigned count = run_length(menu_window_byte(g, run));
    uint16_t at = menu_draw_origin(g);
    for (unsigned i = 0; i < count; i++)
        wram_write8(g, 0x7E0000u | (uint16_t)(at + i), menu_window_byte(g, (uint16_t)(run + 1 + i)));
}

// $C2:F337
/* A per-line effect set up on the line-effect slot a channel names, reading
 * its table where the slot's is kept (128 bytes a slot at $7E:9000), and the
 * commands after it drawing that table. The command holds the channel's own
 * two control bytes (vid_line_effect_decode); the guard takes only those a
 * target has. */
enum { LINE_TABLES = 0x9000, LINE_TABLE_BYTES = 0x80, SLOT_BITS = 7, SLOT_SHIFT = 4 };

static uint8_t line_slot_of(uint16_t channel) { return (uint8_t)((channel >> SLOT_SHIFT) & SLOT_BITS); }

static void set_line_effect(GameState *g, Platform *pf, uint16_t channel, uint8_t control, uint8_t dest)
{
    uint8_t slot = line_slot_of(channel);
    uint16_t table = (uint16_t)(LINE_TABLES + slot * LINE_TABLE_BYTES);
    VidLineEffect e = { 0 };
    vid_line_effect_decode(control, dest, &e);
    e.indirect_bank = 0x7E;
    e.table = PF_GAME(0x7E0000u | table);
    vid_set_line_slot(pf, PF_AT(0xC2F342), slot, &e);
    set_origin(g, table);
    vid_set_line_table(pf, PF_AT(0xC2F35C), slot, table);
}

/* ---- Laying out a loaded graphic --------------------------------------------------------------- */

/* A graphic the menus load or draw into tiles (a label, a name in the
 * dialog's font) sits in VRAM two tile rows high: sixteen tiles of a row
 * above the sixteen of the row below them, and a graphic wider than sixteen
 * going on in the next two rows. Laying it out puts its tiles across a strip
 * of tilemap two rows high, from its first tile on. The tile number's bit 4
 * is the row's: clear on the top row and set on the bottom. */
enum { LOWER_TILE_ROW = 0x10 };

static uint16_t next_column(uint16_t tile) { return (uint16_t)(((tile | LOWER_TILE_ROW) + 1) & ~LOWER_TILE_ROW); }

static uint16_t lay_out_strip(GameState *g, uint16_t at, uint16_t columns, uint16_t tile)
{
    for (uint16_t left = columns; left != 0; left--, at = across(at, 1)) {
        put_tile(g, at, tile);
        put_tile(g, rows_down(at, 1), (uint16_t)(tile | LOWER_TILE_ROW));
        tile = next_column(tile);
    }
    return at;
}

// $C2:FB97
/* A label in palette row 4, in front: its tiles from `first` on. */
enum { LABEL_ATTRIBUTE = 0x3100, TILE_NUMBER_LOW = 0x00EF };

void menu_lay_out_label(GameState *g, uint16_t at, uint16_t columns, uint8_t first)
{
    (void)lay_out_strip(g, at, columns, (uint16_t)((first & TILE_NUMBER_LOW) | LABEL_ATTRIBUTE));
}

// $C2:FBB4
/* A strip of a graphic in the attribute and from the tile `tile` names (in
 * the lower two palette rows' and priority's bits and the tile number's low
 * ten): returns the place past it. */
enum { STRIP_TILE_BITS = 0x33EF };

uint16_t menu_lay_out_strip(GameState *g, uint16_t at, uint16_t columns, uint16_t tile)
{
    return lay_out_strip(g, at, columns, (uint16_t)(tile & STRIP_TILE_BITS));
}

/* ---- The language ------------------------------------------------------------------------------ */

/* A command is an even byte and its operands; a byte with the top bit set
 * ends the list. */
enum { END_OF_LIST = 0x80, COMMANDS = 11, MOST_OPERANDS = 6 };

typedef enum { FILL, COPY_AREA, SWAP_AREAS, WINDOW, STRING, SET_ORIGIN, COPY_BYTES, LINE_EFFECT } WindowOperation;

/* Which parts of a window a window command draws. */
enum { FRAME = 1, INSIDE = 2 };

/* What each command is, how many operand bytes follow it, and the handler
 * (a registered entry, windows.h) whose calibrated time its work is charged
 * at. */
static const struct {
    WindowOperation operation;
    uint8_t parts;
    uint8_t operands;
    uint32_t timed_as;
} LANGUAGE[COMMANDS] = {
    [0x00 / 2] = { FILL, 0, 4, 0xC2ED77 },                  /* bytes (word), tile (word) */
    [0x02 / 2] = { COPY_AREA, 0, 6, 0xC2EF05 },             /* from, to (places), columns, rows */
    [0x04 / 2] = { WINDOW, FRAME | INSIDE, 6, 0xC2EDAA },   /* style, attribute, place, columns, rows */
    [0x06 / 2] = { STRING, 0, 5, 0xC2EF48 },                /* string (word), place, settings */
    [0x08 / 2] = { SET_ORIGIN, 0, 2, 0xC2F332 },            /* origin (word) */
    [0x0A / 2] = { COPY_BYTES, 0, 2, 0xC2F364 },            /* the run (word) */
    [0x0C / 2] = { LINE_EFFECT, 0, 4, 0xC2F337 },           /* channel (word), control, destination */
    [0x0E / 2] = { COPY_BYTES, 0, 2, 0xC2F364 },
    [0x10 / 2] = { SWAP_AREAS, 0, 6, 0xC2F378 },            /* one, the other (places), columns, rows */
    [0x12 / 2] = { WINDOW, FRAME, 6, 0xC2EDB0 },
    [0x14 / 2] = { WINDOW, INSIDE, 6, 0xC2EDB6 },
};

static bool is_command(uint8_t byte) { return !(byte & 1) && byte / 2 < COMMANDS; }
static WindowOperation operation_of(uint8_t command) { return LANGUAGE[command / 2].operation; }

/* A command's operands, decoded by its operation. */
typedef struct {
    uint8_t bytes[MOST_OPERANDS];
} Operands;

static uint16_t operand_word(const Operands *o, unsigned at) { return (uint16_t)(o->bytes[at] | o->bytes[at + 1] << 8); }
static TilePlace operand_place(const Operands *o, unsigned at) { return (TilePlace){ o->bytes[at], o->bytes[at + 1] }; }

/* A window's or a swap's size: 31 columns at most and 63 rows. */
enum { SIZE_COLUMN_BITS = 0x1F, SIZE_ROW_BITS = 0x3F };
static MenuWindowSize operand_size(const Operands *o, unsigned at)
{
    return (MenuWindowSize){ (uint8_t)(o->bytes[at] & SIZE_COLUMN_BITS), (uint8_t)(o->bytes[at + 1] & SIZE_ROW_BITS) };
}

/* Carry out a command. */
static void run_command(GameState *g, Platform *pf, uint8_t command, const Operands *o)
{
    uint8_t parts = LANGUAGE[command / 2].parts;
    switch (operation_of(command)) {
    case FILL: fill_stretch(g, operand_word(o, 0), operand_word(o, 2)); break;
    case COPY_AREA: copy_area(g, operand_place(o, 0), operand_place(o, 2), o->bytes[4], o->bytes[5]); break;
    case SWAP_AREAS: swap_areas(g, operand_place(o, 0), operand_place(o, 2), operand_size(o, 4)); break;
    case WINDOW:
        draw_window(g, operand_place(o, 2), o->bytes[0], o->bytes[1], operand_size(o, 4), parts & FRAME, parts & INSIDE);
        break;
    case STRING: draw_string(g, operand_word(o, 0), operand_place(o, 2), o->bytes[4]); break;
    case SET_ORIGIN: set_origin(g, operand_word(o, 0)); break;
    case COPY_BYTES: copy_bytes(g, operand_word(o, 0)); break;
    case LINE_EFFECT: set_line_effect(g, pf, operand_word(o, 0), o->bytes[2], o->bytes[3]); break;
    }
}

/* ---- What the original leaves in the drawing's cells -------------------------------------------- */

/* The original's handlers keep their working values in the menus' cells: the
 * command's operands, which they are copied into; a window's columns and
 * rows, the rows its drawing walked, the tile its frame got to, the row of
 * the inside's pattern. Nothing reads them after the list but the next
 * command's handler, over again, so they are left once a command is done as
 * the original leaves them, worked out from the command. */
typedef struct {
    uint16_t place;       /* menu_window_place */
    uint16_t line;        /* menu_line_start */
    uint16_t columns;     /* menu_text_work_window_columns */
    uint16_t rows;        /* menu_text_work_window_rows */
    uint16_t right_side;  /* menu_scratch_window_right_side */
    uint16_t tile;        /* menu_scratch_window_tile */
    uint16_t pattern_row; /* menu_scratch_window_pattern_row */
} DrawingCells;

static DrawingCells cells_now(const GameState *g)
{
    return (DrawingCells){ menu_window_place(g), menu_line_start(g), menu_text_work_window_columns(g),
                           menu_text_work_window_rows(g), menu_scratch_window_right_side(g),
                           menu_scratch_window_tile(g), menu_scratch_window_pattern_row(g) };
}

static void put_cells(GameState *g, const DrawingCells *c)
{
    set_menu_window_place(g, c->place);
    set_menu_line_start(g, c->line);
    set_menu_text_work_window_columns(g, c->columns);
    set_menu_text_work_window_rows(g, c->rows);
    set_menu_scratch_window_right_side(g, c->right_side);
    set_menu_scratch_window_tile(g, c->tile);
    set_menu_scratch_window_pattern_row(g, c->pattern_row);
}

/* A frame walks its rows down from the window's place, and gets to the tile
 * past its bottom row's. */
static void note_frame(DrawingCells *c, uint16_t at, MenuWindowSize size, MenuWindowStyle style)
{
    uint16_t sides = inside_of(size.rows);
    uint16_t bottom = past_the_part(side_pair_after(past_the_part(style), sides));
    c->line = rows_down(at, 2u + sides);
    c->right_side = (uint16_t)((size.columns - 1) * MENU_TILE_BYTES);
    c->columns = inside_of(size.columns);
    c->rows = sides;
    c->tile = past_the_part(bottom);
}

/* An inside's first four rows are drawn tile by tile, a row of the pattern at
 * a time (a first row wider than four copied on from its first four, which
 * leaves the columns it didn't count), and the rows after them are copied
 * down from those four, which walks the window's place down with them. */
static void note_inside(DrawingCells *c, uint16_t at, uint16_t columns, uint16_t rows, MenuWindowStyle style)
{
    uint16_t corner = inside_corner(at);
    c->line = rows_down(corner, rows);
    c->tile = style;
    c->pattern_row = rows < INSIDE_PATTERN ? (uint16_t)(rows * INSIDE_PATTERN) : 0;
    c->right_side = rows == 1 && columns > INSIDE_PATTERN ? (uint16_t)(columns - INSIDE_PATTERN) : 0;
    c->columns = rows > INSIDE_PATTERN ? (uint16_t)(2 * columns - 1) : columns;
    c->place = rows > INSIDE_PATTERN ? rows_down(corner, rows - INSIDE_PATTERN) : at;
    c->rows = 0;
}

/* A copy or a swap walks both its rectangles' places down past their rows;
 * a copy counts its row's bytes less one, a swap its columns. */
static void note_area(const GameState *g, DrawingCells *c, const Operands *o, bool copy)
{
    uint8_t rows = copy ? o->bytes[5] : operand_size(o, 4).rows;
    c->place = rows_down(place_of(g, operand_place(o, 0)), rows);
    c->line = rows_down(place_of(g, operand_place(o, 2)), rows);
    c->columns = copy ? (uint16_t)(2 * o->bytes[4] - 1) : operand_size(o, 4).columns;
    c->rows = 0;
}

static void note_command(const GameState *g, DrawingCells *c, uint8_t command, const Operands *o)
{
    switch (operation_of(command)) {
    case COPY_AREA: note_area(g, c, o, true); break;
    case SWAP_AREAS: note_area(g, c, o, false); break;
    case WINDOW: {
        uint16_t at = place_of(g, operand_place(o, 2));
        MenuWindowSize size = operand_size(o, 4);
        MenuWindowStyle style = menu_window_style(g);
        uint8_t parts = LANGUAGE[command / 2].parts;
        c->place = at;
        if (parts & FRAME)
            note_frame(c, at, size, style);
        if (parts & INSIDE)
            note_inside(c, at, inside_of(size.columns), inside_of(size.rows), style);
        break;
    }
    case STRING: c->place = place_of(g, operand_place(o, 2)); break;
    default: break;
    }
}

/* A whole window a screen draws itself, frame and inside, leaves the cells as
 * the original's frame and inside do. Scaffolding, like the rest. */
void menu_leave_window_cells(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style)
{
    DrawingCells c = cells_now(g);
    note_frame(&c, at, size, style);
    note_inside(&c, at, inside_of(size.columns), inside_of(size.rows), style);
    put_cells(g, &c);
}

/* The operand cells after a command: its operands, over the longer command's
 * before it; a copy or swap puts its first place over its second. And the
 * other cells a handler keeps: a window's style number times four, a swap's
 * columns counted down. */
static void leave_operands(GameState *g, uint8_t command, const Operands *o)
{
    WindowOperation op = operation_of(command);
    for (unsigned i = 0; i < LANGUAGE[command / 2].operands && i < MOST_OPERANDS; i++)
        set_menu_window_operands(g, i, o->bytes[i]);
    if (op == COPY_AREA || op == SWAP_AREAS) {
        set_menu_window_operands(g, 2, o->bytes[0]);
        set_menu_window_operands(g, 3, o->bytes[1]);
    }
    if (op == SWAP_AREAS)
        set_menu_scratch_window_columns_left(g, 0);
    if (op == WINDOW)
        set_menu_scratch_window_style_fourfold(g, (uint8_t)(style_number(g, o->bytes[0]) << 2));
}

static void leave_command(GameState *g, uint8_t command, const Operands *o)
{
    DrawingCells c = cells_now(g);
    note_command(g, &c, command, o);
    put_cells(g, &c);
    leave_operands(g, command, o);
}

/* ---- The interpreter ---------------------------------------------------------------------------- */

static Operands operands_at(const GameState *g, uint16_t at, uint8_t command)
{
    Operands o = { { 0 } };
    for (unsigned i = 0; i < LANGUAGE[command / 2].operands && i < MOST_OPERANDS; i++)
        o.bytes[i] = menu_window_byte(g, (uint16_t)(at + i));
    return o;
}

// $C2:ED31
void menu_draw_window_list(GameState *g, Platform *pf, uint16_t list)
{
    uint16_t at = list;
    for (uint8_t command = menu_window_byte(g, at); !(command & END_OF_LIST); command = menu_window_byte(g, at)) {
        if (!is_command(command))
            return;   /* no list of the game's has one: the guard hands such a list to the original */
        Operands o = operands_at(g, (uint16_t)(at + 1), command);
        at = (uint16_t)(at + 1 + LANGUAGE[command / 2].operands);
        TIMED_AS(g, LANGUAGE[command / 2].timed_as, run_command(g, pf, command, &o));
        ORIGINAL_EFFECT(leave_command(g, command, &o));
    }
}

/* ---- Where a list may draw ---------------------------------------------------------------------- */

/* The menus draw above low RAM, where their cells and the tasks' stacks are,
 * and short of the bank's end; the game's lists do. A command's rectangle is
 * where its first row starts to where its last ends. */
enum { LOW_RAM_END = 0x2000, BANK_END = 0x10000, CHANNEL_BITS = 0xFF8F };

static bool reaches_only_buffers(uint16_t first, uint32_t rows, uint32_t row_bytes)
{
    return first >= LOW_RAM_END && rows >= 1 && (uint32_t)first + (rows - 1) * MENU_TILEMAP_ROW_BYTES + row_bytes <= BANK_END;
}

/* A command the game's lists hold, with the drawing origin at `origin`: a
 * fill of at least three bytes, rectangles of at least a tile, windows of at
 * least three tiles square whose inside's tiles stay in their style's byte,
 * a per-line effect on one of the eight channels whose control bytes name a
 * target, and all of it in the buffers. */
static bool command_is_drawable(const GameState *g, uint8_t command, const Operands *o, uint16_t origin)
{
    if (!is_command(command))
        return false;
    TilePlace first = operand_place(o, 0), second = operand_place(o, 2);
    uint16_t at_first = (uint16_t)(origin + first.row * MENU_TILEMAP_ROW_BYTES + (first.column & COLUMN_BITS) * 2);
    uint16_t at_second = (uint16_t)(origin + second.row * MENU_TILEMAP_ROW_BYTES + (second.column & COLUMN_BITS) * 2);
    switch (operation_of(command)) {
    case FILL: return operand_word(o, 0) >= 3 && reaches_only_buffers(origin, 1, operand_word(o, 0));
    case COPY_AREA:
        return reaches_only_buffers(at_first, o->bytes[5], o->bytes[4] * 2u)
               && reaches_only_buffers(at_second, o->bytes[5], o->bytes[4] * 2u) && o->bytes[4] >= 1;
    case SWAP_AREAS: {
        MenuWindowSize size = operand_size(o, 4);
        return size.columns >= 1 && reaches_only_buffers(at_first, size.rows, size.columns * 2u)
               && reaches_only_buffers(at_second, size.rows, size.columns * 2u);
    }
    case WINDOW: {
        MenuWindowSize size = operand_size(o, 4);
        uint8_t tile = style_first_tile(style_number(g, o->bytes[0]));
        return size.columns >= 3 && size.rows >= 3 && tile + LAST_INSIDE_TILE <= 0xFF
               && reaches_only_buffers(at_second, size.rows, size.columns * 2u);
    }
    case STRING: return at_second >= LOW_RAM_END;
    case SET_ORIGIN: return true;
    case COPY_BYTES: {
        unsigned count = run_length(menu_window_byte(g, operand_word(o, 0)));
        return reaches_only_buffers(origin, 1, count) && operand_word(o, 0) + 1u + count <= BANK_END;
    }
    case LINE_EFFECT: {
        VidLineEffect e = { 0 };
        return (operand_word(o, 0) & CHANNEL_BITS) == M_DP_DMA &&   /* the eight channels' own bases */
                vid_line_effect_decode(o->bytes[2], o->bytes[3], &e);
    }
    }
    return false;
}

/* Where the origin is after a command that moves it. */
static uint16_t origin_after(uint8_t command, const Operands *o, uint16_t origin)
{
    switch (operation_of(command)) {
    case SET_ORIGIN: return operand_word(o, 0);
    case LINE_EFFECT: return (uint16_t)(LINE_TABLES + line_slot_of(operand_word(o, 0)) * LINE_TABLE_BYTES);
    default: return origin;
    }
}

/* The string drawer converts numbers from the first power of two: the word
 * its table starts from is 0 whenever the menus draw (menu/text.c). */
static bool strings_convert_from_one(const GameState *g) { return menu_zero(g) == 0; }

/* A list the game draws: commands it knows, each drawable where the origin
 * is by then, and an end within the bank and a sensible length. */
enum { MOST_COMMANDS = 128 };

static bool list_is_drawable(const GameState *g, uint16_t list)
{
    uint16_t origin = menu_draw_origin(g);
    uint32_t at = list;
    for (unsigned n = 0; n < MOST_COMMANDS && at < BANK_END; n++) {
        uint8_t command = menu_window_byte(g, (uint16_t)at);
        if (command & END_OF_LIST)
            return true;
        if (!is_command(command) || at + 1 + LANGUAGE[command / 2].operands > BANK_END)
            return false;
        Operands o = operands_at(g, (uint16_t)(at + 1), command);
        if (!command_is_drawable(g, command, &o, origin))
            return false;
        if (operation_of(command) == STRING && !strings_convert_from_one(g))
            return false;
        origin = origin_after(command, &o, origin);
        at += 1u + LANGUAGE[command / 2].operands;
    }
    return false;
}

/* ---- The entries -------------------------------------------------------------------------------- */

/* The window routines run on the menus' page; a command's handler, as the
 * interpreter calls it, with the data bank on work RAM's. */

bool menu_window_list_is_drawable(Snes *s)
{
    if (!menu_on_page(s))
        return false;
    vars_peek(true);
    bool ok = list_is_drawable(state_of(s), s->cpu.x);
    vars_peek(false);
    return ok;
}

/* What the original leaves in A: the interpreter reads each command byte
 * into an accumulator it clears first (its high byte is read after it); the
 * inside's drawing the last of its working values; the colours' block move
 * the count it runs down to. */
static void leave_a(Snes *s, uint16_t a) { s->cpu.a = a; }

// $C2:ED31
void menu_run_window_list(Snes *s)
{
    menu_draw_window_list(state_of(s), m_platform(s), s->cpu.x);
    ORIGINAL_EFFECT(leave_a(s, 0));
    m_rts(s);
}

/* A handler reads its command's operands from the cells the interpreter
 * copies them into. */
static Operands operands_in_cells(const GameState *g)
{
    Operands o = { { 0 } };
    for (unsigned i = 0; i < MOST_OPERANDS; i++)
        o.bytes[i] = menu_window_operands(g, i);
    return o;
}

static void run_handler(Snes *s, uint8_t command)
{
    GameState *g = state_of(s);
    Operands o = operands_in_cells(g);
    run_command(g, m_platform(s), command, &o);
    ORIGINAL_EFFECT(leave_command(g, command, &o));
    m_rts(s);
}

static bool handler_accepts(Snes *s, uint8_t command)
{
    if (!menu_on_page_work_ram(s))
        return false;
    const GameState *g = state_of(s);
    vars_peek(true);
    Operands o = operands_in_cells(g);
    bool ok = command_is_drawable(g, command, &o, menu_draw_origin(g))
              && (operation_of(command) != STRING || strings_convert_from_one(g));
    vars_peek(false);
    return ok;
}

// $C2:ED77, $C2:EF05, $C2:EDAA, $C2:EF48, $C2:F332, $C2:F364, $C2:F337, $C2:F378, $C2:EDB0, $C2:EDB6
#define MENU_WINDOW_DEFINE_HANDLER(name, command)                  \
    void name(Snes *s) { run_handler(s, command); }                \
    bool name##_accepts(Snes *s) { return handler_accepts(s, command); }
MENU_WINDOW_HANDLERS(MENU_WINDOW_DEFINE_HANDLER)
#undef MENU_WINDOW_DEFINE_HANDLER

/* The frame's and the inside's own entries, and the window a screen draws
 * in the player's design: a window at the place, of the size and in the style
 * the cells hold, as the screens that call them set them; the inside's own
 * entry takes its size inside the frame, as the frame's drawing leaves it. */
static MenuWindowSize size_in_cells(const GameState *g)
{
    return (MenuWindowSize){ (uint8_t)(menu_window_operands(g, 4) & SIZE_COLUMN_BITS),
                             (uint8_t)(menu_window_operands(g, 5) & SIZE_ROW_BITS) };
}

static void leave_frame(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style)
{
    DrawingCells c = cells_now(g);
    note_frame(&c, at, size, style);
    put_cells(g, &c);
}

static void leave_inside(GameState *g, uint16_t at, uint16_t columns, uint16_t rows, MenuWindowStyle style)
{
    DrawingCells c = cells_now(g);
    note_inside(&c, at, columns, rows, style);
    put_cells(g, &c);
}

/* The inside's drawing ends on the pattern's row where it had fewer than
 * four, and on the window's place walked down where it copied rows. */
static uint16_t inside_leaves_a(const GameState *g, uint16_t rows)
{
    return rows > INSIDE_PATTERN ? menu_window_place(g) : (uint16_t)(rows < INSIDE_PATTERN ? rows * INSIDE_PATTERN : 0);
}

// $C2:EDF6
void menu_window_frame(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = menu_window_place(g);
    MenuWindowSize size = size_in_cells(g);
    MenuWindowStyle style = menu_window_style(g);
    menu_draw_window_frame(g, at, size, style);
    ORIGINAL_EFFECT(leave_frame(g, at, size, style));
    m_rts(s);
}

// $C2:EE7F
void menu_window_inside(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = menu_window_place(g);
    uint16_t columns = menu_text_work_window_columns(g), rows = menu_text_work_window_rows(g);
    MenuWindowStyle style = menu_window_style(g);
    fill_inside(g, at, columns, rows, style);
    ORIGINAL_EFFECT(leave_inside(g, at, columns, rows, style));
    ORIGINAL_EFFECT(leave_a(s, inside_leaves_a(g, rows)));
    m_rts(s);
}

// $C2:ED08
void menu_window_colours(Snes *s)
{
    GameState *g = state_of(s);
    menu_load_window_colours(g, menu_window_style(g));
    ORIGINAL_EFFECT(leave_a(s, 0xFFFF));
    m_rts(s);
}

// $C2:ECDB
/* A save slot's window, in the player's design and the slot's own palette
 * row, the one after the slot's number (in the tiles from $300 on), with its
 * colours; at the place and of the size the cells hold. */
enum { SLOT_TILES = 3 };

static MenuWindowStyle slot_style(const GameState *g, uint8_t slot)
{
    return menu_design_style(g, (uint8_t)(((slot + 1) << PALETTE_SHIFT) | SLOT_TILES));
}

void menu_window_in_design(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = menu_window_place(g);
    MenuWindowSize size = size_in_cells(g);
    MenuWindowStyle style = slot_style(g, menu_save_slot_shown(g));
    set_menu_window_style(g, style);
    menu_draw_window_frame(g, at, size, style);
    menu_fill_window_inside(g, at, size, style);
    menu_load_window_colours(g, style);
    ORIGINAL_EFFECT(leave_frame(g, at, size, style));
    ORIGINAL_EFFECT(leave_inside(g, at, inside_of(size.columns), inside_of(size.rows), style));
    m_rts(s);
}

/* The frame, the inside and the slot's window draw where the game's screens
 * put them; the colours go to a palette row the game has. */
static bool window_in_cells_is_drawable(Snes *s, bool inside_size)
{
    if (!menu_on_page_work_ram(s))
        return false;
    const GameState *g = state_of(s);
    vars_peek(true);
    MenuWindowSize size = size_in_cells(g);
    if (inside_size)
        size = (MenuWindowSize){ (uint8_t)(menu_text_work_window_columns(g) + 2), (uint8_t)(menu_text_work_window_rows(g) + 2) };
    bool fits = inside_size ? menu_text_work_window_columns(g) < SIZE_COLUMN_BITS && menu_text_work_window_rows(g) < SIZE_ROW_BITS
                            : true;
    uint8_t tile = (uint8_t)menu_window_style(g);
    bool ok = fits && size.columns >= 3 && size.rows >= 3 && tile + LAST_INSIDE_TILE <= 0xFF
              && reaches_only_buffers(menu_window_place(g), size.rows, size.columns * 2u);
    vars_peek(false);
    return ok;
}

bool menu_window_frame_is_drawable(Snes *s) { return window_in_cells_is_drawable(s, false); }
bool menu_window_inside_is_drawable(Snes *s) { return window_in_cells_is_drawable(s, true); }

/* Loading colours counts a palette change, a word wide in the original: a
 * count at 255 would carry into the next byte. The palette is sent at the
 * next vblank and the count cleared, so a screen's drawing counts a handful
 * of changes from a count near 0, far from the carry. */
enum { MOST_CHANGES_BEFORE_DRAWING = 0xF0 };

bool menu_palette_changes_have_room(const GameState *g) { return menu_palette_changed(g) < MOST_CHANGES_BEFORE_DRAWING; }

bool menu_palette_change_counts(Snes *s)
{
    vars_peek(true);
    bool ok = menu_palette_changes_have_room(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_window_colours_are_loadable(Snes *s) { return menu_on_page_work_ram(s) && menu_palette_change_counts(s); }

bool menu_window_in_design_is_drawable(Snes *s)
{
    if (!menu_on_page(s) || !menu_palette_change_counts(s))
        return false;
    const GameState *g = state_of(s);
    vars_peek(true);
    MenuWindowSize size = size_in_cells(g);
    uint8_t tile = (uint8_t)menu_design_style(g, 0);
    bool ok = size.columns >= 3 && size.rows >= 3 && tile + LAST_INSIDE_TILE <= 0xFF
              && reaches_only_buffers(menu_window_place(g), size.rows, size.columns * 2u);
    vars_peek(false);
    return ok;
}

/* The strips' entries: the place in X, the columns in Y, the tile in A. The
 * strip leaves X past it, which its callers read. */
// $C2:FB97
void menu_lay_out_label_strip(Snes *s)
{
    menu_lay_out_label(state_of(s), s->cpu.x, s->cpu.y, (uint8_t)s->cpu.a);
    m_rts(s);
}

// $C2:FBB4
void menu_lay_out_graphic_strip(Snes *s)
{
    s->cpu.x = menu_lay_out_strip(state_of(s), s->cpu.x, s->cpu.y, s->cpu.a);
    m_rts(s);
}

/* A strip the game lays out is of at least a column, in the buffers. */
bool menu_strip_is_placeable(Snes *s)
{
    uint16_t columns = s->cpu.y;
    return menu_on_page_work_ram(s) && columns >= 1
           && reaches_only_buffers(s->cpu.x, 2, (uint32_t)columns * MENU_TILE_BYTES);
}
