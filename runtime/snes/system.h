/* The development system: interpreter CPU, bus, PPU registers and APU, driven
 * by an NTSC scanline timing model.
 *
 * Timing is instruction-granular. Each instruction costs its bus accesses at
 * their real speeds plus an estimate for internal CPU cycles; interrupts and
 * HDMA land on the first instruction boundary after their scheduled time.
 *
 * A frame ends at the start of vblank (scanline 225), after the NMI flag is set
 * and before the NMI handler runs.
 */
#ifndef CT_SNES_SYSTEM_H
#define CT_SNES_SYSTEM_H

#include "../apu/apu.h"
#include "../cpu/cpu65816.h"
#include "bus.h"
#include "ppu.h"

#define SNES_CLOCKS_PER_LINE 1364
#define SNES_LINES_PER_FRAME 262
#define SNES_VBLANK_LINE 225

typedef struct HdmaChannel {
    bool active;
    bool do_transfer;
} HdmaChannel;

typedef struct Snes {
    Cpu cpu;
    SnesBus bus;
    Ppu ppu;
    Apu apu;

    uint32_t hclock;    /* master clocks into the current line */
    uint16_t vline;
    uint64_t master;    /* total master clocks since power-on */
    uint32_t frame;     /* completed frames */
    bool frame_done;

    HdmaChannel hdma[8];
    bool hdma_done_this_line;
    bool htimer_done_this_line;
} Snes;

/* rom must stay valid for the lifetime of the system. */
void snes_init(Snes *snes, const uint8_t *rom, size_t rom_size);
void snes_power_on(Snes *snes);

/* Run until the start of the next vblank. pad1 is in JOY1 bit order. */
void snes_run_frame(Snes *snes, uint16_t pad1);

#endif
