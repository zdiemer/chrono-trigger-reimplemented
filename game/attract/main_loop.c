/* Code the game decompresses to $7E:3000 and runs from RAM during the attract
 * sequence and title screen: the scene program, whose ports are in game/scene/
 * (scene/scene.h says where; these belong with them). Every entry here has a guard that checks, by
 * hash, that RAM holds the code the port was written for: the same addresses
 * hold other code and data at other times. */
#include "attract/attract.h"

#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The scene program's main loop, at $7E:3023: each pass calls the program's
 * frame routine and then looks whether a sequence script has ended the scene
 * (scene_program_over; scene/script.c), and goes on to the program's restart
 * ($7E:302A) once one has. The routine is named by the call's own operand
 * ($7E:3024), which a scene's setup writes: the title's is an empty one (an
 * RTS, $7E:3176), the attract demo's race sets the race's scaling of its
 * pictures there (scene_race_scale_pictures, $7E:51D1), and the endings set
 * their own scaling ($7E:8930, the original's still).
 *
 * A pass with the race's scaling is a port that calls it through the machine
 * (the scaling stops in the scaler's code, which takes frames) and makes the
 * test. Any other pass is an idle entry, which can't call: it makes the empty
 * routine's pass whole, and with frame work it stops at the routine and the
 * loop's test after it is the original's. */
enum { WAIT_LOOP = 0x7E3023, FRAME_ROUTINE_OPERAND = 0x7E3024, PROGRAM_RESTART = 0x7E302A };
enum { CODE_BANK = 0x7E0000, JSR = 0x20, RTS = 0x60, RETURN_FROM_THE_CALL = 0x3025 };
enum { RACE_SCALING = 0x51D1 };

static uint16_t frame_routine(Snes *s)
{
    return (uint16_t)(snes_bus_peek(&s->bus, FRAME_ROUTINE_OPERAND) | snes_bus_peek(&s->bus, FRAME_ROUTINE_OPERAND + 1) << 8);
}

/* The call, then the loop's test (its 4 bytes after the call's 3), whatever
 * routine the call names. */
static bool loop_present(Snes *s)
{
    return snes_bus_peek(&s->bus, WAIT_LOOP) == JSR && m_code_matches(s, WAIT_LOOP + 3, 4, 0x2B0A967642BA802Bull);
}

bool attract_wait_loop_present(Snes *s) { return loop_present(s) && frame_routine(s) != RACE_SCALING; }

/* The pass that scales reads the flag on the scene's page. */
bool attract_frame_pass_present(Snes *s)
{
    return loop_present(s) && frame_routine(s) == RACE_SCALING && m_direct_page(s) == 0;
}

/* What the loop leaves for the restart: the flag in A, as its test loads it. */
static void leave_flag(Snes *s, uint16_t over) { m_lda16(s, over); }

// $7E:3023
/* One pass of the wait. Idle entry, entered with 16-bit A; the call's return
 * address is left on the stack as the call (and the empty routine's RTS)
 * leave it. */
void attract_wait_loop(Snes *s)
{
    uint16_t routine = frame_routine(s);
    m_push16(s, RETURN_FROM_THE_CALL);
    if (snes_bus_peek(&s->bus, CODE_BANK | routine) != RTS) {
        s->cpu.pc = routine;
        return;
    }
    m_rts(s);
    uint16_t over = m_read16(s, m_dp(s, 0x00));
    leave_flag(s, over);
    s->cpu.pc = over ? (uint16_t)PROGRAM_RESTART : (uint16_t)WAIT_LOOP;
}

// $7E:3023
/* One pass with the race's scaling: the routine, then the test. */
void attract_frame_pass(Snes *s)
{
    m_jsr(s, WAIT_LOOP, CODE_BANK | RACE_SCALING);
    uint16_t over = scene_program_over(state_of(s));
    leave_flag(s, over);
    m_stop_at(s, over ? PROGRAM_RESTART : WAIT_LOOP, 0);
}

/* Guard for the vblank wait at $7E:31B0-$7E:31BE. */
bool attract_vblank_wait_present(Snes *s)
{
    return m_code_matches(s, 0x7E31B0, 15, 0x04AE9A24245E978Dull);
}

/* $7E:31B0: wait while the display is in vblank ($4212 bit 7 set). Idle entry,
 * 8-bit A. Continues to the second loop at $7E:31B7. */
void attract_vblank_end_loop(Snes *s)
{
    uint8_t status = m_lda8(s, m_read8(s, m_abs(s, 0x4212)));
    m_bit_imm8(s, 0x80);
    if (!(status & 0x80))
        s->cpu.pc = 0x31B7;
}

/* $7E:31B7: wait until vblank starts, then return. Idle entry, 8-bit A. */
void attract_vblank_start_loop(Snes *s)
{
    uint8_t status = m_lda8(s, m_read8(s, m_abs(s, 0x4212)));
    m_bit_imm8(s, 0x80);
    if (status & 0x80) {
        s->cpu.pc = 0x31BE;
        m_rts(s);
    }
}
