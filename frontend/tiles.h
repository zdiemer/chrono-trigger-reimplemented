/* The game's graphics as text a person can edit: every resource of the
 * catalogue whose codec is tiles (re/assets.tsv), written out from a store
 * and read back into one (frontend/decoded.h), so an edited tile reaches the
 * game through the same accessors as the cartridge's (docs/PLATFORM.md,
 * "Graphics").
 *
 * A resource is 8x8 tiles of two or four bit planes, as the SNES keeps them
 * (`tiles:2bpp`, `tiles:4bpp`: a tile's lines two planes at a time, a byte of
 * each), or the same packed (`lz:2bpp`, `lz:4bpp`: frontend/lz.h), which the
 * game unpacks as it loads them. A packed resource is written unpacked. Read
 * back, its tiles are packed again as the cartridge's packer packs them, so
 * an edited one must still fit the cartridge's room for it; an unedited one
 * is taken as the store under the text has it, which is the same bytes
 * sooner.
 *
 * The text, sixteen tiles a row, a character a pixel and a space between
 * tiles:
 *
 *   [setup_character_graphics.0]
 *   # 77 tiles, 4 bit planes, packed
 *   packed short
 *   # 0000
 *   ........ ...11... ..2222..
 *   ...
 *
 * '.' is color 0 (clear) and '1' to '3', or '1' to '9' and 'A' to 'F', the
 * others; the colors themselves are the palette's the game draws the tile
 * with, which isn't the tile's own. A packed resource first says `packed`
 * and the runs its stream has, `short` or `long`. Each row of tiles is eight
 * lines; only the last row may have fewer than sixteen. A resource that
 * isn't packed has as many tiles as the cartridge's; a packed one has as
 * many as its text gives, no more than the stream beneath unpacks to (the
 * game unpacks it into a buffer made for those). */
#ifndef CT_FRONTEND_TILES_H
#define CT_FRONTEND_TILES_H

#include "decoded.h"

/* The graphics, as a kind of decoded resource. */
extern const DecodedKind tile_text;

#endif
