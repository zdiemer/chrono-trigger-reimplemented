#include "bus.h"

#include <string.h>

static const uint8_t dma_pattern[8][4] = {
    { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
    { 0, 1, 2, 3 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
};
static const uint8_t dma_unit_len[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

void snes_bus_init(SnesBus *bus, const uint8_t *rom, size_t rom_size, SnesIo io)
{
    memset(bus, 0, sizeof *bus);
    bus->rom = rom;
    bus->rom_size = rom_size;
    bus->io = io;
    for (int i = 0; i < 8; i++)
        memset(bus->dma[i].reg, 0xFF, sizeof bus->dma[i].reg);
}

/* ---- ROM / RAM decoding ------------------------------------------------------ */

typedef enum { REGION_OPEN, REGION_ROM, REGION_WRAM, REGION_SRAM, REGION_IO } Region;

static Region decode(const SnesBus *bus, uint32_t addr, size_t *index)
{
    uint8_t bank = addr >> 16;
    uint16_t a = addr & 0xFFFF;

    if (bank == 0x7E || bank == 0x7F) {
        *index = (size_t)(bank - 0x7E) << 16 | a;
        return REGION_WRAM;
    }
    if (bank >= 0xC0 || (bank >= 0x40 && bank <= 0x7D)) {
        *index = ((size_t)(bank & 0x3F) << 16 | a) % bus->rom_size;
        return REGION_ROM;
    }
    /* $00-$3F and $80-$BF system area */
    if (a < 0x2000) {
        *index = a;
        return REGION_WRAM;
    }
    if (a >= 0x8000) {
        *index = ((size_t)(bank & 0x3F) << 16 | a) % bus->rom_size;
        return REGION_ROM;
    }
    if (a >= 0x6000 && (bank & 0x7F) >= 0x20) {
        *index = (a - 0x6000) % SNES_SRAM_SIZE;
        return REGION_SRAM;
    }
    *index = a;
    return REGION_IO;
}

static uint32_t access_clocks(const SnesBus *bus, uint32_t addr)
{
    uint8_t bank = addr >> 16;
    uint16_t a = addr & 0xFFFF;
    if (bank & 0x40)
        return (bank & 0x80) && bus->fastrom ? 6 : 8;
    if (a >= 0x8000)
        return (bank & 0x80) && bus->fastrom ? 6 : 8;
    if (a < 0x2000 || a >= 0x6000)
        return 8;
    if (a >= 0x4000 && a < 0x4200)
        return 12;
    return 6;
}

/* $2180-$2183: work RAM data port. The address written to $2181-$2183 selects
 * a WRAM byte by its low 17 bits (bit 16 picks $7E or $7F, so $00:3000 and
 * $7E:3000 are the same byte); each read or write of $2180 accesses it and
 * advances the address. Chrono Trigger's decompressor writes through it. */
static uint8_t *wram_port_byte(SnesBus *bus)
{
    return &bus->wram[bus->wram_port_addr & (SNES_WRAM_SIZE - 1)];
}

/* ---- CPU I/O registers ------------------------------------------------------- */

static uint8_t read_io(SnesBus *bus, uint16_t a)
{
    uint8_t ob = bus->open_bus;
    if (a >= 0x2100 && a <= 0x213F)
        return bus->io.ppu_read ? bus->io.ppu_read(bus->io.ctx, a & 0xFF, ob) : ob;
    if (a >= 0x2140 && a <= 0x217F)
        return bus->io.apu_read ? bus->io.apu_read(bus->io.ctx, a & 3) : ob;
    if (a >= 0x4300 && a <= 0x437F)
        return bus->dma[(a >> 4) & 7].reg[a & 0xF];
    if (a == 0x2180) {
        uint8_t v = *wram_port_byte(bus);
        bus->wram_port_addr = (bus->wram_port_addr + 1) & 0xFFFFFF;
        return v;
    }

    switch (a) {
    case 0x4016: {
        uint8_t bit = (bus->serial[0] >> 15) & 1;
        if (!bus->latch)
            bus->serial[0] = (uint16_t)(bus->serial[0] << 1 | 1);
        return (ob & 0xFC) | bit;
    }
    case 0x4017: {
        uint8_t bit = (bus->serial[1] >> 15) & 1;
        if (!bus->latch)
            bus->serial[1] = (uint16_t)(bus->serial[1] << 1 | 1);
        return (ob & 0xE0) | 0x1C | bit;
    }
    case 0x4210: {
        uint8_t v = (bus->nmi_flag ? 0x80 : 0) | (ob & 0x70) | 0x02;
        bus->nmi_flag = false;
        return v;
    }
    case 0x4211: {
        uint8_t v = (bus->irq_flag ? 0x80 : 0) | (ob & 0x7F);
        bus->irq_flag = false;
        return v;
    }
    case 0x4212:
        return (bus->in_vblank ? 0x80 : 0) | (bus->in_hblank ? 0x40 : 0) | (ob & 0x3E);
    case 0x4213: return bus->wrio;
    case 0x4214: return bus->rddiv & 0xFF;
    case 0x4215: return bus->rddiv >> 8;
    case 0x4216: return bus->rdmpy & 0xFF;
    case 0x4217: return bus->rdmpy >> 8;
    case 0x4218: case 0x421A: case 0x421C: case 0x421E:
        return bus->joy[(a - 0x4218) / 2] & 0xFF;
    case 0x4219: case 0x421B: case 0x421D: case 0x421F:
        return bus->joy[(a - 0x4219) / 2] >> 8;
    }
    return ob;
}

static void run_dma(SnesBus *bus, uint8_t channels);

static void write_io(SnesBus *bus, uint16_t a, uint8_t v)
{
    if (a >= 0x2100 && a <= 0x213F) {
        if (bus->io.ppu_write)
            bus->io.ppu_write(bus->io.ctx, a & 0xFF, v);
        return;
    }
    if (a >= 0x2140 && a <= 0x217F) {
        if (bus->io.apu_write)
            bus->io.apu_write(bus->io.ctx, a & 3, v);
        return;
    }
    if (a >= 0x4300 && a <= 0x437F) {
        bus->dma[(a >> 4) & 7].reg[a & 0xF] = v;
        return;
    }
    if (a >= 0x2180 && a <= 0x2183) {
        if (a == 0x2180) {
            uint8_t *b = wram_port_byte(bus);
            *b = v;
            if (bus->wram_written)
                bus->wram_written[b - bus->wram] = 1;
            bus->wram_port_addr = (bus->wram_port_addr + 1) & 0xFFFFFF;
        } else {
            unsigned shift = 8 * (unsigned)(a - 0x2181);
            bus->wram_port_addr = (bus->wram_port_addr & ~((uint32_t)0xFF << shift)) | (uint32_t)v << shift;
        }
        return;
    }

    switch (a) {
    case 0x4016:
        if ((bus->latch & 1) && !(v & 1)) {
            bus->serial[0] = bus->pad[0];
            bus->serial[1] = bus->pad[1];
        }
        bus->latch = v & 1;
        break;
    case 0x4200:
        if (!(bus->nmitimen & 0x80) && (v & 0x80) && bus->nmi_flag)
            bus->nmi_request = true;
        bus->nmitimen = v;
        if (!(v & 0x30))
            bus->irq_flag = false;
        break;
    case 0x4201: bus->wrio = v; break;
    case 0x4202: bus->wrmpya = v; break;
    case 0x4203:
        bus->wrmpyb = v;
        bus->rdmpy = (uint16_t)(bus->wrmpya * v);
        break;
    case 0x4204: bus->wrdiv = (bus->wrdiv & 0xFF00) | v; break;
    case 0x4205: bus->wrdiv = (uint16_t)((bus->wrdiv & 0x00FF) | v << 8); break;
    case 0x4206:
        if (v == 0) {
            bus->rddiv = 0xFFFF;
            bus->rdmpy = bus->wrdiv;
        } else {
            bus->rddiv = bus->wrdiv / v;
            bus->rdmpy = bus->wrdiv % v;
        }
        break;
    case 0x4207: bus->htime = (bus->htime & 0x100) | v; break;
    case 0x4208: bus->htime = (uint16_t)((bus->htime & 0xFF) | (v & 1) << 8); break;
    case 0x4209: bus->vtime = (bus->vtime & 0x100) | v; break;
    case 0x420A: bus->vtime = (uint16_t)((bus->vtime & 0xFF) | (v & 1) << 8); break;
    case 0x420B: {
        /* A DMA transfer can write to $420B itself; transfers don't nest (the
         * value is latched but doesn't start another one). DMA runs to
         * completion inside this call, so the flag is never part of a saved
         * state. */
        static bool in_dma;
        bus->mdmaen = v;
        if (!in_dma) {
            in_dma = true;
            run_dma(bus, v);
            in_dma = false;
        }
        break;
    }
    case 0x420C: bus->hdmaen = v; break;
    case 0x420D: bus->fastrom = v & 1; break;
    }
}

/* ---- DMA ------------------------------------------------------------------------ */

static void run_dma(SnesBus *bus, uint8_t channels)
{
    for (int ch = 0; ch < 8; ch++) {
        if (!(channels & (1 << ch)))
            continue;
        uint8_t *r = bus->dma[ch].reg;
        uint8_t ctrl = r[0];
        uint8_t mode = ctrl & 7;
        bool to_a = ctrl & 0x80;
        /* DMAP bit 3: fixed address; bit 4: decrement (bits 0-2 are the mode). */
        bool fixed = ctrl & 0x08;
        int step = (ctrl & 0x10) ? -1 : 1;
        uint32_t bank = (uint32_t)r[4] << 16;
        uint16_t addr = (uint16_t)(r[2] | r[3] << 8);
        uint32_t count = (uint32_t)(r[5] | r[6] << 8);
        if (count == 0)
            count = 0x10000;

        for (uint32_t i = 0; i < count; i++) {
            uint16_t b = 0x2100 | (uint8_t)(r[1] + dma_pattern[mode][i % dma_unit_len[mode]]);
            if (to_a)
                snes_bus_write(bus, bank | addr, snes_bus_read(bus, b));
            else
                snes_bus_write(bus, b, snes_bus_read(bus, bank | addr));
            if (!fixed)
                addr = (uint16_t)(addr + step);
        }
        r[2] = addr & 0xFF;
        r[3] = addr >> 8;
        r[5] = r[6] = 0;
    }
}

/* ---- public ------------------------------------------------------------------ */

uint8_t snes_bus_read(void *ctx, uint32_t addr)
{
    SnesBus *bus = ctx;
    size_t i;
    uint8_t v;
    bus->clocks += access_clocks(bus, addr);
    switch (decode(bus, addr, &i)) {
    case REGION_ROM: v = bus->rom[i]; break;
    case REGION_WRAM: v = bus->wram[i]; break;
    case REGION_SRAM: v = bus->sram[i]; break;
    case REGION_IO: v = read_io(bus, (uint16_t)i); break;
    default: v = bus->open_bus; break;
    }
    bus->open_bus = v;
    return v;
}

void snes_bus_write(void *ctx, uint32_t addr, uint8_t value)
{
    SnesBus *bus = ctx;
    size_t i;
    bus->clocks += access_clocks(bus, addr);
    bus->open_bus = value;
    switch (decode(bus, addr, &i)) {
    case REGION_WRAM:
        bus->wram[i] = value;
        if (bus->wram_written)
            bus->wram_written[i] = 1;
        break;
    case REGION_SRAM: bus->sram[i] = value; break;
    case REGION_IO: write_io(bus, (uint16_t)i, value); break;
    default: break;
    }
}

uint8_t snes_bus_peek(const SnesBus *bus, uint32_t addr)
{
    size_t i;
    switch (decode(bus, addr, &i)) {
    case REGION_ROM: return bus->rom[i];
    case REGION_WRAM: return bus->wram[i];
    case REGION_SRAM: return bus->sram[i];
    default: return bus->open_bus;
    }
}

void snes_bus_auto_joypad(SnesBus *bus)
{
    for (int i = 0; i < 4; i++)
        bus->joy[i] = bus->pad[i];
    bus->serial[0] = 0xFFFF;
    bus->serial[1] = 0xFFFF;
}
