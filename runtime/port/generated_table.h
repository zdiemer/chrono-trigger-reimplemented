/* The table of generated blocks (docs/RECOMP.md, M1).
 *
 * With a ROM at configure time the build generates it from the ROM
 * (tools/generate); without one, runtime/port/generated_table_stub.c provides an
 * empty table. Hosts register the entries after the hand-written ones, and only
 * if the loaded ROM's CRC-32 is generated_rom_crc. */
#ifndef CT_PORT_GENERATED_TABLE_H
#define CT_PORT_GENERATED_TABLE_H

#include "port/native.h"

extern const NativeEntry generated_natives[];
extern const int generated_native_count;
extern const uint32_t generated_rom_crc;
extern const GeneratedIndexEntry generated_index[];
extern const int generated_index_count;

/* The RAM sections ([[ramcode]] hints, docs/MATCHING.md) as the generator
 * built them (M3): name, canonical run address in $7E:0000-$7F:FFFF, size and
 * image. The image is ROM-derived and exists only in the build tree. For
 * reports (ct_dev --misses). */
typedef struct GeneratedRamSection {
    const char *name;
    uint32_t base;
    uint32_t size;
    const uint8_t *image;
} GeneratedRamSection;

extern const GeneratedRamSection generated_ram_sections[];
extern const int generated_ram_section_count;

#endif
