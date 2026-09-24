#include "apu.h"

#include <string.h>

#define MASTER_HZ 21477272u   /* NTSC main-CPU master clock */
#define SPC_HZ 1024000u       /* SPC700 cycle rate */

/* Time the boot ROM's upload loop spends per poll and per accepted byte, in
 * SPC700 cycles. The main CPU waits for each echo, so these set how many
 * frames the upload takes. Calibrated against the trace oracle. */
#define IPL_POLL_CYCLES 4
#define IPL_BYTE_CYCLES 25

/* Timers 0 and 1 tick at 8 kHz, timer 2 at 64 kHz. */
static const uint32_t timer_period[3] = { 128, 128, 16 };

unsigned long apu_port_clears[2];

/* ---- SPC700 bus ------------------------------------------------------------------ */

static uint8_t smp_read(void *ctx, uint16_t addr)
{
    Apu *apu = ctx;
    if (addr < 0x00F0 || addr > 0x00FF)
        return apu->ram[addr];   /* reads of the boot ROM overlay at $FFC0 aren't modeled (see enter_ipl) */
    switch (addr) {
    case 0x00F2: return apu->dsp_addr;
    case 0x00F3: return dsp_read(&apu->dsp, apu->dsp_addr);
    case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F7:
        return apu->cpu_in[addr - 0x00F4];
    case 0x00F8: case 0x00F9:
        return apu->ram[addr];
    case 0x00FD: case 0x00FE: case 0x00FF: {
        ApuTimer *t = &apu->timer[addr - 0x00FD];
        uint8_t v = t->output;
        t->output = 0;
        return v;
    }
    }
    return 0x00;   /* $F0, $F1, $FA-$FC are write-only */
}

static void smp_write(void *ctx, uint16_t addr, uint8_t v)
{
    Apu *apu = ctx;
    if (addr < 0x00F0 || addr > 0x00FF) {
        apu->ram[addr] = v;
        return;
    }
    switch (addr) {
    case 0x00F1:
        for (int i = 0; i < 3; i++) {
            bool on = v & (1 << i);
            if (on && !apu->timer[i].enabled) {
                apu->timer[i].stage = 0;
                apu->timer[i].output = 0;
            }
            apu->timer[i].enabled = on;
        }
        if (v & 0x10) {
            apu->cpu_in[0] = apu->cpu_in[1] = 0;
            apu_port_clears[0]++;
        }
        if (v & 0x20) {
            apu->cpu_in[2] = apu->cpu_in[3] = 0;
            apu_port_clears[1]++;
        }
        apu->control = v;
        break;
    case 0x00F2: apu->dsp_addr = v; break;
    case 0x00F3:
        if (!(apu->dsp_addr & 0x80)) {
            if (apu->on_dsp_write)
                apu->on_dsp_write(apu->on_insn_ctx, apu, apu->dsp_addr, v);
            dsp_write(&apu->dsp, apu->dsp_addr, v);
        }
        break;
    case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F7:
        apu->cpu_out[addr - 0x00F4] = v;
        break;
    case 0x00F8: case 0x00F9:
        apu->ram[addr] = v;
        break;
    case 0x00FA: case 0x00FB: case 0x00FC:
        apu->timer[addr - 0x00FA].target = v;
        break;
    }
}

/* A host program in place of the SPC700's runs a sample's time at a go. */
#define HOST_CYCLES DSP_SAMPLE_CYCLES

/* Timers and DSP run from the same SPC700 clock. */
static void tick_timers(Apu *apu, unsigned cycles)
{
    dsp_run(&apu->dsp, apu->ram, cycles);
    for (int i = 0; i < 3; i++) {
        ApuTimer *t = &apu->timer[i];
        t->divider += cycles;
        while (t->divider >= timer_period[i]) {
            t->divider -= timer_period[i];
            if (!t->enabled)
                continue;
            t->stage++;
            if (t->stage == t->target) {   /* target 0 wraps at 256 */
                t->stage = 0;
                t->output = (t->output + 1) & 0x0F;
            }
        }
    }
}

/* ---- main-CPU ports and the upload protocol ----------------------------------------- */

void apu_init(Apu *apu)
{
    memset(apu, 0, sizeof *apu);
    spc700_init(&apu->spc, (Spc700Bus){ apu, smp_read, smp_write });
    dsp_init(&apu->dsp);
    apu->phase = APU_IPL_WAIT_KICK;
    apu->cpu_out[0] = 0xAA;
    apu->cpu_out[1] = 0xBB;
    apu->control = 0xB0;
    apu->ipl_poll_cycles = IPL_POLL_CYCLES;
    apu->ipl_byte_cycles = IPL_BYTE_CYCLES;
}

uint8_t apu_read_port(void *ctx, uint8_t port)
{
    Apu *apu = ctx;
    return apu->cpu_out[port & 3];
}

/* The boot ROM clears direct page $01-$EF (MOV (X),A : DEC X : BNE, about 10
 * cycles a byte) before announcing itself. */
#define IPL_CLEAR_CYCLES 2400

/* A jump to $FFC0 with the boot ROM mapped ($F1 bit 7) restarts it: games send
 * the driver a command to do that before uploading a new one (Chrono Trigger
 * does it after a game over). The ROM's code is modeled by the upload protocol
 * below, so only its effects are reproduced here. */
static void enter_ipl(Apu *apu)
{
    memset(apu->ram + 0x01, 0, 0xEF);
    apu->spc.sp = 0xEF;
    apu->cpu_out[0] = 0xAA;
    apu->cpu_out[1] = 0xBB;
    apu->phase = APU_IPL_WAIT_KICK;
    apu->ipl_busy = IPL_CLEAR_CYCLES;
}

static void start_driver(Apu *apu, uint16_t entry)
{
    apu->phase = APU_DRIVER;
    apu->spc.pc = entry;
    apu->spc.sp = 0xEF;
    apu->spc.a = apu->spc.x = apu->spc.y = 0;
    apu->spc.psw = SPC_FLAG_Z;
    apu->spc.halted = false;
}

/* Upload protocol. Like the boot ROM, this polls the ports rather than reacting
 * to individual writes: games often write ports 0 and 1 with one 16-bit write,
 * port 0 first, so both must be settled before they're read.
 *   kick:     port0 = $CC, ports 2-3 = address, port1 nonzero to receive a
 *             block there or 0 to jump there
 *   byte i:   port1 = data, port0 = i & $FF (starting at 0); echoed on port 0.
 *             Bytes go to consecutive addresses; blocks can exceed 256 bytes.
 *   next:     ports 2-3 = address, port1 nonzero for another block or 0 to
 *             jump, port0 = any other new value; echoed */
static bool ipl_poll(Apu *apu)
{
    uint8_t v = apu->cpu_in[0];
    uint16_t addr = (uint16_t)(apu->cpu_in[2] | apu->cpu_in[3] << 8);

    if (apu->phase == APU_IPL_WAIT_KICK) {
        if (v != 0xCC)
            return false;
    } else if (v == apu->last_port0) {
        return false;
    } else if (v == apu->expected) {
        apu->ram[apu->write_ptr++] = apu->cpu_in[1];
        apu->cpu_out[0] = v;
        apu->expected++;
        apu->last_port0 = v;
        return true;
    }

    apu->cpu_out[0] = v;
    apu->last_port0 = v;
    if (apu->cpu_in[1] == 0) {
        start_driver(apu, addr);
        return true;
    }
    apu->write_ptr = addr;
    apu->expected = 0;
    apu->phase = APU_IPL_TRANSFER;
    return true;
}

void apu_write_port(void *ctx, uint8_t port, uint8_t value)
{
    Apu *apu = ctx;
    apu->cpu_in[port & 3] = value;
}

static void run(Apu *apu, uint32_t master_clocks)
{
    apu->budget += (int64_t)master_clocks * SPC_HZ;
    while (apu->phase != APU_DRIVER) {
        int64_t available = apu->budget / MASTER_HZ;
        if (available <= 0)
            return;
        /* The timer dividers run from power-on, so they advance during upload too. */
        if (apu->ipl_busy > available) {
            apu->ipl_busy -= (int32_t)available;
            apu->budget -= available * MASTER_HZ;
            tick_timers(apu, (unsigned)available);
            apu->spc_cycles += (uint64_t)available;
            return;
        }
        apu->budget -= (int64_t)apu->ipl_busy * MASTER_HZ;
        tick_timers(apu, (unsigned)apu->ipl_busy);
        apu->spc_cycles += (uint64_t)apu->ipl_busy;
        apu->ipl_busy = (int32_t)(ipl_poll(apu) ? apu->ipl_byte_cycles : apu->ipl_poll_cycles);
    }
    while (apu->budget > 0) {
        if (apu->spc.pc == 0xFFC0 && (apu->control & 0x80)) {
            enter_ipl(apu);
            run(apu, 0);
            return;
        }
        unsigned cycles;
        if (apu->host) {
            cycles = apu->host(apu->host_ctx, apu, HOST_CYCLES);
        } else {
            if (apu->on_insn)
                apu->on_insn(apu->on_insn_ctx, apu);
            cycles = spc700_step(&apu->spc);
        }
        tick_timers(apu, cycles);
        apu->spc_cycles += cycles;
        apu->budget -= (int64_t)cycles * MASTER_HZ;
    }
}

void apu_advance(Apu *apu, uint32_t master_clocks)
{
    if (apu->lead) {
        uint32_t absorbed = apu->lead < master_clocks ? (uint32_t)apu->lead : master_clocks;
        apu->lead -= absorbed;
        master_clocks -= absorbed;
        if (!master_clocks)
            return;
    }
    run(apu, master_clocks);
}

void apu_run_ahead(Apu *apu, uint32_t master_clocks)
{
    apu->lead += master_clocks;
    run(apu, master_clocks);
}

size_t apu_take_samples(Apu *apu, int16_t *out, size_t max_frames)
{
    return dsp_take_samples(&apu->dsp, out, max_frames);
}
