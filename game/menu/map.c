/* Menu mode: the map the sprite-object mode scrolls and redraws.
 *
 * The mode at $C2:0000 (game/menu/objects.c) shows a map in three planes. A
 * plane is 96 x 64 map tiles of 16 x 16 pixels, one byte each in work RAM;
 * the byte picks one of 256 tile groups, four background tiles laid out two
 * by two, also in work RAM; and the group's tiles go into a 64 x 32 tilemap
 * in VRAM, which the SNES holds as two 32 x 32 screens side by side. Three
 * words of bank $C2 say where a plane's groups, map and tilemap are.
 *
 * An object script drives it (game/menu/object_script.c's scroll and redraw
 * commands):
 *
 * - **Scrolling** ($C2:0568 sideways, $C2:066C up and down) moves the plane's
 *   scroll shadow one 8-pixel tile at a time, carrying its top-left map tile
 *   along, and each time a whole tile column or row comes uncovered it builds
 *   the strip of tilemap words that fills it and queues the strip for the next
 *   vblank. A strip is 32 words, built in the mode's upload buffer at
 *   $7E:F000, and the queue is the one menu_object_upload_vram_queue
 *   ($C2:0405) empties.
 * - **A redraw** ($C2:09C5) does the same work for the whole plane: eighteen
 *   blocks of sixteen map tiles, each block two tilemap columns, sent straight
 *   to VRAM rather than queued. The screen is blanked while it runs.
 *
 * The map bytes themselves are put there by the screen's own setup and by the
 * object scripts' write-a-map-byte command, so nothing here loads them.
 */
#include "menu/map.h"

#include "platform/video.h"
#include "port/machine.h"
#include "port/native.h"
#include "ram.h"

/* ---- the shape of a plane ------------------------------------------------------------------ */

enum {
    TILE_PIXELS = 8,            /* pixels across a background tile, and down it */
    MAP_COLUMNS = 0x60,         /* map tiles across a plane: one row of map bytes */
    MAP_ROWS = 0x40,            /* and down it */
    MAP_BYTES = MAP_COLUMNS * MAP_ROWS,
    TILE_COLUMNS = 2 * MAP_COLUMNS,   /* background tiles across it, two per map tile ... */
    TILE_ROWS = 2 * MAP_ROWS,         /* ... and down */
    MAP_WIDTH = TILE_COLUMNS * TILE_PIXELS,   /* pixels across it ... */
    MAP_HEIGHT = TILE_ROWS * TILE_PIXELS,     /* ... and down */
    TILEMAP_COLUMNS = 0x20,     /* background tiles across one tilemap screen ... */
    TILEMAP_ROWS = 0x20,        /* ... and down */
    TILEMAP_WIDTH = 2 * TILEMAP_COLUMNS * TILE_PIXELS,  /* pixels across both screens ... */
    TILEMAP_HEIGHT = TILEMAP_ROWS * TILE_PIXELS,        /* ... and down one */
    TILEMAP_SCREEN = 0x400,     /* words from one tilemap screen to the next */
    STRIP_WORDS = 0x20,         /* tilemap words in one strip: a whole tilemap column */
    STRIP_BYTES = 2 * STRIP_WORDS,
    COLUMN_TILES = 0x1E,        /* tiles a column strip fills ... */
    ROW_TILES = 0x22,           /* ... and a row strip */
    SCREEN_ROWS = 0x1C,         /* tiles down the screen, which a scroll looks ahead by */
    /* The bank the maps, the tile groups and the upload buffer are in. The
     * strip builders put the data bank there for their loop and put it back
     * after, and a caller can tell: the strip goes through a pointer the
     * caller chose, so one that lands on the stack takes the saved data bank
     * with it and the routine comes back with whatever was written over it. */
    MAP_BANK = 0x7E,
};

/* A tile group: the four background tiles of one map tile, as words. */
enum { GROUP_BYTES = 8, GROUP_TOP_LEFT = 0, GROUP_TOP_RIGHT = 2,
       GROUP_BOTTOM_LEFT = 4, GROUP_BOTTOM_RIGHT = 6 };

/* $C2:0556, $C2:055C and $C2:0562: for each plane, where its tile groups and
 * its map bytes are in work RAM, and where its tilemap is in VRAM. `plane` is
 * the plane number already doubled, as the original indexes the tables. */
enum { PLANE_TABLES = 0xC20556u, PLANE_TABLE_BYTES = 3 * 2 };  /* a word per plane */

static uint16_t plane_word(Snes *s, uint16_t table, uint16_t plane)
{
    return m_read16(s, (PLANE_TABLES + table * PLANE_TABLE_BYTES + plane) & 0xFFFFFFu);
}

static uint16_t menu_map_groups(Snes *s, uint16_t plane) { return plane_word(s, 0, plane); }
uint16_t menu_map_data(Snes *s, uint16_t plane) { return plane_word(s, 1, plane); }
static uint16_t menu_map_tilemap(Snes *s, uint16_t plane) { return plane_word(s, 2, plane); }

/* ---- the mode's work RAM ------------------------------------------------------------------- */

enum {
    /* Where each plane's top-left background tile is on the map, a word per
     * plane. The third plane's column is the first plane's row, and the third
     * plane's tile groups are the first plane's map, so only the first two
     * planes can be scrolled; the game only ever uses those. */
    MAP_COLUMN = (uint16_t)RAM_ABS(RAM_MENU_MAP_COLUMN),
    MAP_ROW = (uint16_t)RAM_ABS(RAM_MENU_MAP_ROW),
    /* The planes' scroll, a word per plane, which the vblank handler sends to
     * the background registers. */
    SCROLL_X = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_SCROLL_X),
    SCROLL_Y = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_SCROLL_Y),
    /* The next free bytes of the upload buffer, and the VRAM upload queue. */
    BUFFER = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_UPLOAD_BUFFER),
    QUEUE = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_VRAM_QUEUE),
    QUEUE_BUSY = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_VRAM_QUEUE_BUSY),
    QUEUE_END = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_VRAM_QUEUE_END),
};

/* A record of the queue: where the bytes come from, where they go and how the
 * VRAM port steps while they go. The bytes are the SNES settings the queue
 * carries; menu_object_upload_vram_queue decodes them. */
enum { QUEUE_SOURCE = 0, QUEUE_BANK = 2, QUEUE_VRAM = 3, QUEUE_SIZE = 5, QUEUE_PORT = 7,
       QUEUE_RECORD = 8 };
enum { PORT_COLUMN = 0x81,  /* stepping a whole tilemap row at a time, for a column strip */
       PORT_ROW = 0x80 };   /* stepping a word at a time, for a row strip */

/* Where in the mode's direct-page scratch ($00-$1B) the scrolling keeps its
 * working values. */
enum {
    CELL_PLANE = 0x00,      /* the plane the caller asks for, 1-3 */
    CELL_STEP = 0x01,       /* how far it moved, in whole tiles, signed */
    CELL_MAP = 0x02,        /* where the strip's map bytes start */
    CELL_AT = 0x04,         /* how far into the map the next one is */
    CELL_BUFFER = 0x06,     /* where in the upload buffer the strip is built */
    CELL_COLUMN = 0x08,     /* the map column, then the tile half, then the VRAM address */
    CELL_ROW = 0x0A,        /* the map row, then where the strip starts in the buffer */
    CELL_LEFT = 0x0C,       /* tiles left to scroll by */
    CELL_PLANE_INDEX = 0x0E, /* the plane number doubled, as the tables index it */
    CELL_GROUPS = 0x10,     /* where the plane's tile groups are */
    CELL_DATA = 0x13,       /* where its map is */
    CELL_TILEMAP = 0x16,    /* where its tilemap is in VRAM */
    CELL_TILES_LEFT = 0x19, /* tiles left in the strip, and a part of the row offset */
};

/* And the redraw, which walks the same map with cells of its own. */
enum {
    REDRAW_GROUPS_LEFT = 0x00, /* map tiles left in the block */
    REDRAW_BLOCKS = 0x02,      /* blocks left in the plane */
    REDRAW_AT = 0x04,          /* how far into the map the next tile is */
    REDRAW_OUT = 0x06,         /* where in the upload buffer its tiles go */
    REDRAW_COLUMN = 0x08,      /* the map column the block reads */
    REDRAW_ROW = 0x0A,         /* the map row it starts at */
    REDRAW_PIXEL_X = 0x0C,     /* the block's place on the plane, in pixels */
    REDRAW_PIXEL_Y = 0x0E,
    REDRAW_TILEMAP = 0x10,     /* where the plane's tilemap is in VRAM */
    REDRAW_VRAM = 0x12,        /* and where the block goes in it */
    REDRAW_GROUPS = 0x14,      /* where its tile groups are */
    REDRAW_DATA = 0x16,        /* where its map is */
    REDRAW_MAP = 0x18,         /* the map row the block reads, as a pointer */
    REDRAW_PART = 0x1A,        /* a part of the row offset */
};

bool menu_map_page_is_menu(Snes *s) { return s->cpu.d == RAM_PAGE_MENU; }
bool menu_map_page_is_io(Snes *s) { return m_io_bank((uint8_t)s->cpu.d); }

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* An address the original reaches through the data bank: a base plus an
 * offset, the sum carrying out of the bank as its absolute indexed and
 * indirect indexed forms do. */
static uint32_t through_bank(Snes *s, uint16_t base, uint16_t offset)
{
    return (m_abs(s, base) + offset) & 0xFFFFFFu;
}

/* The plane the routine is working on, which the original keeps in X while it
 * does, and its callers read back. */
static uint16_t plane_index(Snes *s) { return m_ldx(s, dp16(s, CELL_PLANE_INDEX)); }

/* The same through the direct page, where the sum stays in bank zero. */
static uint32_t through_page(Snes *s, uint8_t base, uint16_t offset)
{
    return (uint16_t)(s->cpu.d + base + offset);
}

/* ---- building a strip ---------------------------------------------------------------------- */

/* Where a map row starts, which is the row times the bytes in one. The
 * original swaps the row into the high byte, and adds a quarter of that to a
 * half of the quarter; it leaves the quarter in `part`, so the port does too. */
static uint16_t map_row_offset(Snes *s, uint16_t row, uint8_t part)
{
    uint16_t quarter = (uint16_t)((uint16_t)(row << 8 | row >> 8) >> 2);
    set_dp16(s, part, quarter);
    return (uint16_t)((quarter >> 1) + quarter + (quarter & 1));
}

/* The four tiles of the map tile `byte` names, in the plane's groups. */
static uint16_t group_of(uint16_t groups, uint8_t byte)
{
    return (uint16_t)(byte * GROUP_BYTES + groups);
}

// $C2:0811
/* Set a column strip up: which map tile it starts at, where it is built, which
 * of each group's two tile rows comes first (the map tile is two tiles tall,
 * so an odd row starts halfway down one), and where in the strip's 32 words
 * the top of the screen falls. */
static void start_column_strip(Snes *s)
{
    uint16_t plane = dp16(s, CELL_PLANE_INDEX);
    set_dp16(s, CELL_AT, map_row_offset(s, dp16(s, CELL_ROW), CELL_TILES_LEFT));
    set_dp16(s, CELL_MAP, (uint16_t)(dp16(s, CELL_DATA) + dp16(s, CELL_COLUMN)));
    set_dp16(s, CELL_BUFFER, m_read16(s, m_abs(s, BUFFER)));
    set_dp16(s, CELL_COLUMN, (m_read16(s, through_bank(s, MAP_ROW, plane)) & 1) ? 0 : 1);
    uint16_t top = (uint16_t)(m_read16(s, through_page(s, SCROLL_Y, plane)) - TILE_PIXELS);
    set_dp16(s, CELL_ROW, (uint16_t)(top & (TILEMAP_HEIGHT - 1) & ~(TILE_PIXELS - 1)) >> 2);
}

// $C2:08DB
/* The same for a row strip, which walks along a map row instead of down a
 * column and covers both of the strip's halves. */
static void start_row_strip(Snes *s)
{
    uint16_t plane = dp16(s, CELL_PLANE_INDEX);
    set_dp16(s, CELL_AT, dp16(s, CELL_COLUMN));
    set_dp16(s, CELL_MAP, (uint16_t)(map_row_offset(s, dp16(s, CELL_ROW), CELL_TILES_LEFT)
                                     + dp16(s, CELL_DATA)));
    set_dp16(s, CELL_BUFFER, m_read16(s, m_abs(s, BUFFER)));
    set_dp16(s, CELL_COLUMN, (m_read16(s, through_bank(s, MAP_COLUMN, plane)) & 1) ? 0 : 1);
    uint16_t left = (uint16_t)(m_read16(s, through_page(s, SCROLL_X, plane)) - TILE_PIXELS);
    set_dp16(s, CELL_ROW, (uint16_t)(left & (TILEMAP_WIDTH - 1) & ~(TILE_PIXELS - 1)) >> 2);
}

// $C2:0773 / $C2:07C2
/* Build the strip that fills an uncovered tile column: one tilemap word per
 * map tile down the column, taking the group's right-hand tiles rather than
 * its left ones where the column falls on the right half of a map tile, and
 * stepping to the next map row each time a group's pair is done. */
static void build_column_strip(Snes *s, bool right)
{
    start_column_strip(s);
    uint16_t top = right ? GROUP_TOP_RIGHT : GROUP_TOP_LEFT;
    uint16_t bottom = right ? GROUP_BOTTOM_RIGHT : GROUP_BOTTOM_LEFT;
    m_push8(s, s->cpu.db);
    s->cpu.db = MAP_BANK;
    set_dp16(s, CELL_TILES_LEFT, COLUMN_TILES);
    do {
        uint16_t at = dp16(s, CELL_AT);
        uint16_t group = group_of(dp16(s, CELL_GROUPS),
                                  m_read8(s, through_bank(s, dp16(s, CELL_MAP), at)));
        uint16_t out = dp16(s, CELL_ROW);
        bool second = (dp16(s, CELL_COLUMN) & 1) != 0;
        m_write16(s, through_bank(s, dp16(s, CELL_BUFFER), out),
                  m_read16(s, through_bank(s, (uint16_t)(second ? bottom : top), group)));
        if (second) {
            at = (uint16_t)(at + MAP_COLUMNS);
            set_dp16(s, CELL_AT, at < MAP_BYTES ? at : 0);
        }
        set_dp16(s, CELL_ROW, (uint16_t)(out + 2) & (STRIP_BYTES - 1));
        set_dp16(s, CELL_COLUMN, (uint16_t)(dp16(s, CELL_COLUMN) + 1));
        set_dp16(s, CELL_TILES_LEFT, (uint16_t)(dp16(s, CELL_TILES_LEFT) - 1));
    } while (dp16(s, CELL_TILES_LEFT) != 0);
    s->cpu.db = m_pull8(s);
}

// $C2:0843 / $C2:088F
/* And the strip for an uncovered tile row, which is two strips' worth of words
 * side by side: the group's bottom tiles rather than its top ones where the
 * row falls on the bottom half of a map tile. */
static void build_row_strip(Snes *s, bool bottom)
{
    start_row_strip(s);
    uint16_t left = bottom ? GROUP_BOTTOM_LEFT : GROUP_TOP_LEFT;
    uint16_t right = bottom ? GROUP_BOTTOM_RIGHT : GROUP_TOP_RIGHT;
    m_push8(s, s->cpu.db);
    s->cpu.db = MAP_BANK;
    set_dp16(s, CELL_TILES_LEFT, ROW_TILES);
    do {
        uint16_t at = dp16(s, CELL_AT);
        uint16_t group = group_of(dp16(s, CELL_GROUPS),
                                  m_read8(s, through_bank(s, dp16(s, CELL_MAP), at)));
        uint16_t out = dp16(s, CELL_ROW);
        bool second = (dp16(s, CELL_COLUMN) & 1) != 0;
        m_write16(s, through_bank(s, dp16(s, CELL_BUFFER), out),
                  m_read16(s, through_bank(s, (uint16_t)(second ? right : left), group)));
        if (second) {
            at = (uint16_t)(at + 1);
            set_dp16(s, CELL_AT, at < MAP_COLUMNS ? at : 0);
        }
        set_dp16(s, CELL_ROW, (uint16_t)(out + 2) & (2 * STRIP_BYTES - 1));
        set_dp16(s, CELL_COLUMN, (uint16_t)(dp16(s, CELL_COLUMN) + 1));
        set_dp16(s, CELL_TILES_LEFT, (uint16_t)(dp16(s, CELL_TILES_LEFT) - 1));
    } while (dp16(s, CELL_TILES_LEFT) != 0);
    s->cpu.db = m_pull8(s);
}

/* ---- queueing a strip ---------------------------------------------------------------------- */

/* A field of the record `at` bytes into the queue. The two records a row strip
 * takes are one set of fields `QUEUE_RECORD` further on, as the original
 * addresses them: the offset is in the field, not in `at`, so a queue that has
 * wrapped past `$FF` records doesn't fold the second onto the first. */
static uint32_t queue_at(Snes *s, uint8_t at, uint8_t field)
{
    return (m_abs(s, (uint16_t)(QUEUE + field)) + at) & 0xFFFFFFu;
}

/* The vblank handler leaves the queue alone while this is set; the map puts it
 * back to zero rather than counting its holds back down. */
static uint8_t hold_queue(Snes *s)
{
    uint32_t busy = m_abs(s, QUEUE_BUSY);
    m_write8(s, busy, (uint8_t)(m_read8(s, busy) + 1));
    return m_read8(s, m_abs(s, QUEUE_END));
}

static void release_queue(Snes *s, uint8_t end)
{
    m_write8(s, m_abs(s, QUEUE_END), end);
    m_write8(s, m_abs(s, QUEUE_BUSY), 0);
}

/* Strips are taken from the upload buffer one after another, so each one moves
 * it on; menu_object_upload_vram_queue puts it back once the queue is out. */
static void take_buffer(Snes *s, uint16_t bytes)
{
    uint32_t at = m_abs(s, BUFFER);
    m_write16(s, at, (uint16_t)(m_read16(s, at) + bytes));
}

// $C2:090D
/* Queue the column strip: it goes to the plane's tilemap at the tile column
 * the scroll has reached, in whichever of the tilemap's two screens that falls
 * in, with the port stepping a tilemap row at a time. */
static void queue_column_strip(Snes *s)
{
    uint16_t column = (uint16_t)(dp16(s, CELL_COLUMN) / TILE_PIXELS) & (2 * TILEMAP_COLUMNS - 1);
    if (column >= TILEMAP_COLUMNS)
        column = (uint16_t)(column + TILEMAP_SCREEN - TILEMAP_COLUMNS);
    uint16_t vram = (uint16_t)(column + dp16(s, CELL_TILEMAP));
    set_dp16(s, CELL_COLUMN, vram);
    uint8_t at = hold_queue(s);
    m_write8(s, queue_at(s, at, QUEUE_BANK), MAP_BANK);
    m_write8(s, queue_at(s, at, QUEUE_PORT), PORT_COLUMN);
    m_write16(s, queue_at(s, at, QUEUE_SOURCE), m_read16(s, m_abs(s, BUFFER)));
    m_write16(s, queue_at(s, at, QUEUE_VRAM), vram);
    m_write16(s, queue_at(s, at, QUEUE_SIZE), STRIP_BYTES);
    release_queue(s, (uint8_t)(at + QUEUE_RECORD));
    take_buffer(s, STRIP_BYTES);
    m_ldx(s, at);
}

// $C2:0961
/* Queue the row strip, which takes two records: the tilemap's left screen from
 * the strip's first half and its right screen from the second, both with the
 * port stepping a word at a time. */
static void queue_row_strip(Snes *s)
{
    uint16_t row = (uint16_t)((dp16(s, CELL_ROW) & (TILEMAP_HEIGHT - 1) & ~(TILE_PIXELS - 1)) << 2);
    uint16_t vram = (uint16_t)(row + dp16(s, CELL_TILEMAP));
    set_dp16(s, CELL_COLUMN, vram);
    set_dp16(s, CELL_ROW, (uint16_t)(vram + TILEMAP_SCREEN));
    uint8_t at = hold_queue(s);
    enum { NEXT = QUEUE_RECORD };
    m_write8(s, queue_at(s, at, QUEUE_BANK), MAP_BANK);
    m_write8(s, queue_at(s, at, QUEUE_BANK + NEXT), MAP_BANK);
    m_write8(s, queue_at(s, at, QUEUE_PORT), PORT_ROW);
    m_write8(s, queue_at(s, at, QUEUE_PORT + NEXT), PORT_ROW);
    m_write16(s, queue_at(s, at, QUEUE_SIZE), STRIP_BYTES);
    m_write16(s, queue_at(s, at, QUEUE_SIZE + NEXT), STRIP_BYTES);
    uint16_t source = m_read16(s, m_abs(s, BUFFER));
    m_write16(s, queue_at(s, at, QUEUE_SOURCE), source);
    m_write16(s, queue_at(s, at, QUEUE_SOURCE + NEXT), (uint16_t)(source + STRIP_BYTES));
    m_write16(s, queue_at(s, at, QUEUE_VRAM), vram);
    m_write16(s, queue_at(s, at, QUEUE_VRAM + NEXT), (uint16_t)(vram + TILEMAP_SCREEN));
    release_queue(s, (uint8_t)(at + 2 * QUEUE_RECORD));
    take_buffer(s, 2 * STRIP_BYTES);
    m_ldx(s, at);
}

/* ---- scrolling ----------------------------------------------------------------------------- */

/* The plane's top-left tile, which the scroll carries with it. Both wrap round
 * the map rather than stopping at its edge. */
static uint16_t back_one(uint16_t at, uint16_t wrap)
{
    at = (uint16_t)(at - 1);
    return (at & 0x8000u) ? (uint16_t)(wrap - 1) : at;
}

static uint16_t on_one(uint16_t at, uint16_t wrap)
{
    at = (uint16_t)(at + 1);
    return at < wrap ? at : 0;
}

// $C2:059D
/* Scroll the plane left by `tiles` tiles, filling in the column that comes
 * uncovered on the left each time the scroll crosses a tile boundary. */
static void scroll_left(Snes *s, uint16_t tiles)
{
    set_dp16(s, CELL_LEFT, tiles);
    do {
        uint16_t plane = plane_index(s);
        uint32_t column = through_bank(s, MAP_COLUMN, plane);
        uint32_t scroll_at = through_page(s, SCROLL_X, plane);
        if ((m_read16(s, scroll_at) & (TILE_PIXELS - 1)) == 0)
            m_write16(s, column, back_one(m_read16(s, column), TILE_COLUMNS));
        uint16_t scroll = back_one(m_read16(s, scroll_at), MAP_WIDTH);
        m_write16(s, scroll_at, scroll);
        if ((scroll & (TILE_PIXELS - 1)) == 0) {
            set_dp16(s, CELL_COLUMN, back_one(m_read16(s, column), TILE_COLUMNS) >> 1);
            set_dp16(s, CELL_ROW,
                     back_one(m_read16(s, through_bank(s, MAP_ROW, plane)), TILE_ROWS) >> 1);
            build_column_strip(s, (m_read16(s, scroll_at) & TILE_PIXELS) == 0);
            plane = plane_index(s);
            set_dp16(s, CELL_COLUMN,
                     (uint16_t)(m_read16(s, through_page(s, SCROLL_X, plane)) - TILE_PIXELS));
            queue_column_strip(s);
        }
        set_dp16(s, CELL_LEFT, (uint16_t)(dp16(s, CELL_LEFT) - 1));
    } while (dp16(s, CELL_LEFT) != 0);
}

// $C2:0607
/* And right, where the column that comes uncovered is the one a screen's width
 * further on. */
static void scroll_right(Snes *s, uint16_t tiles)
{
    set_dp16(s, CELL_LEFT, tiles);
    do {
        uint16_t plane = plane_index(s);
        uint32_t column = through_bank(s, MAP_COLUMN, plane);
        uint32_t scroll_at = through_page(s, SCROLL_X, plane);
        uint16_t scroll = on_one(m_read16(s, scroll_at), MAP_WIDTH);
        m_write16(s, scroll_at, scroll);
        if ((scroll & (TILE_PIXELS - 1)) == 0) {
            uint16_t ahead = on_one(m_read16(s, column), TILE_COLUMNS);
            m_write16(s, column, ahead);
            ahead = (uint16_t)(ahead + TILEMAP_COLUMNS);
            if (ahead >= TILE_COLUMNS)
                ahead = (uint16_t)(ahead - TILE_COLUMNS);
            set_dp16(s, CELL_COLUMN, ahead >> 1);
            set_dp16(s, CELL_ROW,
                     back_one(m_read16(s, through_bank(s, MAP_ROW, plane)), TILE_ROWS) >> 1);
            build_column_strip(s, (m_read16(s, scroll_at) & TILE_PIXELS) != 0);
            plane = plane_index(s);
            set_dp16(s, CELL_COLUMN,
                     (uint16_t)(m_read16(s, through_page(s, SCROLL_X, plane))
                                + TILEMAP_COLUMNS * TILE_PIXELS));
            queue_column_strip(s);
        }
        set_dp16(s, CELL_LEFT, (uint16_t)(dp16(s, CELL_LEFT) - 1));
    } while (dp16(s, CELL_LEFT) != 0);
}

// $C2:06A1
/* Scroll the plane up, filling in the row that comes uncovered at the top. */
static void scroll_up(Snes *s, uint16_t tiles)
{
    set_dp16(s, CELL_LEFT, tiles);
    do {
        uint16_t plane = plane_index(s);
        uint32_t row = through_bank(s, MAP_ROW, plane);
        uint32_t scroll_at = through_page(s, SCROLL_Y, plane);
        if ((m_read16(s, scroll_at) & (TILE_PIXELS - 1)) == 0)
            m_write16(s, row, back_one(m_read16(s, row), TILE_ROWS));
        uint16_t scroll = back_one(m_read16(s, scroll_at), MAP_HEIGHT);
        m_write16(s, scroll_at, scroll);
        if ((scroll & (TILE_PIXELS - 1)) == 0) {
            set_dp16(s, CELL_COLUMN,
                     back_one(m_read16(s, through_bank(s, MAP_COLUMN, plane)), TILE_COLUMNS) >> 1);
            set_dp16(s, CELL_ROW, back_one(m_read16(s, row), TILE_ROWS) >> 1);
            build_row_strip(s, (m_read16(s, scroll_at) & TILE_PIXELS) == 0);
            plane = plane_index(s);
            set_dp16(s, CELL_ROW,
                     (uint16_t)(m_read16(s, through_page(s, SCROLL_Y, plane)) - TILE_PIXELS));
            queue_row_strip(s);
        }
        set_dp16(s, CELL_LEFT, (uint16_t)(dp16(s, CELL_LEFT) - 1));
    } while (dp16(s, CELL_LEFT) != 0);
}

// $C2:070B
/* And down, where the row that comes uncovered is a screen's height further
 * on. */
static void scroll_down(Snes *s, uint16_t tiles)
{
    set_dp16(s, CELL_LEFT, tiles);
    do {
        uint16_t plane = plane_index(s);
        uint32_t row = through_bank(s, MAP_ROW, plane);
        uint32_t scroll_at = through_page(s, SCROLL_Y, plane);
        uint16_t scroll = on_one(m_read16(s, scroll_at), MAP_HEIGHT);
        m_write16(s, scroll_at, scroll);
        if ((scroll & (TILE_PIXELS - 1)) == 0) {
            m_write16(s, row, on_one(m_read16(s, row), TILE_ROWS));
            set_dp16(s, CELL_COLUMN,
                     back_one(m_read16(s, through_bank(s, MAP_COLUMN, plane)), TILE_COLUMNS) >> 1);
            uint16_t ahead = (uint16_t)(m_read16(s, row) + SCREEN_ROWS);
            if (ahead >= TILE_ROWS)
                ahead = (uint16_t)(ahead - TILE_ROWS);
            set_dp16(s, CELL_ROW, ahead >> 1);
            build_row_strip(s, (m_read16(s, scroll_at) & TILE_PIXELS) != 0);
            plane = plane_index(s);
            set_dp16(s, CELL_ROW,
                     (uint16_t)(m_read16(s, through_page(s, SCROLL_Y, plane))
                                + SCREEN_ROWS * TILE_PIXELS));
            queue_row_strip(s);
        }
        set_dp16(s, CELL_LEFT, (uint16_t)(dp16(s, CELL_LEFT) - 1));
    } while (dp16(s, CELL_LEFT) != 0);
}

/* The plane a scroll or redraw names, 1-3, and its tables. Zero means the
 * caller asked for a plane that doesn't exist, which the original answers by
 * spinning; the ports hand that address back to the interpreter instead. */
static uint16_t scroll_plane(Snes *s)
{
    uint16_t plane = dp16(s, CELL_PLANE) & 3;
    if (plane == 0)
        return 0;
    uint16_t index = (uint16_t)((plane - 1) << 1);
    set_dp16(s, CELL_PLANE_INDEX, index);
    m_ldx(s, index);
    set_dp16(s, CELL_GROUPS, menu_map_groups(s, index));
    set_dp16(s, CELL_DATA, menu_map_data(s, index));
    set_dp16(s, CELL_TILEMAP, m_lda16(s, menu_map_tilemap(s, index)));
    return plane;
}

/* What the scroll routines leave for their callers: an 8-bit accumulator, with
 * the step in it where they took one (above whatever the last word they read
 * left in its high byte, which nothing reads). */
static void scroll_return(Snes *s, uint8_t step)
{
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, step);
    m_rts(s);
}

/* How many tiles a negative step moves, which the original works out by
 * complementing the byte inside a word of ones. */
static uint16_t step_back(uint8_t step) { return (uint16_t)(0x100u - step); }

/* The wait the original makes of a plane number it can't use. */
static void refuse_plane(Snes *s, uint32_t at)
{
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, 0);
    m_stop_at_own(s, at, 0);
}

// $C2:0568
/* Scroll a plane sideways by the whole tiles the caller's move covered. */
void menu_map_scroll_x(Snes *s)
{
    if (scroll_plane(s) == 0) {
        refuse_plane(s, 0xC2059Bu);
        return;
    }
    uint8_t step = dp8(s, CELL_STEP);
    if (step == 0) {
        scroll_return(s, 0);
        return;
    }
    if (step & 0x80)
        scroll_left(s, step_back(step));
    else
        scroll_right(s, step);
    scroll_return(s, step);
}

// $C2:066C
/* And up or down, the same way. */
void menu_map_scroll_y(Snes *s)
{
    if (scroll_plane(s) == 0) {
        refuse_plane(s, 0xC2069Fu);
        return;
    }
    uint8_t step = dp8(s, CELL_STEP);
    if (step == 0) {
        scroll_return(s, 0);
        return;
    }
    if (step & 0x80)
        scroll_up(s, step_back(step));
    else
        scroll_down(s, step);
    scroll_return(s, step);
}

// $C2:2720
/* Put the map's top-left tile where the party is: sixteen tiles left of the
 * party's tile and fourteen above it, wrapping round the map. The first two
 * planes, which are the ones that scroll, get the same tile. */
void menu_map_centre_on_party(Snes *s)
{
    enum { HALF_WIDTH = 0x10, HALF_HEIGHT = 0x0E };
    uint16_t column = (uint16_t)((m_read16(s, m_abs(s, RAM_ABS(RAM_LOCATION_X))) & 0x00FF)
                                 - HALF_WIDTH);
    if (column & 0x8000u)
        column = (uint16_t)(column + TILE_COLUMNS);
    m_write16(s, m_dp(s, MAP_COLUMN), column);
    m_write16(s, m_dp(s, MAP_COLUMN + RAM_MENU_MAP_COLUMN_STRIDE), column);

    uint16_t row = (uint16_t)((m_read16(s, m_abs(s, RAM_ABS(RAM_LOCATION_Y))) & 0x00FF)
                              - HALF_HEIGHT);
    if (row & 0x8000u)
        row = (uint16_t)(row + TILE_ROWS);
    m_write16(s, m_dp(s, MAP_ROW), row);
    m_write16(s, m_dp(s, MAP_ROW + RAM_MENU_MAP_ROW_STRIDE), row);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* ---- redrawing a whole plane --------------------------------------------------------------- */

// $C2:0AAE
/* Put one map tile's four tiles in the upload buffer: the left-hand pair goes
 * in this block's first tilemap column and the right-hand pair in its second,
 * a strip further on. */
static void put_tile(Snes *s, uint16_t buffer, uint16_t out, uint16_t group, uint16_t tile)
{
    m_write16(s, (m_abs(s, buffer) + out) & 0xFFFFFFu,
              m_read16(s, (m_abs(s, tile) + group) & 0xFFFFFFu));
}

static void place_group(Snes *s)
{
    uint16_t group = group_of(dp16(s, REDRAW_GROUPS),
                              m_read8(s, (m_abs(s, dp16(s, REDRAW_MAP)) + dp16(s, REDRAW_AT))
                                             & 0xFFFFFFu));
    uint16_t buffer = m_read16(s, m_dp(s, RAM_MENU_OBJECT_UPLOAD_BUFFER_DP));
    uint16_t out = dp16(s, REDRAW_OUT);
    put_tile(s, buffer, out, group, GROUP_TOP_LEFT);
    put_tile(s, buffer, (uint16_t)(out + 2), group, GROUP_BOTTOM_LEFT);
    out = (uint16_t)(out + STRIP_BYTES);   /* a whole strip on: the block's second column */
    put_tile(s, buffer, out, group, GROUP_TOP_RIGHT);
    out = (uint16_t)(out + 2);
    put_tile(s, buffer, out, group, GROUP_BOTTOM_RIGHT);
    m_ldy(s, out);   /* the original leaves the last offset in Y */
}

// $C2:0A6C
/* Build one block of the redraw: sixteen map tiles down a map column, which is
 * the two tilemap columns the block covers, filled from the top of the screen
 * round the tilemap's 32 rows. */
static void build_block(Snes *s)
{
    set_dp16(s, REDRAW_GROUPS_LEFT, 0x10);
    set_dp16(s, REDRAW_MAP, (uint16_t)(dp16(s, REDRAW_COLUMN) + dp16(s, REDRAW_DATA)));
    set_dp16(s, REDRAW_AT, map_row_offset(s, dp16(s, REDRAW_ROW), REDRAW_PART));
    uint16_t line = dp16(s, REDRAW_PIXEL_Y) & (TILEMAP_HEIGHT - 1);
    set_dp16(s, REDRAW_OUT, (uint16_t)(line & ~0xFu) >> 2);
    do {
        place_group(s);
        uint16_t at = (uint16_t)(dp16(s, REDRAW_AT) + MAP_COLUMNS);
        set_dp16(s, REDRAW_AT, at < MAP_BYTES ? at : 0);
        set_dp16(s, REDRAW_OUT, (uint16_t)(dp16(s, REDRAW_OUT) + 4) & (STRIP_BYTES - 1));
        set_dp16(s, REDRAW_GROUPS_LEFT, (uint16_t)(dp16(s, REDRAW_GROUPS_LEFT) - 1));
    } while (dp16(s, REDRAW_GROUPS_LEFT) != 0);
}

// $C2:0ADC
/* Send a block's two tilemap columns to VRAM: the word address is the plane's
 * tilemap plus the tile column, wrapped into whichever of its two screens that
 * falls in, and each column is a strip of the upload buffer sent with the port
 * stepping a tilemap row at a time. Only a redraw uses it, with the screen
 * blanked. */
static void upload_block(Snes *s)
{
    Platform *pf = m_platform(s);
    static const VidVramPort column_port = { .step = 32, .after_high = true };
    /* STX $2116 : STA $2115 : STA $4370 : STA $4371 : STX $4372 : STA $4374 :
     * STX $4375 : STA $420B */
    static const uint8_t first_steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    /* STA $2116 : STA $4372 : STA $4374 : STA $2115 : STA $4370 : STA $4371 :
     * STX $4375 : STA $420B */
    static const uint8_t second_steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_VRAM_PORT, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    enum { MENU_DMA = 7, DMA_WORDS = 0x01 };

    uint16_t column = dp16(s, REDRAW_PIXEL_X) / TILE_PIXELS;
    if (column >= TILEMAP_COLUMNS)
        column = (uint16_t)(column + TILEMAP_SCREEN - TILEMAP_COLUMNS);
    uint16_t vram = (uint16_t)(column + dp16(s, REDRAW_TILEMAP));
    set_dp16(s, REDRAW_VRAM, vram);

    uint16_t source = m_read16(s, m_dp(s, RAM_MENU_OBJECT_UPLOAD_BUFFER_DP));
    uint8_t bank = m_read8(s, m_dp(s, RAM_MENU_OBJECT_UPLOAD_BUFFER_DP + 2));
    const VidSnesXfer first = { MENU_DMA, DMA_WORDS, first_steps, &column_port };
    vid_upload_vram(pf, PF_AT(0xC20AF6), vram, PF_GAME((uint32_t)bank << 16 | source),
                    STRIP_BYTES, &first);
    const VidSnesXfer second = { MENU_DMA, DMA_WORDS, second_steps, &column_port };
    vid_upload_vram(pf, PF_AT(0xC20B22), (uint16_t)(vram + 1),
                    PF_GAME((uint32_t)bank << 16 | (uint16_t)(source + STRIP_BYTES)),
                    STRIP_BYTES, &second);
    m_ldx(s, STRIP_BYTES);   /* the size, which the callers read back */
}

void menu_map_upload_block(Snes *s)
{
    upload_block(s);
    m_rts(s);
}

// $C2:09C5
/* Redraw a whole plane: put its scroll where its top-left tile says, then walk
 * eighteen blocks of sixteen map tiles across it, building each in the upload
 * buffer and sending it straight to VRAM. */
void menu_map_redraw(Snes *s)
{
    /* The data bank goes on the map for the whole pass, and the byte it pushes
     * is still on the stack where the port hands back a plane it can't use. */
    m_push8(s, s->cpu.db);
    s->cpu.db = MAP_BANK;

    uint16_t plane = dp16(s, CELL_PLANE) & 3;
    if (plane == 0) {
        refuse_plane(s, 0xC20A6Au);
        return;
    }
    uint16_t index = (uint16_t)((plane - 1) << 1);
    uint16_t column = m_read16(s, through_page(s, MAP_COLUMN, index));
    uint16_t row = m_read16(s, through_page(s, MAP_ROW, index));
    m_write16(s, through_page(s, SCROLL_X, index), (uint16_t)(column * TILE_PIXELS));
    m_write16(s, through_page(s, SCROLL_Y, index), (uint16_t)(row * TILE_PIXELS));
    set_dp16(s, REDRAW_GROUPS, menu_map_groups(s, index));
    set_dp16(s, REDRAW_DATA, menu_map_data(s, index));
    set_dp16(s, REDRAW_TILEMAP, menu_map_tilemap(s, index));

    /* The block the screen's left edge falls in, and the map tile it reads:
     * one back where the edge sits on a map tile's left half. */
    set_dp16(s, REDRAW_COLUMN, (column & 1) ? column >> 1 : back_one(column >> 1, MAP_COLUMNS));
    set_dp16(s, REDRAW_ROW, (row & 1) ? row >> 1 : back_one(row >> 1, MAP_ROWS));
    uint16_t scroll_x = m_read16(s, through_page(s, SCROLL_X, index));
    uint16_t scroll_y = m_read16(s, through_page(s, SCROLL_Y, index));
    uint16_t back_x = (scroll_x & TILE_PIXELS) ? TILE_PIXELS : 2 * TILE_PIXELS;
    uint16_t back_y = (scroll_y & TILE_PIXELS) ? TILE_PIXELS : 2 * TILE_PIXELS;
    set_dp16(s, REDRAW_PIXEL_X, (uint16_t)(scroll_x - back_x) & (TILEMAP_WIDTH - 1));
    set_dp16(s, REDRAW_PIXEL_Y, (uint16_t)(scroll_y - back_y) & (TILEMAP_HEIGHT - 1));

    set_dp16(s, REDRAW_BLOCKS, 0x12);
    do {
        build_block(s);
        NATIVE_TIME_AS(s, 0xC20ADCu, upload_block(s));
        set_dp16(s, REDRAW_COLUMN, on_one(dp16(s, REDRAW_COLUMN), MAP_COLUMNS));
        set_dp16(s, REDRAW_PIXEL_X,
                 (uint16_t)(dp16(s, REDRAW_PIXEL_X) + 0x10) & (TILEMAP_WIDTH - 1));
        set_dp16(s, REDRAW_BLOCKS, (uint16_t)(dp16(s, REDRAW_BLOCKS) - 1));
    } while (dp16(s, REDRAW_BLOCKS) != 0);

    m_sep(s, CPU_FLAG_M);
    s->cpu.db = m_pull8(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}
