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
#include "battle/message.h"
#include "battle/sound.h"
#include "battle/wait.h"
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

/* The pause's turn, on every pass of a wait: only a paused battle has
 * anything to do in it (battle/sound.c), and it does it from the call at
 * `site` (a JSR, or the JSL of the long form at $CD:0036). */
static bool give_the_pause_its_turn(Snes *s, uint32_t site, bool long_call)
{
    if (!battle_paused(state_of(s)))
        return true;
    return long_call ? battle_run_long(s, site, 0xCD0036, battle_sit_out_the_pause_long)
                     : battle_run(s, site, 0xCD3E44, battle_sit_out_the_pause);
}

// $C1:007E
/* The battle's wait for the next frame: the frame's update asked for, and
 * waited for. A paused battle sits the pause out on a pass of it, and its
 * update, held while paused, comes after. */
static bool wait_for_the_frame(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_frame_pending(g, (uint8_t)(battle_frame_pending(g) + 1));
    for (;;) {
        if (!give_the_pause_its_turn(s, 0xC10080, true))
            return false;
        if (!battle_frame_pending(g))
            return true;
        tm_wait_for_handlers(m_platform(s), PF_LOOP(0xC10080, 0xC10088));
    }
}

// $CF:FCFB
/* The same as the battle hands back to the field, when there is no pause. */
static void wait_for_a_last_frame(GameState *g, Platform *pf)
{
    set_battle_frame_pending(g, (uint8_t)(battle_frame_pending(g) + 1));
    while (battle_frame_pending(g))
        tm_wait_for_handlers(pf, PF_LOOP(0xCFFCFD, 0xCFFD01));
}

// $CD:3E7D
/* The frame work's wait: the vblank handler's frame waited for, the pause
 * given its turn on each pass, then the next letter of a battle message
 * typed. */
static bool wait_for_the_handler(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_nmi_pending(g, (uint8_t)(battle_nmi_pending(g) + 1));
    for (;;) {
        if (!give_the_pause_its_turn(s, 0xCD3E81, false))
            return false;
        if (!battle_nmi_pending(g))
            break;
        tm_wait_for_handlers(m_platform(s), PF_LOOP(0xCD3E81, 0xCD3E89));
    }
    return battle_run(s, 0xCD3E89, 0xCD04DF, battle_type_the_message_entry);
}

// $CD:044A
/* The battle's start and end run a small vblank handler of their own
 * (battle_nmi_alternate), which clears what this sets once it has finished a
 * frame. */
static void wait_for_the_small_handler(GameState *g, Platform *pf)
{
    set_battle_alternate_frame_pending(g, 1);
    while (battle_alternate_frame_pending(g))
        tm_wait_for_handlers(pf, PF_LOOP(0xCD044E, 0xCD0452));
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

void battle_wait_for_frame(Snes *s)
{
    if (!wait_for_the_frame(s))
        return;
    m_rts(s);
}

void battle_wait_for_a_last_frame(Snes *s)
{
    wait_for_a_last_frame(state_of(s), m_platform(s));
    m_rts(s);
}

/* The original keeps A on the stack while it waits, and hands it back as a
 * pull does. */
void battle_wait_for_the_handler(Snes *s)
{
    m_push8(s, (uint8_t)s->cpu.a);
    if (!wait_for_the_handler(s))
        return;
    uint8_t kept = m_pull8(s);
    m_set_a8(s, kept);
    m_set_nz8(s, kept);
    m_rts(s);
}

// $CD:3E75
/* The frame work's wait for the next frame: the handler's frame, then the
 * menu's turn (battle/frame_step.c), which reads of the registers A's high
 * byte and the carry. */
enum { MENU_TURN_READS_A_HIGH_AND_CARRY = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z };

void battle_wait_for_the_next_frame(Snes *s)
{
    if (!battle_run(s, 0xCD3E75, 0xCD3E7D, battle_wait_for_the_handler))
        return;
    if (!battle_run_long_dc(s, 0xCD3E78, 0xC10012, MENU_TURN_READS_A_HIGH_AND_CARRY, battle_menu_turn_long))
        return;
    m_rts(s);
}

// $CD:3E3B
/* A number of those, A's low byte (0 is 256); the count goes down in A and
 * on the stack, where the original keeps it. */
void battle_wait_some_frames(Snes *s)
{
    uint8_t left = (uint8_t)s->cpu.a;
    do {
        m_set_a8(s, left);
        m_push8(s, left);
        if (!battle_run(s, 0xCD3E3C, 0xCD3E75, battle_wait_for_the_next_frame))
            return;
        left = (uint8_t)(m_pull8(s) - 1);
    } while (left);
    m_set_a8(s, left);
    m_rts(s);
}

void battle_wait_for_the_small_handler(Snes *s)
{
    wait_for_the_small_handler(state_of(s), m_platform(s));
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
 * them a step a frame (battle_load_graphics). */
enum { GRAPHICS_LOADED = 4 };

void battle_wait_graphics_loaded_loop(Snes *s)
{
    uint8_t step = battle_graphics_load_step(state_of(s));
    m_set_a8(s, step);
    m_cmp8(s, step, GRAPHICS_LOADED);
    if (step == GRAPHICS_LOADED)
        s->cpu.pc = 0xF062;
}
