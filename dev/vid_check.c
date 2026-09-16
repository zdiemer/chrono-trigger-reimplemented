#include "vid_check.h"

#include <stdlib.h>
#include <string.h>

#include "platform/observe.h"
#include "platform/snes/decode.h"

enum { KINDS = 64, EXAMPLES = 3 };

typedef struct {
    const char *kind;
    uint64_t frames;
    uint32_t first_frame;
    char example[EXAMPLES][192];
    uint32_t example_frame[EXAMPLES];
    int examples;
} KindRow;

struct VidCheck {
    Snes *s;
    VidDecoder *dec;
    VidObserver *obs;
    uint64_t frames, equal;
    uint32_t from_frame;
    bool seeded;
    KindRow kinds[KINDS];
    int kind_count;
};

static uint8_t peek(void *ctx, uint32_t addr) { return snes_bus_peek(&((Snes *)ctx)->bus, addr); }

VidCheck *vid_check_create(Snes *s, uint32_t from_frame)
{
    VidCheck *vc = calloc(1, sizeof *vc);
    vc->s = s;
    vc->from_frame = from_frame;
    vc->seeded = from_frame == 0;
    vc->dec = vid_decoder_create();
    vc->obs = vid_obs_create(peek, s);
    vid_observer = vc->obs;
    return vc;
}

static void note(VidCheck *vc, const VidDiff *d)
{
    KindRow *row = NULL;
    for (int i = 0; i < vc->kind_count; i++)
        if (vc->kinds[i].kind == d->kind)
            row = &vc->kinds[i];
    if (!row) {
        if (vc->kind_count == KINDS)
            return;
        row = &vc->kinds[vc->kind_count++];
        row->kind = d->kind;
        row->first_frame = vc->s->frame;
    }
    row->frames++;
    if (row->examples < EXAMPLES) {
        snprintf(row->example[row->examples], sizeof row->example[0], "%s", d->text);
        row->example_frame[row->examples] = vc->s->frame;
        row->examples++;
    }
}

void vid_check_line(VidCheck *vc, uint16_t line)
{
    if (!vc->seeded && line == 0 && vc->s->frame >= vc->from_frame) {
        VidLineState st;
        vid_decode_state(&vc->s->ppu, &st);
        vid_obs_seed(vc->obs, &st, vc->s->ppu.vram, vc->s->ppu.cgram, vc->s->ppu.oam);
        vc->seeded = true;
    }
    vid_decode_line(vc->dec, &vc->s->ppu, line);
    vid_obs_line(vc->obs, line);
    if (line != 224 || !vc->seeded)
        return;
    vc->frames++;
    VidDiff d;
    if (vid_frame_diff(vid_decoder_frame(vc->dec), vid_obs_frame(vc->obs), &d))
        note(vc, &d);
    else
        vc->equal++;
}

static int kind_order(const void *pa, const void *pb)
{
    const KindRow *a = pa, *b = pb;
    return a->frames == b->frames ? 0 : (a->frames > b->frames ? -1 : 1);
}

int vid_check_write(VidCheck *vc, const char *path, FILE *summary)
{
    qsort(vc->kinds, (size_t)vc->kind_count, sizeof vc->kinds[0], kind_order);
    fprintf(summary, "vid-check: %llu of %llu frames equal (%.2f%%), %u calls not modelled\n",
            (unsigned long long)vc->equal, (unsigned long long)vc->frames,
            vc->frames ? 100.0 * (double)vc->equal / (double)vc->frames : 0.0, vid_obs_unmodelled(vc->obs));
    for (int i = 0; i < vc->kind_count; i++)
        fprintf(summary, "  %-16s %8llu frames, first %u\n", vc->kinds[i].kind,
                (unsigned long long)vc->kinds[i].frames, vc->kinds[i].first_frame);
    if (!path)
        return 0;
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# ct_dev --vid-check: the command-built VidFrame against the one decoded from the PPU registers\n");
    fprintf(f, "# %llu of %llu frames equal; %u calls not modelled\n", (unsigned long long)vc->equal,
            (unsigned long long)vc->frames, vid_obs_unmodelled(vc->obs));
    for (int i = 0; i < vc->kind_count; i++) {
        fprintf(f, "\n%s: %llu frames, first at frame %u\n", vc->kinds[i].kind,
                (unsigned long long)vc->kinds[i].frames, vc->kinds[i].first_frame);
        for (int k = 0; k < vc->kinds[i].examples; k++)
            fprintf(f, "  frame %u: %s\n", vc->kinds[i].example_frame[k], vc->kinds[i].example[k]);
    }
    return fclose(f) == 0 ? 0 : -1;
}

void vid_check_destroy(VidCheck *vc)
{
    if (!vc)
        return;
    if (vid_observer == vc->obs)
        vid_observer = NULL;
    vid_obs_destroy(vc->obs);
    vid_decoder_destroy(vc->dec);
    free(vc);
}
