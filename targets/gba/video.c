/* The video service on the GBA with no picture (targets/gba/main.c), in
 * place of runtime/platform/native/video.c: every call keeps only what the
 * game's own play depends on, and draws nothing.
 *
 * What the game's play depends on is the time its sends take, charged as
 * the native backend charges them (platform/time_model.h), and the display's
 * changes below the line handler, which the native backend keeps for the
 * frame after. What the game reads back of VRAM (vid_vram_word: the jet bike
 * race's and the rising palace's maps) answers 0 here, as there is no VRAM
 * kept until the GBA's video backend (issue #367): a movie through those
 * scenes plays otherwise on the GBA until then. */
#include "platform/native/host.h"
#include "platform/time_model.h"
#include "platform/timing.h"

void vid_host_start(Platform *pf, uint8_t (*read)(void *ctx, uint32_t addr), void *ctx)
{
    (void)pf, (void)read, (void)ctx;
}

void vid_host_stop(Platform *pf) { (void)pf; }

const VidFrame *vid_host_picture(Platform *pf)
{
    (void)pf;
    return NULL;
}

void vid_host_frame_begins(Platform *pf)
{
    VidHost *v = &pf->video;
    if (v->below_display)
        v->display = v->display_below;
    if (v->below_line_effects)
        v->line_effects = v->line_effects_below;
    v->display_from = v->effects_from = 0;
    v->below_display = v->below_line_effects = false;
}

size_t vid_host_observer_bytes(const Platform *pf)
{
    (void)pf;
    return 0;
}

/* ---- The view: the original's screen ------------------------------------------------------- */

void vid_host_set_view(Platform *pf, VidView view) { (void)pf, (void)view; }

VidView vid_view(const Platform *pf)
{
    (void)pf;
    return (VidView){ VID_ORIGINAL_VIEW_WIDTH, VID_ORIGINAL_VIEW_HEIGHT };
}

void vid_show_a_layer_across(Platform *pf, VidLayer bg, int16_t left, int16_t right)
{
    (void)pf, (void)bg, (void)left, (void)right;
}

void vid_lay_out_a_map_across(Platform *pf, uint16_t map_base, const VidMapLayout *layout)
{
    (void)pf, (void)map_base, (void)layout;
}

void vid_part_the_windows(Platform *pf, const VidWindowParting *parting) { (void)pf, (void)parting; }

void vid_show_a_sprite_in_the_margins(Platform *pf, const VidMarginSprite *s, const VidSpritePicture *picture)
{
    (void)pf, (void)s, (void)picture;
}

void vid_read_vram(const Platform *pf, uint16_t vram, uint16_t *words, uint16_t count)
{
    (void)pf, (void)vram;
    for (uint16_t i = 0; i < count; i++)
        words[i] = 0;
}

bool vid_layer_last_shown(const Platform *pf, VidLayer bg, uint16_t line, VidBg *out)
{
    (void)pf, (void)bg, (void)line, (void)out;
    return false;
}

uint16_t vid_vram_word(Platform *pf, PfOrigin o, uint16_t vram_addr)
{
    (void)pf, (void)o, (void)vram_addr;
    return 0;
}

/* ---- The picture's settings ------------------------------------------------------------------- */

/* A change the line handler makes shows from the line after its own. */
static uint16_t below_the_line_handler(const Platform *pf)
{
    return pf->timing.part == TM_HOST_IN_LINE ? (uint16_t)(pf->timing.line + 1) : 0;
}

static void display_from(Platform *pf, uint16_t from, const VidDisplay *d)
{
    VidHost *v = &pf->video;
    if (from) {
        v->display_from = from;
        v->below_display = true;
        v->display_below = *d;
        return;
    }
    v->display = *d;
    v->below_display = false;
}

void vid_set_display(Platform *pf, PfOrigin o, const VidDisplay *d)
{
    (void)o;
    display_from(pf, below_the_line_handler(pf), d);
}

void vid_raster_display(Platform *pf, PfOrigin o, uint16_t line, const VidDisplay *d)
{
    (void)o;
    display_from(pf, line == VID_AT_LINE_HANDLER ? below_the_line_handler(pf) : line, d);
}

void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots)
{
    (void)o;
    VidHost *v = &pf->video;
    uint16_t from = below_the_line_handler(pf);
    if (from) {
        v->effects_from = from;
        v->below_line_effects = true;
        v->line_effects_below = slots;
        return;
    }
    v->line_effects = slots;
    v->below_line_effects = false;
}

void vid_set_mode(Platform *pf, PfOrigin o, const VidMode *m) { (void)pf, (void)o, (void)m; }
void vid_set_bg_map(Platform *pf, PfOrigin o, VidLayer bg, const VidBg *b) { (void)pf, (void)o, (void)bg, (void)b; }
void vid_set_bg_chars(Platform *pf, PfOrigin o, VidLayer first, uint16_t base0, uint16_t base1)
{
    (void)pf, (void)o, (void)first, (void)base0, (void)base1;
}
void vid_set_scroll(Platform *pf, PfOrigin o, VidLayer bg, uint16_t hofs, uint16_t vofs, uint32_t fields)
{
    (void)pf, (void)o, (void)bg, (void)hofs, (void)vofs, (void)fields;
}
void vid_set_mosaic(Platform *pf, PfOrigin o, uint8_t bgs, uint8_t size) { (void)pf, (void)o, (void)bgs, (void)size; }
void vid_set_mode7(Platform *pf, PfOrigin o, const VidMode7 *m7, uint32_t fields)
{
    (void)pf, (void)o, (void)m7, (void)fields;
}
void vid_set_obj_config(Platform *pf, PfOrigin o, const VidObjConfig *c) { (void)pf, (void)o, (void)c; }
void vid_set_screens(Platform *pf, PfOrigin o, uint8_t main_layers, uint8_t sub_layers, uint32_t fields)
{
    (void)pf, (void)o, (void)main_layers, (void)sub_layers, (void)fields;
}
void vid_set_video_config(Platform *pf, PfOrigin o, const VidVideoConfig *c) { (void)pf, (void)o, (void)c; }
void vid_set_windows(Platform *pf, PfOrigin o, const VidWindows *w, uint32_t fields)
{
    (void)pf, (void)o, (void)w, (void)fields;
}
void vid_set_color_math(Platform *pf, PfOrigin o, const VidColorMath *m, uint32_t fields)
{
    (void)pf, (void)o, (void)m, (void)fields;
}
void vid_set_fixed_color(Platform *pf, PfOrigin o, uint8_t channels, uint8_t intensity)
{
    (void)pf, (void)o, (void)channels, (void)intensity;
}
void vid_set_line_slot(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e)
{
    (void)pf, (void)o, (void)slot, (void)e;
}
void vid_set_line_table(Platform *pf, PfOrigin o, uint8_t slot, uint16_t table)
{
    (void)pf, (void)o, (void)slot, (void)table;
}

/* ---- Sends: their time, as the native backend charges it ---------------------------------------- */

static void charge_send(Platform *pf, uint32_t bytes)
{
    if (pf->timing.part != TM_HOST_IDLE)
        tm_host_charge(pf, time_of_transfer(bytes));
}

void vid_start_vram_uploads(Platform *pf, PfOrigin o, const VidVramPort *port) { (void)pf, (void)o, (void)port; }

void vid_send_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len, const VidVramPort *port)
{
    (void)o, (void)vram_addr, (void)src, (void)port;
    charge_send(pf, len);
}

void vid_send_view_vram(Platform *pf, uint16_t vram_addr, PfData src, uint32_t len, const VidVramPort *port)
{
    (void)pf, (void)vram_addr, (void)src, (void)len, (void)port;
}

void vid_send_vram_and_wait(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint32_t len)
{
    (void)o, (void)vram_addr, (void)src;
    charge_send(pf, len);
}

void vid_send_vram_next_frame(Platform *pf, PfOrigin o, PfOrigin wait, uint16_t vram_addr, PfData src, uint32_t len,
                              const VidVramPort *port)
{
    tm_wait_frame(pf, wait);
    vid_send_vram(pf, o, vram_addr, src, len, port);
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
    (void)o, (void)vram_addr, (void)src, (void)half, (void)port;
    charge_send(pf, len);
}

void vid_send_vram_fill(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData value, uint32_t len,
                        const VidVramPort *port)
{
    (void)o, (void)vram_addr, (void)value, (void)port;
    charge_send(pf, len);
}

void vid_send_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count)
{
    (void)o, (void)first, (void)src;
    charge_send(pf, 2u * count);
}

void vid_fill_vram_half(Platform *pf, PfOrigin o, PfData value, uint32_t len, VidVramHalf half)
{
    (void)o, (void)value, (void)half;
    charge_send(pf, len);
}

void vid_continue_vram(Platform *pf, PfOrigin o, PfData src, uint32_t len)
{
    (void)o, (void)src;
    charge_send(pf, len);
}

void vid_continue_vram_half(Platform *pf, PfOrigin o, PfData src, uint32_t len, VidVramHalf half)
{
    (void)o, (void)src, (void)half;
    charge_send(pf, len);
}

void vid_send_oam(Platform *pf, PfOrigin o, PfData oam, const VidSpritePlace *places)
{
    (void)pf, (void)o, (void)oam, (void)places;
}

void vid_send_oam_fill(Platform *pf, PfOrigin o, uint16_t word_addr, uint8_t value, uint16_t len)
{
    (void)pf, (void)o, (void)word_addr, (void)value, (void)len;
}

void vid_set_sprite(Platform *pf, PfOrigin o, uint8_t index, const VidSprite *sp)
{
    (void)pf, (void)o, (void)index, (void)sp;
}

void vid_stop_uploads(Platform *pf, PfOrigin o) { (void)pf, (void)o; }

/* A screen sets the display, as the native backend's does, and the rest. */
void vid_apply_screen(Platform *pf, PfOrigin o, const VidScreen *sc)
{
    if (sc->display)
        vid_set_display(pf, o, sc->display);
    pf->video.screens_applied++;
}

void vid_set_vram_address(Platform *pf, PfOrigin o, uint16_t vram_addr) { (void)pf, (void)o, (void)vram_addr; }
void vid_write_vram(Platform *pf, PfOrigin o, uint16_t value) { (void)pf, (void)o, (void)value; }
void vid_write_vram_byte(Platform *pf, PfOrigin o, bool high, uint8_t value)
{
    (void)pf, (void)o, (void)high, (void)value;
}
void vid_set_palette_address(Platform *pf, PfOrigin o, uint8_t first) { (void)pf, (void)o, (void)first; }
void vid_set_palette_color(Platform *pf, PfOrigin o, uint8_t index, uint16_t color)
{
    (void)pf, (void)o, (void)index, (void)color;
}
void vid_set_oam_address(Platform *pf, PfOrigin o, uint16_t word_addr, bool priority_rotation)
{
    (void)pf, (void)o, (void)word_addr, (void)priority_rotation;
}
