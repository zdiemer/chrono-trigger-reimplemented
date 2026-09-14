/* Sony SPC700 interpreter (the S-SMP sound CPU).
 *
 * Instruction-granular: spc700_step() runs one instruction against the bus
 * and returns how many CPU cycles it took, which drives the APU timers and
 * DSP. Cycle counts match the SingleStepTests SPC700 suite.
 */
#ifndef CT_SPC700_H
#define CT_SPC700_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SPC_FLAG_C = 0x01,
    SPC_FLAG_Z = 0x02,
    SPC_FLAG_I = 0x04,
    SPC_FLAG_H = 0x08,
    SPC_FLAG_B = 0x10,
    SPC_FLAG_P = 0x20,   /* direct page is $01xx when set */
    SPC_FLAG_V = 0x40,
    SPC_FLAG_N = 0x80,
};

typedef struct Spc700Bus {
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} Spc700Bus;

typedef struct Spc700 {
    uint16_t pc;
    uint8_t a, x, y, sp, psw;
    bool halted;   /* SLEEP or STOP: only a reset resumes execution */
    Spc700Bus bus;
} Spc700;

void spc700_init(Spc700 *spc, Spc700Bus bus);

/* Reset: PC from the vector at $FFFE, A/X/Y/SP/PSW cleared, halt released.
 * The boot ROM sets up SP itself. */
void spc700_reset(Spc700 *spc);

/* Execute one instruction. Returns cycles used; a halted CPU idles 2 cycles per call. */
unsigned spc700_step(Spc700 *spc);

#endif
