/* What the kinds of decoded resource that are packed share (frontend/tiles.h,
 * frontend/maps.h): a packed resource is written unpacked, after a line that
 * says how its stream packs, and read back packed again (frontend/lz.h).
 *
 * The line is `packed short` or `packed long`, the runs the stream has. Read
 * back, a resource whose unpacked bytes are the store beneath's is served as
 * that store has it, which is what packing it again would give, only sooner;
 * an edited one is packed again and must fit the cartridge's room for it, and
 * may unpack to no more bytes than the stream beneath does, since the game
 * unpacks it into a buffer made for those. */
#ifndef CT_FRONTEND_PACKED_H
#define CT_FRONTEND_PACKED_H

#include "decoded.h"

/* How a packed resource's stream runs, as its text says (DecodedReading.form). */
enum { PACKED_UNSAID, PACKED_SHORT_RUNS, PACKED_LONG_RUNS };

/* A packed resource's bytes unpacked: malloc'd (room for LZ_MOST), its length
 * and runs; NULL where they aren't a stream. */
uint8_t *packed_unpack(const uint8_t *bytes, uint32_t len, size_t *unpacked_len, bool *long_runs);

/* The line saying how a stream packs. */
void packed_write_form(DecodedText *t, bool long_runs);

/* A resource's first line, `packed short` or `packed long`, read into r->form;
 * and r->most, what the stream beneath unpacks to. NULL, or what is wrong. */
const char *packed_read_form(const Decoded *d, DecodedReading *r, const char *s, const char *end);

/* The resource's unpacked bytes, r->made, packed into its slot: NULL, or what is wrong. */
const char *packed_end(const Decoded *d, DecodedReading *r);

#endif
