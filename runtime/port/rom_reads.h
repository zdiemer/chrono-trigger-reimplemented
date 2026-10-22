/* The cartridge reads the ports make, recorded: what tools/assets.py builds
 * the catalogue of resources from (ct_dev --asset-reads, docs/PLATFORM.md
 * "The assets, as built"). */
#ifndef CT_PORT_ROM_READS_H
#define CT_PORT_ROM_READS_H

#include <stdint.h>

/* Bytes for a map of every 24-bit address, a bit each. */
enum { ROM_READS_MAP_BYTES = 0x1000000 / 8 };

/* Set a bit in `map` (ROM_READS_MAP_BYTES, zeroed by the caller) for each
 * address a named accessor reads from now on; NULL stops. */
void rom_reads_record(uint8_t *map);

#endif
