/* Finding the cartridge save a movie starts from (a "# sram crc32" line, see
 * docs/ORACLE.md) by its CRC-32, so tools don't need --sram. */
#ifndef CT_SRAM_FIND_H
#define CT_SRAM_FIND_H

#include <stddef.h>
#include <stdint.h>

/* Searches, in order, the vendored saves next to the movie (`../saves` and
 * `../../saves` from its directory: oracle/movies/saves/x.ctm finds
 * oracle/saves), `saves` next to the movie, and the directory the ROM is in,
 * each up to 3 levels deep, for an 8192-byte file ending in .srm (any case)
 * whose CRC-32 is `crc`. Writes its path to out and returns 0, or returns -1.
 * Any of movie_path and rom_path may be NULL. */
int ct_sram_find(uint32_t crc, const char *movie_path, const char *rom_path, char *out, size_t n);

#endif
