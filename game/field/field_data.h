/* What the field's code reads from the ROM, apart from the map and event data.
 *
 * Every such read goes through an accessor here, named after what the data is
 * for, so a build that loads the data from somewhere other than the ROM image
 * changes this file and nothing that uses it. */
#ifndef CT_GAME_FIELD_DATA_H
#define CT_GAME_FIELD_DATA_H

#include "port/machine.h"

/* The wave a waving layer's scroll follows: signed 16-bit horizontal offsets,
 * a word each, at $C0:FF30. `at` is the original's byte index; the line
 * effects read 16 words 4 bytes apart from an even start of at most $3E, so
 * the table runs to $C0:FFAB. */
static inline uint16_t field_wave_offset(Snes *s, uint16_t at) { return m_read16(s, 0xC0FF30u + at); }

#endif
