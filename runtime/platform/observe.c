/* Video observer: the command-built VidFrame (platform/observe.h). */
#include "platform/observe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VidObserver *vid_observer;

/* A per-line effect as the observer follows it through a frame: the slot's
 * table, and the cursor HDMA keeps in it. */
typedef struct {
    VidLineEffect e;
    bool programmed;
    bool active, do_transfer;
    uint8_t table_bank;
    uint16_t cursor;     /* next byte of the table */
    uint16_t indirect;   /* indirect mode: next byte of the values */
    uint8_t count;       /* lines left, with the repeat bit */
} ObsSlot;

struct VidObserver {
    uint8_t (*read)(void *ctx, uint32_t addr);
    void *ctx;

    VidLineState state;
    uint8_t vram[0x10000];
    uint16_t cgram[256];
    uint8_t oam[0x220];

    /* Latches the scroll and Mode 7 registers share, which per-line effects and
     * the scroll calls both go through (as in runtime/snes/ppu.c). */
    uint8_t bgofs_latch, bghofs_latch, m7_latch;

    VidVramPort port;
    uint16_t vram_addr;
    uint8_t cg_addr, cg_latch;
    bool cg_second;
    uint16_t oam_addr;
    uint8_t oam_latch;
    uint8_t oam_first_sprite;   /* the priority rotation, as the address register sets it */

    ObsSlot slot[8];
    uint8_t enabled_slots;

    bool palette_dirty;
    uint32_t unmodelled;
    VidFrame frame;
};

VidObserver *vid_obs_create(uint8_t (*read)(void *ctx, uint32_t addr), void *ctx)
{
    VidObserver *ob = calloc(1, sizeof *ob);
    ob->read = read;
    ob->ctx = ctx;
    ob->port.step = 1;
    ob->state.mosaic_size = 1;
    ob->state.display.blank = true;   /* forced blank at power-on */
    ob->frame.vram = ob->vram;
    ob->frame.cgram = ob->cgram;
    return ob;
}

void vid_obs_destroy(VidObserver *ob) { free(ob); }

uint32_t vid_obs_unmodelled(const VidObserver *ob) { return ob->unmodelled; }

const VidFrame *vid_obs_frame(const VidObserver *ob) { return &ob->frame; }

void vid_obs_seed_memory(VidObserver *ob, const uint8_t *vram, const uint16_t *cgram, const uint8_t *oam)
{
    memcpy(ob->vram, vram, sizeof ob->vram);
    memcpy(ob->cgram, cgram, sizeof ob->cgram);
    memcpy(ob->oam, oam, sizeof ob->oam);
    ob->palette_dirty = true;
}

void vid_obs_seed(VidObserver *ob, const VidLineState *state, const uint8_t *vram, const uint16_t *cgram,
                  const uint8_t *oam)
{
    ob->state = *state;
    vid_obs_seed_memory(ob, vram, cgram, oam);
}

/* ---- video memory ------------------------------------------------------------ */

static uint16_t vram_mapped(const VidObserver *ob)
{
    uint16_t a = ob->vram_addr;
    switch (ob->port.remap) {
    case 1: a = (uint16_t)((a & 0xFF00) | ((a << 3) & 0x00F8) | ((a >> 5) & 0x0007)); break;
    case 2: a = (uint16_t)((a & 0xFE00) | ((a << 3) & 0x01F8) | ((a >> 6) & 0x0007)); break;
    case 3: a = (uint16_t)((a & 0xFC00) | ((a << 3) & 0x03F8) | ((a >> 7) & 0x0007)); break;
    default: break;
    }
    return (uint16_t)(a & 0x7FFF);
}

static void vram_put(VidObserver *ob, bool high, uint8_t v)
{
    ob->vram[(uint16_t)((vram_mapped(ob) << 1) + (high ? 1 : 0))] = v;
    if (ob->port.after_high == high)
        ob->vram_addr = (uint16_t)(ob->vram_addr + ob->port.step);
}

/* CGRAM and OAM through their ports, as runtime/snes/ppu.c writes them. */
static void cgram_put(VidObserver *ob, uint8_t v)
{
    if (!ob->cg_second) {
        ob->cg_latch = v;
    } else {
        ob->cgram[ob->cg_addr++] = (uint16_t)((v & 0x7F) << 8 | ob->cg_latch);
        ob->palette_dirty = true;
    }
    ob->cg_second = !ob->cg_second;
}

static void oam_put(VidObserver *ob, uint8_t v)
{
    if (ob->oam_addr & 0x200) {
        ob->oam[0x200 + (ob->oam_addr & 0x1F)] = v;
    } else if (!(ob->oam_addr & 1)) {
        ob->oam_latch = v;
    } else {
        ob->oam[ob->oam_addr - 1] = ob->oam_latch;
        ob->oam[ob->oam_addr] = v;
    }
    ob->oam_addr = (uint16_t)((ob->oam_addr + 1) & 0x3FF);
}

/* ---- the state the commands build -------------------------------------------- */

static void set_scroll_h(VidObserver *ob, int bg, uint8_t v)
{
    if (bg == 0) {
        ob->state.mode7.hofs = (int16_t)(((uint16_t)(v << 8 | ob->m7_latch) & 0x1FFF) ^ 0x1000) - 0x1000;
        ob->m7_latch = v;
    }
    ob->state.bg[bg].hofs = (uint16_t)((v << 8 | (ob->bgofs_latch & ~7) | (ob->bghofs_latch & 7)) & 0x3FF);
    ob->bgofs_latch = v;
    ob->bghofs_latch = v;
}

static void set_scroll_v(VidObserver *ob, int bg, uint8_t v)
{
    if (bg == 0) {
        ob->state.mode7.vofs = (int16_t)(((uint16_t)(v << 8 | ob->m7_latch) & 0x1FFF) ^ 0x1000) - 0x1000;
        ob->m7_latch = v;
    }
    ob->state.bg[bg].vofs = (uint16_t)((v << 8 | ob->bgofs_latch) & 0x3FF);
    ob->bgofs_latch = v;
}

static void set_fixed_channels(VidObserver *ob, uint8_t channels, uint8_t intensity)
{
    uint16_t c = intensity & 31;
    if (channels & VID_RED)
        ob->state.fixed_color = (uint16_t)((ob->state.fixed_color & ~0x001F) | c);
    if (channels & VID_GREEN)
        ob->state.fixed_color = (uint16_t)((ob->state.fixed_color & ~0x03E0) | c << 5);
    if (channels & VID_BLUE)
        ob->state.fixed_color = (uint16_t)((ob->state.fixed_color & ~0x7C00) | c << 10);
}

void vid_obs_display(VidObserver *ob, const VidDisplay *d) { ob->state.display = *d; }
void vid_obs_mode(VidObserver *ob, const VidMode *m)
{
    ob->state.mode = *m;
    for (int bg = 0; bg < 4; bg++)
        ob->state.bg[bg].large_tiles = (m->large_tiles >> bg) & 1;
}

void vid_obs_bg_map(VidObserver *ob, VidLayer bg, const VidBg *b)
{
    ob->state.bg[bg].map_base = b->map_base;
    ob->state.bg[bg].map_w = b->map_w;
    ob->state.bg[bg].map_h = b->map_h;
}

void vid_obs_bg_chars(VidObserver *ob, VidLayer first, uint16_t base0, uint16_t base1)
{
    ob->state.bg[first].char_base = base0;
    ob->state.bg[first + 1].char_base = base1;
}

void vid_obs_scroll(VidObserver *ob, VidLayer bg, uint16_t hofs, uint16_t vofs, uint32_t fields)
{
    if (fields & VID_SCROLL_H) {
        set_scroll_h(ob, bg, (uint8_t)hofs);
        set_scroll_h(ob, bg, (uint8_t)(hofs >> 8));
    }
    if (fields & VID_SCROLL_V) {
        set_scroll_v(ob, bg, (uint8_t)vofs);
        set_scroll_v(ob, bg, (uint8_t)(vofs >> 8));
    }
}

void vid_obs_mosaic(VidObserver *ob, uint8_t bgs, uint8_t size)
{
    ob->state.mosaic_size = size;
    for (int bg = 0; bg < 4; bg++)
        ob->state.bg[bg].mosaic = (bgs >> bg) & 1;
}

void vid_obs_mode7(VidObserver *ob, const VidMode7 *m7, uint32_t fields)
{
    VidMode7 *d = &ob->state.mode7;
    if (fields & VID_M7_SELECT) {
        d->hflip = m7->hflip;
        d->vflip = m7->vflip;
        d->outside = m7->outside;
    }
    if (fields & VID_M7_MATRIX) {
        d->a = m7->a;
        d->b = m7->b;
        d->c = m7->c;
        d->d = m7->d;
        ob->m7_latch = (uint8_t)((uint16_t)m7->d >> 8);
    }
    if (fields & VID_M7_MATRIX_AB) {
        d->a = m7->a;
        d->b = m7->b;
        ob->m7_latch = (uint8_t)((uint16_t)m7->b >> 8);
    }
    if (fields & VID_M7_MATRIX_A) {
        d->a = m7->a;
        ob->m7_latch = (uint8_t)((uint16_t)m7->a >> 8);
    }
    if (fields & VID_M7_MATRIX_B_HIGH) {
        uint8_t hi = (uint8_t)((uint16_t)m7->b >> 8);
        d->b = (int16_t)((uint16_t)(hi << 8) | ob->m7_latch);
        ob->m7_latch = hi;
    }
    if (fields & VID_M7_MATRIX_B) {
        d->b = m7->b;
        ob->m7_latch = (uint8_t)((uint16_t)m7->b >> 8);
    }
    if (fields & VID_M7_CENTER) {
        d->cx = vid_mode7_13bit((uint16_t)m7->cx);
        d->cy = vid_mode7_13bit((uint16_t)m7->cy);
        ob->m7_latch = (uint8_t)((uint16_t)m7->cy >> 8);
    }
    if (fields & VID_M7_SCROLL) {
        set_scroll_h(ob, 0, (uint8_t)m7->hofs);
        set_scroll_h(ob, 0, (uint8_t)((uint16_t)m7->hofs >> 8));
        set_scroll_v(ob, 0, (uint8_t)m7->vofs);
        set_scroll_v(ob, 0, (uint8_t)((uint16_t)m7->vofs >> 8));
    }
}

void vid_obs_obj_config(VidObserver *ob, const VidObjConfig *c) { ob->state.obj = *c; }

void vid_obs_screens(VidObserver *ob, uint8_t main_layers, uint8_t sub_layers, uint32_t fields)
{
    if (fields & VID_SCREEN_MAIN)
        ob->state.main_layers = main_layers;
    if (fields & VID_SCREEN_SUB)
        ob->state.sub_layers = sub_layers;
}

void vid_obs_video_config(VidObserver *ob, const VidVideoConfig *c) { ob->state.config = *c; }

void vid_obs_windows(VidObserver *ob, const VidWindows *w, uint32_t fields)
{
    static const uint8_t pair_layer[3][2] = { { VID_BG1, VID_BG2 }, { VID_BG3, VID_BG4 }, { VID_OBJ, VID_BACK } };
    VidWindows *d = &ob->state.windows;
    for (int g = 0; g < 3; g++)
        if (fields & (1u << g))
            for (int i = 0; i < 2; i++) {
                d->w1[pair_layer[g][i]] = w->w1[pair_layer[g][i]];
                d->w2[pair_layer[g][i]] = w->w2[pair_layer[g][i]];
            }
    if (fields & VID_WINDOWS_W1_LEFT) d->w1_left = w->w1_left;
    if (fields & VID_WINDOWS_W1_RIGHT) d->w1_right = w->w1_right;
    if (fields & VID_WINDOWS_W2_LEFT) d->w2_left = w->w2_left;
    if (fields & VID_WINDOWS_W2_RIGHT) d->w2_right = w->w2_right;
    if (fields & VID_WINDOWS_BG_LOGIC)
        for (int l = 0; l < 4; l++)
            d->logic[l] = w->logic[l];
    if (fields & VID_WINDOWS_OBJ_LOGIC) {
        d->logic[VID_OBJ] = w->logic[VID_OBJ];
        d->logic[VID_BACK] = w->logic[VID_BACK];
        d->snes_extra_logic = w->snes_extra_logic;
    }
    if (fields & VID_WINDOWS_MAIN) d->win_main = w->win_main;
    if (fields & VID_WINDOWS_SUB) d->win_sub = w->win_sub;
}

void vid_obs_color_math(VidObserver *ob, const VidColorMath *m, uint32_t fields)
{
    VidColorMath *d = &ob->state.math;
    if (fields & VID_MATH_CONTROL) {
        d->clip_black = m->clip_black;
        d->prevent_math = m->prevent_math;
        d->sub_screen = m->sub_screen;
        d->direct = m->direct;
        d->snes_extra = m->snes_extra;
    }
    if (fields & VID_MATH_LAYERS) {
        d->subtract = m->subtract;
        d->half = m->half;
        d->layers = m->layers;
    }
}

void vid_obs_fixed_color(VidObserver *ob, uint8_t channels, uint8_t intensity)
{
    set_fixed_channels(ob, channels, intensity);
}

void vid_obs_line_effect(VidObserver *ob, uint8_t slot, const VidLineEffect *e)
{
    ob->slot[slot & 7].e = *e;
    ob->slot[slot & 7].programmed = true;
}

void vid_obs_line_table(VidObserver *ob, uint8_t slot, uint16_t table)
{
    PfData *t = &ob->slot[slot & 7].e.table;
    t->addr = (t->addr & 0xFF0000) | table;
}

void vid_obs_enable_line_effects(VidObserver *ob, uint8_t slots) { ob->enabled_slots = slots; }

void vid_obs_vram_port(VidObserver *ob, const VidVramPort *port) { ob->port = *port; }

void vid_obs_vram_address(VidObserver *ob, uint16_t vram_addr) { ob->vram_addr = vram_addr; }

void vid_obs_palette_address(VidObserver *ob, uint8_t first)
{
    ob->cg_addr = first;
    ob->cg_second = false;
}

void vid_obs_oam_address(VidObserver *ob, uint16_t word_addr, bool priority_rotation)
{
    ob->oam_addr = (uint16_t)((word_addr & 0x1FF) << 1);
    ob->oam_first_sprite = priority_rotation ? (uint8_t)((ob->oam_addr >> 2) & 0x7F) : 0;
}

void vid_obs_write_vram(VidObserver *ob, uint16_t value, int half)
{
    if (half < 0) {
        vram_put(ob, false, (uint8_t)value);
        vram_put(ob, true, (uint8_t)(value >> 8));
    } else {
        vram_put(ob, half != 0, (uint8_t)value);
    }
}

void vid_obs_upload_vram(VidObserver *ob, uint16_t vram_addr, PfData src, uint32_t len, bool fill,
                         const VidVramPort *port)
{
    if (port)
        ob->port = *port;
    if (len == 0)
        return;
    ob->vram_addr = vram_addr;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t a = src.addr + (fill ? (i & 1) : i);
        vram_put(ob, (i & 1) != 0, ob->read(ob->ctx, a));
    }
}

void vid_obs_upload_vram_half(VidObserver *ob, uint16_t vram_addr, PfData src, uint32_t len,
                              VidVramHalf half, const VidVramPort *port)
{
    if (port)
        ob->port = *port;
    if (len == 0)
        return;
    ob->vram_addr = vram_addr;
    for (uint32_t i = 0; i < len; i++)
        vram_put(ob, half == VID_VRAM_HIGH_BYTES, ob->read(ob->ctx, src.addr + i));
}

void vid_obs_upload_palette(VidObserver *ob, uint8_t first, PfData src, uint16_t count)
{
    if (count == 0)
        return;
    uint8_t at = first;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t lo = ob->read(ob->ctx, src.addr + 2u * i);
        uint8_t hi = ob->read(ob->ctx, src.addr + 2u * i + 1);
        ob->cgram[at++] = (uint16_t)((hi & 0x7F) << 8 | lo);
    }
    ob->palette_dirty = true;
}

void vid_obs_upload_oam(VidObserver *ob, PfData oam)
{
    for (uint32_t i = 0; i < sizeof ob->oam; i++)
        ob->oam[i] = ob->read(ob->ctx, oam.addr + i);
}

void vid_obs_fill_oam(VidObserver *ob, uint16_t word_addr, uint8_t value, uint32_t len)
{
    vid_obs_oam_address(ob, word_addr, false);
    for (uint32_t i = 0; i < len; i++)
        oam_put(ob, value);
}

void vid_obs_write_oam(VidObserver *ob, uint8_t v) { oam_put(ob, v); }

/* The SNES-only escape: a transfer whose mode and destination are game data.
 * Where the destination is video memory the observer can still follow it, byte
 * by byte in the pattern the transfer mode gives (as runtime/snes/bus.c does);
 * a transfer that reads a register into memory, or writes the work RAM port,
 * changes no video state, and anything else is counted as not modelled. */
void vid_obs_snes_transfer(VidObserver *ob, const VidSnesTransfer *t, uint8_t control)
{
    static const uint8_t pattern[8][4] = {
        { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
        { 0, 1, 2, 3 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
    };
    static const uint8_t unit_len[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };
    if (control & 0x80)
        return;
    if ((t->dest & 0xFC) == 0x80)   /* $2180-$2183: memory, not video */
        return;
    if (t->target_reg == 0x16)
        ob->vram_addr = t->target;
    else if (t->target_reg == 0x21)
        ob->cg_addr = (uint8_t)t->target;
    else if (t->target_reg != 0) {
        ob->unmodelled++;
        return;
    }
    uint8_t mode = control & 7;
    bool fixed = (control & 0x08) != 0;
    int step = (control & 0x10) ? -1 : 1;
    uint32_t len = t->size ? t->size : 0x10000;
    uint32_t bank = t->source & 0xFF0000;
    uint16_t addr = (uint16_t)t->source;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t reg = (uint8_t)(t->dest + pattern[mode][i % unit_len[mode]]);
        uint8_t v = ob->read(ob->ctx, bank | addr);
        if (reg == 0x18 || reg == 0x19)
            vram_put(ob, reg == 0x19, v);
        else if (reg == 0x22)
            cgram_put(ob, v);
        else if (reg == 0x04)
            oam_put(ob, v);
        else {
            ob->unmodelled++;
            return;
        }
        if (!fixed)
            addr = (uint16_t)(addr + step);
    }
}

/* ---- per-line effects --------------------------------------------------------- */

/* Values per line and which of the target's fields each carries, following the
 * transfer modes the SNES backend programs (platform/snes/video.h). */
static uint8_t effect_unit_len(VidLineTarget target)
{
    switch (target) {
    case VID_LINE_WINDOWS: return 4;
    case VID_LINE_WINDOW1:
    case VID_LINE_WINDOW2: return 2;
    case VID_LINE_SCROLL_BG1:
    case VID_LINE_SCROLL_BG2:
    case VID_LINE_SCROLL_BG3: return 4;
    case VID_LINE_BG_MAPS:
    case VID_LINE_SCREENS:
    case VID_LINE_COLOR_MATH: return 4;
    case VID_LINE_MODE:
    case VID_LINE_MATH_LAYERS:
    case VID_LINE_MATH_CONTROL_AND_LAYERS:
    case VID_LINE_SCROLL_BG1_H:
    case VID_LINE_SCROLL_BG1_V:
    case VID_LINE_SCROLL_BG2_V:
    case VID_LINE_SCROLL_BG3_H:
    case VID_LINE_SCROLL_BG3_V: return 2;
    case VID_LINE_MODE7_AB:
    case VID_LINE_MODE7_CD:
    case VID_LINE_MODE7_CENTER: return 4;
    case VID_LINE_MODE7_A:
    case VID_LINE_MODE7_B:
    case VID_LINE_MODE7_C:
    case VID_LINE_MODE7_D: return 2;
    default: return 1;
    }
}

static void effect_apply(VidObserver *ob, VidLineTarget target, int i, uint8_t v)
{
    switch (target) {
    case VID_LINE_WINDOWS:
        if (i == 0) ob->state.windows.w1_left = v;
        else if (i == 1) ob->state.windows.w1_right = v;
        else if (i == 2) ob->state.windows.w2_left = v;
        else ob->state.windows.w2_right = v;
        break;
    case VID_LINE_WINDOW1:
        if (i == 0) ob->state.windows.w1_left = v;
        else ob->state.windows.w1_right = v;
        break;
    case VID_LINE_WINDOW2:
        if (i == 0) ob->state.windows.w2_left = v;
        else ob->state.windows.w2_right = v;
        break;
    case VID_LINE_SCROLL_BG1:
    case VID_LINE_SCROLL_BG2:
    case VID_LINE_SCROLL_BG3: {
        int bg = target - VID_LINE_SCROLL_BG1;
        if (i < 2)
            set_scroll_h(ob, bg, v);
        else
            set_scroll_v(ob, bg, v);
        break;
    }
    case VID_LINE_FIXED_COLOR:
        set_fixed_channels(ob, (uint8_t)(v >> 5), (uint8_t)(v & 0x1F));
        break;
    case VID_LINE_BRIGHTNESS:
        ob->state.display = vid_display_decode(v);
        break;
    case VID_LINE_BG_MAPS:
        vid_bg_map_decode(&ob->state.bg[i], v);
        break;
    case VID_LINE_SCREENS:
        if (i == 0) ob->state.main_layers = v;
        else if (i == 1) ob->state.sub_layers = v;
        else if (i == 2) ob->state.windows.win_main = v;
        else ob->state.windows.win_sub = v;
        break;
    case VID_LINE_COLOR_MATH:
        if (i == 0) vid_color_math_decode(&ob->state.math, VID_MATH_CONTROL, v);
        else if (i == 1) vid_color_math_decode(&ob->state.math, VID_MATH_LAYERS, v);
        else if (i == 2) set_fixed_channels(ob, (uint8_t)(v >> 5), (uint8_t)(v & 0x1F));
        else ob->state.config = vid_video_config_decode(v);
        break;
    case VID_LINE_MODE:
        if (i == 0) {
            VidMode m = vid_mode_decode(v);
            vid_obs_mode(ob, &m);
        } else {
            vid_obs_mosaic(ob, (uint8_t)(v & 0x0F), (uint8_t)((v >> 4) + 1));
        }
        break;
    case VID_LINE_MATH_LAYERS:
        if (i == 0)
            vid_color_math_decode(&ob->state.math, VID_MATH_LAYERS, v);
        else
            set_fixed_channels(ob, (uint8_t)(v >> 5), (uint8_t)(v & 0x1F));
        break;
    case VID_LINE_WINDOW2_LEFT:
        ob->state.windows.w2_left = v;
        break;
    case VID_LINE_WINDOW2_RIGHT:
        ob->state.windows.w2_right = v;
        break;
    case VID_LINE_BG_MODE: {
        VidMode m = vid_mode_decode(v);
        vid_obs_mode(ob, &m);
        break;
    }
    case VID_LINE_MAIN_SCREEN:
        ob->state.main_layers = v;
        break;
    case VID_LINE_MATH_LAYERS_ONLY:
        vid_color_math_decode(&ob->state.math, VID_MATH_LAYERS, v);
        break;
    case VID_LINE_MATH_CONTROL_AND_LAYERS:
        vid_color_math_decode(&ob->state.math, i == 0 ? VID_MATH_CONTROL : VID_MATH_LAYERS, v);
        break;
    case VID_LINE_SCROLL_BG1_H:
        set_scroll_h(ob, 0, v);
        break;
    case VID_LINE_SCROLL_BG1_V:
        set_scroll_v(ob, 0, v);
        break;
    case VID_LINE_SCROLL_BG2_V:
        set_scroll_v(ob, 1, v);
        break;
    case VID_LINE_SCROLL_BG3_H:
        set_scroll_h(ob, 2, v);
        break;
    case VID_LINE_SCROLL_BG3_V:
        set_scroll_v(ob, 2, v);
        break;
    case VID_LINE_WINDOW1_LEFT:
        ob->state.windows.w1_left = v;
        break;
    case VID_LINE_WINDOW1_RIGHT:
        ob->state.windows.w1_right = v;
        break;
    case VID_LINE_MODE7_AB:
    case VID_LINE_MODE7_CD: {
        /* The Mode 7 registers share one latch: each write leaves the byte
         * before it in the low half, so a pair of writes is one value. */
        VidMode7 *m = &ob->state.mode7;
        int16_t *value = target == VID_LINE_MODE7_AB ? (i < 2 ? &m->a : &m->b) : (i < 2 ? &m->c : &m->d);
        *value = (int16_t)((uint16_t)v << 8 | ob->m7_latch);
        ob->m7_latch = v;
        break;
    }
    case VID_LINE_MODE7_A:
    case VID_LINE_MODE7_B:
    case VID_LINE_MODE7_C:
    case VID_LINE_MODE7_D: {
        VidMode7 *m = &ob->state.mode7;
        int16_t *value = target == VID_LINE_MODE7_A ? &m->a : target == VID_LINE_MODE7_B ? &m->b
                       : target == VID_LINE_MODE7_C ? &m->c : &m->d;
        *value = (int16_t)((uint16_t)v << 8 | ob->m7_latch);
        ob->m7_latch = v;
        break;
    }
    case VID_LINE_BG2_MAP:
        vid_bg_map_decode(&ob->state.bg[1], v);
        break;
    case VID_LINE_MODE7_CENTER: {
        /* through the Mode 7 latch, as the matrix's pairs */
        VidMode7 *m = &ob->state.mode7;
        int16_t *value = i < 2 ? &m->cx : &m->cy;
        *value = vid_mode7_13bit((uint16_t)((uint16_t)v << 8 | ob->m7_latch));
        ob->m7_latch = v;
        break;
    }
    case VID_LINE_TARGETS:
        break;
    }
}

static void slot_load_entry(VidObserver *ob, ObsSlot *sl)
{
    uint32_t bank = (uint32_t)sl->table_bank << 16;
    sl->count = ob->read(ob->ctx, bank | sl->cursor);
    sl->cursor++;
    if (sl->e.indirect) {
        sl->indirect = (uint16_t)(ob->read(ob->ctx, bank | sl->cursor) |
                                  ob->read(ob->ctx, bank | (uint16_t)(sl->cursor + 1)) << 8);
        sl->cursor = (uint16_t)(sl->cursor + 2);
    }
    if (sl->count == 0)
        sl->active = false;
    sl->do_transfer = true;
}

/* The start of a frame reloads every enabled slot's table cursor. */
static void effects_init(VidObserver *ob)
{
    for (int i = 0; i < 8; i++) {
        ObsSlot *sl = &ob->slot[i];
        sl->active = (ob->enabled_slots & (1u << i)) && sl->programmed;
        if (!sl->active)
            continue;
        sl->table_bank = (uint8_t)(sl->e.table.addr >> 16);
        sl->cursor = (uint16_t)sl->e.table.addr;
        slot_load_entry(ob, sl);
    }
}

/* One line's worth of every enabled slot, as the hardware does it after the
 * line has been reached. */
static void effects_line(VidObserver *ob)
{
    for (int i = 0; i < 8; i++) {
        ObsSlot *sl = &ob->slot[i];
        if (!sl->active || !(ob->enabled_slots & (1u << i)))
            continue;
        if (sl->do_transfer) {
            int n = effect_unit_len(sl->e.target);
            for (int k = 0; k < n; k++) {
                uint32_t src;
                if (sl->e.indirect) {
                    src = (uint32_t)sl->e.indirect_bank << 16 | sl->indirect;
                    sl->indirect++;
                } else {
                    src = (uint32_t)sl->table_bank << 16 | sl->cursor;
                    sl->cursor++;
                }
                effect_apply(ob, sl->e.target, k, ob->read(ob->ctx, src));
            }
        }
        sl->count--;
        sl->do_transfer = (sl->count & 0x80) != 0;
        if (!(sl->count & 0x7F))
            slot_load_entry(ob, sl);
    }
}

/* ---- the frame ---------------------------------------------------------------- */

static void decode_sprites(VidObserver *ob)
{
    for (int n = 0; n < VID_SPRITES; n++) {
        const uint8_t *e = &ob->oam[n * 4];
        uint8_t hi = (uint8_t)(ob->oam[0x200 + n / 4] >> ((n % 4) * 2));
        ob->frame.sprites[n] = (VidSprite){ .x = (int16_t)(e[0] | (hi & 1) << 8),
                                            .y = e[1],
                                            .tile = (uint16_t)(e[2] | (e[3] & 1) << 8),
                                            .palette = (uint8_t)((e[3] >> 1) & 7),
                                            .priority = (uint8_t)((e[3] >> 4) & 3),
                                            .hflip = (e[3] & 0x40) != 0,
                                            .vflip = (e[3] & 0x80) != 0,
                                            .large = ((hi >> 1) & 1) != 0 };
    }
}

void vid_obs_frame_start(VidObserver *ob)
{
    ob->frame.span_count = 0;
    effects_init(ob);
}

void vid_obs_line(VidObserver *ob, uint16_t line)
{
    if (line == 0) {
        effects_line(ob);
        return;
    }
    if (line == 1) {
        decode_sprites(ob);
        ob->frame.first_sprite = ob->oam_first_sprite;
    }
    VidSpan *last = ob->frame.span_count ? &ob->frame.spans[ob->frame.span_count - 1] : NULL;
    if (!last || ob->palette_dirty || memcmp(&last->state, &ob->state, sizeof ob->state) != 0) {
        if (ob->frame.span_count < VID_MAX_SPANS) {
            VidSpan *sp = &ob->frame.spans[ob->frame.span_count++];
            sp->line = line;
            sp->palette_changed = ob->palette_dirty;
            sp->state = ob->state;
        }
        ob->palette_dirty = false;
    }
    effects_line(ob);
}

/* ---- comparison ---------------------------------------------------------------- */

#define DIFF(what, fmt, ...)                                                                                           \
    do {                                                                                                               \
        out->kind = (what);                                                                                            \
        snprintf(out->text, sizeof out->text, fmt, __VA_ARGS__);                                                       \
        return true;                                                                                                   \
    } while (0)

#define CMP(what, field)                                                                                               \
    do {                                                                                                               \
        if ((long)(x->field) != (long)(y->field))                                                                      \
            DIFF(what, "line %u: " what " %ld, commands %ld", (unsigned)line, (long)(x->field), (long)(y->field));      \
    } while (0)

static bool state_diff(const VidLineState *x, const VidLineState *y, unsigned line, VidDiff *out)
{
    CMP("display.brightness", display.brightness);
    CMP("display.blank", display.blank);
    CMP("display.snes_extra", display.snes_extra);
    CMP("mode.mode", mode.mode);
    CMP("mode.bg3_priority", mode.bg3_priority);
    CMP("mode.large_tiles", mode.large_tiles);
    CMP("mosaic_size", mosaic_size);
    for (int b = 0; b < 4; b++) {
        if (x->bg[b].map_base != y->bg[b].map_base)
            DIFF("bg.map_base", "line %u: bg%d.map_base $%04X, commands $%04X", line, b + 1, x->bg[b].map_base,
                 y->bg[b].map_base);
        if (x->bg[b].map_w != y->bg[b].map_w || x->bg[b].map_h != y->bg[b].map_h)
            DIFF("bg.map_size", "line %u: bg%d map size %ux%u, commands %ux%u", line, b + 1, x->bg[b].map_w,
                 x->bg[b].map_h, y->bg[b].map_w, y->bg[b].map_h);
        if (x->bg[b].char_base != y->bg[b].char_base)
            DIFF("bg.char_base", "line %u: bg%d.char_base $%04X, commands $%04X", line, b + 1, x->bg[b].char_base,
                 y->bg[b].char_base);
        if (x->bg[b].hofs != y->bg[b].hofs)
            DIFF("bg.hofs", "line %u: bg%d.hofs %u, commands %u", line, b + 1, x->bg[b].hofs, y->bg[b].hofs);
        if (x->bg[b].vofs != y->bg[b].vofs)
            DIFF("bg.vofs", "line %u: bg%d.vofs %u, commands %u", line, b + 1, x->bg[b].vofs, y->bg[b].vofs);
        if (x->bg[b].mosaic != y->bg[b].mosaic || x->bg[b].large_tiles != y->bg[b].large_tiles)
            DIFF("bg.mosaic", "line %u: bg%d mosaic/large %d%d, commands %d%d", line, b + 1, x->bg[b].mosaic,
                 x->bg[b].large_tiles, y->bg[b].mosaic, y->bg[b].large_tiles);
    }
    CMP("mode7.a", mode7.a);
    CMP("mode7.b", mode7.b);
    CMP("mode7.c", mode7.c);
    CMP("mode7.d", mode7.d);
    CMP("mode7.cx", mode7.cx);
    CMP("mode7.cy", mode7.cy);
    CMP("mode7.hofs", mode7.hofs);
    CMP("mode7.vofs", mode7.vofs);
    CMP("mode7.hflip", mode7.hflip);
    CMP("mode7.vflip", mode7.vflip);
    CMP("mode7.outside", mode7.outside);
    CMP("obj.size_select", obj.size_select);
    CMP("obj.base", obj.base);
    CMP("obj.name_select", obj.name_select);
    for (int l = 0; l < VID_LAYERS; l++) {
        if (x->windows.w1[l] != y->windows.w1[l] || x->windows.w2[l] != y->windows.w2[l])
            DIFF("windows.use", "line %u: window use of layer %d %d/%d, commands %d/%d", line, l, x->windows.w1[l],
                 x->windows.w2[l], y->windows.w1[l], y->windows.w2[l]);
        if (x->windows.logic[l] != y->windows.logic[l])
            DIFF("windows.logic", "line %u: window logic of layer %d %d, commands %d", line, l, x->windows.logic[l],
                 y->windows.logic[l]);
    }
    CMP("windows.w1_left", windows.w1_left);
    CMP("windows.w1_right", windows.w1_right);
    CMP("windows.w2_left", windows.w2_left);
    CMP("windows.w2_right", windows.w2_right);
    CMP("windows.win_main", windows.win_main);
    CMP("windows.win_sub", windows.win_sub);
    CMP("windows.snes_extra_logic", windows.snes_extra_logic);
    CMP("math.clip_black", math.clip_black);
    CMP("math.prevent_math", math.prevent_math);
    CMP("math.sub_screen", math.sub_screen);
    CMP("math.direct", math.direct);
    CMP("math.subtract", math.subtract);
    CMP("math.half", math.half);
    CMP("math.layers", math.layers);
    CMP("math.snes_extra", math.snes_extra);
    CMP("config.interlace", config.interlace);
    CMP("config.obj_interlace", config.obj_interlace);
    CMP("config.overscan", config.overscan);
    CMP("config.pseudo_hires", config.pseudo_hires);
    CMP("config.extbg", config.extbg);
    CMP("config.snes_extra", config.snes_extra);
    CMP("fixed_color", fixed_color);
    CMP("main_layers", main_layers);
    CMP("sub_layers", sub_layers);
    return false;
}

/* The state a frame gives line `n`. */
static const VidLineState *state_at(const VidFrame *f, unsigned n)
{
    const VidLineState *st = NULL;
    for (unsigned i = 0; i < f->span_count; i++) {
        if (f->spans[i].line > n)
            break;
        st = &f->spans[i].state;
    }
    return st;
}

bool vid_frame_diff(const VidFrame *a, const VidFrame *b, VidDiff *out)
{
    if (a->span_count == 0 || b->span_count == 0)
        DIFF("spans", "span counts %u and %u (one frame has no lines)", a->span_count, b->span_count);
    for (unsigned line = 1; line <= 224; line++) {
        const VidLineState *x = state_at(a, line), *y = state_at(b, line);
        if (!x || !y)
            DIFF("spans", "line %u: no state (spans %u and %u)", line, a->span_count, b->span_count);
        if (state_diff(x, y, line, out)) {
            /* How much of the frame is affected, and how the two split it into
             * spans, tells a state that moved from one that is timed wrong. */
            unsigned lines = 0;
            VidDiff ignored;
            for (unsigned l = 1; l <= 224; l++)
                if (state_diff(state_at(a, l), state_at(b, l), l, &ignored))
                    lines++;
            size_t n = strlen(out->text);
            snprintf(out->text + n, sizeof out->text - n, " (%u of 224 lines, spans %u and %u)", lines, a->span_count,
                     b->span_count);
            return true;
        }
    }
    for (int n = 0; n < VID_SPRITES; n++) {
        const VidSprite *x = &a->sprites[n], *y = &b->sprites[n];
        if (x->x != y->x || x->y != y->y || x->tile != y->tile || x->palette != y->palette ||
            x->priority != y->priority || x->hflip != y->hflip || x->vflip != y->vflip || x->large != y->large)
            DIFF("sprite", "sprite %d: (%d,%u) tile $%03X pal %u pri %u, commands (%d,%u) tile $%03X pal %u pri %u", n,
                 x->x, x->y, x->tile, x->palette, x->priority, y->x, y->y, y->tile, y->palette, y->priority);
    }
    if (a->first_sprite != b->first_sprite)
        DIFF("first_sprite", "first sprite %u, commands %u", a->first_sprite, b->first_sprite);
    for (int i = 0; i < 256; i++)
        if (a->cgram[i] != b->cgram[i])
            DIFF("palette", "palette entry %d is $%04X, commands $%04X", i, a->cgram[i], b->cgram[i]);
    for (int i = 0; i < 0x10000; i++)
        if (a->vram[i] != b->vram[i])
            DIFF("vram", "VRAM $%04X is $%02X, commands $%02X", i, a->vram[i], b->vram[i]);
    return false;
}
