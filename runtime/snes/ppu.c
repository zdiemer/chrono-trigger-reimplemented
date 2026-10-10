#include "ppu.h"

#include <string.h>

void ppu_init(Ppu *p)
{
    memset(p, 0, sizeof *p);
    p->reg[0x00] = 0x80;   /* forced blank at power-on */
}

/* ---- VRAM ------------------------------------------------------------------ */

static uint16_t vram_mapped(const Ppu *p)
{
    uint16_t a = p->vram_addr;
    switch (p->reg[0x15] & 0x0C) {
    case 0x04: a = (a & 0xFF00) | ((a << 3) & 0x00F8) | ((a >> 5) & 0x0007); break;
    case 0x08: a = (a & 0xFE00) | ((a << 3) & 0x01F8) | ((a >> 6) & 0x0007); break;
    case 0x0C: a = (a & 0xFC00) | ((a << 3) & 0x03F8) | ((a >> 7) & 0x0007); break;
    }
    return a & 0x7FFF;
}

static void vram_step(Ppu *p)
{
    static const uint16_t step[4] = { 1, 32, 128, 128 };
    p->vram_addr += step[p->reg[0x15] & 3];
}

static void vram_reload_prefetch(Ppu *p)
{
    uint16_t i = (uint16_t)(vram_mapped(p) << 1);
    p->vram_prefetch = (uint16_t)(p->vram[i] | p->vram[(uint16_t)(i + 1)] << 8);
}

static void vram_write(Ppu *p, bool high, uint8_t v)
{
    if (!p->active_display) {
        uint16_t i = (uint16_t)((vram_mapped(p) << 1) + high);
        p->vram[i] = v;
        p->vram_page_epoch[i >> 8] = p->write_epoch;
        p->vram_writes++;
    }
    if (((p->reg[0x15] & 0x80) != 0) == high)
        vram_step(p);
}

/* ---- OAM -------------------------------------------------------------------- */

static void oam_write(Ppu *p, uint8_t v)
{
    if (p->oam_addr & 0x200) {
        if (!p->active_display) {
            p->oam[0x200 + (p->oam_addr & 0x1F)] = v;
            p->oam_writes++;
        }
    } else if (!(p->oam_addr & 1)) {
        p->oam_latch = v;
    } else if (!p->active_display) {
        p->oam[p->oam_addr - 1] = p->oam_latch;
        p->oam[p->oam_addr] = v;
        p->oam_writes++;
    }
    p->oam_addr = (p->oam_addr + 1) & 0x3FF;
}

static uint8_t oam_read(Ppu *p)
{
    uint8_t v = (p->oam_addr & 0x200) ? p->oam[0x200 + (p->oam_addr & 0x1F)] : p->oam[p->oam_addr];
    p->oam_addr = (p->oam_addr + 1) & 0x3FF;
    return v;
}

void ppu_vblank(Ppu *p)
{
    if (!ppu_force_blank(p))
        p->oam_addr = p->oam_reload;
}

static int16_t sext13(uint16_t v) { return (int16_t)((v & 0x1FFF) ^ 0x1000) - 0x1000; }

/* ---- registers ---------------------------------------------------------------- */

void ppu_latch_counters(Ppu *p)
{
    p->h_latch = p->hcounter;
    p->v_latch = p->vcounter;
    p->counters_latched = true;
    p->latch_count++;
}

void ppu_write(void *ctx, uint8_t reg, uint8_t v)
{
    Ppu *p = ctx;
    reg &= 0x3F;
    p->reg[reg] = v;
    switch (reg) {
    case 0x02:
        p->oam_reload = (p->oam_reload & 0x200) | (uint16_t)(v << 1);
        p->oam_addr = p->oam_reload;
        break;
    case 0x03:
        p->oam_reload = (uint16_t)((p->oam_reload & 0x1FE) | (v & 1) << 9);
        p->oam_addr = p->oam_reload;
        break;
    case 0x04:
        oam_write(p, v);
        break;
    case 0x0D: case 0x0F: case 0x11: case 0x13: {   /* BGnHOFS */
        int bg = (reg - 0x0D) / 2;
        if (bg == 0) {
            p->m7hofs = sext13((uint16_t)(v << 8 | p->m7_latch));
            p->m7_latch = v;
        }
        p->bg_hofs[bg] = (uint16_t)((v << 8 | (p->bgofs_latch & ~7) | (p->bghofs_latch & 7)) & 0x3FF);
        p->bgofs_latch = v;
        p->bghofs_latch = v;
        break;
    }
    case 0x0E: case 0x10: case 0x12: case 0x14: {   /* BGnVOFS */
        int bg = (reg - 0x0E) / 2;
        if (bg == 0) {
            p->m7vofs = sext13((uint16_t)(v << 8 | p->m7_latch));
            p->m7_latch = v;
        }
        p->bg_vofs[bg] = (uint16_t)((v << 8 | p->bgofs_latch) & 0x3FF);
        p->bgofs_latch = v;
        break;
    }
    case 0x1F:
        p->m7x = sext13((uint16_t)(v << 8 | p->m7_latch));
        p->m7_latch = v;
        break;
    case 0x20:
        p->m7y = sext13((uint16_t)(v << 8 | p->m7_latch));
        p->m7_latch = v;
        break;
    case 0x16:
        p->vram_addr = (p->vram_addr & 0xFF00) | v;
        vram_reload_prefetch(p);
        break;
    case 0x17:
        p->vram_addr = (uint16_t)((p->vram_addr & 0x00FF) | v << 8);
        vram_reload_prefetch(p);
        break;
    case 0x18: vram_write(p, false, v); break;
    case 0x19: vram_write(p, true, v); break;
    case 0x1B:
        p->m7a = (int16_t)(v << 8 | p->m7_latch);
        p->m7_latch = v;
        break;
    case 0x1C:
        p->m7b = (int16_t)(v << 8 | p->m7_latch);
        p->m7_latch = v;
        break;
    case 0x1D:
        p->m7c = (int16_t)(v << 8 | p->m7_latch);
        p->m7_latch = v;
        break;
    case 0x1E:
        p->m7d = (int16_t)(v << 8 | p->m7_latch);
        p->m7_latch = v;
        break;
    case 0x32: {
        uint16_t c = v & 31;
        if (v & 0x20) p->fixed_color = (uint16_t)((p->fixed_color & ~0x001F) | c);
        if (v & 0x40) p->fixed_color = (uint16_t)((p->fixed_color & ~0x03E0) | c << 5);
        if (v & 0x80) p->fixed_color = (uint16_t)((p->fixed_color & ~0x7C00) | c << 10);
        break;
    }
    case 0x21:
        p->cg_addr = v;
        p->cg_second = false;
        break;
    case 0x22:
        if (!p->cg_second) {
            p->cg_latch = v;
        } else {
            p->cgram[p->cg_addr] = (uint16_t)((v & 0x7F) << 8 | p->cg_latch);
            p->cg_addr++;
            p->cgram_writes++;
        }
        p->cg_second = !p->cg_second;
        break;
    }
}

uint8_t ppu_read(void *ctx, uint8_t reg, uint8_t open_bus)
{
    Ppu *p = ctx;
    reg &= 0x3F;
    switch (reg) {
    case 0x04: case 0x05: case 0x06: case 0x08: case 0x09: case 0x0A:
    case 0x14: case 0x15: case 0x16: case 0x18: case 0x19: case 0x1A:
    case 0x24: case 0x25: case 0x26: case 0x28: case 0x29: case 0x2A:
        return p->ppu1_open_bus;
    case 0x34: case 0x35: case 0x36: {
        int32_t product = (int32_t)p->m7a * (int8_t)(p->m7b >> 8);
        return p->ppu1_open_bus = (uint8_t)(product >> (8 * (reg - 0x34)));
    }
    case 0x37:
        ppu_latch_counters(p);
        return open_bus;
    case 0x38:
        return p->ppu1_open_bus = oam_read(p);
    case 0x39: {
        uint8_t v = p->vram_prefetch & 0xFF;
        if (!(p->reg[0x15] & 0x80)) {
            vram_reload_prefetch(p);
            vram_step(p);
        }
        return p->ppu1_open_bus = v;
    }
    case 0x3A: {
        uint8_t v = p->vram_prefetch >> 8;
        if (p->reg[0x15] & 0x80) {
            vram_reload_prefetch(p);
            vram_step(p);
        }
        return p->ppu1_open_bus = v;
    }
    case 0x3B: {
        uint8_t v;
        if (!p->cg_second) {
            v = p->cgram[p->cg_addr] & 0xFF;
        } else {
            v = (uint8_t)((p->cgram[p->cg_addr] >> 8 & 0x7F) | (p->ppu2_open_bus & 0x80));
            p->cg_addr++;
        }
        p->cg_second = !p->cg_second;
        return p->ppu2_open_bus = v;
    }
    case 0x3C: {
        uint8_t v = p->h_second ? (uint8_t)((p->h_latch >> 8 & 1) | (p->ppu2_open_bus & 0xFE))
                                : (uint8_t)(p->h_latch & 0xFF);
        p->h_second = !p->h_second;
        return p->ppu2_open_bus = v;
    }
    case 0x3D: {
        uint8_t v = p->v_second ? (uint8_t)((p->v_latch >> 8 & 1) | (p->ppu2_open_bus & 0xFE))
                                : (uint8_t)(p->v_latch & 0xFF);
        p->v_second = !p->v_second;
        return p->ppu2_open_bus = v;
    }
    case 0x3E:
        return p->ppu1_open_bus = (uint8_t)((p->time_over ? 0x80 : 0) | (p->range_over ? 0x40 : 0) |
                                            (p->ppu1_open_bus & 0x10) | 0x01);
    case 0x3F: {
        uint8_t v = (uint8_t)((p->counters_latched ? 0x80 : 0) | (p->ppu2_open_bus & 0x20) | 0x03);
        p->h_second = p->v_second = false;
        if (p->wrio_latch_enable)
            p->counters_latched = false;
        return p->ppu2_open_bus = v;
    }
    }
    return open_bus;
}
