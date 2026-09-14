/* The development system: interpreter CPU, bus, PPU registers and APU, driven
 * by an NTSC scanline timing model.
 *
 * Timing is instruction-granular. Each instruction costs its exact bus accesses
 * at their real speeds plus its exact internal cycles; interrupts and HDMA land
 * on the first instruction boundary after their scheduled time.
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

    /* Optional: called for lines 0-224 at the start of hblank, before that
     * line's HDMA writes, with PPU state as it stood while the line was drawn. */
    void (*on_line)(void *ctx, struct Snes *snes, uint16_t line);
    void *on_line_ctx;

    /* Optional: called before each instruction. Returns -1 to let the
     * interpreter run it, or the master clocks spent if the hook executed
     * something itself (native code standing in for a routine). */
    int32_t (*intercept)(void *ctx, struct Snes *snes);
    void *intercept_ctx;
} Snes;

/* rom must stay valid for the lifetime of the system. */
void snes_init(Snes *snes, const uint8_t *rom, size_t rom_size);
void snes_power_on(Snes *snes);

/* Copy the whole machine into dst, re-pointing internal references at dst.
 * Hooks and the WRAM write mask are not carried over. */
void snes_clone(Snes *dst, const Snes *src);

/* Save states: the raw machine image, valid only for the build that wrote it.
 * Loading keeps the destination's ROM, hooks and write mask. Return 0 on success. */
int snes_state_save(const Snes *snes, const char *path);
int snes_state_load(Snes *snes, const char *path);

/* Run until the start of the next vblank. pad1 is in JOY1 bit order. */
void snes_run_frame(Snes *snes, uint16_t pad1);

#endif
