/* The game's packed maps, tilemaps and other packed resources that aren't
 * graphics, as text a person can edit (frontend/decoded.h), so an edited map
 * reaches the game through the same accessors as the cartridge's
 * (docs/PLATFORM.md, "Maps and tilemaps").
 *
 * Every resource is packed (frontend/packed.h): it says how its stream packs
 * first, is written unpacked as a grid of cells, and is packed again as it is
 * read. The catalogue's codec (re/assets.tsv) says what a cell is and how
 * many go across:
 *
 *   lz:field-map    a location's map (game/field/map.c): its header, then
 *                   each layer's map, a byte a 16x16 tile naming one of the
 *                   tile set's looks, then a cell of attributes a tile
 *   lz:looks        a tile set's looks: a 16x16 tile's four 8x8 tilemap
 *                   entries, two by two, eight looks a row
 *   lz:tilemap:N    tilemap entries, N a row
 *   lz:bytes:N      bytes in hex, N a row
 *   lz:nibbles:N    a hex digit a cell, N a row, two to a byte, the high first
 *   lz:colors:N     BGR555 colors, N a row
 *   lz, lz:event    bytes in hex, sixteen a row (scripts and lists the game
 *                   unpacks, whose own text is still to come: issue #130)
 *
 * A tilemap entry is `TTT:P:hvp`: the tile's number (three hex digits), its
 * palette, and `h`, `v` and `p` where it is flipped across, flipped down and
 * in front, `-` where not. A color is `RR.GG.BB`, red, green and blue 0-31,
 * with `+` after it where the word's unused top bit is set.
 *
 *   [field_packed_map.0]
 *   packed short
 *   bg1 32x32
 *   bg2 32x32 still drift 00
 *   bg3 16x16 none drift 00
 *   screens 13
 *   color-math 00
 *   map bg1
 *   00 01 02 ...
 *   map bg2
 *   ...
 *   attributes
 *   000000 000040 ...
 *
 * A map's header gives each layer's size (16 to 64 tiles each way by 16),
 * whether BG3 has a map (`none` where not), whether BG2 moves at half the
 * screen's pace (`half`), whether BG2 and BG3 stay still as it scrolls
 * (`still`), and their drift; and which layers are on the main and sub
 * screens and which take color math, as the game keeps them (hex). Each
 * layer's map is its size's rows, and the attributes are BG1's size in cells
 * of three bytes: the tile's properties, then its upper and lower attribute
 * bytes. The attributes may stop short, as five of the cartridge's maps do:
 * the game reads the rest from whatever follows the map. Read back, runs of
 * the same attributes are packed as the game reads them, as the cartridge's
 * are.
 *
 * Rows are full but for the last; a resource whose bytes don't come out in
 * whole cells ends with a `rest` line of the bytes left. */
#ifndef CT_FRONTEND_MAPS_H
#define CT_FRONTEND_MAPS_H

#include "decoded.h"

/* The maps, as a kind of decoded resource. */
extern const DecodedKind map_text;

#endif
