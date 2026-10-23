/* Decompression of scripts, graphics, maps and RAM code. */
#ifndef CT_GAME_DATA_DECOMPRESS_H
#define CT_GAME_DATA_DECOMPRESS_H

#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* The stream the decompressor's cells name (decompress_source) unpacked to
 * where they say (decompress_dest, in bank $7E or $7F), as its callers name
 * them first; its unpacked length, which it keeps in decompress_length; and
 * the routine it stands for, as its callers time it. */
enum { UNPACK_STREAM_ENTRY = 0xC30002 };
uint16_t unpack_stream(GameState *g);

void decompress(Snes *s);

#endif
