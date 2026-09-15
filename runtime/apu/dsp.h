/* S-DSP: the SNES sound generator. Eight BRR sample voices with pitch
 * modulation, ADSR/GAIN envelopes and noise, mixed with an echo unit (ring
 * buffer in sound RAM, feedback, 8-tap FIR) into 32000 Hz stereo.
 *
 * Sample-granular: one output sample every 32 SPC700 cycles, driven by the
 * APU's cycle count, so generation is deterministic and independent of the
 * host. Register writes take effect from the next sample; ENVX, OUTX and ENDX
 * are updated as each sample is generated. Behavior follows the documented
 * hardware (fullsnes, and the findings of the accurate S-DSP emulators): the
 * 4-sample BRR decode cadence, the 5-sample KON delay, KON/KOFF polled every
 * other sample, the global rate counter shared by envelopes and noise, and the
 * overflow quirks of the Gaussian and FIR sums.
 */
#ifndef CT_DSP_H
#define CT_DSP_H

#include <stddef.h>
#include <stdint.h>

#define DSP_SAMPLE_CYCLES 32   /* SPC700 cycles per output sample */
#define DSP_SAMPLE_RATE 32000
#define DSP_OUT_FRAMES 4096    /* stereo frames buffered until the host takes them */

/* Register addresses (voice registers are 0xV0-0xV9). */
enum {
    DSP_VOLL = 0x0, DSP_VOLR = 0x1, DSP_PITCHL = 0x2, DSP_PITCHH = 0x3, DSP_SRCN = 0x4,
    DSP_ADSR0 = 0x5, DSP_ADSR1 = 0x6, DSP_GAIN = 0x7, DSP_ENVX = 0x8, DSP_OUTX = 0x9,
    DSP_FIR = 0xF,
    DSP_MVOLL = 0x0C, DSP_MVOLR = 0x1C, DSP_EVOLL = 0x2C, DSP_EVOLR = 0x3C,
    DSP_KON = 0x4C, DSP_KOFF = 0x5C, DSP_FLG = 0x6C, DSP_ENDX = 0x7C,
    DSP_EFB = 0x0D, DSP_PMON = 0x2D, DSP_NON = 0x3D, DSP_EON = 0x4D,
    DSP_DIR = 0x5D, DSP_ESA = 0x6D, DSP_EDL = 0x7D,
};

typedef enum { DSP_ENV_RELEASE, DSP_ENV_ATTACK, DSP_ENV_DECAY, DSP_ENV_SUSTAIN } DspEnvMode;

typedef struct DspVoice {
    int16_t buf[12];      /* ring of the last 12 decoded 15-bit samples */
    uint8_t buf_pos;      /* where the next 4 decoded samples go (0, 4 or 8) */
    uint8_t brr_offset;   /* next nybble byte within the 9-byte block (1-7) */
    uint16_t brr_addr;    /* current block header */
    int32_t interp_pos;   /* pitch counter: bits 12-14 sample index, 4-11 Gaussian index */
    int32_t env;          /* 11-bit envelope level */
    int32_t hidden_env;   /* last computed level before clipping (GAIN mode 7 reads it) */
    uint8_t env_mode;     /* DspEnvMode */
    uint8_t kon_delay;    /* samples left in the key-on sequence (5 at key-on) */
} DspVoice;

typedef struct Dsp {
    uint8_t regs[128];
    DspVoice voice[8];

    int32_t counter;      /* global rate counter, counts down */
    uint16_t noise;       /* 15-bit LFSR */
    uint8_t every_other;  /* KON/KOFF are polled when this toggles to 1 */
    uint8_t new_kon;      /* KON as written, cleared once acted on */
    uint8_t kon, koff;    /* polled values */

    uint8_t echo_esa;     /* ESA as sampled for the next echo access */
    uint8_t echo_flg;     /* FLG as sampled for the left echo write */
    uint16_t echo_offset; /* bytes into the echo ring */
    uint16_t echo_length; /* ring size, sampled from EDL when the offset wraps */
    uint8_t fir_pos;
    int16_t fir_hist[8][2];

    uint32_t clock;       /* SPC700 cycles toward the next sample */
    uint64_t samples;     /* samples generated since power-on */

    uint32_t out_count;   /* frames waiting in out */
    uint32_t out_dropped; /* frames lost because out was full */
    int16_t out[DSP_OUT_FRAMES * 2];
} Dsp;

void dsp_init(Dsp *dsp);

uint8_t dsp_read(const Dsp *dsp, uint8_t addr);
void dsp_write(Dsp *dsp, uint8_t addr, uint8_t value);

/* Generate one sample now. ram is sound RAM: BRR data and the directory are
 * read from it and the echo ring buffer lives in it. */
void dsp_sample(Dsp *dsp, uint8_t *ram);

/* Advance by SPC700 cycles, generating a sample every DSP_SAMPLE_CYCLES. */
static inline void dsp_run(Dsp *dsp, uint8_t *ram, uint32_t cycles)
{
    dsp->clock += cycles;
    while (dsp->clock >= DSP_SAMPLE_CYCLES) {
        dsp->clock -= DSP_SAMPLE_CYCLES;
        dsp_sample(dsp, ram);
    }
}

/* Move up to max_frames buffered stereo frames (L, R interleaved) into out. */
size_t dsp_take_samples(Dsp *dsp, int16_t *out, size_t max_frames);

/* Exposed for tests. */
int dsp_rate_fires(int32_t counter, unsigned rate);   /* envelope/noise rate 0-31 */
/* Decode one 9-byte BRR block into 16 15-bit samples, continuing from the two
 * previous samples in *p1 (newest) and *p2, which are updated. */
void dsp_decode_brr_block(const uint8_t *block, int16_t out[16], int16_t *p1, int16_t *p2);

#endif
