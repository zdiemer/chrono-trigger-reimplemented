/* The game's text as text a person can edit (frontend/decoded.h): the
 * dialog's and the menus' strings, the battle's names and the dictionary
 * the strings share words from, so an edited line of dialog reaches the game
 * through the same accessors as the cartridge's (docs/PLATFORM.md, "Text").
 *
 * The catalogue's codec (re/assets.tsv) says what a resource is:
 *
 *   text:dialog              a string the typesetter sets (game/menu/typeset.c)
 *   text:names:N             names of N glyphs each, a line a name
 *   text:dialog-dictionary   the dictionary's words, as they are kept
 *
 * A string is written as it reads, its lines run on until a break, each break
 * ending a line of the text (lines are joined again as they are read):
 *
 *   [strings_f74900.12]
 *   Truce Dome?{line}
 *   Never heard of it.{line}
 *
 * A glyph is its letter, digit or sign (“ ” ♪ ♥ … ∞ in UTF-8). Read back, the
 * glyphs are packed with the dictionary's words, the store under the text's:
 * from the left, at each glyph the longest word starting there. Where the
 * cartridge packed a string otherwise, its text says so where it must, so an
 * unedited string reads back as its bytes: a word in angle brackets is that
 * word (<you>r), `|` ends a run that is packed on its own, a glyph in hex is
 * that glyph, and {unpacked} keeps the run after it as its glyphs (the menus'
 * strings, which the cartridge doesn't pack). A control code is its name in braces: {line}, {page},
 * each maybe `read` (once the player has read on) and `indent`; {stop},
 * {wait N}, {pause}, {number N} (bytes), {character}, {Crono} to {Magus},
 * {leader}, {member N}, {Nadia}, {item}, {Epoch}, {tech}, {enemy}; and
 * {space} a space at either end of a line. Any byte, or bytes, can be
 * written in hex in braces: {F3}, {01 7F}. The string's end is not written.
 *
 * A name is its glyphs in quotes, all N of them ("Bronze Edge"); the
 * dictionary is a word a line, `XX "glyphs"`, XX its code ($21-$A0), in the
 * order the words are kept.
 *
 * Read back, a string may be as long as the cartridge's (its slot, the end
 * included), a shorter one ended there, and an edited one may not end in a
 * code that takes the byte after it; the dictionary's words must fit their
 * slot too. */
#ifndef CT_FRONTEND_TEXT_H
#define CT_FRONTEND_TEXT_H

#include "decoded.h"

/* The text, as a kind of decoded resource. */
extern const DecodedKind game_text;

/* The dialog string the store has at the cartridge's address `at`, as a line
 * of plain text into out (n bytes, its end included): its glyphs and words,
 * a space for each break, the names it names, cut short with "..." where it
 * doesn't fit; what a script's text shows beside the commands that say it.
 * False where the catalogue keeps no string there. */
bool text_said_at(const AssetStore *store, uint32_t at, char *out, size_t n);

#endif
