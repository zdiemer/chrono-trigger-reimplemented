/* S-PPU register layer: VRAM/CGRAM/OAM access, Mode 7 multiply, H/V counter
 * latches and status. No rendering; a renderer reads the state kept here.
 *
 * Memory layouts match docs/ORACLE.md regions so traces compare directly:
 * VRAM is 64 KiB byte-addressed (word address << 1), CGRAM is 256 BGR555 words,
 * OAM is 544 bytes (128 x 4 low table, 32 high table).
 */
#ifndef CT_SNES_PPU_H
#define CT_SNES_PPU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Ppu {
    uint8_t vram[0x10000];
    uint16_t cgram[256];
    uint8_t oam[0x220];

    uint8_t reg[0x40];          /* last value written to $2100-$213F */

    uint16_t vram_addr;         /* word address, $2116/$2117 */
    uint16_t vram_prefetch;

    uint8_t cg_addr;
    bool cg_second;
    uint8_t cg_latch;

    uint16_t oam_reload;        /* 10-bit byte address from $2102/$2103 */
    uint16_t oam_addr;
    uint8_t oam_latch;

    /* Scroll registers ($210D-$2114) share two write latches; Mode 7 values
     * ($210D/$210E, $211B-$2120) share a third. Values are decoded on write. */
    uint8_t bgofs_latch, bghofs_latch;
    uint16_t bg_hofs[4], bg_vofs[4];   /* 10-bit */
    uint8_t m7_latch;
    int16_t m7a, m7b, m7c, m7d;
    int16_t m7x, m7y, m7hofs, m7vofs;  /* 13-bit, sign-extended */

    uint16_t fixed_color;              /* COLDATA ($2132) as BGR555 */

    bool range_over, time_over;        /* $213E, set by the renderer */

    /* VRAM bytes, CGRAM entries and OAM entries written since power-on
     * (wrapping). Renderers and the VidFrame decoder use changes to them as a
     * hint to recheck the memory; emulation never reads them. A change that
     * doesn't go through these writes (a state load) is caught by comparing
     * contents at a frame's start. */
    uint32_t vram_writes, cgram_writes, oam_writes;

    /* Write tracking as for work RAM (SNES_TRACK_PAGE_BITS in bus.h): the epoch
     * of each VRAM page's latest write. */
    uint64_t write_epoch;
    uint64_t vram_page_epoch[0x10000 >> 8];

    uint16_t h_latch, v_latch;
    bool h_second, v_second;
    bool counters_latched;
    uint8_t ppu1_open_bus, ppu2_open_bus;

    /* Supplied by the system before each register access. */
    uint16_t hcounter, vcounter;   /* dots, scanlines */
    bool active_display;           /* visible line and not force-blanked */
    bool wrio_latch_enable;        /* $4201 bit 7 */
} Ppu;

void ppu_init(Ppu *ppu);

/* SnesIo-compatible callbacks; ctx is the Ppu. */
uint8_t ppu_read(void *ctx, uint8_t reg, uint8_t open_bus);
void ppu_write(void *ctx, uint8_t reg, uint8_t value);

/* Latch the H/V counters, as a $2137 read or a WRIO falling edge does. */
void ppu_latch_counters(Ppu *ppu);

/* Called at the start of vblank: OAM address reload. */
void ppu_vblank(Ppu *ppu);

static inline bool ppu_force_blank(const Ppu *ppu) { return ppu->reg[0x00] & 0x80; }

/* OAM priority rotation: first sprite index, or 0 when rotation is off. */
static inline uint8_t ppu_first_sprite(const Ppu *ppu)
{
    return (ppu->reg[0x03] & 0x80) ? (uint8_t)((ppu->oam_reload >> 2) & 0x7F) : 0;
}

#endif
