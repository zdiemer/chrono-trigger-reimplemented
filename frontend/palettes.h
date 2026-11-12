/* The game's palettes as text a person can edit: every resource of the
 * catalogue whose codec is a palette (re/assets.tsv, `palette:N` or
 * `palette:NxM`), written out from a store and read back into one, so an
 * edited color reaches the game through the same accessors as the cartridge's
 * (docs/PLATFORM.md, "The assets, as built").
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
 * each palette (M rows), counting from 0; the import ignores comments and
 * blank lines, and serves only the resources the text names, the rest as the
 * store under it has them.
 *
 * The text is made from the user's own cartridge and is kept beside their
 * asset pack, never shared. Like the pack, it is bytes: which file holds it is
 * the driver's business. */
#ifndef CT_FRONTEND_PALETTES_H
#define CT_FRONTEND_PALETTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "data/assets.h"

/* Whether a resource of the catalogue is a palette, and its colors a row and
 * rows a palette (1 where the codec gives no palette size). */
bool palette_codec(const AssetEntry *e, unsigned *row_colors, unsigned *palette_rows);

/* Every palette `store` serves, as text: malloc'd, *len bytes (not
 * terminated); NULL with *why when the store lacks one. */
char *palettes_export(const AssetStore *store, size_t *len, const char **why);

/* Palettes read from text, served over `base`: NULL with *why and the line
 * (from 1) where the text is wrong. `base` must outlive the result. */
typedef struct AssetPalettes AssetPalettes;
AssetPalettes *palettes_import(const char *text, size_t len, const AssetStore *base, unsigned *line,
                               const char **why);
void palettes_store(AssetPalettes *palettes, AssetStore *store);
void palettes_close(AssetPalettes *palettes);

/* Whether every palette `store` serves comes back as the same bytes when
 * written as text and read again: false with *why naming the first that
 * doesn't. */
bool palettes_round_trip(const AssetStore *store, const char **why);

#endif
