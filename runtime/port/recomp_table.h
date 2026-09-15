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

#endif
