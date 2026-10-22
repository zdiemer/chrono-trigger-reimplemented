/* The cartridge's data for a build with no machine (ct_native): game/rom.h's
 * reads, which every named resource accessor (game/ *_data.h) goes through,
 * answered from a ROM file's bytes the driver loaded into a buffer, at the
 * HiROM address the accessor names.
 *
 * This is the one seam between the game's data and where it comes from: the
 * asset pack (docs/PLAN.md, Phase 4a, "Assets") replaces this file, and no
 * accessor or port changes with it. The driver owns the bytes; nothing here
 * opens a file. */
#ifndef CT_PORT_ROM_IMAGE_H
#define CT_PORT_ROM_IMAGE_H

#include <stdint.h>

/* The image the reads answer from, `bytes` long, kept by the caller for as long
 * as the game runs. A read past it, or of an address that isn't the
 * cartridge's, reads 0 and is counted (rom_image_strays). */
void rom_image_set(const uint8_t *image, uint32_t bytes);
unsigned long rom_image_strays(uint32_t *first);

#endif
