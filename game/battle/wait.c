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
} WaitAsC;

/* The pause's guard and the menu turn's own guard ask nothing of the game's
 * state that the pause doesn't keep; the message's typing, that the menus'
 * text code can go on from a message being set up. */
static bool pause_as_c(void *ctx)
{
    WaitAsC *c = ctx;
    TIMED_AS(c->game, BATTLE_PAUSE_ENTRY, battle_sit_out_the_pause(c->game, c->pf));
    return true;
}

static bool type_the_message_as_c(void *ctx)
{
    GameState *g = ((WaitAsC *)ctx)->game;
    if (!battle_message_typing_fits(g))
        return battle_needs_the_machine(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, "a message the menus' text code doesn't go on from");
    TIMED_AS(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, battle_type_the_message(g));
    return true;
}

static bool handler_as_c(void *ctx)
{
    WaitAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_HANDLER_WAIT, done = wait_for_the_handler(c->game, c->pf, pause_as_c, type_the_message_as_c, c));
    return done;
}

static bool menu_turn_as_c(void *ctx)
{
    WaitAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_MENU_TURN_ENTRY, done = battle_menu_turn(c->game, c->pf));
    return done;
}

static bool next_frame_as_c(void *ctx, uint8_t left)
{
    (void)left;
    WaitAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_NEXT_FRAME_WAIT, done = wait_for_the_next_frame(handler_as_c, menu_turn_as_c, c));
    return done;
}

bool battle_wait_for_frame(GameState *g, Platform *pf)
{
    WaitAsC c = { g, pf };
    return wait_for_the_frame(g, pf, pause_as_c, &c);
}

bool battle_wait_for_the_handler(GameState *g, Platform *pf)
{
    WaitAsC c = { g, pf };
    return wait_for_the_handler(g, pf, pause_as_c, type_the_message_as_c, &c);
}

bool battle_wait_for_the_next_frame(GameState *g, Platform *pf)
{
    WaitAsC c = { g, pf };
    return wait_for_the_next_frame(handler_as_c, menu_turn_as_c, &c);
}

bool battle_wait_some_frames(GameState *g, Platform *pf, uint8_t frames)
{
    WaitAsC c = { g, pf };
    return wait_some_frames(frames, next_frame_as_c, &c);
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* The pause's turn, from the call at `site` (a JSR, or the JSL of the long
 * form at $CD:0036). */
static bool pause_from(Snes *s, uint32_t site, bool long_call)
{
    return long_call ? battle_run_long(s, site, 0xCD0036, battle_sit_out_the_pause_long)
                     : battle_run(s, site, BATTLE_PAUSE_ENTRY, battle_sit_out_the_pause_entry);
}

static bool frame_pause_on_the_machine(void *ctx) { return pause_from(ctx, 0xC10080, true); }
static bool handler_pause_on_the_machine(void *ctx) { return pause_from(ctx, 0xCD3E81, false); }

static bool type_the_message_on_the_machine(void *ctx)
{
    return battle_run(ctx, 0xCD3E89, BATTLE_TYPE_THE_MESSAGE_ENTRY, battle_type_the_message_entry);
}

bool battle_wait_for_the_handler_from(Snes *s, uint32_t site)
{
    return battle_wait_frame(s, site, BATTLE_HANDLER_WAIT, BATTLE_WAIT_READS);
}

bool battle_wait_for_the_next_frame_from(Snes *s, uint32_t site)
{
    return battle_wait_frame(s, site, BATTLE_NEXT_FRAME_WAIT, BATTLE_WAIT_READS);
}

/* ---- The entries ---------------------------------------------------------------------------- */

/* The waits leave A's low byte as the test that ended them left it, 0, which
 * their callers don't read; they are declared so (registry.c). */

void battle_wait_for_frame_entry(Snes *s)
{
    if (!wait_for_the_frame(state_of(s), m_platform(s), frame_pause_on_the_machine, s))
        return;
    m_rts(s);
}

void battle_wait_for_a_last_frame_entry(Snes *s)
{
    battle_wait_for_a_last_frame(state_of(s), m_platform(s));
    m_rts(s);
}

/* The original keeps A on the stack while it waits, and hands it back as a
 * pull does. */
void battle_wait_for_the_handler_entry(Snes *s)
{
    m_push8(s, (uint8_t)s->cpu.a);
    if (!wait_for_the_handler(state_of(s), m_platform(s), handler_pause_on_the_machine, type_the_message_on_the_machine, s))
        return;
    uint8_t kept = m_pull8(s);
    m_set_a8(s, kept);
    m_set_nz8(s, kept);
    m_rts(s);
}

/* The frame work's wait for the next frame: the handler's frame, then the
 * menu's turn, which reads of the registers A's high byte and the carry. */
enum { MENU_TURN_READS_A_HIGH_AND_CARRY = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z };

static bool handler_on_the_machine(void *ctx)
{
    return battle_run(ctx, 0xCD3E75, BATTLE_HANDLER_WAIT, battle_wait_for_the_handler_entry);
}

static bool menu_turn_on_the_machine(void *ctx)
{
    return battle_run_long_dc(ctx, 0xCD3E78, BATTLE_MENU_TURN_ENTRY, MENU_TURN_READS_A_HIGH_AND_CARRY,
                              battle_menu_turn_long);
}

void battle_wait_for_the_next_frame_entry(Snes *s)
{
    if (!wait_for_the_next_frame(handler_on_the_machine, menu_turn_on_the_machine, s))
        return;
    m_rts(s);
}

/* The count goes down in A and on the stack, where the original keeps it. */
static bool next_frame_on_the_machine(void *ctx, uint8_t left)
{
    Snes *s = ctx;
    m_set_a8(s, left);
    m_push8(s, left);
    if (!battle_run(s, 0xCD3E3C, BATTLE_NEXT_FRAME_WAIT, battle_wait_for_the_next_frame_entry))
        return false;
    (void)m_pull8(s);
    return true;
}

void battle_wait_some_frames_entry(Snes *s)
{
    if (!wait_some_frames((uint8_t)s->cpu.a, next_frame_on_the_machine, s))
        return;
    m_set_a8(s, 0);
    m_rts(s);
}

void battle_wait_for_the_small_handler_entry(Snes *s)
{
    battle_wait_for_the_small_handler(state_of(s), m_platform(s));
    m_rts(s);
}

/* ---- The waits still made on the machine ---------------------------------------------------- */

/* One pass of a wait on a byte the interrupts clear: on to `done` (in the
 * loop's bank) once it is 0, the byte tested left in A. */
static void wait_for_zero(Snes *s, uint8_t byte, uint16_t done)
{
    m_set_a8(s, byte);
    m_set_nz8(s, byte);
    if (byte == 0)
        s->cpu.pc = done;
}

// $C1:003C
/* Wait for the window's slide to have finished (through $CD:0012, which
 * answers 1 while it slides). */
void battle_wait_window_slide_loop(Snes *s)
{
    bool sliding = battle_window_slide_frames(state_of(s)) != 0;
    wait_for_zero(s, sliding, 0x0042);
}

// $CC:F05B (in the battle's setup, $CC:ED87)
/* Wait for the battle's graphics to have loaded: the vblank handler loads
 * them a step a frame (battle_load_graphics). Once they have, the setup's
 * graphics return to the setup ($CC:F062). */
enum { GRAPHICS_LOADED = 4 };

void battle_wait_graphics_loaded_loop(Snes *s)
{
    uint8_t step = battle_graphics_load_step(state_of(s));
    m_set_a8(s, step);
    m_cmp8(s, step, GRAPHICS_LOADED);
    if (step == GRAPHICS_LOADED)
        m_rtl(s);
}
