/* Resources decoded into text a person can edit, and read back into a store
 * over the pack: what the palettes (frontend/palettes.h) and the fonts
 * (frontend/fonts.h) share (docs/PLATFORM.md, "Decoded resources").
 *
 * A kind of decoded resource says which rows of the catalogue it decodes and
 * how one is written as lines of text and read back from them; the rest is
 * here. The text is the kind's opening comment, then each resource it
 * decodes under a `[name]` line, its name in the catalogue (re/assets.tsv).
 * Anything after '#' on a line is a comment, and blank lines don't count.
 * Read back, the text serves only the resources it names, the rest as the
 * store under it has them; text that is wrong is refused at its line.
 *
 * The text is made from the user's own cartridge and is kept beside their
 * asset pack, never shared. Like the pack, it is bytes: which file holds it
 * is the driver's business. */
#ifndef CT_FRONTEND_DECODED_H
#define CT_FRONTEND_DECODED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "data/assets.h"

/* Text being written: grows as it is put, `full` once memory ran out. */
typedef struct {
    char *text;
    size_t len, cap;
    bool full;
} DecodedText;
void decoded_put(DecodedText *t, const char *s);

/* Resources read from text, served over the store they were read over. */
typedef struct Decoded Decoded;

/* The resource a kind is reading lines of: its row of the catalogue, its
 * bytes as they are being made, and two counts of the kind's own (how many
 * colors, how many glyphs and lines of the one being read). */
typedef struct {
    int entry;
    uint8_t *bytes;
    uint32_t items, lines;
} DecodedReading;

typedef struct {
    const char *what;     /* "palettes": what the messages call them */
    const char *header;   /* the text's opening comment */
    /* Whether it decodes a row of the catalogue; *named says whether the row
     * has a [name] of its own or is written with another's. */
    bool (*decodes)(const AssetEntry *e, bool *named);
    /* Write a named resource's lines, `bytes` being what the store serves for
     * it: false where it needs another resource the store hasn't. */
    bool (*write)(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes);
    /* Read a line of the resource (trimmed: not empty, no comment), and its
     * end: NULL, or what is wrong. */
    const char *(*read_line)(Decoded *d, DecodedReading *r, const char *s, const char *end);
    const char *(*read_end)(Decoded *d, DecodedReading *r);
} DecodedKind;

/* The catalogue's row of a name (`len` bytes of it), looked for from *hint on:
 * the rows are asked for in the catalogue's order. -1 where there is none. */
int decoded_entry(const char *name, size_t len, uint16_t *hint);

/* Where the text puts a decoded resource's bytes, which it then serves: a
 * kind reading one resource may fill another it decodes unnamed. */
uint8_t *decoded_bytes(Decoded *d, int entry);

/* A number, in base 10 or 16, from *s (spaces before it skipped): false
 * unless it is there, is at most `most`, and ends where the line or a space does. */
bool decoded_number(const char **s, const char *end, unsigned base, unsigned most, unsigned *v);
/* The rest of a line after spaces: whether it starts with `word` and a space or the line's end. */
bool decoded_word(const char **s, const char *end, const char *word);

/* Every resource of the kind `store` serves, as text: malloc'd, *len bytes
 * (not terminated); NULL with *why when the store lacks one. */
char *decoded_export(const DecodedKind *kind, const AssetStore *store, size_t *len, const char **why);

/* Resources read from text, served over `base`: NULL with *why and the line
 * (from 1) where the text is wrong. `base` must outlive the result. */
Decoded *decoded_import(const DecodedKind *kind, const char *text, size_t len, const AssetStore *base,
                        unsigned *line, const char **why);
void decoded_store(Decoded *d, AssetStore *store);
void decoded_close(Decoded *d);

/* Whether every resource of the kind `store` serves comes back as the same
 * bytes when written as text and read again: false with *why naming the
 * first that doesn't. */
bool decoded_round_trip(const DecodedKind *kind, const AssetStore *store, const char **why);

#endif
