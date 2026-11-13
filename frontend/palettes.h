/* The game's palettes as text a person can edit: every resource of the
 * catalogue whose codec is a palette (re/assets.tsv, `palette:N` or
 * `palette:NxM`), written out from a store and read back into one
 * (frontend/decoded.h), so an edited color reaches the game through the same
 * accessors as the cartridge's (docs/PLATFORM.md, "Palettes, the first
 * decoded resource").
 *
 * The text, one color a line:
 *
 *   # comment: anything after '#' on a line
 *   [location_palettes]
 *   # palette 0, row 0
 *    0  0  0
 *   31 24 12
 *   31 31 31 +
 *
 * A `[name]` line starts the colors of the resource of that name; each color
 * is its red, green and blue, 0-31, and a `+` after them keeps the top bit of
 * the color's word, which the hardware ignores and a few of the cartridge's
 * words have set. A resource has exactly as many colors as the catalogue's
 * row has words. The export writes a comment before each row (N colors) and
 * each palette (M rows), counting from 0. */
#ifndef CT_FRONTEND_PALETTES_H
#define CT_FRONTEND_PALETTES_H

#include <stdbool.h>

#include "decoded.h"

/* Whether a resource of the catalogue is a palette, and its colors a row and
 * rows a palette (1 where the codec gives no palette size). */
bool palette_codec(const AssetEntry *e, unsigned *row_colors, unsigned *palette_rows);

/* The palettes, as a kind of decoded resource. */
extern const DecodedKind palette_text;

#endif
