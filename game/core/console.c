/* The console: how the game starts, and how its interrupts reach each mode.
 *
 * The console powers on, and resets, in the CPU's 6502 emulation mode at the
 * reset stub, which switches to the 65816's own mode and goes to the boot.
 * The boot sets the game's stack, puts every register of the console and of
 * the picture where the game wants it at the start, and hands over to the
 * field engine's first start, which shows the title. The game starts itself
 * again the same way: the scenes jump to the reset stub, the battle's demo
 * ends at its copy in bank $C0, and the field goes back to the boot.
 *
 * The cartridge's vectors send the vblank and the line interrupt through two
 * stubs in work RAM, one each, which the running mode points at its own
 * handlers (console_install_*: the field's, the menus', the battle's and the
 * scenes'), or makes a bare return while it wants none. */
#include "core/console.h"

#include "core/display.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The interrupts' stubs ------------------------------------------------------------------- */

/* A stub is a long jump to the mode's handler, or a return where the mode has
 * turned the interrupt away. */
enum { STUB_LONG_JUMP = 0x5C, STUB_RETURN = 0x40, STUB_KIND_BITS = 0xFF };

static uint32_t stub_jumping_to(uint32_t handler) { return STUB_LONG_JUMP | handler << 8; }
static bool stub_jumps(uint32_t stub) { return (stub & STUB_KIND_BITS) == STUB_LONG_JUMP; }
static bool stub_returns(uint32_t stub) { return (stub & STUB_KIND_BITS) == STUB_RETURN; }
static uint32_t stub_handler(uint32_t stub) { return stub >> 8; }

void console_install_vblank_handler(GameState *g, uint32_t handler) { set_nmi_vector(g, stub_jumping_to(handler)); }
void console_install_line_handler(GameState *g, uint32_t handler) { set_irq_vector(g, stub_jumping_to(handler)); }

/* ---- The start --------------------------------------------------------------------------------- */

/* The picture at the start: forced blank; the sprites 16x16 and 32x32 with
 * their tiles at VRAM word $6000; mode 1 with BG3 in front; every tilemap,
 * character base and scroll at 0 and the mosaic off; the Mode 7 plane
 * unflipped with the identity matrix; no window but window 1 on BG3, drawing
 * outside it; no color math; all five layers on both screens; and a black
 * fixed color (its red left as it was). It matters out of proportion to its
 * size: a comparison from power-on starts from the state the calls build. */
static const VidScreen power_on_screen = {
    .display = &(const VidDisplay){ .blank = true },
    .sprites = { .size_select = 3, .base = 0x6000 },
    .mode = { .mode = 1, .bg3_priority = true },
    .mosaic_size = 1,
    .bg = { { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) },
            { VID_TILEMAP(0x0000, 32, 32) } },
    .mode7 = { .a = 0x0100, .d = 0x0100 },
    .windows = { .w1 = { [VID_BG3] = VID_WIN_OUT } },
    .main_layers = VID_ALL_LAYERS,
    .sub_layers = VID_ALL_LAYERS,
    /* VRAM written a byte at a time, the address stepping after the low one */
    .vram_port = { .step = 1, .after_high = false },
};

/* The controller port's programmable lines, all high: nothing latched. */
enum { IO_LINES_RELEASED = 0xFF };

/* The multiplier and the divider start from nothing, which only the
 * comparison with the original sees: nothing reads them before setting them. */
static void clear_arithmetic(GameState *g)
{
    hw_set_multiplicand(g, 0);
    (void)hw_multiply_by(g, 0);
    hw_set_dividend(g, 0);
    hw_divide_by(g, 0);
}

void console_starts(GameState *g, Platform *pf)
{
    tm_fast_rom(pf, PF_AT(0xFDC016), true);
    tm_enable_interrupts(pf, PF_AT(0xFDC01A), 0);
    vid_stop_uploads(pf, PF_AT(0xFDC01C));
    vid_enable_line_effects(pf, PF_AT(0xFDC01E), 0);
    ORIGINAL_EFFECT(clear_arithmetic(g));
    tm_reset_timers(pf, PF_AT(0xFDC02A));
    in_set_io_lines(pf, PF_AT(0xFDC034), IO_LINES_RELEASED);
    vid_apply_screen(pf, PF_AT(0xFDC036), &power_on_screen);
}

/* ---- The entries ------------------------------------------------------------------------------- */

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
 * bank $00, and the console as the game starts it. What it leaves in the
 * registers besides, the field engine's start sets again. */
void console_boot(Snes *s)
{
    s->cpu.s = CONSOLE_STACK_TOP;
    m_rep(s, CPU_FLAG_X);
    m_sep(s, CPU_FLAG_M);
    s->cpu.db = 0x00;
    console_starts(state_of(s), m_platform(s));
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
static void take_interrupt(Snes *s, uint32_t stub)
{
    if (stub_returns(stub))
        m_rti(s);
    else
        m_jump_to(s, stub_handler(stub));
}

// $00:0500
void console_vblank_stub(Snes *s) { take_interrupt(s, nmi_vector(state_of(s))); }

// $00:0504
void console_line_stub(Snes *s) { take_interrupt(s, irq_vector(state_of(s))); }

/* The guards: the stub is one a mode set, a jump or a return (before any
 * mode has, it holds whatever work RAM held at power-on). */
static bool stub_is_set(uint32_t stub) { return stub_jumps(stub) || stub_returns(stub); }

bool console_vblank_stub_is_set(Snes *s)
{
    vars_peek(true);
    bool set = stub_is_set(nmi_vector(state_of(s)));
    vars_peek(false);
    return set;
}

bool console_line_stub_is_set(Snes *s)
{
    vars_peek(true);
    bool set = stub_is_set(irq_vector(state_of(s)));
    vars_peek(false);
    return set;
}
