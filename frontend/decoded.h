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
 * bytes as they are being made, and what the kind keeps of its own as it
 * reads: counts (how many colors; how many glyphs and lines; how many tiles
 * and lines, and how many tiles a line has), how the resource is kept, which
 * part of it is being read and where in its bytes, how much it may make, and
 * bytes it makes before it makes the resource's from them (a packed
 * resource's unpacked bytes: decoded_grow), or keeps beside them. */
typedef struct {
    int entry;
    uint8_t *bytes;
    uint32_t items, lines, across;
    unsigned form, part;
    uint32_t at;
    uint32_t most;   /* the most bytes the kind will make of it, once it knows */
    uint8_t *made;
    uint32_t made_len, made_room;
    /* the line being read (from 1), and its last line so far; read_end may
     * set wrong_at to the line what is wrong is on, or leave it 0 for the
     * resource's [name] line */
    unsigned line, last_line, wrong_at;
    void *own;   /* what the kind keeps of its own as it reads, kept from resource to resource (forget) */
} DecodedReading;

/* `more` bytes, 0, added to the reading's own bytes: where they start, or NULL
 * once memory runs out. What it has made is dropped at the next resource. */
uint8_t *decoded_grow(DecodedReading *r, uint32_t more);

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
    /* NULL, or let go of r->own once the text is read */
    void (*forget)(DecodedReading *r);
} DecodedKind;

/* The catalogue's row of a name (`len` bytes of it), looked for from *hint on:
 * the rows are asked for in the catalogue's order. -1 where there is none. */
int decoded_entry(const char *name, size_t len, uint16_t *hint);

/* Where the text puts a decoded resource's bytes, which it then serves: a
 * kind reading one resource may fill another it decodes unnamed. */
uint8_t *decoded_bytes(Decoded *d, int entry);

/* What the store the text is read over serves for a row of the catalogue:
 * its bytes, or NULL where it hasn't them. */
const uint8_t *decoded_beneath(const Decoded *d, int entry);

/* A number, in base 10 or 16, from *s (spaces before it skipped): false
 * unless it is there, is at most `most`, and ends where the line or a space does. */
bool decoded_number(const char **s, const char *end, unsigned base, unsigned most, unsigned *v);
/* The rest of a line after spaces: whether it starts with `word` and a space or the line's end. */
bool decoded_word(const char **s, const char *end, const char *word);

/* Every resource of the kind `store` serves, as text: malloc'd, *len bytes
 * (and a NUL after them); NULL with *why when the store lacks one. Its opening
 * comment stamps the rows of the catalogue the kind decodes. */
char *decoded_export(const DecodedKind *kind, const AssetStore *store, size_t *len, const char **why);

/* Whether a text's opening comment says it was written from the rows of the
 * catalogue the kind decodes as they are now: one written from others (an
 * older catalogue's names, codecs and lengths for them) isn't read. A change
 * to rows the kind doesn't decode leaves its texts good. */
bool decoded_of_these_rows(const DecodedKind *kind, const char *text, size_t len);

/* Resources read from text, served over `base`: NULL with *why and the line
 * (from 1) where the text is wrong; what is wrong with a resource as a whole
 * (too short, say) at its [name] line. `base` must outlive the result. */
Decoded *decoded_import(const DecodedKind *kind, const char *text, size_t len, const AssetStore *base,
                        unsigned *line, const char **why);
void decoded_store(Decoded *d, AssetStore *store);
void decoded_close(Decoded *d);

/* Whether every resource of the kind `store` serves comes back as the same
 * bytes when written as text and read again: false with *why naming the
 * first that doesn't. */
bool decoded_round_trip(const DecodedKind *kind, const AssetStore *store, const char **why);

#endif
