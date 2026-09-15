#include "zst.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ZSNES 1.51 save state layout (file offsets; multi-byte values little-endian).
 * From ZsKnight's zst_frmt.txt and _Demo_'s SPC section description (romhacking.net
 * documents 170 and 171) and creaothceann's addendum (document 190), checked
 * against Chrono Trigger states: see docs/RUNTIME.md, "ZSNES save states". */
enum {
    ZST_MAGIC_LEN = 28,         /* "ZSNES Save State File V143", $1A, 143 */

    ZST_CURYPOS = 0x1D,         /* scanline the state was taken at (224) */
    ZST_SPCON = 0x24,
    ZST_A = 0x29, ZST_DB = 0x2B, ZST_PB = 0x2C, ZST_S = 0x2D, ZST_D = 0x2F,
    ZST_X = 0x31, ZST_Y = 0x33, ZST_P = 0x35, ZST_E = 0x36, ZST_PC = 0x37,

    ZST_NMITIMEN = 0x4A, ZST_VTIME = 0x4C, ZST_BRIGHTNESS = 0x4E, ZST_FORCE_BLANK = 0x50,
    ZST_OBJ_BASE = 0x51, ZST_OBJ_SIZE_SMALL = 0x59, ZST_OBJ_SIZE_LARGE = 0x5A, ZST_OAM_RELOAD = 0x63,
    ZST_BGMODE = 0x66, ZST_BG3_PRIORITY = 0x67, ZST_BG_TILE_SIZE = 0x68,
    ZST_MOSAIC_ON = 0x69, ZST_MOSAIC_SIZE = 0x6A,
    ZST_BG_MAP_BASE = 0x6B,     /* 4 words: VRAM byte address */
    ZST_BG_MAP_SIZE = 0x8B,     /* 4 bytes */
    ZST_BG_TILE_BASE = 0x8F,    /* 4 words: VRAM byte address */
    ZST_BG_HOFS = 0x97,         /* 4 words, then a debug word */
    ZST_BG_VOFS = 0xA1,         /* 4 words */
    ZST_VRAM_ADDR = 0xAD,       /* dword: VRAM byte address */
    ZST_CG_ADDR = 0xB1,
    ZST_SCREENS = 0xB4,         /* word: main, sub */
    ZST_WRMPYA = 0xB9, ZST_WRDIV = 0xBA, ZST_RDDIV = 0xBC, ZST_RDMPY = 0xBE,
    ZST_WINDOW_POS = 0xC7,      /* WH0-WH3 */
    ZST_WINDOW_SEL = 0xCB,      /* 6 nybbles: BG1-BG4, OBJ, color */
    ZST_WINDOW_LOGIC = 0xD1,    /* WBGLOG, WOBJLOG */
    ZST_WINDOW_MASK = 0xD3,     /* TMW, TSW */
    ZST_M7SEL = 0xD5, ZST_M7_MATRIX = 0xD6,   /* A, B, C, D, X, Y words */
    ZST_WRAM_PORT = 0xE9,
    ZST_DMA = 0xED,             /* $4300-$437F as 8 x 16 bytes */
    ZST_HDMA_CURRENT = 0x170,
    ZST_FIXED_COLOR = 0x20A,    /* red, green, blue */
    ZST_CGWSEL = 0x20E, ZST_CGADSUB = 0x20F,
    ZST_OAM = 0x218, ZST_CGRAM = 0x618,
    ZST_VMAIN = 0xA18, ZST_HTIME = 0xA27,

    ZST_WRAM = 0xC13, ZST_VRAM = 0x20C13,
    ZST_SPC_RAM = 0x30C13,      /* 64 KiB, then 16 bytes */
    ZST_SPC_PC = 0x40C23,       /* dwords: PC, A, X, Y, PSW, NZ, SP, direct page, cycles */
    ZST_SPC_A = 0x40C27, ZST_SPC_X = 0x40C2B, ZST_SPC_Y = 0x40C2F, ZST_SPC_PSW = 0x40C33,
    ZST_SPC_SP = 0x40C3B, ZST_SPC_DP = 0x40C3F,
    ZST_SPC_TO_CPU = 0x40C47,   /* ports as the main CPU reads them */
    ZST_TIMER_ON = 0x40C4B, ZST_TIMER_TARGET = 0x40C4C, ZST_TIMER_LEFT = 0x40C4F,
    ZST_VOICE_BLOCK = 0x40DEB,  /* 8 dwords: each voice's next BRR block in sound RAM */
    ZST_DSP = 0x4117F,          /* 128 register bytes (and 128 unused) */
    ZST_SRAM = 0x4135B,
};

static uint16_t rd16(const uint8_t *d, size_t o) { return (uint16_t)(d[o] | d[o + 1] << 8); }
static uint32_t rd32(const uint8_t *d, size_t o)
{
    return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24;
}

static void fail(char *err, size_t n, const char *msg)
{
    if (err && n)
        snprintf(err, n, "%s", msg);
}

/* ZSNES keeps OBSEL as the two sprite sizes in pixels / 8, squared. */
static uint8_t obsel_size(uint8_t small, uint8_t large)
{
    static const uint8_t pairs[6][2] = { { 1, 4 }, { 1, 16 }, { 1, 64 }, { 4, 16 }, { 4, 64 }, { 16, 64 } };
    for (uint8_t i = 0; i < 6; i++) {
        if (pairs[i][0] == small && pairs[i][1] == large)
            return i;
    }
    return 0;
}

static void ppu_word(Ppu *ppu, uint8_t reg, uint16_t v)
{
    ppu_write(ppu, reg, v & 0xFF);   /* through the shared latch: low byte, then high */
    ppu_write(ppu, reg, v >> 8);
}

static void import_ppu(Ppu *ppu, const uint8_t *z)
{
    memcpy(ppu->vram, z + ZST_VRAM, sizeof ppu->vram);
    memcpy(ppu->oam, z + ZST_OAM, sizeof ppu->oam);
    for (int i = 0; i < 256; i++)
        ppu->cgram[i] = rd16(z, ZST_CGRAM + 2 * (size_t)i) & 0x7FFF;

    ppu_write(ppu, 0x00, (uint8_t)((z[ZST_FORCE_BLANK] & 0x80) | (z[ZST_BRIGHTNESS] & 0x0F)));
    ppu_write(ppu, 0x01, (uint8_t)(obsel_size(z[ZST_OBJ_SIZE_SMALL], z[ZST_OBJ_SIZE_LARGE]) << 5 |
                                   ((rd32(z, ZST_OBJ_BASE) >> 14) & 7)));
    uint16_t oam_reload = rd16(z, ZST_OAM_RELOAD) & 0x3FF;
    ppu_write(ppu, 0x02, (uint8_t)(oam_reload >> 1));
    ppu_write(ppu, 0x03, (uint8_t)(oam_reload >> 9));
    ppu_write(ppu, 0x05, (uint8_t)((z[ZST_BGMODE] & 7) | (z[ZST_BG3_PRIORITY] ? 0x08 : 0) |
                                   (z[ZST_BG_TILE_SIZE] & 0x0F) << 4));
    ppu_write(ppu, 0x06, (uint8_t)((z[ZST_MOSAIC_SIZE] & 0x0F) << 4 | (z[ZST_MOSAIC_ON] & 0x0F)));
    for (uint8_t bg = 0; bg < 4; bg++) {
        uint16_t base = rd16(z, ZST_BG_MAP_BASE + 2 * (size_t)bg);
        ppu_write(ppu, (uint8_t)(0x07 + bg), (uint8_t)(((base >> 9) & 0xFC) | (z[ZST_BG_MAP_SIZE + bg] & 3)));
    }
    for (uint8_t pair = 0; pair < 2; pair++) {
        uint16_t lo = rd16(z, ZST_BG_TILE_BASE + 4 * (size_t)pair);
        uint16_t hi = rd16(z, ZST_BG_TILE_BASE + 4 * (size_t)pair + 2);
        ppu_write(ppu, (uint8_t)(0x0B + pair), (uint8_t)((lo >> 13) | (hi >> 13) << 4));
    }
    /* Mode 7 values first: BG1's scroll writes share their latch and set the
     * Mode 7 offsets from the same values. */
    ppu_write(ppu, 0x1A, z[ZST_M7SEL]);
    for (uint8_t i = 0; i < 6; i++)
        ppu_word(ppu, (uint8_t)(0x1B + i), rd16(z, ZST_M7_MATRIX + 2 * (size_t)i));
    for (uint8_t bg = 0; bg < 4; bg++) {
        ppu_word(ppu, (uint8_t)(0x0D + 2 * bg), rd16(z, ZST_BG_HOFS + 2 * (size_t)bg));
        ppu_word(ppu, (uint8_t)(0x0E + 2 * bg), rd16(z, ZST_BG_VOFS + 2 * (size_t)bg));
    }
    ppu_write(ppu, 0x15, z[ZST_VMAIN]);
    uint16_t vram_addr = (uint16_t)(rd32(z, ZST_VRAM_ADDR) >> 1);
    ppu_write(ppu, 0x16, vram_addr & 0xFF);
    ppu_write(ppu, 0x17, vram_addr >> 8);
    ppu_write(ppu, 0x21, (uint8_t)rd16(z, ZST_CG_ADDR));
    for (uint8_t i = 0; i < 3; i++)
        ppu_write(ppu, (uint8_t)(0x23 + i), (uint8_t)((z[ZST_WINDOW_SEL + 2 * i] & 0x0F) |
                                                      (z[ZST_WINDOW_SEL + 2 * i + 1] & 0x0F) << 4));
    for (uint8_t i = 0; i < 4; i++)
        ppu_write(ppu, (uint8_t)(0x26 + i), z[ZST_WINDOW_POS + i]);
    ppu_write(ppu, 0x2A, z[ZST_WINDOW_LOGIC]);
    ppu_write(ppu, 0x2B, z[ZST_WINDOW_LOGIC + 1]);
    ppu_write(ppu, 0x2C, z[ZST_SCREENS]);
    ppu_write(ppu, 0x2D, z[ZST_SCREENS + 1]);
    ppu_write(ppu, 0x2E, z[ZST_WINDOW_MASK]);
    ppu_write(ppu, 0x2F, z[ZST_WINDOW_MASK + 1]);
    ppu_write(ppu, 0x30, z[ZST_CGWSEL]);
    ppu_write(ppu, 0x31, z[ZST_CGADSUB]);
    for (uint8_t c = 0; c < 3; c++)
        ppu_write(ppu, 0x32, (uint8_t)((0x20 << c) | (z[ZST_FIXED_COLOR + c] & 0x1F)));
    ppu_write(ppu, 0x33, 0x00);

    /* Write-only latches and read state the file doesn't hold. */
    ppu->bgofs_latch = ppu->bghofs_latch = ppu->m7_latch = 0;
    ppu->cg_second = false;
    ppu->cg_latch = 0;
    ppu->oam_latch = 0;
    ppu->h_second = ppu->v_second = false;
    ppu->counters_latched = false;
    ppu->range_over = ppu->time_over = false;
    ppu->vram_writes++;   /* VRAM changed: the renderer's hint to recheck it */
}

static void import_bus(SnesBus *bus, const uint8_t *z)
{
    memcpy(bus->wram, z + ZST_WRAM, SNES_WRAM_SIZE);
    memcpy(bus->sram, z + ZST_SRAM, SNES_SRAM_SIZE);
    bus->nmitimen = z[ZST_NMITIMEN];
    bus->vtime = rd16(z, ZST_VTIME) & 0x1FF;
    bus->htime = rd16(z, ZST_HTIME) & 0x1FF;
    bus->wrmpya = z[ZST_WRMPYA];
    bus->wrdiv = rd16(z, ZST_WRDIV);
    bus->rddiv = rd16(z, ZST_RDDIV);
    bus->rdmpy = rd16(z, ZST_RDMPY);
    bus->wram_port_addr = rd32(z, ZST_WRAM_PORT) & 0xFFFFFF;
    for (int ch = 0; ch < 8; ch++)
        memcpy(bus->dma[ch].reg, z + ZST_DMA + 16 * (size_t)ch, 16);
    bus->hdmaen = z[ZST_HDMA_CURRENT];   /* the game's NMI handlers rewrite it */
    bus->mdmaen = 0;
    bus->fastrom = true;                 /* Chrono Trigger sets it at boot ($C2:0026); not in the state */
    bus->nmi_flag = false;
    bus->open_bus = 0;
}

static void import_cpu(Cpu *cpu, const uint8_t *z)
{
    cpu->a = rd16(z, ZST_A);
    cpu->x = rd16(z, ZST_X);
    cpu->y = rd16(z, ZST_Y);
    cpu->d = rd16(z, ZST_D);
    cpu->db = z[ZST_DB];
    cpu->pb = z[ZST_PB];
    cpu->pc = rd16(z, ZST_PC);
    cpu->p = z[ZST_P];
    cpu->e = z[ZST_E] & 1;
    cpu->s = cpu->e ? (uint16_t)(0x0100 | (rd16(z, ZST_S) & 0xFF)) : rd16(z, ZST_S);
    if (cpu->e)
        cpu->p |= CPU_FLAG_M | CPU_FLAG_X;
    if (cpu->p & CPU_FLAG_X) {
        cpu->x &= 0xFF;
        cpu->y &= 0xFF;
    }
    cpu->waiting = cpu->stopped = false;
}

/* The S-DSP: registers exactly; voices approximately. A voice with a nonzero
 * envelope goes on playing from the block ZSNES had reached, at its ENVX level,
 * with an empty decode history; the rest are silent. */
static void import_dsp(Dsp *dsp, const uint8_t *z)
{
    uint32_t dropped = dsp->out_dropped;
    uint64_t samples = dsp->samples;
    dsp_init(dsp);
    dsp->samples = samples;
    dsp->out_dropped = dropped;
    memcpy(dsp->regs, z + ZST_DSP, sizeof dsp->regs);
    const uint8_t *r = dsp->regs;
    dsp->koff = r[DSP_KOFF];
    dsp->echo_esa = r[DSP_ESA];
    dsp->echo_flg = r[DSP_FLG];
    dsp->echo_length = (uint16_t)((r[DSP_EDL] & 0x0F) * 0x800);
    for (int n = 0; n < 8; n++) {
        DspVoice *v = &dsp->voice[n];
        const uint8_t *vr = r + n * 0x10;
        v->brr_addr = (uint16_t)rd32(z, ZST_VOICE_BLOCK + 4 * (size_t)n);
        v->brr_offset = 1;
        v->env = vr[DSP_ENVX] << 4;
        v->hidden_env = v->env;
        if (!v->env || (dsp->koff & (1 << n)))
            v->env_mode = DSP_ENV_RELEASE;
        else if ((vr[DSP_ADSR0] & 0x80) && (v->env >> 8) > (vr[DSP_ADSR1] >> 5))
            v->env_mode = DSP_ENV_DECAY;
        else
            v->env_mode = DSP_ENV_SUSTAIN;
    }
}

static void import_apu(Apu *apu, const uint8_t *z)
{
    memcpy(apu->ram, z + ZST_SPC_RAM, sizeof apu->ram);
    const uint8_t *io = apu->ram + 0xF0;
    Spc700 *spc = &apu->spc;
    spc->pc = (uint16_t)rd32(z, ZST_SPC_PC);
    spc->a = z[ZST_SPC_A];
    spc->x = z[ZST_SPC_X];
    spc->y = z[ZST_SPC_Y];
    spc->sp = z[ZST_SPC_SP];
    spc->psw = (uint8_t)((z[ZST_SPC_PSW] & ~SPC_FLAG_P) | (rd32(z, ZST_SPC_DP) ? SPC_FLAG_P : 0));
    spc->halted = false;

    for (int i = 0; i < 4; i++) {
        apu->cpu_in[i] = io[0x04 + i];   /* sound RAM $F4-$F7 holds what the main CPU wrote */
        apu->cpu_out[i] = z[ZST_SPC_TO_CPU + i];
    }
    apu->control = io[0x01];
    apu->dsp_addr = io[0x02];
    for (int i = 0; i < 3; i++) {
        ApuTimer *t = &apu->timer[i];
        t->enabled = (z[ZST_TIMER_ON] >> i) & 1;
        t->target = io[0x0A + i];
        uint8_t left = z[ZST_TIMER_LEFT + i];
        t->stage = (uint8_t)(t->target - left);   /* ticks left before the output counts */
        t->output = io[0x0D + i] & 0x0F;
        t->divider = 0;
    }
    apu->phase = APU_DRIVER;
    apu->budget = 0;
    apu->lead = 0;
    apu->ipl_busy = 0;
    import_dsp(&apu->dsp, z);
}

int zst_import(Snes *snes, const uint8_t *z, size_t len, char *err, size_t err_len)
{
    static const char magic[ZST_MAGIC_LEN + 1] = "ZSNES Save State File V143\x1A\x8F";
    if (len < ZST_SRAM + SNES_SRAM_SIZE || memcmp(z, magic, ZST_MAGIC_LEN) != 0) {
        fail(err, err_len, "not a ZSNES 1.51 save state (header \"ZSNES Save State File V143\")");
        return -1;
    }
    if (!z[ZST_SPCON]) {
        fail(err, err_len, "the state was saved with sound off, so it has no SPC700 state");
        return -1;
    }
    import_bus(&snes->bus, z);
    import_cpu(&snes->cpu, z);
    import_ppu(&snes->ppu, z);
    import_apu(&snes->apu, z);
    snes->halted = false;
    snes_enter_vblank(snes);
    return 0;
}

int zst_import_file(Snes *snes, const char *path, char *err, size_t err_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fail(err, err_len, "cannot open the file");
        return -1;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
            data = malloc((size_t)n);
            if (data && fread(data, 1, (size_t)n, f) == (size_t)n)
                len = (size_t)n;
        }
    }
    fclose(f);
    int rc = -1;
    if (len)
        rc = zst_import(snes, data, len, err, err_len);
    else
        fail(err, err_len, "cannot read the file");
    free(data);
    return rc;
}
