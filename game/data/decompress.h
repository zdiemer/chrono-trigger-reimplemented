/* Decompression of scripts, graphics, maps and RAM code. */
#ifndef CT_GAME_DATA_DECOMPRESS_H
#define CT_GAME_DATA_DECOMPRESS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* The stream the decompressor's cells name (decompress_source) unpacked to
 * where they say (decompress_dest, in bank $7E or $7F), as its callers name
 * them first; its unpacked length, which it keeps in decompress_length. Its
 * time is the time model's, charged here: callers call it plainly. */
uint16_t unpack_stream(GameState *g);
/* Whether a packed resource terminates within one bank of input and its
 * output fits the caller's storage. Does not write work RAM or seek its port. */
bool unpack_stream_fits(GameState *g, uint32_t from, uint16_t capacity);
/* The same, as the scene engine calls it, at the routine rather than its
 * jump, which it is timed as. */
uint16_t unpack_stream_direct(GameState *g);
/* Either, for a caller whose original reaches the unpacker with its state
 * saved and given back around the call, and keeps the pace that takes (the
 * native plays' recorded outcomes hold it to that): the unpacking with the
 * time of that way in. `called` is the same work, for a caller that names
 * the call rather than the run. */
uint16_t unpack_stream_run(GameState *g, bool direct);
uint16_t unpack_stream_called(GameState *g, bool direct);

#endif
