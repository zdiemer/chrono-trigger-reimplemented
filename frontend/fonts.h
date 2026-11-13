/* The game's fonts as text a person can edit: every resource of the catalogue
 * whose codec is a font (re/assets.tsv), written out from a store and read
 * back into one (frontend/decoded.h), so an edited glyph reaches the game
 * through the same accessors as the cartridge's (docs/PLATFORM.md, "Fonts").
 *
 * The fonts are glyphs of two bitplanes, so four colors: the 8x8 ones
 * (`font:8x8`, a glyph's 16 bytes a line's two planes after each other) and
 * the dialog's proportional 12x12 ones (`font:12x12`: every glyph's left
 * eight pixels, 24 bytes a glyph, then their right four, 24 bytes a pair of
 * glyphs, the even one's in each byte's high half). A font may have widths,
 * a byte a glyph from one on (`font-widths:FONT:XX`, from glyph $XX), which
 * are written with its glyphs.
 *
 * The text, a glyph a block, a character a pixel:
 *
 *   [dialog_font]
 *   glyph A0 width 7
 *   ............
 *   .1111.......
 *   11..11......
 *   ...
 *
 * A `[name]` line starts a font's glyphs, every one of them in order; each
 * starts with `glyph` and its number in hex, then `width` and its width where
 * the font has one for it, and has a line of pixels for each of its lines:
 * '.' is color 0 (clear) and '1' to '3' the other three. */
#ifndef CT_FRONTEND_FONTS_H
#define CT_FRONTEND_FONTS_H

#include "decoded.h"

/* The fonts, as a kind of decoded resource. */
extern const DecodedKind font_text;

#endif
