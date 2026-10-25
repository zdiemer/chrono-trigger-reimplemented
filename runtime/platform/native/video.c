/* Video service, host backend (runtime/platform/native/host.h): every call
 * folded into the picture's state by the observer (platform/observe.h), which
 * keeps VRAM, the palette, the sprite table and every setting as the calls
 * leave them, and expands the line effects from the game's tables. Nothing
 * is drawn yet: the observer's frame is what a renderer would draw
 * (docs/TODO.md, "The native program").
 *
 * The data a call names is the game's, read through the reader the driver
 * gives (vid_host_start). A host makes every send at once: it has no picture
 * being drawn to wait for. */
#include <stdlib.h>

#include "platform/native/host.h"
#include "platform/observe.h"

void vid_host_start(Platform *pf, uint8_t (*read)(void *ctx, uint32_t addr),
                    void (*write)(void *ctx, uint32_t addr, uint8_t v), void *ctx)
{
    VidHost *v = &pf->video;
    vid_obs_destroy(v->observer);
    v->observer = vid_obs_create(read, ctx);
    v->read = read;
    v->write = write;
    v->ctx = ctx;
}

void vid_host_stop(Platform *pf)
{
    vid_obs_destroy(pf->video.observer);
    pf->video.observer = NULL;
}

const VidFrame *vid_host_picture(Platform *pf)
{
    VidObserver *ob = pf->video.observer;
    vid_obs_frame_start(ob);
    for (uint16_t line = 0; line <= VID_HOST_LAST_LINE; line++)
        vid_obs_line(ob, line);
    return vid_obs_frame(ob);
}

static VidObserver *ob(Platform *pf) { return pf->video.observer; }

/* ---- The picture's settings ------------------------------------------------------------------- */

void vid_set_display(Platform *pf, PfOrigin o, const VidDisplay *d)
{
    (void)o;
    pf->video.display = *d;
    vid_obs_display(ob(pf), d);
}

void vid_raster_display(Platform *pf, PfOrigin o, uint16_t line, const VidDisplay *d)
{
    (void)line;
    vid_set_display(pf, o, d);
}

void vid_set_mode(Platform *pf, PfOrigin o, const VidMode *m) { (void)o; vid_obs_mode(ob(pf), m); }

void vid_set_bg_map(Platform *pf, PfOrigin o, VidLayer bg, const VidBg *b) { (void)o; vid_obs_bg_map(ob(pf), bg, b); }

void vid_set_bg_chars(Platform *pf, PfOrigin o, VidLayer first, uint16_t base0, uint16_t base1)
{
    (void)o;
    vid_obs_bg_chars(ob(pf), first, base0, base1);
}

void vid_set_scroll(Platform *pf, PfOrigin o, VidLayer bg, uint16_t hofs, uint16_t vofs, uint32_t fields)
{
    (void)o;
    vid_obs_scroll(ob(pf), bg, hofs, vofs, fields);
}

void vid_set_mosaic(Platform *pf, PfOrigin o, uint8_t bgs, uint8_t size) { (void)o; vid_obs_mosaic(ob(pf), bgs, size); }

/* The matrix's first two values double as the signed multiplier, whose
 * product a SNES escape copies (vid_snes_copy, below). */
void vid_set_mode7(Platform *pf, PfOrigin o, const VidMode7 *m7, uint32_t fields)
{
    (void)o;
    VidHost *v = &pf->video;
    if (fields & (VID_M7_MATRIX | VID_M7_MATRIX_AB | VID_M7_MATRIX_A))
        v->multiplicand = m7->a;
    if (fields & (VID_M7_MATRIX | VID_M7_MATRIX_AB | VID_M7_MATRIX_B | VID_M7_MATRIX_B_HIGH))
        v->multiplier = (int8_t)((uint16_t)m7->b >> 8);
    vid_obs_mode7(ob(pf), m7, fields);
}

void vid_set_obj_config(Platform *pf, PfOrigin o, const VidObjConfig *c) { (void)o; vid_obs_obj_config(ob(pf), c); }

void vid_set_screens(Platform *pf, PfOrigin o, uint8_t main_layers, uint8_t sub_layers, uint32_t fields)
{
    (void)o;
    vid_obs_screens(ob(pf), main_layers, sub_layers, fields);
}

void vid_set_video_config(Platform *pf, PfOrigin o, const VidVideoConfig *c) { (void)o; vid_obs_video_config(ob(pf), c); }

void vid_set_windows(Platform *pf, PfOrigin o, const VidWindows *w, uint32_t fields)
{
    (void)o;
    vid_obs_windows(ob(pf), w, fields);
}

void vid_set_color_math(Platform *pf, PfOrigin o, const VidColorMath *m, uint32_t fields)
{
    (void)o;
    vid_obs_color_math(ob(pf), m, fields);
}

void vid_set_fixed_color(Platform *pf, PfOrigin o, uint8_t channels, uint8_t intensity)
{
    (void)o;
    vid_obs_fixed_color(ob(pf), channels, intensity);
}

/* ---- The line effects ------------------------------------------------------------------------- */

void vid_set_line_slot(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e)
{
    (void)o;
    vid_obs_line_effect(ob(pf), slot, e);
}

void vid_set_line_effect(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e, const uint8_t *write_order)
{
    (void)write_order;
    vid_set_line_slot(pf, o, slot, e);
}

void vid_set_line_table(Platform *pf, PfOrigin o, uint8_t slot, uint16_t table)
{
    (void)o;
    vid_obs_line_table(ob(pf), slot, table);
}

void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots)
{
    (void)o;
    pf->video.line_effects = slots;
    vid_obs_enable_line_effects(ob(pf), slots);
}

/* ---- Sends ------------------------------------------------------------------------------------- */

void vid_start_vram_uploads(Platform *pf, PfOrigin o, const VidVramPort *port)
{
    (void)o;
    if (port)
        vid_obs_vram_port(ob(pf), port);
}

void vid_send_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len, const VidVramPort *port)
{
    (void)o;
    vid_obs_upload_vram(ob(pf), vram_addr, src, len, false, port);
}

void vid_send_vram_and_wait(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len)
{
    (void)o;
    vid_obs_upload_vram(ob(pf), vram_addr, src, len, false, &(VidVramPort){ .step = 1, .after_high = true });
}

void vid_send_vram_in_mode(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len,
                           const VidVramPort *port, uint8_t transfer_mode)
{
    (void)transfer_mode;
    vid_send_vram(pf, o, vram_addr, src, len, port);
}

void vid_send_vram_half(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len, VidVramHalf half,
                        const VidVramPort *port)
{
    (void)o;
    vid_obs_upload_vram_half(ob(pf), vram_addr, src, len, half, port);
}

void vid_send_vram_fill(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData value, uint32_t len,
                        const VidVramPort *port)
{
    (void)o;
    vid_obs_upload_vram(ob(pf), vram_addr, value, len, true, port);
}

void vid_send_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count)
{
    (void)o;
    vid_obs_upload_palette(ob(pf), first, src, count);
}

void vid_send_oam(Platform *pf, PfOrigin o, PfData oam) { (void)o; vid_obs_upload_oam(ob(pf), oam); }

void vid_send_oam_fill(Platform *pf, PfOrigin o, uint16_t word_addr, uint8_t value, uint16_t len)
{
    (void)o;
    vid_obs_fill_oam(ob(pf), word_addr, value, len);
}

/* A sprite's entry's last byte: its tile's ninth bit, palette, priority and
 * flips. */
static uint8_t sprite_attributes(const VidSprite *sp)
{
    return (uint8_t)((sp->vflip ? 0x80 : 0) | (sp->hflip ? 0x40 : 0) | (sp->priority & 3) << 4 | (sp->palette & 7) << 1 |
                     (sp->tile >> 8 & 1));
}

void vid_set_sprite(Platform *pf, PfOrigin o, uint8_t index, const VidSprite *sp)
{
    (void)o;
    const uint8_t entry[VID_OAM_ENTRY_BYTES] = { vid_oam_x_low(sp->x), sp->y, (uint8_t)sp->tile, sprite_attributes(sp) };
    vid_obs_oam_address(ob(pf), (uint16_t)(index * VID_OAM_ENTRY_BYTES / 2), false);
    for (unsigned i = 0; i < VID_OAM_ENTRY_BYTES; i++)
        vid_obs_write_oam(ob(pf), entry[i]);
}

/* No transfers wait for the vblank on a host: the sends are made at once. */
void vid_stop_uploads(Platform *pf, PfOrigin o) { (void)pf, (void)o; }

/* ---- A screen at once ------------------------------------------------------------------------- */

/* A screen sets all four layers and every window setting. */
enum { SCREEN_LAYERS = 4, ALL_WINDOWS = VID_WINDOWS_SUB * 2 - 1 };

void vid_apply_screen(Platform *pf, PfOrigin o, const VidScreen *sc)
{
    if (sc->display)
        vid_set_display(pf, o, sc->display);
    vid_set_obj_config(pf, o, &sc->sprites);
    vid_set_oam_address(pf, o, 0x0000, false);
    vid_set_mode(pf, o, &sc->mode);
    vid_set_mosaic(pf, o, sc->mosaic_layers, sc->mosaic_size);
    for (unsigned bg = 0; bg < SCREEN_LAYERS; bg++) {
        vid_set_bg_map(pf, o, (VidLayer)bg, &sc->bg[bg]);
        vid_set_scroll(pf, o, (VidLayer)bg, sc->bg[bg].hofs, sc->bg[bg].vofs, VID_SCROLL_H | VID_SCROLL_V);
    }
    vid_set_bg_chars(pf, o, VID_BG1, sc->bg[VID_BG1].char_base, sc->bg[VID_BG2].char_base);
    vid_set_bg_chars(pf, o, VID_BG3, sc->bg[VID_BG3].char_base, sc->bg[VID_BG4].char_base);
    vid_start_vram_uploads(pf, o, &sc->vram_port);
    vid_set_vram_address(pf, o, 0x0000);
    vid_set_mode7(pf, o, &sc->mode7, VID_M7_SELECT | VID_M7_MATRIX | VID_M7_CENTER | VID_M7_SCROLL);
    vid_set_palette_address(pf, o, 0x00);
    vid_set_windows(pf, o, &sc->windows, ALL_WINDOWS);
    vid_set_screens(pf, o, sc->main_layers, sc->sub_layers, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_color_math(pf, o, &sc->math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_fixed_color(pf, o, VID_RED, (uint8_t)(sc->fixed_color & 0x1F));
    vid_set_fixed_color(pf, o, VID_GREEN, (uint8_t)(sc->fixed_color >> 5 & 0x1F));
    vid_set_fixed_color(pf, o, VID_BLUE, (uint8_t)(sc->fixed_color >> 10 & 0x1F));
    vid_set_video_config(pf, o, &sc->config);
    pf->video.screens_applied++;
}

/* ---- The older forms, with the SNES's write order at the call, which a host ignores ----------- */

void vid_begin_vram_uploads(Platform *pf, PfOrigin o, const VidVramPort *port, PfData source, const VidSnesXfer *x)
{
    (void)source, (void)x;
    vid_start_vram_uploads(pf, o, port);
}

void vid_set_vram_address(Platform *pf, PfOrigin o, uint16_t vram_addr) { (void)o; vid_obs_vram_address(ob(pf), vram_addr); }
void vid_write_vram(Platform *pf, PfOrigin o, uint16_t value) { (void)o; vid_obs_write_vram(ob(pf), value, -1); }

void vid_write_vram_byte(Platform *pf, PfOrigin o, bool high, uint8_t value)
{
    (void)o;
    vid_obs_write_vram(ob(pf), value, high ? 1 : 0);
}

void vid_upload_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len, const VidSnesXfer *x)
{
    vid_send_vram(pf, o, vram_addr, src, len, x->port);
}

void vid_upload_vram_half(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len, VidVramHalf half,
                          const VidSnesXfer *x)
{
    vid_send_vram_half(pf, o, vram_addr, src, len, half, x->port);
}

void vid_fill_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData value, uint32_t len, const VidSnesXfer *x)
{
    vid_send_vram_fill(pf, o, vram_addr, value, len, x->port);
}

void vid_set_palette_address(Platform *pf, PfOrigin o, uint8_t first) { (void)o; vid_obs_palette_address(ob(pf), first); }

void vid_set_oam_address(Platform *pf, PfOrigin o, uint16_t word_addr, bool priority_rotation)
{
    (void)o;
    vid_obs_oam_address(ob(pf), word_addr, priority_rotation);
}

void vid_upload_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count, const VidSnesXfer *x)
{
    (void)x;
    vid_send_palette(pf, o, first, src, count);
}

void vid_upload_oam(Platform *pf, PfOrigin o, PfData oam, const VidSnesXfer *x)
{
    (void)x;
    vid_send_oam(pf, o, oam);
}
