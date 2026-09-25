/* A script changing the map (events $E4 now, and $E5 through the field's
 * main loop): a rectangle of the map's tiles copied over another place -- a
 * door that opens, a bridge that appears, a wall that falls.
 *
 * The copy the script describes (field_tile_copy) names the rectangle by its
 * top left and bottom right tiles, where it goes by its top left, and which
 * of the map's tables it takes (field/layers.h): each layer's tiles, the
 * tiles' properties, their upper and their lower attributes -- or of the
 * properties only the bit that says which half of the tile set a layer's
 * tile comes from, BG1's or BG2's, which moves with the layer's tiles where
 * the rest of the properties stay.
 *
 * It copies a row at a time, top to bottom and left to right, a tile at a
 * time, as the original's block moves do: a rectangle copied onto itself a
 * little right or down repeats its first tiles. */
#include "field/layers.h"

#include "field/bridge.h"
#include "field/field.h"
#include "field/object.h"
#include "field/parts.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "vars.h"

/* The properties' bits that say which half of its tile set a layer's tile
 * comes from. */
enum { BG1_TILE_SET_HALF = 0x01, BG2_TILE_SET_HALF = 0x02 };

static uint8_t table_byte(const GameState *g, MapTable table, MapPlace p)
{
    switch (table) {
    case MAP_PROPERTIES: return field_tile_properties(g, p.row, p.column);
    case MAP_UPPER_ATTRIBUTES: return field_tile_upper_attributes(g, p.row, p.column);
    case MAP_LOWER_ATTRIBUTES: return field_tile_lower_attributes(g, p.row, p.column);
    default: return field_layer_maps(g, (unsigned)table, p.row, p.column);
    }
}

static void set_table_byte(GameState *g, MapTable table, MapPlace p, uint8_t v)
{
    switch (table) {
    case MAP_PROPERTIES: set_field_tile_properties(g, p.row, p.column, v); break;
    case MAP_UPPER_ATTRIBUTES: set_field_tile_upper_attributes(g, p.row, p.column, v); break;
    case MAP_LOWER_ATTRIBUTES: set_field_tile_lower_attributes(g, p.row, p.column, v); break;
    default: set_field_layer_maps(g, (unsigned)table, p.row, p.column, v); break;
    }
}

/* ---- The copy described -------------------------------------------------------------------- */

uint16_t field_map_table_address(MapTable table)
{
    static const uint16_t address[MAP_TABLES] = { 0x3000, 0x3040, 0x3080, 0x30C0, 0x7000, 0x7040 };
    return address[table];
}

TileRectangle field_described_tile_copy(const GameState *g)
{
    MapPlace from = map_place_at(field_tile_copy_from(g)), last = map_place_at(field_tile_copy_to(g));
    return (TileRectangle){ from, (uint8_t)(last.column - from.column), (uint8_t)(last.row - from.row),
                            map_place_at(field_tile_copy_destination(g)) };
}

static MapPlace offset_by(MapPlace p, unsigned row, unsigned column)
{
    return (MapPlace){ (uint8_t)(p.row + row), (uint8_t)(p.column + column) };
}

/* ---- What the original leaves (field/scratch.h) -------------------------------------------- */

/* A whole table's copy leaves its width, the rows it counted down, the
 * columns it started each row at, offset by where the original keeps the
 * table, and where that is; a bit's copy the last bit it took, and the columns within
 * the row. */
static void leave_table_copied(GameState *g, TileRectangle r, MapTable table)
{
    uint16_t base = field_map_table_address(table);
    original_cell16(g, 0xDB, r.columns);
    original_cell16(g, 0xDD, 0);
    original_cell16(g, 0xDF, (uint8_t)(r.from.column + base));
    original_cell16(g, 0xE1, (uint8_t)(r.to.column + base));
    /* the table's address, in the cells the direction routine keeps its
     * own tile in */
    set_field_own_tile_x(g, (uint8_t)base);
    set_field_own_tile_y(g, (uint8_t)(base >> 8));
}

static void leave_bit_copied(GameState *g, TileRectangle r, uint8_t bit)
{
    MapPlace last = offset_by(r.from, r.rows, r.columns);
    original_cell8(g, 0xD9, (uint8_t)(field_tile_properties(g, last.row, last.column) & bit));
    original_cell16(g, 0xDB, r.columns);
    original_cell16(g, 0xDD, 0);
    original_cell16(g, 0xDF, r.from.column);
    original_cell16(g, 0xE1, r.to.column);
    original_cell16(g, 0xE3, 0);
}

/* ---- Copying ------------------------------------------------------------------------------- */

static void copy_table(GameState *g, TileRectangle r, MapTable table)
{
    for (unsigned row = 0; row <= r.rows; row++)
        for (unsigned column = 0; column <= r.columns; column++)
            set_table_byte(g, table, offset_by(r.to, row, column),
                           table_byte(g, table, offset_by(r.from, row, column)));
    ORIGINAL_EFFECT(leave_table_copied(g, r, table));
}

static void copy_property_bit(GameState *g, TileRectangle r, uint8_t bit)
{
    for (unsigned row = 0; row <= r.rows; row++) {
        for (unsigned column = 0; column <= r.columns; column++) {
            MapPlace from = offset_by(r.from, row, column), to = offset_by(r.to, row, column);
            uint8_t there = field_tile_properties(g, to.row, to.column);
            set_field_tile_properties(g, to.row, to.column,
                                      (uint8_t)((there & ~bit) | (field_tile_properties(g, from.row, from.column) & bit)));
        }
    }
    ORIGINAL_EFFECT(leave_bit_copied(g, r, bit));
}

// $C0:AF4E, and its table copy $C0:B096
/* Make the copy the script described, table by table. */
void map_copy_tiles(GameState *g)
{
    uint8_t maps = field_tile_copy_maps(g);
    TileRectangle r = field_described_tile_copy(g);
    for (MapTable table = MAP_BG1_TILES; table < MAP_TABLES; table++)
        if (maps & 1u << table)
            copy_table(g, r, table);
    if (maps & COPY_BG1_TILE_SET_HALF)
        copy_property_bit(g, r, BG1_TILE_SET_HALF);
    if (maps & COPY_BG2_TILE_SET_HALF)
        copy_property_bit(g, r, BG2_TILE_SET_HALF);
}

bool map_tile_copy_fits(uint16_t from, uint16_t to, uint16_t destination)
{
    MapPlace first = map_place_at(from), last = map_place_at(to), there = map_place_at(destination);
    enum { COLUMNS = FIELD_TILE_PROPERTIES_COLUMNS, ROWS = FIELD_TILE_PROPERTIES_ROWS };
    return last.column >= first.column && last.row >= first.row && last.column < COLUMNS && last.row < ROWS &&
           there.column + (last.column - first.column) < COLUMNS && there.row + (last.row - first.row) < ROWS;
}

/* ---- The entry -------------------------------------------------------------------------------- */

/* The main loop's copy ($C0:1790) and event $E4's call it on the field's page
 * with 16-bit index registers; it leaves Y and the overflow as its last
 * table's copy does (field/leaves.c). */
void field_copy_tiles(Snes *s)
{
    GameState *g = state_of(s);
    map_copy_tiles(g);
    s->cpu.y = field_tile_copy_y(g, s->cpu.y);
    m_set_flag(s, CPU_FLAG_V, field_tile_copy_overflow(g, m_flag(s, CPU_FLAG_V)));
    m_rts(s);
}

/* Its guard: the rectangle and where it goes are on the map (past its edges
 * the original's block moves run on into the next table), and the data bank
 * reaches low RAM. */
bool field_tile_copy_fits(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = map_tile_copy_fits(field_tile_copy_from(g), field_tile_copy_to(g), field_tile_copy_destination(g));
    vars_peek(false);
    return ok;
}
