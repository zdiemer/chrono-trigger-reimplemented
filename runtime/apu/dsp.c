#include "dsp.h"

#include <string.h>

/* 4-point interpolation kernel, 512 entries (the S-DSP's table; each set of four
 * weights sums to 2047-2049). */
static const int16_t gauss[512] = {
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
       1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    2,    2,    2,    2,    2,
       2,    2,    3,    3,    3,    3,    3,    4,    4,    4,    4,    4,    5,    5,    5,    5,
       6,    6,    6,    6,    7,    7,    7,    8,    8,    8,    9,    9,    9,   10,   10,   10,
      11,   11,   11,   12,   12,   13,   13,   14,   14,   15,   15,   15,   16,   16,   17,   17,
      18,   19,   19,   20,   20,   21,   21,   22,   23,   23,   24,   24,   25,   26,   27,   27,
      28,   29,   29,   30,   31,   32,   32,   33,   34,   35,   36,   36,   37,   38,   39,   40,
      41,   42,   43,   44,   45,   46,   47,   48,   49,   50,   51,   52,   53,   54,   55,   56,
      58,   59,   60,   61,   62,   64,   65,   66,   67,   69,   70,   71,   73,   74,   76,   77,
      78,   80,   81,   83,   84,   86,   87,   89,   90,   92,   94,   95,   97,   99,  100,  102,
     104,  106,  107,  109,  111,  113,  115,  117,  118,  120,  122,  124,  126,  128,  130,  132,
     134,  137,  139,  141,  143,  145,  147,  150,  152,  154,  156,  159,  161,  163,  166,  168,
     171,  173,  175,  178,  180,  183,  186,  188,  191,  193,  196,  199,  201,  204,  207,  210,
     212,  215,  218,  221,  224,  227,  230,  233,  236,  239,  242,  245,  248,  251,  254,  257,
     260,  263,  267,  270,  273,  276,  280,  283,  286,  290,  293,  297,  300,  304,  307,  311,
     314,  318,  321,  325,  328,  332,  336,  339,  343,  347,  351,  354,  358,  362,  366,  370,
     374,  378,  381,  385,  389,  393,  397,  401,  405,  410,  414,  418,  422,  426,  430,  434,
     439,  443,  447,  451,  456,  460,  464,  469,  473,  477,  482,  486,  491,  495,  499,  504,
     508,  513,  517,  522,  527,  531,  536,  540,  545,  550,  554,  559,  563,  568,  573,  577,
     582,  587,  592,  596,  601,  606,  611,  615,  620,  625,  630,  635,  640,  644,  649,  654,
     659,  664,  669,  674,  678,  683,  688,  693,  698,  703,  708,  713,  718,  723,  728,  732,
     737,  742,  747,  752,  757,  762,  767,  772,  777,  782,  787,  792,  797,  802,  806,  811,
     816,  821,  826,  831,  836,  841,  846,  851,  855,  860,  865,  870,  875,  880,  884,  889,
     894,  899,  904,  908,  913,  918,  923,  927,  932,  937,  941,  946,  951,  955,  960,  965,
     969,  974,  978,  983,  988,  992,  997, 1001, 1005, 1010, 1014, 1019, 1023, 1027, 1032, 1036,
    1040, 1045, 1049, 1053, 1057, 1061, 1066, 1070, 1074, 1078, 1082, 1086, 1090, 1094, 1098, 1102,
    1106, 1109, 1113, 1117, 1121, 1125, 1128, 1132, 1136, 1139, 1143, 1146, 1150, 1153, 1157, 1160,
    1164, 1167, 1170, 1174, 1177, 1180, 1183, 1186, 1190, 1193, 1196, 1199, 1202, 1205, 1207, 1210,
    1213, 1216, 1219, 1221, 1224, 1227, 1229, 1232, 1234, 1237, 1239, 1241, 1244, 1246, 1248, 1251,
    1253, 1255, 1257, 1259, 1261, 1263, 1265, 1267, 1269, 1270, 1272, 1274, 1275, 1277, 1279, 1280,
    1282, 1283, 1284, 1286, 1287, 1288, 1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1297, 1298,
    1299, 1300, 1300, 1301, 1302, 1302, 1303, 1303, 1303, 1304, 1304, 1304, 1304, 1304, 1305, 1305,
};

/* Samples between steps for each 5-bit envelope/noise rate (0 never steps). */
static const uint16_t rate_period[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1,
};

/* The global counter runs down through 30720 values (the least common multiple
 * of the periods). Rates come in three phase groups: periods 1<<n, 3<<n and
 * 5<<n step at different points of the count. */
#define COUNTER_RANGE 30720

static const uint16_t rate_phase[3] = { 536, 0, 1040 };   /* by rate % 3 */

static inline int clamp16(int v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

int dsp_rate_fires(int32_t counter, unsigned rate)
{
    rate &= 0x1F;
    if (rate == 0)
        return 0;
    return (counter + rate_phase[rate % 3]) % rate_period[rate] == 0;
}

void dsp_init(Dsp *d)
{
    memset(d, 0, sizeof *d);
    d->regs[DSP_FLG] = 0xE0;
    d->echo_flg = 0xE0;
    d->noise = 0x4000;
    d->every_other = 1;
    for (int i = 0; i < 8; i++)
        d->voice[i].brr_offset = 1;
}

uint8_t dsp_read(const Dsp *d, uint8_t addr)
{
    return d->regs[addr & 0x7F];
}

void dsp_write(Dsp *d, uint8_t addr, uint8_t value)
{
    addr &= 0x7F;
    d->regs[addr] = value;
    if (addr == DSP_KON)
        d->new_kon = value;
    else if (addr == DSP_ENDX)
        d->regs[DSP_ENDX] = 0;   /* any write acknowledges all voices */
}

/* ---- BRR ------------------------------------------------------------------------ */

/* One 15-bit sample from a signed nybble (-8..7), the block header and the two
 * previous samples. */
static int brr_sample(int nybble, int header, int p1, int p2)
{
    int shift = header >> 4;
    int s = shift <= 12 ? (nybble * (1 << shift)) >> 1 : (nybble < 0 ? -2048 : 0);
    switch ((header >> 2) & 3) {
    case 1: s += p1 + ((-p1) >> 4); break;
    case 2: s += 2 * p1 + ((-3 * p1) >> 5) - p2 + (p2 >> 4); break;
    case 3: s += 2 * p1 + ((-13 * p1) >> 6) - p2 + ((3 * p2) >> 4); break;
    }
    /* Saturate to 16 bits, then keep 15 (the top bit is lost, not saturated). */
    s = clamp16(s);
    return (int16_t)(uint16_t)(s * 2) >> 1;
}

static inline int nybble(unsigned n) { return (int)(n ^ 8) - 8; }

void dsp_decode_brr_block(const uint8_t *block, int16_t out[16], int16_t *p1, int16_t *p2)
{
    int a = *p1, b = *p2;
    for (int i = 0; i < 16; i++) {
        uint8_t byte = block[1 + i / 2];
        int s = brr_sample(nybble(i & 1 ? byte & 0x0F : byte >> 4), block[0], a, b);
        out[i] = (int16_t)s;
        b = a;
        a = s;
    }
    *p1 = (int16_t)a;
    *p2 = (int16_t)b;
}

/* The DSP decodes a block two bytes (four samples) at a time. */
static void decode_brr(DspVoice *v, const uint8_t *ram, uint8_t header)
{
    int p1 = v->buf[(v->buf_pos + 11) % 12];
    int p2 = v->buf[(v->buf_pos + 10) % 12];
    uint16_t at = (uint16_t)(v->brr_addr + v->brr_offset);
    unsigned bytes = (unsigned)ram[at] << 8 | ram[(uint16_t)(at + 1)];
    for (int k = 0; k < 4; k++) {
        int s = brr_sample(nybble((bytes >> (12 - 4 * k)) & 0x0F), header, p1, p2);
        v->buf[(v->buf_pos + k) % 12] = (int16_t)s;
        p2 = p1;
        p1 = s;
    }
    v->buf_pos = (uint8_t)((v->buf_pos + 4) % 12);
}

/* 16-bit result with bit 0 clear (a 15-bit sample doubled). */
static int interpolate(const DspVoice *v)
{
    int i = (v->interp_pos >> 4) & 0xFF;
    int base = v->buf_pos + (v->interp_pos >> 12);
    int oldest = v->buf[base % 12], older = v->buf[(base + 1) % 12];
    int old = v->buf[(base + 2) % 12], newest = v->buf[(base + 3) % 12];
    int out = (gauss[255 - i] * oldest) >> 10;
    out += (gauss[511 - i] * older) >> 10;
    out += (gauss[256 + i] * old) >> 10;
    out = (int16_t)(uint16_t)out;   /* these three additions wrap */
    out += (gauss[i] * newest) >> 10;
    return clamp16(out) & ~1;
}

/* ---- envelope ------------------------------------------------------------------- */

static void run_envelope(const Dsp *d, DspVoice *v, const uint8_t *vr)
{
    int env = v->env;
    if (v->env_mode == DSP_ENV_RELEASE) {
        env -= 8;   /* every sample, whatever the settings */
        v->env = env < 0 ? 0 : env;
        return;
    }

    int rate, level;   /* level: register whose top 3 bits are the sustain level */
    uint8_t adsr0 = vr[DSP_ADSR0];
    if (adsr0 & 0x80) {
        level = vr[DSP_ADSR1];
        if (v->env_mode == DSP_ENV_ATTACK) {
            rate = (adsr0 & 0x0F) * 2 + 1;
            env += rate < 31 ? 0x20 : 0x400;
        } else {
            env -= 1;
            env -= env >> 8;
            rate = v->env_mode == DSP_ENV_DECAY ? ((adsr0 >> 4) & 7) * 2 + 16 : level & 0x1F;
        }
    } else {
        /* GAIN. The sustain comparison below then reads the GAIN register. */
        level = vr[DSP_GAIN];
        int mode = level >> 5;
        if (mode < 4) {
            env = level * 0x10;   /* direct */
            rate = 31;
        } else {
            rate = level & 0x1F;
            if (mode == 4) {
                env -= 0x20;   /* linear decrease */
            } else if (mode == 5) {
                env -= 1;      /* exponential decrease */
                env -= env >> 8;
            } else {
                env += 0x20;   /* linear increase; mode 7 slows above $600 */
                if (mode == 7 && (unsigned)v->hidden_env >= 0x600)
                    env += 0x08 - 0x20;
            }
        }
    }

    /* Phase changes don't wait for the rate counter. */
    if (v->env_mode == DSP_ENV_DECAY && (env >> 8) == (level >> 5))
        v->env_mode = DSP_ENV_SUSTAIN;
    v->hidden_env = env;
    if ((unsigned)env > 0x7FF) {
        env = env < 0 ? 0 : 0x7FF;
        if (v->env_mode == DSP_ENV_ATTACK)
            v->env_mode = DSP_ENV_DECAY;
    }
    if (dsp_rate_fires(d->counter, (unsigned)rate))
        v->env = env;
}

/* ---- one sample ------------------------------------------------------------------ */

static inline uint16_t le16(const uint8_t *ram, uint16_t addr)
{
    return (uint16_t)(ram[addr] | ram[(uint16_t)(addr + 1)] << 8);
}

void dsp_sample(Dsp *d, uint8_t *ram)
{
    uint8_t *r = d->regs;
    const uint8_t flg = r[DSP_FLG], pmon = r[DSP_PMON] & 0xFE, non = r[DSP_NON], eon = r[DSP_EON];
    const int noise_out = (int16_t)(uint16_t)(d->noise * 2);
    uint8_t endx = r[DSP_ENDX];
    int main_sum[2] = { 0, 0 }, echo_sum[2] = { 0, 0 };
    int prev_out = 0;

    for (int n = 0; n < 8; n++) {
        DspVoice *v = &d->voice[n];
        uint8_t *vr = r + n * 0x10;
        const uint8_t bit = (uint8_t)(1 << n);
        const uint16_t entry = (uint16_t)(r[DSP_DIR] * 0x100 + vr[DSP_SRCN] * 4);
        uint8_t header = ram[v->brr_addr];

        int pitch = vr[DSP_PITCHL] | (vr[DSP_PITCHH] & 0x3F) << 8;
        if (pmon & bit)
            pitch += ((prev_out >> 5) * pitch) >> 10;

        /* Key-on sequence: restart at the directory's start address, then three
         * blocks' worth of decoding to fill the history, with no envelope and no
         * pitch. */
        if (v->kon_delay) {
            if (v->kon_delay == 5) {
                v->brr_addr = le16(ram, entry);
                v->brr_offset = 1;
                v->buf_pos = 0;
                header = 0;
            }
            v->env = 0;
            v->hidden_env = 0;
            v->kon_delay--;
            v->interp_pos = (v->kon_delay & 3) ? 0x4000 : 0;
            pitch = 0;
        }

        int out = 0;
        if (v->env) {
            int s = (non & bit) ? noise_out : interpolate(v);
            out = ((s * v->env) >> 11) & ~1;
        }
        const uint8_t envx = (uint8_t)(v->env >> 4);

        /* End-without-loop blocks and soft reset silence immediately. */
        if ((flg & 0x80) || (header & 3) == 1) {
            v->env_mode = DSP_ENV_RELEASE;
            v->env = 0;
        }
        if (d->every_other) {
            if (d->koff & bit)
                v->env_mode = DSP_ENV_RELEASE;
            if (d->kon & bit) {
                v->kon_delay = 5;
                v->env_mode = DSP_ENV_ATTACK;
            }
        }
        if (!v->kon_delay)
            run_envelope(d, v, vr);

        /* Decode the next four samples once the counter passes them. */
        if (v->interp_pos >= 0x4000) {
            decode_brr(v, ram, header);
            v->brr_offset += 2;
            if (v->brr_offset >= 9) {
                v->brr_addr = (uint16_t)(v->brr_addr + 9);
                if (header & 1) {
                    v->brr_addr = le16(ram, (uint16_t)(entry + 2));
                    endx |= bit;
                }
                v->brr_offset = 1;
            }
        }
        v->interp_pos = (v->interp_pos & 0x3FFF) + pitch;
        if (v->interp_pos > 0x7FFF)
            v->interp_pos = 0x7FFF;

        for (int ch = 0; ch < 2; ch++) {
            int amp = (out * (int8_t)vr[DSP_VOLL + ch]) >> 7;
            main_sum[ch] = clamp16(main_sum[ch] + amp);
            if (eon & bit)
                echo_sum[ch] = clamp16(echo_sum[ch] + amp);
        }
        if (v->kon_delay == 5)
            endx &= (uint8_t)~bit;
        vr[DSP_ENVX] = envx;
        vr[DSP_OUTX] = (uint8_t)(out >> 8);
        prev_out = out;
    }
    r[DSP_ENDX] = endx;

    /* Echo: read the ring entry, run it through the FIR history, mix, and write
     * the voices plus feedback back into the same entry. */
    const uint16_t ptr = (uint16_t)((d->echo_esa << 8) + d->echo_offset);
    d->fir_pos = (uint8_t)((d->fir_pos + 1) & 7);
    int16_t *newest = d->fir_hist[d->fir_pos];
    newest[0] = (int16_t)((int16_t)le16(ram, ptr) >> 1);
    newest[1] = (int16_t)((int16_t)le16(ram, (uint16_t)(ptr + 2)) >> 1);

    int16_t frame[2];
    int echo_out[2];
    for (int ch = 0; ch < 2; ch++) {
        int sum = 0;
        for (int t = 0; t < 7; t++)   /* FIR0 weights the oldest sample */
            sum += (d->fir_hist[(d->fir_pos + 1 + t) & 7][ch] * (int8_t)r[t << 4 | DSP_FIR]) >> 6;
        sum = (int16_t)(uint16_t)sum;
        sum += (int16_t)(uint16_t)((newest[ch] * (int8_t)r[0x70 | DSP_FIR]) >> 6);
        const int echo_in = clamp16(sum) & ~1;

        const int mvol = (int8_t)r[DSP_MVOLL + (ch << 4)], evol = (int8_t)r[DSP_EVOLL + (ch << 4)];
        const int o = (int16_t)(uint16_t)((main_sum[ch] * mvol) >> 7) + (int16_t)(uint16_t)((echo_in * evol) >> 7);
        frame[ch] = (int16_t)clamp16(o);

        const int fb = echo_sum[ch] + (int16_t)(uint16_t)((echo_in * (int8_t)r[DSP_EFB]) >> 7);
        echo_out[ch] = clamp16(fb) & ~1;
    }
    if (flg & 0x40)
        frame[0] = frame[1] = 0;

    d->echo_esa = r[DSP_ESA];
    if (d->echo_offset == 0)
        d->echo_length = (uint16_t)((r[DSP_EDL] & 0x0F) * 0x800);
    d->echo_offset = (uint16_t)(d->echo_offset + 4);
    if (d->echo_offset >= d->echo_length)
        d->echo_offset = 0;
    for (int ch = 0; ch < 2; ch++) {
        if (ch == 1)
            d->echo_flg = flg;   /* the right write already sees this sample's FLG */
        if (!(d->echo_flg & 0x20)) {
            uint16_t a = (uint16_t)(ptr + 2 * ch);
            ram[a] = (uint8_t)echo_out[ch];
            ram[(uint16_t)(a + 1)] = (uint8_t)(echo_out[ch] >> 8);
        }
    }

    /* KON/KOFF are polled every other sample; a KON bit acts once. */
    d->every_other ^= 1;
    if (d->every_other) {
        d->new_kon &= (uint8_t)~d->kon;
        d->kon = d->new_kon;
        d->koff = r[DSP_KOFF];
    }
    if (--d->counter < 0)
        d->counter = COUNTER_RANGE - 1;
    if (dsp_rate_fires(d->counter, flg & 0x1F)) {
        unsigned n = d->noise;
        d->noise = (uint16_t)((n >> 1) | ((n ^ (n >> 1)) & 1) << 14);
    }

    d->samples++;
    if (d->out_count < DSP_OUT_FRAMES) {
        d->out[d->out_count * 2] = frame[0];
        d->out[d->out_count * 2 + 1] = frame[1];
        d->out_count++;
    } else {
        d->out_dropped++;
    }
}

size_t dsp_take_samples(Dsp *d, int16_t *out, size_t max_frames)
{
    size_t n = d->out_count < max_frames ? d->out_count : max_frames;
    if (n == 0)
        return 0;
    memcpy(out, d->out, n * 2 * sizeof *out);
    d->out_count -= (uint32_t)n;
    if (d->out_count)
        memmove(d->out, d->out + n * 2, d->out_count * 2 * sizeof *d->out);
    return n;
}
