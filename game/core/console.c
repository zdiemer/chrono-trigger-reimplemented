/* The console: how the machine reaches the program (core/program.c).
 *
 * The console powers on, and resets, in the CPU's 6502 emulation mode at the
 * reset stub, which switches to the 65816's own mode and goes to the boot.
 * The boot sets the game's stack and the registers' widths, starts the
 * program (program_boot) and hands over to the field engine's first start,
 * which shows the title. The game starts itself again the same way: the
 * scenes jump to the reset stub, the battle's demo ends at its copy in bank
 * $C0, and the field goes back to the boot.
 *
 * The cartridge's vectors send the vblank and the line interrupt through two
 * stubs in work RAM, one each, which the running mode points at its own
 * handlers (program_install_*), or makes a bare return while it wants none.
 *
 * These are the program's entries as the CPU reaches them, by address; a build
 * with no machine calls the program's functions instead and has none of them. */
#include "core/console.h"

#include "core/program.h"
#include "port/machine.h"
#include "port/vars_ram.h"

enum {
    BOOT = 0xFDC000,
    FIELD_FIRST_START = 0xC0000E,   /* the field engine's start at power-on: the title, then the game */
    VBLANK_STUB = 0x000500,
    LINE_STUB = 0x000504,
};

// $00:FF00 (and its copy at $C0:FF00)
/* The reset: the CPU leaves the emulation mode it powers on in, with its
 * interrupts masked, and goes to the boot. The game resets itself through
 * here already in native mode, where the switch changes nothing. What the
 * switch leaves in the carry the boot doesn't read. */
void console_reset(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);
    s->cpu.e = false;
    m_stop_at(s, BOOT, DC_C);
}

// $FD:C000
/* The boot: the game's stack, 8-bit A and 16-bit index registers, the data
 * bank $00, and the program started. What it leaves in the registers besides,
 * the field engine's start sets again. */
void console_boot(Snes *s)
{
    s->cpu.s = CONSOLE_STACK_TOP;
    m_rep(s, CPU_FLAG_X);
    m_sep(s, CPU_FLAG_M);
    s->cpu.db = 0x00;
    program_boot(state_of(s), m_platform(s));
    m_stop_at(s, FIELD_FIRST_START, DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C | DC_D);
}

// $00:FF10
/* The vblank's vector: on through its stub. */
void console_vblank_vector(Snes *s) { m_stop_at(s, VBLANK_STUB, 0); }

// $00:FF14
/* The line interrupt's vector: on through its stub. */
void console_line_vector(Snes *s) { m_stop_at(s, LINE_STUB, 0); }

/* An interrupt goes to the handler its stub names, or back to what it
 * interrupted where the mode has turned it away. */
static void take_interrupt(Snes *s, ProgramStub stub)
{
    if (stub.kind == PROGRAM_STUB_RETURNS)
        m_rti(s);
    else
        m_jump_to(s, stub.handler);
}

// $00:0500
void console_vblank_stub(Snes *s) { take_interrupt(s, program_vblank_stub(state_of(s))); }

// $00:0504
void console_line_stub(Snes *s) { take_interrupt(s, program_line_stub(state_of(s))); }

/* The guards: the stub is one a mode set, a jump or a return. */
bool console_vblank_stub_is_set(Snes *s)
{
    vars_peek(true);
    bool set = program_vblank_stub(state_of(s)).kind != PROGRAM_STUB_UNSET;
    vars_peek(false);
    return set;
}

bool console_line_stub_is_set(Snes *s)
{
    vars_peek(true);
    bool set = program_line_stub(state_of(s)).kind != PROGRAM_STUB_UNSET;
    vars_peek(false);
    return set;
}
