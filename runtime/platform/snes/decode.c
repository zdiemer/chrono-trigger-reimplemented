/* VidFrame decoder (platform/snes/decode.h). */
#include "platform/snes/decode.h"

#include <stdlib.h>
#include <string.h>

struct VidDecoder {
    VidFrame frame;
    uint16_t cgram[256];   /* CGRAM as of the last line, to notice changes */
    bool palette_dirty;
    bool have_cgram;
};

VidDecoder *vid_decoder_create(void) { return calloc(1, sizeof(VidDecoder)); }
void vid_decoder_destroy(VidDecoder *d) { free(d); }
const VidFrame *vid_decoder_frame(const VidDecoder *d) { return &d->frame; }

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
    if (memcmp(d->cgram, p->cgram, sizeof d->cgram) != 0) {
        memcpy(d->cgram, p->cgram, sizeof d->cgram);
        d->palette_dirty = d->have_cgram;
        d->have_cgram = true;
    }
    d->frame.vram = p->vram;
    d->frame.cgram = p->cgram;
    if (line == 0) {
        d->frame.span_count = 0;
        return;
    }
    if (line == 1)
        decode_sprites(d, p);

    VidLineState st;
    vid_decode_state(p, &st);
    VidSpan *last = d->frame.span_count ? &d->frame.spans[d->frame.span_count - 1] : NULL;
    if (!last || d->palette_dirty || memcmp(&last->state, &st, sizeof st) != 0) {
        if (d->frame.span_count < VID_MAX_SPANS) {
            VidSpan *sp = &d->frame.spans[d->frame.span_count++];
            sp->line = line;
            sp->palette_changed = d->palette_dirty;
            sp->state = st;
        }
        d->palette_dirty = false;
    }
}
