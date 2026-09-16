/* VidFrame decoder (platform/snes/decode.h). */
#include "platform/snes/decode.h"

#include <stdlib.h>
#include <string.h>

/* Video memory can change while a frame is being drawn: CGRAM between spans (the
 * palette animation, a fade), VRAM under forced blank (the battle's line
 * interrupt), OAM with it. The lines before such a change read what memory held
 * then, so the decoder keeps snapshots of the old contents and hands them to the
 * spans that were already emitted; spans after the change read the frame's own
 * pointers, which are what memory ends up holding. One snapshot of each per
 * frame is what the movies need; the counter says when that isn't enough. */
enum { VID_DEC_VRAM_SNAPSHOTS = 8, VID_DEC_CGRAM_SNAPSHOTS = 32, VID_DEC_SPRITE_SNAPSHOTS = 8 };

struct VidDecoder {
    VidFrame frame;
    uint16_t cgram[256];   /* CGRAM as of the last line, to notice changes */
    uint32_t cgram_writes;
    bool palette_dirty;
    bool have_cgram;

    uint8_t vram[0x10000];   /* VRAM as of the last line */
    uint32_t vram_writes;
    bool have_vram, vram_dirty;

    uint8_t oam[0x220];      /* OAM as of the last line */
    uint32_t oam_writes;
    bool have_oam, sprites_dirty;

    uint16_t cgram_snap[VID_DEC_CGRAM_SNAPSHOTS][256];
    uint8_t vram_snap[VID_DEC_VRAM_SNAPSHOTS][0x10000];
    VidSpriteSet sprite_snap[VID_DEC_SPRITE_SNAPSHOTS];
    int cgram_used, vram_used, sprites_used;
    int cgram_peak, vram_peak, sprites_peak;
    uint32_t snapshots_lost;
};

VidDecoder *vid_decoder_create(void) { return calloc(1, sizeof(VidDecoder)); }
void vid_decoder_destroy(VidDecoder *d) { free(d); }
const VidFrame *vid_decoder_frame(const VidDecoder *d) { return &d->frame; }
uint32_t vid_decoder_snapshots_lost(const VidDecoder *d) { return d->snapshots_lost; }
void vid_decoder_snapshot_peak(const VidDecoder *d, int *vram, int *cgram, int *sprites)
{
    *vram = d->vram_peak;
    *cgram = d->cgram_peak;
    *sprites = d->sprites_peak;
}

/* Memory is about to change: the spans emitted so far keep the contents they
 * were drawn with. */
static void keep_cgram(VidDecoder *d)
{
    bool need = false;
    for (uint16_t i = 0; i < d->frame.span_count; i++)
        need |= d->frame.spans[i].cgram == NULL;
    if (!need)
        return;
    if (d->cgram_used == VID_DEC_CGRAM_SNAPSHOTS) {
        d->snapshots_lost++;
        return;
    }
    uint16_t *slot = d->cgram_snap[d->cgram_used++];
    if (d->cgram_used > d->cgram_peak)
        d->cgram_peak = d->cgram_used;
    memcpy(slot, d->cgram, sizeof d->cgram);
    for (uint16_t i = 0; i < d->frame.span_count; i++)
        if (!d->frame.spans[i].cgram)
            d->frame.spans[i].cgram = slot;
}

/* VRAM is snapshotted only when a span that shows something reads it: mid-frame
 * uploads happen under forced blank, whose lines are black whatever VRAM holds,
 * and the battle's line interrupt does several in a frame. */
static void keep_vram(VidDecoder *d)
{
    bool need = false;
    for (uint16_t i = 0; i < d->frame.span_count; i++)
        need |= d->frame.spans[i].vram == NULL && !d->frame.spans[i].state.display.blank;
    if (!need)
        return;
    if (d->vram_used == VID_DEC_VRAM_SNAPSHOTS) {
        d->snapshots_lost++;
        return;
    }
    uint8_t *slot = d->vram_snap[d->vram_used++];
    if (d->vram_used > d->vram_peak)
        d->vram_peak = d->vram_used;
    memcpy(slot, d->vram, sizeof d->vram);
    for (uint16_t i = 0; i < d->frame.span_count; i++)
        if (!d->frame.spans[i].vram)
            d->frame.spans[i].vram = slot;
}

/* Likewise for the sprites, which only spans that show them read. */
static void keep_sprites(VidDecoder *d)
{
    bool need = false;
    for (uint16_t i = 0; i < d->frame.span_count; i++) {
        const VidSpan *sp = &d->frame.spans[i];
        need |= sp->sprites == NULL && !sp->state.display.blank &&
                ((sp->state.main_layers | sp->state.sub_layers) & VID_LAYER_BIT(VID_OBJ)) != 0;
    }
    if (!need)
        return;
    if (d->sprites_used == VID_DEC_SPRITE_SNAPSHOTS) {
        d->snapshots_lost++;
        return;
    }
    VidSpriteSet *slot = &d->sprite_snap[d->sprites_used++];
    if (d->sprites_used > d->sprites_peak)
        d->sprites_peak = d->sprites_used;
    memcpy(slot->sprites, d->frame.sprites, sizeof slot->sprites);
    slot->first_sprite = d->frame.first_sprite;
    for (uint16_t i = 0; i < d->frame.span_count; i++)
        if (!d->frame.spans[i].sprites)
            d->frame.spans[i].sprites = slot;
}

void vid_decode_state(const Ppu *p, VidLineState *st)
{
    memset(st, 0, sizeof *st);
    st->display = vid_display_decode(p->reg[0x00]);
    st->obj = vid_obj_config_decode(p->reg[0x01]);
    st->mode = vid_mode_decode(p->reg[0x05]);
    st->mosaic_size = (uint8_t)((p->reg[0x06] >> 4) + 1);
    for (int b = 0; b < 4; b++) {
        VidBg *bg = &st->bg[b];
        vid_bg_map_decode(bg, p->reg[0x07 + b]);
        uint8_t nba = p->reg[0x0B + b / 2];
        bg->char_base = (uint16_t)(((b & 1) ? nba >> 4 : nba & 0x0F) << 12);
        bg->hofs = p->bg_hofs[b];
        bg->vofs = p->bg_vofs[b];
        bg->mosaic = (p->reg[0x06] >> b) & 1;
        bg->large_tiles = (p->reg[0x05] >> (4 + b)) & 1;
    }
    st->mode7 = (VidMode7){ .a = p->m7a,
                            .b = p->m7b,
                            .c = p->m7c,
                            .d = p->m7d,
                            .cx = p->m7x,
                            .cy = p->m7y,
                            .hofs = p->m7hofs,
                            .vofs = p->m7vofs,
                            .hflip = (p->reg[0x1A] & 1) != 0,
                            .vflip = (p->reg[0x1A] & 2) != 0,
                            .outside = (uint8_t)(p->reg[0x1A] >> 6) };
    for (int g = 0; g < 3; g++)
        vid_windows_decode(&st->windows, 1u << g, p->reg[0x23 + g]);
    vid_windows_decode(&st->windows, VID_WINDOWS_W1_LEFT, p->reg[0x26]);
    vid_windows_decode(&st->windows, VID_WINDOWS_W1_RIGHT, p->reg[0x27]);
    vid_windows_decode(&st->windows, VID_WINDOWS_W2_LEFT, p->reg[0x28]);
    vid_windows_decode(&st->windows, VID_WINDOWS_W2_RIGHT, p->reg[0x29]);
    vid_windows_decode(&st->windows, VID_WINDOWS_BG_LOGIC, p->reg[0x2A]);
    vid_windows_decode(&st->windows, VID_WINDOWS_OBJ_LOGIC, p->reg[0x2B]);
    vid_windows_decode(&st->windows, VID_WINDOWS_MAIN, p->reg[0x2E]);
    vid_windows_decode(&st->windows, VID_WINDOWS_SUB, p->reg[0x2F]);
    vid_color_math_decode(&st->math, VID_MATH_CONTROL, p->reg[0x30]);
    vid_color_math_decode(&st->math, VID_MATH_LAYERS, p->reg[0x31]);
    st->config = vid_video_config_decode(p->reg[0x33]);
    st->fixed_color = p->fixed_color;
    st->main_layers = p->reg[0x2C];
    st->sub_layers = p->reg[0x2D];
}

static void decode_sprites(VidDecoder *d, const Ppu *p)
{
    for (int n = 0; n < VID_SPRITES; n++) {
        const uint8_t *e = &p->oam[n * 4];
        uint8_t hi = (uint8_t)(p->oam[0x200 + n / 4] >> ((n % 4) * 2));
        d->frame.sprites[n] = (VidSprite){ .x = (int16_t)(e[0] | (hi & 1) << 8),
                                           .y = e[1],
                                           .tile = (uint16_t)(e[2] | (e[3] & 1) << 8),
                                           .palette = (uint8_t)((e[3] >> 1) & 7),
                                           .priority = (uint8_t)((e[3] >> 4) & 3),
                                           .hflip = (e[3] & 0x40) != 0,
                                           .vflip = (e[3] & 0x80) != 0,
                                           .large = ((hi >> 1) & 1) != 0 };
    }
    d->frame.first_sprite = ppu_first_sprite(p);
}

void vid_decode_line(VidDecoder *d, const Ppu *p, uint16_t line)
{
    if (line == 0) {
        /* The frame that was drawn has been consumed: its spans and their
         * snapshots go, so the ones taken below belong to the new frame. */
        d->frame.span_count = 0;
        d->cgram_used = d->vram_used = d->sprites_used = 0;
    }
    /* CGRAM, VRAM and OAM are compared at every frame's start, which catches
     * writes in vblank and state loads, and during the frame only when their
     * write counters moved, which catches what a line interrupt does under
     * forced blank (render.c uses the VRAM counter the same way). */
    bool frame_start = line == 0;
    if (frame_start || !d->have_cgram || p->cgram_writes != d->cgram_writes) {
        d->cgram_writes = p->cgram_writes;
        if (memcmp(d->cgram, p->cgram, sizeof d->cgram) != 0) {
            keep_cgram(d);
            memcpy(d->cgram, p->cgram, sizeof d->cgram);
            d->palette_dirty = d->have_cgram;
        }
        d->have_cgram = true;
    }
    if (frame_start || !d->have_vram || p->vram_writes != d->vram_writes) {
        d->vram_writes = p->vram_writes;
        if (memcmp(d->vram, p->vram, sizeof d->vram) != 0) {
            keep_vram(d);
            memcpy(d->vram, p->vram, sizeof d->vram);
            d->vram_dirty = d->have_vram;
        }
        d->have_vram = true;
    }
    /* The sprites are whatever OAM holds, re-decoded when it or the priority
     * rotation changes; the spans before a mid-frame change keep the set they
     * showed, as with VRAM and CGRAM. */
    if (frame_start || !d->have_oam || p->oam_writes != d->oam_writes ||
        d->frame.first_sprite != ppu_first_sprite(p)) {
        d->oam_writes = p->oam_writes;
        if (d->frame.first_sprite != ppu_first_sprite(p) || memcmp(d->oam, p->oam, sizeof d->oam) != 0) {
            keep_sprites(d);
            memcpy(d->oam, p->oam, sizeof d->oam);
            decode_sprites(d, p);
            d->sprites_dirty = d->have_oam;
        }
        d->have_oam = true;
    }
    d->frame.vram = p->vram;
    d->frame.cgram = p->cgram;
    if (line == 0)
        return;

    VidLineState st;
    vid_decode_state(p, &st);
    VidSpan *last = d->frame.span_count ? &d->frame.spans[d->frame.span_count - 1] : NULL;
    /* A change of video memory starts a span too: the lines before it read what
     * memory held then, which is what the snapshots above keep. */
    if (!last || d->palette_dirty || d->vram_dirty || d->sprites_dirty ||
        memcmp(&last->state, &st, sizeof st) != 0) {
        if (d->frame.span_count < VID_MAX_SPANS) {
            VidSpan *sp = &d->frame.spans[d->frame.span_count++];
            sp->line = line;
            sp->palette_changed = d->palette_dirty;
            sp->state = st;
            sp->cgram = NULL;
            sp->vram = NULL;
            sp->sprites = NULL;
        }
        d->palette_dirty = false;
        d->vram_dirty = false;
        d->sprites_dirty = false;
    }
}
