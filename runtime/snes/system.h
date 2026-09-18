/* The development system: CPU (the interpreter, or native code through the
 * intercept), bus, PPU registers and APU, driven by an NTSC scanline timing model.
 *
 * Timing is instruction-granular. Each instruction costs its exact bus accesses
 * at their real speeds plus its exact internal cycles; HDMA lands on the first
 * instruction boundary after its scheduled time. The CPU also pauses while work
 * RAM is refreshed and while HDMA and DMA use the bus. Interrupts are recognized
 * where the CPU polls for them, at the start of an instruction's last cycle, so
 * one asserted during the last cycle waits for the next instruction.
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

/* A pause of the CPU (work RAM refresh, HDMA): the CPU's own time when it
 * began (master clocks minus all earlier pauses), and the total of the pauses before it. */
typedef struct SnesStall {
    uint64_t cpu_at;
    uint64_t total_before;
} SnesStall;
#define SNES_STALL_HISTORY 16

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

    /* Interrupt recognition, in master clocks since power-on. */
    uint64_t poll_at;       /* start of the last cycle of the latest polled step */
    uint64_t nmi_at;        /* when the scheduled NMI's edge reaches the CPU */
    bool nmi_scheduled;
    uint64_t irq_at;        /* when the timer IRQ ($4211 bit 7) reaches the CPU */

    /* CPU pauses: their total since power-on, and the latest ones (a ring), to
     * find where a step's last cycle began when a pause fell inside it. */
    uint64_t stall_total;
    SnesStall stalls[SNES_STALL_HISTORY];
    uint8_t stall_next;

    HdmaChannel hdma[8];
    bool hdma_done_this_line;
    bool htimer_done_this_line;
    bool refresh_done_this_line;

    /* Optional: called for lines 0-224 at the start of hblank, before that
     * line's HDMA writes, with PPU state as it stood while the line was drawn. */
    void (*on_line)(void *ctx, struct Snes *snes, uint16_t line);
    void *on_line_ctx;

    /* Optional: called at the top of a frame, where HDMA loads each enabled
     * channel, before any of the frame's code runs. A consumer that models
     * HDMA itself has to latch the enabled channels here, as the hardware
     * does, rather than at line 0's hblank. */
    void (*on_frame_top)(void *ctx, struct Snes *snes);
    void *on_frame_top_ctx;

    /* Optional: called before each instruction. Returns -1 to let the
     * interpreter run it (or, without it, cpu_service and the miss path below),
     * or the master clocks spent if the hook executed something itself (native
     * code standing in for a routine). */
    int32_t (*intercept)(void *ctx, struct Snes *snes);
    void *intercept_ctx;
    /* Set by an intercept that returned time: the length of the last cycle of
     * what it ran, where interrupts are sampled; 0 means one internal cycle. */
    uint32_t intercept_last_cycle;

    /* The interpreter, for the steps the intercept doesn't take: cpu_step, set
     * by hosts that link it (snes_init leaves it NULL, so the system code
     * itself never pulls the interpreter into a program). */
    CpuStep (*interpreter)(Cpu *cpu);
    /* Without the interpreter (native_only, or no interpreter set; docs/RECOMP.md,
     * "Without the interpreter"): interrupt entry, WAI and STP go through
     * cpu_service, and an instruction the intercept doesn't run is a miss.
     * on_miss is called with PC at it; returning true runs it on the
     * interpreter (if set), false (or no on_miss) halts the machine: `halted`
     * is set, the frame ends there, and snes_run_frame does nothing until a
     * state load clears it. */
    bool native_only;
    bool (*on_miss)(void *ctx, struct Snes *snes);
    void *on_miss_ctx;
    bool halted;
} Snes;

/* rom must stay valid for the lifetime of the system. */
void snes_init(Snes *snes, const uint8_t *rom, size_t rom_size);
void snes_power_on(Snes *snes);

/* Copy the whole machine into dst, re-pointing internal references at dst.
 * Hooks (the interpreter and native_only stay) and the WRAM write mask are not
 * carried over. */
void snes_clone(Snes *dst, const Snes *src);

/* snes_clone for a copy that is kept and refreshed (shadow mode's): dst was
 * made from src by snes_clone_since, which returned `since`, and since then
 * work RAM and VRAM changed on either machine only through their writes (see
 * SNES_TRACK_PAGE_BITS). Only the pages written since are copied. `since` 0
 * copies everything. Starts a new write epoch on src, and returns it: the
 * `since` for the next refresh, and the epoch writes to either machine are
 * stamped with from now on. */
uint64_t snes_clone_since(Snes *dst, Snes *src, uint64_t since);

/* After work RAM or VRAM changed other than through the bus and the PPU:
 * count every page as written now, so the next snes_clone_since refresh copies
 * all of both (and shadow mode compares all of both). Loading a state does
 * this itself. */
void snes_mark_all_written(Snes *snes);

/* Save states (state.c; docs/RUNTIME.md, "Save states"): the machine written
 * field by field under names, so a state loads in any later build. A field
 * the state lacks keeps its power-on value and one this build lacks is
 * skipped; a changed field, a damaged file or another ROM's state is refused.
 * Loading keeps the destination's ROM, hooks (interpreter and native_only
 * included) and write mask, and clears `halted`.
 *
 * snes_state_write returns the state as a malloc'd buffer of *len bytes (NULL
 * if out of memory); snes_state_read loads one, returning 0, or -1 with the
 * machine unchanged and why in err (if given). snes_state_save and
 * snes_state_load do the same with a file, for the development tools. */
uint8_t *snes_state_write(const Snes *snes, size_t *len);
int snes_state_read(Snes *snes, const uint8_t *data, size_t len, char *err, size_t err_len);
/* The fields a state holds, for tests: fn gets each one's name, address,
 * element size and element count. */
typedef void (*SnesStateField)(void *ctx, const char *name, void *field, unsigned size, uint32_t count);
void snes_state_fields(Snes *snes, SnesStateField fn, void *ctx);
int snes_state_save(const Snes *snes, const char *path);
int snes_state_load(Snes *snes, const char *path);

/* Put the timing model where snes_run_frame leaves it: the start of vblank
 * (line 225) with RDNMI set, the NMI scheduled if $4200 enables it, joypads
 * latched and the OAM address reloaded. Pending interrupts and HDMA progress
 * are dropped. For hosts that overwrite the machine's contents from a foreign
 * save state (snes/zst.h), which then continues at the next frame. */
void snes_enter_vblank(Snes *snes);

/* Run until the start of the next vblank. pad1 is in JOY1 bit order. */
void snes_run_frame(Snes *snes, uint16_t pad1);

/* For native code standing in for several instructions within one step (a
 * generated block, docs/RECOMP.md "Exact timing"):
 *
 * snes_horizon: the latest master clock at which an instruction can start
 * without the system having anything to do before it: no line event (HDMA and
 * rendering, work RAM refresh, the line's end with vblank, NMI flag and
 * V-timer), H-timer IRQ,
 * or interrupt recognition between the previous instruction and it. Until an
 * instruction starts past it, instructions may run back to back without
 * returning to the system loop. I/O writes that change interrupt timing
 * ($4200-$420A) can move it earlier.
 *
 * snes_sync: let `clocks` pass on the machine inside the step (APU, counters,
 * hblank) without polling interrupts, before I/O whose result depends on the
 * time. Only valid up to the horizon; the step then returns the rest. */
uint64_t snes_horizon(const Snes *snes);
void snes_sync(Snes *snes, uint32_t clocks);

/* The timing spike's measure (docs/RECOMP.md, "Can a hand port be exact?"):
 * like snes_horizon, but only the points no atomic stand-in for a stretch of
 * code can move past in any design -- the next interrupt the system may
 * recognize (a scheduled NMI, a raised IRQ, the H/V timer's next position), and
 * the frame's boundaries (the start of line 0 and of vblank, where the frame
 * ends and NMI fires). Refresh, HDMA and ordinary line ends are left out: they
 * pause the CPU or act on state a stretch may not touch. */
uint64_t snes_interrupt_horizon(const Snes *snes);
/* An upper bound on the master clocks `cpu_clocks` of CPU time starting now
 * take, with the refresh and HDMA pauses of the lines they cross. */
uint64_t snes_span_bound(const Snes *snes, uint64_t cpu_clocks);

/* Master clocks from now until the next point in the line where $4212's
 * hblank/vblank bits can change (hblank end, hblank start, or the line's end).
 * For skipping ahead in busy-wait loops that poll them. */
uint32_t snes_clocks_to_next_poll_change(const Snes *snes);

#endif
