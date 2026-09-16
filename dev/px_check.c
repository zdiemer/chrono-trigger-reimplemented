#include "px_check.h"

#include <stdlib.h>
#include <string.h>

#include "platform/native/render.h"
#include "platform/snes/decode.h"

enum { KINDS = 32, EXAMPLES = 3, PIXELS = VID_RENDER_WIDTH * VID_RENDER_HEIGHT };

typedef struct {
    const char *kind;
    uint64_t frames, lines, pixels;
    uint32_t first_frame;
    char example[EXAMPLES][224];
    uint32_t example_frame[EXAMPLES];
    int examples;
} KindRow;

struct PxCheck {
    Snes *s;
    const Renderer *ref;
    VidDecoder *dec;
    VidRenderer *vr;
    uint32_t *pixels;                 /* the native renderer's frame */
    uint64_t frames, equal, diff_lines, diff_pixels;
    uint64_t flag_diffs;              /* frames where range-over or time-over differ */
    /* When video memory last changed while the frame was being drawn (0: not
     * since line 1), so a difference can be told from a gap in the VidFrame. */
    uint16_t cgram_line, vram_line, oam_line, obsel_line;
    uint16_t cgram_seen[256];
    uint8_t oam_seen[0x220];
    uint32_t vram_writes_seen;
    uint8_t obsel_seen;
    KindRow kinds[KINDS];
    int kind_count;
};

PxCheck *px_check_create(Snes *s, const Renderer *reference)
{
    PxCheck *pc = calloc(1, sizeof *pc);
    pc->s = s;
    pc->ref = reference;
    pc->dec = vid_decoder_create();
    pc->vr = vid_render_create();
    pc->pixels = malloc(sizeof *pc->pixels * PIXELS);
    return pc;
}

static KindRow *row_for(PxCheck *pc, const char *kind)
{
    for (int i = 0; i < pc->kind_count; i++)
        if (pc->kinds[i].kind == kind)
            return &pc->kinds[i];
    if (pc->kind_count == KINDS)
        return NULL;
    KindRow *row = &pc->kinds[pc->kind_count++];
    row->kind = kind;
    row->first_frame = pc->s->frame;
    return row;
}

static const char *mode_kind(uint8_t mode)
{
    static const char *const names[8] = { "mode 0", "mode 1", "mode 2", "mode 3",
                                          "mode 4", "mode 5", "mode 6", "mode 7" };
    return names[mode & 7];
}

/* The state of `line` in the frame: the span it falls in. */
static const VidLineState *state_at(const VidFrame *f, int line)
{
    const VidLineState *st = NULL;
    for (uint16_t i = 0; i < f->span_count; i++) {
        if (f->spans[i].line > line)
            break;
        st = &f->spans[i].state;
    }
    return st;
}

static void compare(PxCheck *pc, const VidFrame *f)
{
    const uint32_t *ours = pc->pixels, *ref = pc->ref->pixels;
    pc->frames++;
    if (!memcmp(ours, ref, sizeof *ours * PIXELS)) {
        pc->equal++;
        return;
    }
    int first_line = 0, first_x = 0;
    uint64_t lines = 0, pixels = 0;
    for (int y = 0; y < VID_RENDER_HEIGHT; y++) {
        bool any = false;
        for (int x = 0; x < VID_RENDER_WIDTH; x++) {
            size_t i = (size_t)y * VID_RENDER_WIDTH + (size_t)x;
            if (ours[i] == ref[i])
                continue;
            pixels++;
            if (!any) {
                any = true;
                lines++;
                if (!first_line) {
                    first_line = y + 1;
                    first_x = x;
                }
            }
        }
    }
    pc->diff_lines += lines;
    pc->diff_pixels += pixels;

    /* What the difference is put down to: a mid-frame change of something the
     * VidFrame carries only as it ended up, if one happened at or before the
     * first differing line, otherwise the BG mode there. */
    const char *kind;
    if (pc->vram_line && first_line >= pc->vram_line)
        kind = "vram mid-frame";
    else if (pc->cgram_line && first_line >= pc->cgram_line)
        kind = "cgram mid-frame";
    else if (pc->oam_line && first_line >= pc->oam_line)
        kind = "oam mid-frame";
    else if (pc->obsel_line && first_line >= pc->obsel_line)
        kind = "obsel mid-frame";
    else {
        const VidLineState *st = state_at(f, first_line);
        kind = st ? mode_kind(st->mode.mode) : "no span";
    }
    KindRow *row = row_for(pc, kind);
    if (!row)
        return;
    row->frames++;
    row->lines += lines;
    row->pixels += pixels;
    if (row->examples < EXAMPLES) {
        const VidLineState *st = state_at(f, first_line);
        size_t i = (size_t)(first_line - 1) * VID_RENDER_WIDTH + (size_t)first_x;
        snprintf(row->example[row->examples], sizeof row->example[0],
                 "%llu lines, %llu pixels; first at line %d x %d: %06X, render.c %06X"
                 " (mode %u, layers %02X/%02X, math %02X, blank %d; mid-frame vram %u cgram %u oam %u)",
                 (unsigned long long)lines, (unsigned long long)pixels, first_line, first_x,
                 (unsigned)(ours[i] & 0xFFFFFF), (unsigned)(ref[i] & 0xFFFFFF), st ? st->mode.mode : 9,
                 st ? st->main_layers : 0, st ? st->sub_layers : 0, st ? st->math.layers : 0,
                 st ? (int)st->display.blank : -1, pc->vram_line, pc->cgram_line, pc->oam_line);
        row->example_frame[row->examples] = pc->s->frame;
        row->examples++;
    }
}

void px_check_line(PxCheck *pc, uint16_t line)
{
    const Ppu *p = &pc->s->ppu;
    if (line <= 1) {
        pc->cgram_line = pc->vram_line = pc->oam_line = pc->obsel_line = 0;
        memcpy(pc->cgram_seen, p->cgram, sizeof pc->cgram_seen);
        memcpy(pc->oam_seen, p->oam, sizeof pc->oam_seen);
        pc->vram_writes_seen = p->vram_writes;
        pc->obsel_seen = p->reg[0x01];
    } else {
        if (!pc->cgram_line && memcmp(pc->cgram_seen, p->cgram, sizeof pc->cgram_seen) != 0)
            pc->cgram_line = line;
        if (!pc->oam_line && memcmp(pc->oam_seen, p->oam, sizeof pc->oam_seen) != 0)
            pc->oam_line = line;
        if (!pc->vram_line && p->vram_writes != pc->vram_writes_seen)
            pc->vram_line = line;
        if (!pc->obsel_line && p->reg[0x01] != pc->obsel_seen)
            pc->obsel_line = line;
    }
    vid_decode_line(pc->dec, p, line);
    if (line != VID_RENDER_HEIGHT)
        return;

    const VidFrame *f = vid_decoder_frame(pc->dec);
    VidRenderFlags flags;
    vid_render_frame(pc->vr, f, pc->pixels, &flags);
    if (flags.range_over != p->range_over || flags.time_over != p->time_over)
        pc->flag_diffs++;
    compare(pc, f);
}

uint64_t px_check_frames(const PxCheck *pc) { return pc->frames; }
uint64_t px_check_equal(const PxCheck *pc) { return pc->equal; }

static int kind_order(const void *pa, const void *pb)
{
    const KindRow *a = pa, *b = pb;
    return a->frames == b->frames ? 0 : (a->frames > b->frames ? -1 : 1);
}

int px_check_write(PxCheck *pc, const char *path, FILE *summary)
{
    qsort(pc->kinds, (size_t)pc->kind_count, sizeof pc->kinds[0], kind_order);
    fprintf(summary, "px-check: %llu of %llu frames identical (%.4f%%), %llu differing lines, %llu pixels\n",
            (unsigned long long)pc->equal, (unsigned long long)pc->frames,
            pc->frames ? 100.0 * (double)pc->equal / (double)pc->frames : 0.0,
            (unsigned long long)pc->diff_lines, (unsigned long long)pc->diff_pixels);
    if (pc->flag_diffs)
        fprintf(summary, "  range-over/time-over differ on %llu frames\n", (unsigned long long)pc->flag_diffs);
    int vram_peak, cgram_peak, sprite_peak;
    vid_decoder_snapshot_peak(pc->dec, &vram_peak, &cgram_peak, &sprite_peak);
    fprintf(summary, "  mid-frame snapshots: at most %d VRAM, %d CGRAM, %d sprite sets in a frame, %u lost\n",
            vram_peak, cgram_peak, sprite_peak, vid_decoder_snapshots_lost(pc->dec));
    VidRenderStats st;
    vid_render_get_stats(pc->vr, &st);
    fprintf(summary, "  native renderer: %llu spans, %llu lines drawn, %llu kept, %llu blank (%.1f%% drawn)\n",
            (unsigned long long)st.spans, (unsigned long long)st.lines_drawn, (unsigned long long)st.lines_kept,
            (unsigned long long)st.lines_blank,
            st.frames ? 100.0 * (double)st.lines_drawn / ((double)st.frames * VID_RENDER_HEIGHT) : 0.0);
    for (int i = 0; i < pc->kind_count; i++)
        fprintf(summary, "  %-16s %8llu frames, %8llu lines, first %u\n", pc->kinds[i].kind,
                (unsigned long long)pc->kinds[i].frames, (unsigned long long)pc->kinds[i].lines,
                pc->kinds[i].first_frame);
    if (!path)
        return 0;
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# ct_dev --px-check: the native renderer's frame against render.c's, at 256x224\n");
    fprintf(f, "# %llu of %llu frames identical; %llu differing lines, %llu pixels; %u snapshots lost\n",
            (unsigned long long)pc->equal, (unsigned long long)pc->frames, (unsigned long long)pc->diff_lines,
            (unsigned long long)pc->diff_pixels, vid_decoder_snapshots_lost(pc->dec));
    for (int i = 0; i < pc->kind_count; i++) {
        fprintf(f, "\n%s: %llu frames, %llu lines, %llu pixels, first at frame %u\n", pc->kinds[i].kind,
                (unsigned long long)pc->kinds[i].frames, (unsigned long long)pc->kinds[i].lines,
                (unsigned long long)pc->kinds[i].pixels, pc->kinds[i].first_frame);
        for (int k = 0; k < pc->kinds[i].examples; k++)
            fprintf(f, "  frame %u: %s\n", pc->kinds[i].example_frame[k], pc->kinds[i].example[k]);
    }
    return fclose(f) == 0 ? 0 : -1;
}

void px_check_destroy(PxCheck *pc)
{
    if (!pc)
        return;
    vid_render_destroy(pc->vr);
    vid_decoder_destroy(pc->dec);
    free(pc->pixels);
    free(pc);
}
