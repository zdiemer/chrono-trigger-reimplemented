/* Battle: waiting for the display.
 *
 * The battle's code waits, a pass of a loop at a time, for things the vblank
 * and line handlers do (battle/nmi.c): for the next frame's update to have run,
 * for the vblank handler to have finished a frame, for the window's slide to
 * have finished, and at the start for the battle's graphics to have loaded. A
 * screen effect waits for a line of the picture, and the line handler for the
 * horizontal blank before it writes. Each is an idle entry: one pass, which
 * stays at the loop's head while it waits and goes on at its exit once the
 * wait is over; what the pass leaves in A and the flags (the value it tested)
 * is what the original's code after the loop finds. */
#include "battle/bridge.h"
#include "battle/entries.h"
#include "platform/timing.h"
#include "vars.h"

/* One pass of a wait on a byte the interrupts clear: on to `done` (in the
 * loop's bank) once it is 0, the byte tested left in A. */
static void wait_for_zero(Snes *s, uint8_t byte, uint16_t done)
{
    m_set_a8(s, byte);
    m_set_nz8(s, byte);
    if (byte == 0)
        s->cpu.pc = done;
}

// $C1:0080 (the loop in $C1:007E)
/* Wait for the next frame: the frame's update clears its pending flag. Each
 * pass also lets the sound's pause run ($CD:3E44, which does nothing unless
 * the battle is paused: the guard). */
void battle_wait_for_frame_loop(Snes *s) { wait_for_zero(s, battle_frame_pending(state_of(s)), 0x0088); }

// $CD:3E81 (the loop in $CD:3E7D)
/* Wait for the vblank handler to have finished its frame. */
void battle_wait_for_nmi_loop(Snes *s) { wait_for_zero(s, battle_nmi_pending(state_of(s)), 0x3E89); }

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

/* ---- Waiting for the beam ---------------------------------------------------------------- */

/* A pass of a wait on the display's status: on to `done` once `ready`, the
 * status (as the loop masks it) in A. */
static void wait_on_status(Snes *s, uint8_t status, bool ready, uint16_t done)
{
    m_set_a8(s, status);
    m_set_nz8(s, status);
    if (ready)
        s->cpu.pc = done;
}

// $CD:3B84 (the first loop of the wait for a line at $CD:3B82)
/* Wait for the vertical blank to end. */
void battle_wait_vblank_end_loop(Snes *s)
{
    uint8_t status = tm_snes_video_status(m_platform(s), PF_AT(0xCD3B84));
    wait_on_status(s, status, !(status & TM_SNES_IN_VBLANK), 0x3B8A);
}

// $CD:3B8A
/* Then for a horizontal blank. */
void battle_wait_hblank_loop(Snes *s)
{
    uint8_t hblank = tm_snes_video_status(m_platform(s), PF_AT(0xCD3B8A)) & TM_SNES_IN_HBLANK;
    wait_on_status(s, hblank, hblank != 0, 0x3B92);
}

// $D1:FEB8 (in the line handler)
/* The line handler waits for the horizontal blank before it writes. */
void battle_irq_wait_hblank_loop(Snes *s)
{
    uint8_t hblank = tm_snes_video_status(m_platform(s), PF_AT(0xD1FEB8)) & TM_SNES_IN_HBLANK;
    wait_on_status(s, hblank, hblank != 0, 0xFEBF);
}

// $CD:3B92
/* Then, past the horizontal blank, for the beam to reach the line the effect
 * waits for (the first of the battle's scratch cells), whereupon the routine
 * returns. */
void battle_wait_line_loop(Snes *s)
{
    Platform *pf = m_platform(s);
    uint8_t hblank = tm_snes_video_status(pf, PF_AT(0xCD3B92)) & TM_SNES_IN_HBLANK;
    if (hblank) {
        wait_on_status(s, hblank, false, 0);
        return;
    }
    uint8_t line = tm_snes_beam_line_low(pf, PF_AT(0xCD3B99));
    uint8_t target = battle_scratch(state_of(s), 0);
    m_set_a8(s, line);
    m_cmp8(s, line, target);
    if (line < target)
        return;
    s->cpu.pc = 0x3BAA;
    m_rts(s);
}

/* The frame waits' passes do nothing else while the battle isn't paused
 * (the sound's pause is the original's then). */
bool battle_frame_wait_is_idle(Snes *s)
{
    vars_peek(true);
    bool paused = battle_paused(state_of(s)) != 0;
    vars_peek(false);
    return battle_on_page(s) && !paused;
}
