/* SNES memory map for a HiROM cartridge, and the CPU-side I/O registers
 * ($4016-$4017, $4200-$421F, $4300-$437F).
 *
 * The PPU and APU are reached through callbacks so their implementation can be
 * swapped (low-level model now, native renderer later) without touching the bus.
 */
#ifndef CT_SNES_BUS_H
#define CT_SNES_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SNES_WRAM_SIZE 0x20000
#define SNES_SRAM_SIZE 0x2000

typedef struct SnesIo {
    void *ctx;
    uint8_t (*ppu_read)(void *ctx, uint8_t reg, uint8_t open_bus);    /* $2100-$213F */
    void (*ppu_write)(void *ctx, uint8_t reg, uint8_t value);
    uint8_t (*apu_read)(void *ctx, uint8_t port);                    /* $2140-$2143 */
    void (*apu_write)(void *ctx, uint8_t port, uint8_t value);
} SnesIo;

typedef struct DmaChannel {
    uint8_t reg[16];   /* $43x0-$43xF as written */
} DmaChannel;

typedef struct SnesBus {
    const uint8_t *rom;
    size_t rom_size;
    uint8_t wram[SNES_WRAM_SIZE];
    uint8_t sram[SNES_SRAM_SIZE];
    uint8_t open_bus;

    /* Master clocks spent on bus access since the system last cleared it:
     * 6 per fast access, 8 per slow one, 12 for $4000-$41FF. */
    uint32_t clocks;
    /* Value of clocks when the latest CPU access began. A DMA transfer counts
     * as part of the $420B write that started it. */
    uint32_t access_start;

    SnesIo io;

    /* $4200-$421F */
    uint8_t nmitimen;
    uint8_t wrio;
    uint8_t wrmpya, wrmpyb;
    uint16_t wrdiv;
    uint16_t rddiv, rdmpy;
    uint16_t htime, vtime;
    uint8_t hdmaen;
    bool fastrom;

    /* Status latched by the timing model, read back by the game. */
    bool nmi_flag;     /* $4210 bit 7, cleared on read */
    bool irq_flag;     /* $4211 bit 7, cleared on read */
    bool in_vblank;    /* $4212 bit 7 */
    bool in_hblank;    /* $4212 bit 6 */
    bool nmi_request;  /* NMI enabled while the NMI flag was already set */

    /* Joypads: the system sets pad[] each frame; auto-read latches joy[]. */
    uint16_t pad[4];
    uint16_t joy[4];
    uint8_t latch;
    uint16_t serial[2];

    uint32_t wram_port_addr;   /* $2181-$2183: address for the $2180 WRAM data port */

    /* Optional, SNES_WRAM_SIZE bytes: set to 1 for every WRAM byte the game
     * writes. Lets traces be compared against emulators with random power-on RAM. */
    uint8_t *wram_written;

    DmaChannel dma[8];
    uint8_t mdmaen;    /* last $420B write; the system runs the transfers */
} SnesBus;

void snes_bus_init(SnesBus *bus, const uint8_t *rom, size_t rom_size, SnesIo io);

uint8_t snes_bus_read(void *bus, uint32_t addr);
void snes_bus_write(void *bus, uint32_t addr, uint8_t value);

/* Side-effect-free read for debuggers and tracing. I/O reads return open bus. */
uint8_t snes_bus_peek(const SnesBus *bus, uint32_t addr);

/* Latch pad[] into the $4218-$421F registers, as auto-joypad read does at vblank. */
void snes_bus_auto_joypad(SnesBus *bus);

#endif
