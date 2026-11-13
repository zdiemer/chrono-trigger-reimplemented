/* The cartridge's packed streams, unpacked for an editor and packed again
 * byte for byte (docs/PLATFORM.md, "Graphics"). The game unpacks them as it
 * runs (game/data/decompress.c), so a decoded resource that has been edited
 * goes back into the pack packed.
 *
 * A stream is its first segment's length, a word; segments of control bytes,
 * each followed by the items it describes, a bit an item from the lowest (0 a
 * literal byte, 1 a two-byte reference to a run already unpacked); and after
 * each segment a header byte: how many bits of the next control byte count
 * (none: the stream is over) and, where there is a next segment, a word
 * where it ends. A reference is a run's distance back and its length less
 * three: twelve bits and four, or eleven and five where the stream has long
 * runs (the headers' top bits say which). A control byte of 0 is eight
 * literals.
 *
 * The cartridge's streams were packed by Okumura's LZSS (1989) with its
 * binary-tree search: a 4096-byte ring and runs up to 18 bytes, or 2048 and
 * 34 for long runs. lz_pack is that packer, so every stream of the cartridge
 * packs again to its own bytes. */
#ifndef CT_FRONTEND_LZ_H
#define CT_FRONTEND_LZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most a stream unpacks to: a bank of work RAM. */
enum { LZ_MOST = 0x10000 };

/* A stream of `in_len` bytes unpacked into `out` (room for LZ_MOST): its
 * length and whether it has long runs. False where the stream isn't one: it
 * runs past its bytes, unpacks past LZ_MOST, or refers to bytes before its
 * start or to the byte being made. */
bool lz_unpack(const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len, bool *long_runs);

/* `len` bytes (at most LZ_MOST) packed into `out` as the cartridge's packer
 * packs them: the stream's length, or 0 where it needs more than `room`. */
size_t lz_pack(const uint8_t *image, size_t len, bool long_runs, uint8_t *out, size_t room);

#endif
