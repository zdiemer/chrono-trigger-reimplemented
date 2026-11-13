/* The game's tables of records as text a person can edit (frontend/decoded.h):
 * the sprites' records and the locations' exits (docs/PLATFORM.md,
 * "Records").
 *
 *   struct:sprite-records:N    a sprite's record a line, its N bytes in hex
 *                              under a comment naming them
 *   struct:location-exits      each location's exits: `location NNN` (hex),
 *                              then an exit a line, `x y reach location to_x
 *                              to_y` in hex, the location a word
 *
 *   [location_exit_lists]
 *   location 001
 *   3A 0C 00 8002 05 0A
 *
 * Read back, a sprite table has as many records as the cartridge's; the
 * exits may be more or fewer, so long as they fit the table's slot, and each
 * location's run is where the exits before it end. */
#ifndef CT_FRONTEND_RECORDS_H
#define CT_FRONTEND_RECORDS_H

#include "decoded.h"

/* The records, as a kind of decoded resource. */
extern const DecodedKind record_text;

#endif
