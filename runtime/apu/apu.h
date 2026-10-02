/* S-SMP audio subsystem: SPC700, 64 KiB sound RAM, control registers, timers,
 * the S-DSP sound generator (dsp.h), and the four I/O ports shared with the main CPU.
 *
 * The console's IPL boot ROM is not included. Its documented upload protocol
 * (announce $BBAA, accept blocks, jump to the uploaded driver) is implemented
 * directly in C; after the jump, the uploaded driver runs on the SPC700 core.
 * The DSP generates 32000 Hz stereo from the SPC700's cycle count; the host
 * collects it with apu_take_samples().
 */
#ifndef CT_APU_H
#define CT_APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dsp.h"
#include "spc700.h"

typedef enum {
    APU_IPL_WAIT_KICK,   /* announcing $BBAA, waiting for $CC */
    APU_IPL_TRANSFER,    /* receiving a block */
    APU_DRIVER,          /* uploaded code is running on the SPC700 */
} ApuPhase;

typedef struct ApuTimer {
    bool enabled;
    uint8_t target;      /* $FA-$FC; 0 means 256 */
    uint8_t stage;       /* internal up-counter compared with target */
    uint8_t output;      /* 4-bit $FD-$FF, cleared on read */
    uint32_t divider;    /* SPC700 cycles until the next stage tick */
} ApuTimer;

typedef struct Apu {
    uint8_t ram[0x10000];
    uint8_t cpu_in[4];   /* written by the main CPU ($2140-$2143), read at $F4-$F7 */
    uint8_t cpu_out[4];  /* written at $F4-$F7, read by the main CPU */

    ApuPhase phase;
    uint16_t write_ptr;  /* next sound-RAM address in the current block */
    uint8_t expected;    /* next byte index (8-bit, wraps) in the current block */
    uint8_t last_port0;  /* port 0 value the upload protocol last acted on */
    int32_t ipl_busy;    /* SPC700 cycles until the upload protocol polls again */
    uint32_t ipl_poll_cycles;   /* cost of a poll that finds nothing new */
    uint32_t ipl_byte_cycles;   /* cost of accepting one byte or command */

    Spc700 spc;
    uint8_t control;     /* $F1 */
    uint8_t dsp_addr;    /* $F2 */
    Dsp dsp;
    ApuTimer timer[3];

    int64_t budget;      /* time owed to the SPC700 in master clocks x SPC700 Hz; negative is debt */
    uint64_t lead;       /* master clocks run ahead of the main CPU (apu_run_ahead), not yet absorbed */
    uint64_t spc_cycles; /* total SPC700 cycles run */

    /* Optional: called before each SPC700 instruction. */
    void (*on_insn)(void *ctx, const struct Apu *apu);
    void *on_insn_ctx;
    /* Optional: called for each write the SPC700 makes to an S-DSP register
     * (through $F3), before it takes effect; ctx is on_insn_ctx. */
    void (*on_dsp_write)(void *ctx, const struct Apu *apu, uint8_t reg, uint8_t value);
    /* Optional: a program on the host that runs in place of the SPC700's
     * once it has put the APU in APU_DRIVER (frontend/sound_host.h, at the
     * CPU half's boot): given the SPC700 cycles to take, it answers the ports
     * and plays through the S-DSP, and returns the cycles it took; the timers
     * and the S-DSP run on them as ever. */
    unsigned (*host)(void *ctx, struct Apu *apu, unsigned cycles);
    void *host_ctx;
    /* Optional, with `host`: the CPU half's boot upload and its commands
     * taken by the host program as calls, in place of the upload and the
     * ports (platform/audio.h, au_driver_boot_taken). */
    void (*host_boot)(void *ctx, struct Apu *apu);
    void (*host_command)(void *ctx, struct Apu *apu, const uint8_t command[4], uint8_t effects_asked);
} Apu;

/* SPC700 writes to $F1 that cleared the CPU-to-APU ports: [0] ports 0-1, [1]
 * ports 2-3. Counted over every machine in the process (only the machine the
 * system loop runs moves its SPC700); shadow validation uses the counts to leave
 * out ports the SPC700 cleared while an original routine ran. */
extern unsigned long apu_port_clears[2];

void apu_init(Apu *apu);

/* Main-CPU side of the ports (SnesIo-compatible; ctx is the Apu). */
uint8_t apu_read_port(void *ctx, uint8_t port);
void apu_write_port(void *ctx, uint8_t port, uint8_t value);

/* Run the APU for the given number of main-CPU master clocks. Time it has run
 * ahead (lead) is absorbed first. */
void apu_advance(Apu *apu, uint32_t master_clocks);

/* Run the APU for master_clocks ahead of the main CPU, adding them to lead (the
 * APU-direct audio backend, platform/direct/audio.h). */
void apu_run_ahead(Apu *apu, uint32_t master_clocks);

/* Move up to max_frames generated stereo frames (32000 Hz, L/R interleaved)
 * into out; returns how many. Up to DSP_OUT_FRAMES wait in the APU; beyond
 * that, new frames are dropped (counted in apu->dsp.out_dropped). */
size_t apu_take_samples(Apu *apu, int16_t *out, size_t max_frames);

#endif
