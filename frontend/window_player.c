/* ct_play's desktop adapter to the shared window driver. */
#include "driver.h"
#include "bindings.h"

typedef struct {
    Player *player;
    const Files *files;
    char halt[1600];
} PlayerWindow;

static void step(void *ctx, uint16_t buttons)
{
    PlayerWindow *w = ctx;
    player_step(w->player, buttons);
    files_after_frame(w->files, w->player);
}

static const uint32_t *pixels(void *ctx) { return player_pixels(((PlayerWindow *)ctx)->player); }
static size_t take_audio(void *ctx, int16_t *out, size_t frames) { return player_take_audio(ctx, out, frames); }
static void discard_audio(void *ctx) { player_discard_audio(ctx); }
static void title(void *ctx, bool paused, char *out, size_t size)
{
    player_title(((PlayerWindow *)ctx)->player, paused, out, size);
}

static const char *halt(void *ctx)
{
    PlayerWindow *w = ctx;
    Player *pl = w->player;
    if (!pl->snes.halted)
        return NULL;
    char load[64], quit[64];
    snprintf(w->halt, sizeof w->halt,
             "The game stopped at frame %u: %s.\n\nThe address is logged to %s. %s loads the quick save; %s quits.",
             (unsigned)pl->snes.frame, player_halt_reason(pl), player_misses_name(pl),
             bindings_name(ACT_QUICK_LOAD, load, sizeof load, "QuickLoad"),
             bindings_name(ACT_QUIT, quit, sizeof quit, "closing the window"));
    return w->halt;
}

static bool done(void *ctx) { (void)ctx; return false; }
static bool fast(void *ctx)
{
    Player *pl = ((PlayerWindow *)ctx)->player;
    return player_replaying(pl) && !pl->watch;
}
static void quick_save(void *ctx) { player_request_save(((PlayerWindow *)ctx)->player); }
static void quick_load(void *ctx)
{
    PlayerWindow *w = ctx;
    player_cancel_save(w->player);
    files_load_state(w->player, w->files->state);
}
static int flush(void *ctx)
{
    PlayerWindow *w = ctx;
    files_flush(w->files, w->player);
    return 0;
}

int run_window(Player *pl, const Files *files, const WindowOptions *o)
{
    PlayerWindow w = { .player = pl, .files = files };
    WindowClient client = {
        .ctx = &w, .name = "ct_play", .step = step, .pixels = pixels,
        .audio = { pl, take_audio, discard_audio }, .title = title,
        .halt = halt, .done = done, .fast = fast,
        .quick_save = quick_save, .quick_load = quick_load, .flush = flush
    };
    int rc = window_run(&client, o);
    if (files_flush_movie(files, pl) != 0)
        rc = 1;
    return rc;
}
