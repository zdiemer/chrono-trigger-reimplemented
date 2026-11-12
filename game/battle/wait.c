/* Battle: waiting for the frame.
 *
 * The battle's main thread paces itself on its vblank handler (battle/nmi.c).
 * It asks the handler for something, gives the processor up until a handler
 * has run (the timing service's tm_wait_for_handlers), and looks again:
 *
 *   - the frame wait asks for the frame's update -- the game's per-frame work,
 *     which the handler runs unless the battle is paused -- and gives the
 *     pause its turn while it waits (battle_wait_for_frame); the return to the
 *     field waits for its last frames the same way, with no pause to give a
 *     turn to;
 *   - the frame work's wait asks only that the handler finish a frame, giving
 *     the pause its turn the same way, and then types the next letter of a
 *     battle message (battle/message.c); its wait for the next frame adds the
 *     menu's turn, and its wait for some frames makes a number of those;
 *   - the battle's start and end wait for their own small handler
 *     (battle_nmi_alternate) to finish a frame.
 *
 * Two other waits stay passes of the original's loops, run on the machine,
 * each an idle entry: for the window's slide to have finished, and for the
 * battle's graphics to have loaded at its start. What a pass leaves in A and
 * the flags (the value it tested) is what the original's code after the loop
 * finds. (The line handler's wait for its line to be drawn is the timing
 * service's, battle/nmi.c.) */
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/message.h"
#include "battle/sound.h"
#include "battle/program.h"
#include "battle/wait.h"
#include "charge.h"
#include "platform/timing.h"
#include "vars.h"

/* ---- The waits ------------------------------------------------------------------------------ */

// $CD:3E58
/* A frame of a paused fight sat out: the vblank handler has finished a frame
 * once it clears what the battle counted up to ask for it (battle_nmi). */
void battle_sit_out_a_paused_frame(GameState *g, Platform *pf)
{
    set_battle_nmi_pending(g, (uint8_t)(battle_nmi_pending(g) + 1));
    while (battle_nmi_pending(g))
        tm_wait_for_handlers(pf, PF_LOOP(0xCD3E5B, 0xCD3E60));
}

/* The pause's turn (battle/sound.c) and the message's next letter
 * (battle/message.c), each a routine of its own, which the program calls as
 * C and the machine's entries from the original's calls. */
typedef bool (*WaitCall)(void *ctx);

// $C1:007E
/* The battle's wait for the next frame: the frame's update asked for, and
 * waited for. A paused battle sits the pause out on a pass of it, and its
 * update, held while paused, comes after. False where the pause's turn went
 * no further. */
static bool wait_for_the_frame(GameState *g, Platform *pf, WaitCall pause, void *ctx)
{
    set_battle_frame_pending(g, (uint8_t)(battle_frame_pending(g) + 1));
    for (;;) {
        if (battle_paused(g) && !pause(ctx))
            return false;
        if (!battle_frame_pending(g))
            return true;
        tm_wait_for_handlers(pf, PF_LOOP(0xC10080, 0xC10088));
    }
}

// $CF:FCFB
/* The same as the battle hands back to the field, when there is no pause. */
void battle_wait_for_a_last_frame(GameState *g, Platform *pf)
{
    set_battle_frame_pending(g, (uint8_t)(battle_frame_pending(g) + 1));
    while (battle_frame_pending(g))
        tm_wait_for_handlers(pf, PF_LOOP(0xCFFCFD, 0xCFFD01));
}

// $CD:3E7D
/* The frame work's wait: the vblank handler's frame waited for, the pause
 * given its turn on each pass, then the next letter of a battle message
 * typed. */
static bool wait_for_the_handler(GameState *g, Platform *pf, WaitCall pause, WaitCall type_the_message, void *ctx)
{
    set_battle_nmi_pending(g, (uint8_t)(battle_nmi_pending(g) + 1));
    for (;;) {
        if (battle_paused(g) && !pause(ctx))
            return false;
        if (!battle_nmi_pending(g))
            break;
        tm_wait_for_handlers(pf, PF_LOOP(0xCD3E81, 0xCD3E89));
    }
    return type_the_message(ctx);
}

// $CD:3E75
/* The frame work's wait for the next frame: the handler's frame, then the
 * menu's turn (battle/frame_step.c). */
static bool wait_for_the_next_frame(WaitCall handler, WaitCall menu_turn, void *ctx)
{
    return handler(ctx) && menu_turn(ctx);
}

// $CD:3E3B
/* A number of those (0 is 256). */
static bool wait_some_frames(uint8_t frames, bool (*next_frame)(void *ctx, uint8_t left), void *ctx)
{
    uint8_t left = frames;
    do {
        if (!next_frame(ctx, left))
            return false;
    } while (--left);
    return true;
}

// $CD:044A
/* The battle's start and end run a small vblank handler of their own
 * (battle_nmi_alternate), which clears what this sets once it has finished a
 * frame. */
void battle_wait_for_the_small_handler(GameState *g, Platform *pf)
{
    set_battle_alternate_frame_pending(g, 1);
    while (battle_alternate_frame_pending(g))
        tm_wait_for_handlers(pf, PF_LOOP(0xCD044E, 0xCD0452));
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} FrameWaitContext;

/* The pause's guard and the menu turn's own guard ask nothing of the game's
 * state that the pause doesn't keep; the message's typing, that the menus'
 * text code can go on from a message being set up. */
static bool sit_out_a_pause_frame(void *ctx)
{
    FrameWaitContext *c = ctx;
    TIMED_AS(c->game, BATTLE_PAUSE_ENTRY, battle_sit_out_the_pause(c->game, c->pf));
    return true;
}

static bool type_a_letter(void *ctx)
{
    GameState *g = ((FrameWaitContext *)ctx)->game;
    if (!battle_message_typing_fits(g))
        return battle_contract_broken(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, "a message the menus' text code doesn't go on from");
    TIMED_AS(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, battle_type_the_message(g));
    return true;
}

static bool wait_out_the_handler(void *ctx)
{
    FrameWaitContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_HANDLER_WAIT, done = wait_for_the_handler(c->game, c->pf, sit_out_a_pause_frame, type_a_letter, c));
    return done;
}

static bool take_a_menu_turn(void *ctx)
{
    FrameWaitContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_MENU_TURN_ENTRY, done = battle_menu_turn(c->game, c->pf));
    return done;
}

static bool wait_for_a_next_frame(void *ctx, uint8_t left)
{
    (void)left;
    FrameWaitContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_NEXT_FRAME_WAIT, done = wait_for_the_next_frame(wait_out_the_handler, take_a_menu_turn, c));
    return done;
}

bool battle_wait_for_frame(GameState *g, Platform *pf)
{
    FrameWaitContext c = { g, pf };
    return wait_for_the_frame(g, pf, sit_out_a_pause_frame, &c);
}

bool battle_wait_for_the_handler(GameState *g, Platform *pf)
{
    FrameWaitContext c = { g, pf };
    return wait_for_the_handler(g, pf, sit_out_a_pause_frame, type_a_letter, &c);
}

bool battle_wait_for_the_next_frame(GameState *g, Platform *pf)
{
    FrameWaitContext c = { g, pf };
    return wait_for_the_next_frame(wait_out_the_handler, take_a_menu_turn, &c);
}

bool battle_wait_some_frames(GameState *g, Platform *pf, uint8_t frames)
{
    FrameWaitContext c = { g, pf };
    return wait_some_frames(frames, wait_for_a_next_frame, &c);
}

/* The frame work's wait for the next frame: the handler's frame, then the
 * menu's turn, which reads of the registers A's high byte and the carry. */

// $CC:F05B (in the battle's setup, $CC:ED87)
/* Wait for the battle's graphics to have loaded: the vblank handler loads
 * them a step a frame (battle_load_graphics). Once they have, the setup's
 * graphics return to the setup ($CC:F062). */
enum { GRAPHICS_LOADED = 4 };
