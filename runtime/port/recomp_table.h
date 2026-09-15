/* The table of generated blocks (docs/RECOMP.md, M1).
 *
 * With a ROM at configure time the build generates it from the ROM
 * (tools/recomp); without one, runtime/port/recomp_table_stub.c provides an
 * empty table. Hosts register the entries after the hand-written ones, and only
 * if the loaded ROM's CRC-32 is recomp_rom_crc. */
#ifndef CT_PORT_RECOMP_TABLE_H
#define CT_PORT_RECOMP_TABLE_H

#include "port/native.h"

extern const NativeEntry recomp_natives[];
extern const int recomp_native_count;
extern const uint32_t recomp_rom_crc;
extern const RecompIndexEntry recomp_index[];
extern const int recomp_index_count;

/* The RAM sections ([[ramcode]] hints, docs/MATCHING.md) as the generator
 * built them (M3): name, canonical run address in $7E:0000-$7F:FFFF, size and
 * image. The image is ROM-derived and exists only in the build tree. For
 * reports (ct_dev --misses). */
typedef struct RecompRamSection {
    const char *name;
    uint32_t base;
    uint32_t size;
    const uint8_t *image;
} RecompRamSection;

extern const RecompRamSection recomp_ram_sections[];
extern const int recomp_ram_section_count;

#endif
