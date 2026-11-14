/* The game's save kept in the GBA cartridge's battery-backed SRAM, where
 * the save service's bytes (runtime/platform/save.h) persist: mGBA keeps
 * them in the .sav beside the .gba.
 *
 * The SNES's 8 KiB save is the SRAM's first 8 KiB as the .srm has it, then a
 * mark that says it is there; an SRAM without the mark (a new cartridge, or
 * mGBA's first run) holds no save, and the game starts with its save RAM
 * erased, as ct_native does with no --sram. */
#ifndef CT_GBA_CARTRIDGE_SAVE_H
#define CT_GBA_CARTRIDGE_SAVE_H

#include <stdbool.h>
#include <stdint.h>

/* The save into `out` (SAVE_SIZE bytes); false where the cartridge has none. */
bool cartridge_save_load(uint8_t *out);
/* `bytes` (SAVE_SIZE) as the cartridge's save, with its mark. */
void cartridge_save_store(const uint8_t *bytes);

#endif
