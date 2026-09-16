/* Video service, SNES backend: the original's PPU, DMA and HDMA register writes.
 * Included by platform/video.h. */
#ifndef CT_PLATFORM_SNES_VIDEO_H
#define CT_PLATFORM_SNES_VIDEO_H

#include "platform/snes/backend.h"
#include "platform/video.h"

enum {
    PF_SNES_INIDISP = 0x2100, PF_SNES_OBSEL = 0x2101, PF_SNES_OAMADDL = 0x2102, PF_SNES_BGMODE = 0x2105,
    PF_SNES_MOSAIC = 0x2106, PF_SNES_BG1SC = 0x2107, PF_SNES_BG12NBA = 0x210B, PF_SNES_BG1HOFS = 0x210D,
    PF_SNES_VMAIN = 0x2115, PF_SNES_VMADDL = 0x2116, PF_SNES_VMDATAL = 0x2118, PF_SNES_M7SEL = 0x211A,
    PF_SNES_M7A = 0x211B, PF_SNES_CGADD = 0x2121, PF_SNES_W12SEL = 0x2123, PF_SNES_CGWSEL = 0x2130,
    PF_SNES_COLDATA = 0x2132, PF_SNES_SETINI = 0x2133, PF_SNES_MDMAEN = 0x420B, PF_SNES_HDMAEN = 0x420C,
};

static inline uint8_t pf_snes_display_byte(const VidDisplay *d)
{
    return (uint8_t)((d->blank ? 0x80 : 0) | (d->brightness & 0x0F) | (d->snes_extra & 0x70));
}

PF_API void vid_set_display(Platform *pf, PfOrigin o, const VidDisplay *d)
{
    pf_snes_begin("vid_set_display", o);
    pf_snes_put(pf_snes(pf), PF_SNES_INIDISP, pf_snes_display_byte(d));
    if (vid_observer)
        vid_obs_display(vid_observer, d);
    pf_snes_end();
}

PF_API void vid_raster_display(Platform *pf, PfOrigin o, uint16_t line, const VidDisplay *d)
{
    (void)line;
    pf_snes_begin("vid_raster_display", o);
    pf_snes_put(pf_snes(pf), PF_SNES_INIDISP, pf_snes_display_byte(d));
    if (vid_observer)
        vid_obs_display(vid_observer, d);
    pf_snes_end();
}

PF_API void vid_set_mode(Platform *pf, PfOrigin o, const VidMode *m)
{
    pf_snes_begin("vid_set_mode", o);
    pf_snes_put(pf_snes(pf), PF_SNES_BGMODE,
                (uint8_t)((m->mode & 7) | (m->bg3_priority ? 0x08 : 0) | (m->large_tiles & 0x0F) << 4));
    if (vid_observer)
        vid_obs_mode(vid_observer, m);
    pf_snes_end();
}

PF_API void vid_set_bg_map(Platform *pf, PfOrigin o, VidLayer bg, const VidBg *b)
{
    pf_snes_begin("vid_set_bg_map", o);
    pf_snes_put(pf_snes(pf), (uint16_t)(PF_SNES_BG1SC + bg),
                (uint8_t)((b->map_base >> 8 & 0xFC) | (b->map_w == 64 ? 1 : 0) | (b->map_h == 64 ? 2 : 0)));
    if (vid_observer)
        vid_obs_bg_map(vid_observer, bg, b);
    pf_snes_end();
}

PF_API void vid_set_bg_chars(Platform *pf, PfOrigin o, VidLayer first, uint16_t base0, uint16_t base1)
{
    pf_snes_begin("vid_set_bg_chars", o);
    pf_snes_put(pf_snes(pf), (uint16_t)(PF_SNES_BG12NBA + first / 2),
                (uint8_t)((base0 >> 12 & 0x0F) | (base1 >> 12 & 0x0F) << 4));
    if (vid_observer)
        vid_obs_bg_chars(vid_observer, first, base0, base1);
    pf_snes_end();
}

PF_API void vid_set_scroll(Platform *pf, PfOrigin o, VidLayer bg, uint16_t hofs, uint16_t vofs, uint32_t fields)
{
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_set_scroll", o);
    if (fields & VID_SCROLL_H) {
        pf_snes_put(s, (uint16_t)(PF_SNES_BG1HOFS + 2 * bg), (uint8_t)hofs);
        pf_snes_put(s, (uint16_t)(PF_SNES_BG1HOFS + 2 * bg), (uint8_t)(hofs >> 8));
    }
    if (fields & VID_SCROLL_V) {
        pf_snes_put(s, (uint16_t)(PF_SNES_BG1HOFS + 2 * bg + 1), (uint8_t)vofs);
        pf_snes_put(s, (uint16_t)(PF_SNES_BG1HOFS + 2 * bg + 1), (uint8_t)(vofs >> 8));
    }
    if (vid_observer)
        vid_obs_scroll(vid_observer, bg, hofs, vofs, fields);
    pf_snes_end();
}

PF_API void vid_set_mosaic(Platform *pf, PfOrigin o, uint8_t bgs, uint8_t size)
{
    pf_snes_begin("vid_set_mosaic", o);
    pf_snes_put(pf_snes(pf), PF_SNES_MOSAIC, (uint8_t)((bgs & 0x0F) | ((size - 1) & 0x0F) << 4));
    if (vid_observer)
        vid_obs_mosaic(vid_observer, bgs, size);
    pf_snes_end();
}

PF_API void vid_set_mode7(Platform *pf, PfOrigin o, const VidMode7 *m7, uint32_t fields)
{
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_set_mode7", o);
    if (fields & VID_M7_SELECT)
        pf_snes_put(s, PF_SNES_M7SEL,
                    (uint8_t)((m7->outside & 3) << 6 | (m7->vflip ? 2 : 0) | (m7->hflip ? 1 : 0)));
    if (fields & VID_M7_MATRIX) {
        const int16_t v[4] = { m7->a, m7->b, m7->c, m7->d };
        for (int i = 0; i < 4; i++) {
            pf_snes_put(s, (uint16_t)(PF_SNES_M7A + i), (uint8_t)v[i]);
            pf_snes_put(s, (uint16_t)(PF_SNES_M7A + i), (uint8_t)((uint16_t)v[i] >> 8));
        }
    }
    if (fields & VID_M7_CENTER) {
        const int16_t v[2] = { m7->cx, m7->cy };
        for (int i = 0; i < 2; i++) {
            pf_snes_put(s, (uint16_t)(PF_SNES_M7A + 4 + i), (uint8_t)v[i]);
            pf_snes_put(s, (uint16_t)(PF_SNES_M7A + 4 + i), (uint8_t)((uint16_t)v[i] >> 8));
        }
    }
    if (fields & VID_M7_SCROLL) {
        const int16_t v[2] = { m7->hofs, m7->vofs };
        for (int i = 0; i < 2; i++) {
            pf_snes_put(s, (uint16_t)(PF_SNES_BG1HOFS + i), (uint8_t)v[i]);
            pf_snes_put(s, (uint16_t)(PF_SNES_BG1HOFS + i), (uint8_t)((uint16_t)v[i] >> 8));
        }
    }
    if (vid_observer)
        vid_obs_mode7(vid_observer, m7, fields);
    pf_snes_end();
}

PF_API void vid_set_obj_config(Platform *pf, PfOrigin o, const VidObjConfig *c)
{
    pf_snes_begin("vid_set_obj_config", o);
    pf_snes_put(pf_snes(pf), PF_SNES_OBSEL,
                (uint8_t)((c->size_select & 7) << 5 | (c->name_select >> 12 & 3) << 3 | (c->base >> 13 & 7)));
    if (vid_observer)
        vid_obs_obj_config(vid_observer, c);
    pf_snes_end();
}

PF_API void vid_set_screens(Platform *pf, PfOrigin o, uint8_t main_layers, uint8_t sub_layers, uint32_t fields)
{
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_set_screens", o);
    if (fields & VID_SCREEN_MAIN)
        pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + 9), main_layers);
    if (fields & VID_SCREEN_SUB)
        pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + 10), sub_layers);
    if (vid_observer)
        vid_obs_screens(vid_observer, main_layers, sub_layers, fields);
    pf_snes_end();
}

PF_API void vid_set_video_config(Platform *pf, PfOrigin o, const VidVideoConfig *c)
{
    pf_snes_begin("vid_set_video_config", o);
    pf_snes_put(pf_snes(pf), PF_SNES_SETINI,
                (uint8_t)((c->interlace ? 1 : 0) | (c->obj_interlace ? 2 : 0) | (c->overscan ? 4 : 0) |
                          (c->pseudo_hires ? 8 : 0) | (c->extbg ? 0x40 : 0) | (c->snes_extra & 0xB0)));
    if (vid_observer)
        vid_obs_video_config(vid_observer, c);
    pf_snes_end();
}

PF_API void vid_set_windows(Platform *pf, PfOrigin o, const VidWindows *w, uint32_t fields)
{
    static const uint8_t pair_layer[3][2] = { { VID_BG1, VID_BG2 }, { VID_BG3, VID_BG4 }, { VID_OBJ, VID_BACK } };
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_set_windows", o);
    for (int g = 0; g < 3; g++) {
        if (fields & (1u << g)) {
            uint8_t v = 0;
            for (int i = 0; i < 2; i++)
                v |= (uint8_t)((w->w1[pair_layer[g][i]] & 3) << (4 * i) | (w->w2[pair_layer[g][i]] & 3) << (4 * i + 2));
            pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + g), v);
        }
    }
    const uint8_t edges[4] = { w->w1_left, w->w1_right, w->w2_left, w->w2_right };
    for (int i = 0; i < 4; i++)
        if (fields & (VID_WINDOWS_W1_LEFT << i))
            pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + 3 + i), edges[i]);
    if (fields & VID_WINDOWS_BG_LOGIC)
        pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + 7),
                    (uint8_t)((w->logic[0] & 3) | (w->logic[1] & 3) << 2 | (w->logic[2] & 3) << 4 | (w->logic[3] & 3) << 6));
    if (fields & VID_WINDOWS_OBJ_LOGIC)
        pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + 8),
                    (uint8_t)((w->logic[VID_OBJ] & 3) | (w->logic[VID_BACK] & 3) << 2 | (w->snes_extra_logic & 0xF0)));
    if (fields & VID_WINDOWS_MAIN)
        pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + 11), w->win_main);
    if (fields & VID_WINDOWS_SUB)
        pf_snes_put(s, (uint16_t)(PF_SNES_W12SEL + 12), w->win_sub);
    if (vid_observer)
        vid_obs_windows(vid_observer, w, fields);
    pf_snes_end();
}

PF_API void vid_set_color_math(Platform *pf, PfOrigin o, const VidColorMath *m, uint32_t fields)
{
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_set_color_math", o);
    if (fields & VID_MATH_CONTROL)
        pf_snes_put(s, PF_SNES_CGWSEL,
                    (uint8_t)((m->clip_black & 3) << 6 | (m->prevent_math & 3) << 4 | (m->snes_extra & 0x0C) |
                              (m->sub_screen ? 0x02 : 0) | (m->direct ? 0x01 : 0)));
    if (fields & VID_MATH_LAYERS)
        pf_snes_put(s, (uint16_t)(PF_SNES_CGWSEL + 1),
                    (uint8_t)((m->subtract ? 0x80 : 0) | (m->half ? 0x40 : 0) | (m->layers & 0x3F)));
    if (vid_observer)
        vid_obs_color_math(vid_observer, m, fields);
    pf_snes_end();
}

PF_API void vid_set_fixed_color(Platform *pf, PfOrigin o, uint8_t channels, uint8_t intensity)
{
    pf_snes_begin("vid_set_fixed_color", o);
    pf_snes_put(pf_snes(pf), PF_SNES_COLDATA, (uint8_t)((channels & 7) << 5 | (intensity & 0x1F)));
    if (vid_observer)
        vid_obs_fixed_color(vid_observer, channels, intensity);
    pf_snes_end();
}

PF_API void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots)
{
    pf_snes_begin("vid_enable_line_effects", o);
    pf_snes_put(pf_snes(pf), PF_SNES_HDMAEN, slots);
    if (vid_observer)
        vid_obs_enable_line_effects(vid_observer, slots);
    pf_snes_end();
}

/* A channel's register writes, in the order of `steps`. `stop_if_empty`: a
 * size step with `len` 0 ends the sequence (an empty upload). */
typedef struct {
    uint8_t channel, control, dest;
    uint32_t source;
    uint32_t len;
    bool stop_if_empty;
    uint8_t indirect_bank;
    uint8_t target_reg;
    uint16_t target;
} PfSnesChannel;

static inline void pf_snes_program(Snes *s, const PfSnesChannel *c, const uint8_t *steps)
{
    uint16_t base = (uint16_t)(0x4300 | (c->channel & 7) << 4);
    for (; *steps != VID_STEP_END; steps++) {
        switch (*steps) {
        case VID_STEP_CONTROL: pf_snes_put(s, base, c->control); break;
        case VID_STEP_DEST: pf_snes_put(s, (uint16_t)(base + 1), c->dest); break;
        case VID_STEP_SOURCE_LO: pf_snes_put(s, (uint16_t)(base + 2), (uint8_t)c->source); break;
        case VID_STEP_SOURCE_HI: pf_snes_put(s, (uint16_t)(base + 3), (uint8_t)(c->source >> 8)); break;
        case VID_STEP_SOURCE_BANK: pf_snes_put(s, (uint16_t)(base + 4), (uint8_t)(c->source >> 16)); break;
        case VID_STEP_SIZE_LO:
            if (c->stop_if_empty && c->len == 0)
                return;
            pf_snes_put(s, (uint16_t)(base + 5), (uint8_t)c->len);
            break;
        case VID_STEP_SIZE_HI:
            if (c->stop_if_empty && c->len == 0)
                return;
            pf_snes_put(s, (uint16_t)(base + 6), (uint8_t)(c->len >> 8));
            break;
        case VID_STEP_SIZE_CLEAR:
            pf_snes_put(s, (uint16_t)(base + 5), 0);
            pf_snes_put(s, (uint16_t)(base + 6), 0);
            break;
        case VID_STEP_INDIRECT_BANK: pf_snes_put(s, (uint16_t)(base + 7), c->indirect_bank); break;
        case VID_STEP_TARGET_LO: pf_snes_put(s, (uint16_t)(0x2100 | c->target_reg), (uint8_t)c->target); break;
        case VID_STEP_TARGET_HI:
            pf_snes_put(s, (uint16_t)(0x2100 | (uint8_t)(c->target_reg + 1)), (uint8_t)(c->target >> 8));
            break;
        case VID_STEP_START: pf_snes_put(s, PF_SNES_MDMAEN, (uint8_t)(1u << (c->channel & 7))); break;
        }
    }
}

PF_API void vid_set_line_effect(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e,
                                const uint8_t *snes_steps)
{
    /* transfer mode and first register per target */
    static const uint8_t mode[] = { 4, 1, 1, 3, 3, 3, 0, 0, 4, 4, 4, 1 };
    static const uint8_t reg[] = { 0x26, 0x26, 0x28, 0x0D, 0x0F, 0x11, 0x32, 0x00, 0x07, 0x2C, 0x30, 0x05 };
    pf_snes_begin("vid_set_line_effect", o);
    PfSnesChannel c = { .channel = slot, .control = (uint8_t)((e->indirect ? 0x40 : 0) | mode[e->target]),
                        .dest = reg[e->target], .source = e->table.addr, .indirect_bank = e->indirect_bank };
    pf_snes_program(pf_snes(pf), &c, snes_steps);
    if (vid_observer)
        vid_obs_line_effect(vid_observer, slot, e);
    pf_snes_end();
}

static inline uint8_t pf_snes_vram_port_byte(const VidVramPort *p)
{
    uint8_t step = p->snes_step_alt ? 3 : p->step == 1 ? 0 : p->step == 32 ? 1 : 2;
    return (uint8_t)((p->after_high ? 0x80 : 0) | (p->remap & 3) << 2 | step);
}

PF_API void vid_begin_vram_uploads(Platform *pf, PfOrigin o, const VidVramPort *port, const VidSnesXfer *x)
{
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_begin_vram_uploads", o);
    pf_snes_put(s, PF_SNES_VMAIN, pf_snes_vram_port_byte(port));
    if (x) {
        PfSnesChannel c = { .channel = x->channel, .control = x->control, .dest = 0x18 };
        pf_snes_program(s, &c, x->steps);
    }
    if (vid_observer)
        vid_obs_vram_port(vid_observer, port);
    pf_snes_end();
}

PF_API void vid_set_vram_address(Platform *pf, PfOrigin o, uint16_t vram_addr)
{
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_set_vram_address", o);
    pf_snes_put(s, PF_SNES_VMADDL, (uint8_t)vram_addr);
    pf_snes_put(s, (uint16_t)(PF_SNES_VMADDL + 1), (uint8_t)(vram_addr >> 8));
    if (vid_observer)
        vid_obs_vram_address(vid_observer, vram_addr);
    pf_snes_end();
}

PF_API void vid_write_vram(Platform *pf, PfOrigin o, uint16_t value)
{
    Snes *s = pf_snes(pf);
    pf_snes_begin("vid_write_vram", o);
    pf_snes_put(s, PF_SNES_VMDATAL, (uint8_t)value);
    pf_snes_put(s, (uint16_t)(PF_SNES_VMDATAL + 1), (uint8_t)(value >> 8));
    if (vid_observer)
        vid_obs_write_vram(vid_observer, value, -1);
    pf_snes_end();
}

PF_API void vid_write_vram_byte(Platform *pf, PfOrigin o, bool high, uint8_t value)
{
    pf_snes_begin("vid_write_vram_byte", o);
    pf_snes_put(pf_snes(pf), (uint16_t)(PF_SNES_VMDATAL + (high ? 1 : 0)), value);
    if (vid_observer)
        vid_obs_write_vram(vid_observer, value, high ? 1 : 0);
    pf_snes_end();
}

static inline void pf_snes_upload(Platform *pf, PfOrigin o, const char *name, uint8_t dest, uint8_t target_reg,
                                  uint16_t target, PfData src, uint32_t len, const VidSnesXfer *x)
{
    pf_snes_begin(name, o);
    PfSnesChannel c = { .channel = x->channel, .control = x->control, .dest = dest, .source = src.addr,
                        .len = len, .stop_if_empty = true, .target_reg = target_reg, .target = target };
    pf_snes_program(pf_snes(pf), &c, x->steps);
    pf_snes_end();
}

PF_API void vid_upload_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len,
                            const VidSnesXfer *x)
{
    pf_snes_upload(pf, o, "vid_upload_vram", 0x18, 0x16, vram_addr, src, len, x);
    if (vid_observer)
        vid_obs_upload_vram(vid_observer, vram_addr, src, len, false);
}

PF_API void vid_fill_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData value, uint32_t len,
                          const VidSnesXfer *x)
{
    pf_snes_upload(pf, o, "vid_fill_vram", 0x18, 0x16, vram_addr, value, len, x);
    if (vid_observer)
        vid_obs_upload_vram(vid_observer, vram_addr, value, len, true);
}

PF_API void vid_upload_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count,
                               const VidSnesXfer *x)
{
    pf_snes_upload(pf, o, "vid_upload_palette", 0x22, 0x21, first, src, 2u * count, x);
    if (vid_observer)
        vid_obs_upload_palette(vid_observer, first, src, count);
}

PF_API void vid_upload_oam(Platform *pf, PfOrigin o, PfData oam, const VidSnesXfer *x)
{
    pf_snes_upload(pf, o, "vid_upload_oam", 0x04, 0x02, 0, oam, 0x220, x);
    if (vid_observer)
        vid_obs_upload_oam(vid_observer, oam);
}

PF_API void vid_stop_uploads(Platform *pf, PfOrigin o)
{
    pf_snes_begin("vid_stop_uploads", o);
    pf_snes_put(pf_snes(pf), PF_SNES_MDMAEN, 0);
    pf_snes_end();
}

PF_API void vid_snes_transfer(Platform *pf, PfOrigin o, const VidSnesTransfer *t, const VidSnesXfer *x)
{
    pf_snes_begin("vid_snes_transfer", o);
    PfSnesChannel c = { .channel = x->channel, .control = x->control, .dest = t->dest, .source = t->source,
                        .len = t->size, .target_reg = t->target_reg, .target = t->target };
    pf_snes_program(pf_snes(pf), &c, x->steps);
    if (vid_observer)
        vid_obs_snes_transfer(vid_observer, t);
    pf_snes_end();
}

#endif
