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
/* The same, as the scene engine calls it, at the routine rather than its
 * jump, which it is timed as. */
uint16_t unpack_stream_direct(GameState *g);
/* Either, for a caller's machine half, which reached the unpacker through its
 * entry while it had one, and keeps the pace it had (the strict and native
 * plays' recorded outcomes hold it to that): run, as the bridges ran the
 * entry in place, the entry's own time with it; called, as a call on the
 * machine, the time charged before it and its own also passing on the machine
 * either side of it (charge_settle). */
uint16_t unpack_stream_run(GameState *g, bool direct);
uint16_t unpack_stream_called(GameState *g, bool direct);

#endif
