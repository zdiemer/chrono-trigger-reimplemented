/* CGRAM and OAM extraction from core savestates.
 *
 * libretro exposes WRAM, VRAM and SRAM but has no memory ids for CGRAM or
 * OAM, and no tested core publishes them through SET_MEMORY_MAPS. Both are
 * serialized in savestates, so per-core adapters locate them there and check
 * each extraction for layout drift.
 *
 * Only Snes9x is supported: its retro_serialize is a pure snapshot. bsnes
 * v094 (bsnes2014) serializes CGRAM/OAM too, but its retro_serialize calls
 * runtosave(), which advances emulation, so reading it every frame would
 * perturb the run being traced. See docs/ORACLE.md. */
#ifndef CT_PPU_STATE_H
#define CT_PPU_STATE_H

#include <stddef.h>
#include <stdint.h>

#define CT_CGRAM_SIZE 512 /* 256 colors, little-endian BGR555 */
#define CT_OAM_SIZE 544   /* 128 x 4-byte low table, then 32-byte high table */

typedef enum {
    CT_PPU_STATE_NONE,
    CT_PPU_STATE_SNES9X,
} ct_ppu_state_kind;

/* Picks an adapter from retro_system_info.library_name. */
ct_ppu_state_kind ct_ppu_state_detect(const char *library_name);

/* Extracts CGRAM and OAM from a serialized state. On failure returns -1 and
 * sets *why to a static message. */
int ct_ppu_state_extract(ct_ppu_state_kind kind, const uint8_t *state, size_t state_len, uint8_t *cgram,
                         uint8_t *oam, const char **why);

#endif
