/* ct_native --measure (frontend/measure.h). */
#define _POSIX_C_SOURCE 199309L   /* clock_gettime, on hosts other than Windows */
#include "measure.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#include "battle/program.h"
#include "core/program.h"
#include "field/field.h"
#include "menu/display.h"
#include "platform/native/host.h"
#include "platform/native/render.h"
#include "port/arena.h"
#include "port/fiber.h"
#include "port/state_host.h"
#include "port/vars_host.h"
#include "sound_native.h"
#include "warp/warp.h"

typedef enum { MODE_NONE, MODE_FIELD, MODE_BATTLE, MODE_MENU, MODE_SCENE, MODE_WARP, MODES } Mode;
static const char *const mode_names[MODES] = { "none", "field", "battle", "menu", "scene", "warp" };

typedef struct {
    unsigned long frames;
    uint32_t *times[4];         /* each frame's game, sound, render and whole, ns */
    unsigned long room;
    uint64_t game_ns, sound_ns, render_ns;     /* the heaviest frame's parts */
    uint64_t heaviest, game_max, sound_max, render_max;
    uint64_t episode_max;       /* the most resource bytes one stay read */
} ModeTotals;

struct Measure {
    const AssetSource *inner;
    AssetSource source;
    uint8_t *seen;              /* the resources this stay has read, a byte a row */
    Mode mode;
    uint64_t episode;           /* resource bytes this stay has read */
    ModeTotals modes[MODES];
};

uint64_t measure_now_ns(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (uint64_t)((double)now.QuadPart * 1e9 / (double)frequency.QuadPart);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
#endif
}

/* The source the game reads through: the same answers, each resource counted
 * the first time a stay reads it. */
static const uint8_t *counted_resource(void *ctx, AssetId id, uint32_t *length)
{
    Measure *m = ctx;
    const uint8_t *bytes = m->inner->resource(m->inner->ctx, id, length);
    if (bytes && !m->seen[id]) {
        m->seen[id] = 1;
        m->episode += *length;
    }
    return bytes;
}

Measure *measure_create(const AssetSource *inner, AssetSource *out)
{
    Measure *m = calloc(1, sizeof *m);
    uint8_t *seen = calloc(asset_catalogue_count, 1);
    if (!m || !seen) {
        free(m);
        free(seen);
        return NULL;
    }
    m->inner = inner;
    m->seen = seen;
    m->source = (AssetSource){ counted_resource, m };
    *out = m->source;
    return m;
}

void measure_destroy(Measure *m)
{
    if (!m)
        return;
    for (unsigned i = 0; i < MODES; i++)
        for (unsigned k = 0; k < 4; k++)
            free(m->modes[i].times[k]);
    free(m->seen);
    free(m);
}

static Mode mode_of(GameState *g)
{
    ProgramHandler h = program_handlers(g)->vblank;
    if (!h)
        return MODE_NONE;
    if (h == field_vblank)
        return MODE_FIELD;
    if (h == battle_vblank || h == battle_closing_vblank)
        return MODE_BATTLE;
    if (h == menu_vblank || h == menu_object_vblank)
        return MODE_MENU;
    if (h == warp_vblank)
        return MODE_WARP;
    return MODE_SCENE;
}

static void end_stay(Measure *m)
{
    ModeTotals *t = &m->modes[m->mode];
    if (m->episode > t->episode_max)
        t->episode_max = m->episode;
    m->episode = 0;
}

void measure_frame(Measure *m, GameState *g, uint64_t game_ns, uint64_t sound_ns, uint64_t render_ns)
{
    if (!m)
        return;
    ModeTotals *t = &m->modes[m->mode];
    uint64_t whole = game_ns + sound_ns + render_ns;
    if (t->frames == t->room) {
        unsigned long room = t->room ? t->room * 2 : 4096;
        bool grown = true;
        for (unsigned k = 0; k < 4; k++) {
            uint32_t *more = realloc(t->times[k], room * sizeof *more);
            if (more)
                t->times[k] = more;
            grown = grown && more;
        }
        if (grown)
            t->room = room;
    }
    if (t->frames < t->room) {
        uint64_t parts[4] = { game_ns, sound_ns, render_ns, whole };
        for (unsigned k = 0; k < 4; k++)
            t->times[k][t->frames] = parts[k] > UINT32_MAX ? UINT32_MAX : (uint32_t)parts[k];
    }
    t->frames++;
    if (whole > t->heaviest) {
        t->heaviest = whole;
        t->game_ns = game_ns;
        t->sound_ns = sound_ns;
        t->render_ns = render_ns;
    }
    if (game_ns > t->game_max) t->game_max = game_ns;
    if (sound_ns > t->sound_max) t->sound_max = sound_ns;
    if (render_ns > t->render_max) t->render_max = render_ns;
    Mode now = mode_of(g);
    if (now != m->mode) {
        end_stay(m);
        m->mode = now;
        memset(m->seen, 0, asset_catalogue_count);   /* the next stay's reads, each seen again */
    }
}

static int by_value(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void measure_report(const Measure *m, FILE *out, const Platform *pf)
{
    uint64_t catalogue = 0;
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        catalogue += asset_catalogue[i].length;
    fprintf(out, "measure: size game_state %zu\n", state_host_bytes());
    fprintf(out, "measure: size game_state_ram %zu\n", state_host_vars_bytes());
#define PART(name) fprintf(out, "measure: part %s %zu\n", #name, sizeof(((const VarsHost *)0)->name))
#ifdef CT_FLAT_STATE
    fprintf(out, "measure: part work_ram %u\n", 0x20000u);   /* the original's, a byte an address */
#else
    PART(battle); PART(cartridge); PART(core); PART(data); PART(event); PART(field); PART(locations);
    PART(menu); PART(menu_object); PART(overworld); PART(palettes); PART(scene); PART(sound); PART(warp);
    PART(overlay_writer);
#ifndef CT_LEAN
    PART(crossed); PART(crossed_writer);   /* the crossings' tally, which the lean build keeps none of */
#endif
#endif
#undef PART
    fprintf(out, "measure: size arena_used %zu\n", arena_used());
    fprintf(out, "measure: size renderer %zu\n", vid_render_bytes());
    fprintf(out, "measure: size sound_player %zu\n", sound_native_bytes());
    fprintf(out, "measure: size observer %zu\n", vid_host_observer_bytes(pf));
    fprintf(out, "measure: size resources %" PRIu64 "\n", catalogue);
    fprintf(out, "measure: size fiber_stack %zu x %u\n", fiber_stack_size(), fiber_slots());
    for (unsigned s = 0; s < fiber_slots(); s++) {
        size_t used = fiber_stack_high_water(s);
        if (used)
            fprintf(out, "measure: stack %u %zu\n", s, used);
    }
    if (!m)
        return;
    Measure *w = (Measure *)m;
    end_stay(w);
    for (unsigned i = 0; i < MODES; i++) {
        ModeTotals *t = &w->modes[i];
        if (!t->frames)
            continue;
        unsigned long kept = t->frames < t->room ? t->frames : t->room;
        uint64_t p99[4] = { 0, 0, 0, 0 };
        for (unsigned k = 0; k < 4 && kept; k++) {
            qsort(t->times[k], kept, sizeof *t->times[k], by_value);
            p99[k] = t->times[k][(kept * 99) / 100];
        }
        fprintf(out, "measure: mode %s frames %lu heaviest_ns %" PRIu64 " game %" PRIu64 " sound %" PRIu64
                " render %" PRIu64 " p99_ns %" PRIu64 " game_p99 %" PRIu64 " sound_p99 %" PRIu64
                " render_p99 %" PRIu64 " game_max %" PRIu64 " sound_max %" PRIu64 " render_max %" PRIu64
                " resources %" PRIu64 "\n",
                mode_names[i], t->frames, t->heaviest, t->game_ns, t->sound_ns, t->render_ns, p99[3], p99[0],
                p99[1], p99[2], t->game_max, t->sound_max, t->render_max, t->episode_max);
    }
}
