/* The menus: keeping the main loop in step with the display.
 *
 * Each of bank C2's two modes has a main loop that does a frame's work and
 * then waits for its vblank handler: the sprite-object mode's for the next
 * frame to be counted (game/menu/display.c, menu_object_nmi), the menus'
 * for the handler to have sent the uploads the frame queued
 * (menu_nmi_work). Each mode is in its wait at nearly every frame's end, so
 * their waits run as restartable C loops in shipping mode, with the caller
 * frame held on the saved stack. One-pass entries remain for saved addresses
 * and tools; native_idle_held keeps their polling and exit time.
 * Between its waits the menus' loop runs a turn of each of their tasks
 * (menu/tasks.c), which a screen starts and stops. */
#include "menu/menu.h"

#include "charge.h"
#include "core/input.h"
#include "overworld/music.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "port/vars_ram.h"
#include "vars.h"

// $C2:0346, $C2:046E (the wait's start, and the jump to it)
/* The sprite-object mode's wait for the next frame: until the vblank handler
 * has counted a frame past the one it sees now. The mode is in it at nearly
 * every frame's end, so a saved machine has to hold it: the shipping mode
 * leaves it to the loop's named idle continuation, below, which goes on as
 * this does. Its registry alias keeps shadow comparisons. The loop keeps the
 * count it saw in A, comparing the count with it, and the comparison's flags
 * are dead after it (the entries' set). */
enum { FRAME_WAIT = 0xC20346, FRAME_WAIT_LOOP = 0xC20348 };

/* The wait, a frame at a time, past the count it `seen`: `a_frame` lets one
 * go by, false where the program doesn't come back to the wait. */
static bool wait_for_the_next_frame(const GameState *g, uint8_t seen, bool (*a_frame)(void *ctx), void *ctx)
{
    while (menu_object_frame_counter(g) == seen)
        if (!a_frame(ctx))
            return false;
    return true;
}

typedef struct {
    GameState *game;
    Platform *pf;
} MenuFrameContext;

static bool a_frame(void *ctx)
{
    tm_wait_frame(((MenuFrameContext *)ctx)->pf, PF_AT(FRAME_WAIT_LOOP));
    return true;
}

void menu_wait_for_the_next_frame(GameState *g, Platform *pf)
{
    MenuFrameContext c = { g, pf };
    TIMED_AS(g, FRAME_WAIT, (void)wait_for_the_next_frame(g, menu_object_frame_counter(g), a_frame, &c));
}

// $C2:0454

/* The frames waited, each by `a_frame` (false where the program doesn't come
 * back to the wait); whether the last frame sent a sound. A count of 0 is
 * 65,536 frames, as the original counts it down. */
typedef struct {
    bool (*a_frame)(void *ctx, uint16_t left);
    void (*frame_done)(void *ctx);
} FramesLeft;

static bool wait_frames_sending_the_sound(GameState *g, Platform *pf, uint16_t frames, const FramesLeft *f, void *ctx,
                                          bool *sent)
{
    uint16_t left = frames;
    do {
        if (!f->a_frame(ctx, left))
            return false;
        AudioCommand waiting;
        *sent = overworld_take_the_waiting_sound(g, &waiting);
        if (*sent) {
            au_command(pf, PF_CALL(0xC20462, 0xC70004), waiting);
            overworld_waiting_sound_sent(g);
        }
        f->frame_done(ctx);
        left--;
    } while (left != 0);
    return true;
}

static bool a_frame_left(void *ctx, uint16_t left)
{
    (void)left;
    MenuFrameContext *c = ctx;
    menu_wait_for_the_next_frame(c->game, c->pf);
    return true;
}

static void frame_done(void *ctx) { (void)ctx; }

void menu_wait_frames_sending_the_sound(GameState *g, Platform *pf, uint16_t frames)
{
    static const FramesLeft in_the_program = { a_frame_left, frame_done };
    MenuFrameContext c = { g, pf };
    bool sent = false;
    TIMED_AS(g, 0xC20454, (void)wait_frames_sending_the_sound(g, pf, frames, &in_the_program, &c, &sent));
}

void menu_stop_tasks(GameState *g, uint8_t tasks)
{
    for (unsigned task = 0; task < MENU_TASKS; task++)
        if (tasks >> task & 1)
            set_menu_tasks_state(g, task, 0);
}
