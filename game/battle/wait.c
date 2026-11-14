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
 * Two other waits are loops where they are made: for the window's slide to
 * have finished (battle/closing.c), and for the battle's graphics to have
 * loaded at its start (battle/opening.c). (The line handler's wait for its
 * line to be drawn is the timing service's, battle/nmi.c.) */
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/message.h"
#include "battle/sound.h"
#include "battle/program.h"
#include "battle/wait.h"
#include "charge.h"
#include "platform/timing.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The waits ------------------------------------------------------------------------------ */

/* The frame work's waits, each timed as its routine: for its vblank
 * handler's frame ($CD:3E7D), for the next frame, with the menu's turn after
 * ($CD:3E75), and for a number of frames ($CD:3E3B). */
enum { HANDLER_WAIT = 0xCD3E7D, NEXT_FRAME_WAIT = 0xCD3E75, SOME_FRAMES_WAIT = 0xCD3E3B };

// $CD:3E58
/* A frame of a paused fight sat out: the vblank handler has finished a frame
 * once it clears what the battle counted up to ask for it (battle_nmi). */
void battle_sit_out_a_paused_frame(GameState *g, Platform *pf)
{
    set_battle_nmi_pending(g, (uint8_t)(battle_nmi_pending(g) + 1));
    while (battle_nmi_pending(g))
        tm_wait_for_handlers(pf, PF_LOOP(0xCD3E5B, 0xCD3E60));
}

/* The pause's turn (battle/sound.c), which asks nothing of the game's state
 * that the pause doesn't keep. */
static void sit_out_a_pause_frame(GameState *g, Platform *pf)
{
    TIMED_AS(g, BATTLE_PAUSE_ENTRY, battle_sit_out_the_pause(g, pf));
}

// $C1:007E
/* The battle's wait for the next frame: the frame's update asked for, and
 * waited for. A paused battle sits the pause out on a pass of it, and its
 * update, held while paused, comes after. */
static void wait_for_frame(GameState *g, Platform *pf)
{
    set_battle_frame_pending(g, (uint8_t)(battle_frame_pending(g) + 1));
    for (;;) {
        if (battle_paused(g))
            sit_out_a_pause_frame(g, pf);
        if (!battle_frame_pending(g))
            return;
        tm_wait_for_handlers(pf, PF_LOOP(0xC10080, 0xC10088));
    }
}

void battle_wait_for_frame(GameState *g, Platform *pf) { TIMED_AS(g, 0xC1007E, wait_for_frame(g, pf)); }

// $CF:FCFB
/* The same as the battle hands back to the field, when there is no pause. */
void battle_wait_for_a_last_frame(GameState *g, Platform *pf)
{
    set_battle_frame_pending(g, (uint8_t)(battle_frame_pending(g) + 1));
    while (battle_frame_pending(g))
        tm_wait_for_handlers(pf, PF_LOOP(0xCFFCFD, 0xCFFD01));
}

/* The message's next letter (battle/message.c), refused unless the menus'
 * text code can go on from a message being set up. */
static bool type_a_letter(GameState *g)
{
    if (!PORT_CHECK(battle_message_typing_fits(g)))
        return battle_contract_broken(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, "a message the menus' text code doesn't go on from");
    TIMED_AS(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, battle_type_the_message(g));
    return true;
}

// $CD:3E7D
/* The frame work's wait: the vblank handler's frame waited for, the pause
 * given its turn on each pass, then the next letter of a battle message
 * typed. */
static bool wait_for_the_handler(GameState *g, Platform *pf)
{
    set_battle_nmi_pending(g, (uint8_t)(battle_nmi_pending(g) + 1));
    for (;;) {
        if (battle_paused(g))
            sit_out_a_pause_frame(g, pf);
        if (!battle_nmi_pending(g))
            break;
        tm_wait_for_handlers(pf, PF_LOOP(0xCD3E81, 0xCD3E89));
    }
    return type_a_letter(g);
}

bool battle_wait_for_the_handler(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, HANDLER_WAIT, done = wait_for_the_handler(g, pf));
    return done;
}

// $CD:3E75
/* The frame work's wait for the next frame: the handler's frame, then the
 * menu's turn (battle/frame_step.c). */
static bool wait_for_the_next_frame(GameState *g, Platform *pf)
{
    if (!battle_wait_for_the_handler(g, pf))
        return false;
    bool done = false;
    TIMED_AS(g, BATTLE_MENU_TURN_ENTRY, done = battle_menu_turn(g, pf));
    return done;
}

bool battle_wait_for_the_next_frame(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, NEXT_FRAME_WAIT, done = wait_for_the_next_frame(g, pf));
    return done;
}

// $CD:3E3B
/* A number of those (0 is 256). */
static bool wait_some_frames(GameState *g, Platform *pf, uint8_t frames)
{
    uint8_t left = frames;
    do {
        if (!battle_wait_for_the_next_frame(g, pf))
            return false;
    } while (--left);
    return true;
}

bool battle_wait_some_frames(GameState *g, Platform *pf, uint8_t frames)
{
    bool done = false;
    TIMED_AS(g, SOME_FRAMES_WAIT, done = wait_some_frames(g, pf, frames));
    return done;
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
