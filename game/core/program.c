/* The program's top: the boot, the main thread, and the frame's two
 * interrupts sent to the running mode's handlers.
 *
 * The boot puts every register of the console and of the picture where the
 * game wants it at the start; the main thread then hands over to the field
 * engine's first start, which shows the title. Each mode installs its own
 * vblank and line handlers by writing the stubs in work RAM the interrupts
 * go through -- a long jump to its handler, or a return while it wants none
 * -- and the frame's interrupts go wherever the stubs say.
 *
 * On the machine, core/console.c's entries are this file's callers: the CPU
 * reaches the boot through the reset and the stubs through the cartridge's
 * vectors, and goes on from them by address. With no machine, the driver
 * calls the main thread and the handlers itself, and where the program's next
 * step is still only a machine entry, the program goes no further
 * (program_needs_the_machine): the field's first start, and every mode's
 * handlers, which are still entries that work on the machine's registers and
 * stack (docs/TODO.md, "The native program"). */
#include "core/program.h"

#include "battle/program.h"
#include "cpu_io.h"
#include "field/field.h"
#include "field/main_loop.h"
#include "menu/display.h"
#include "scene/scene.h"
#include "original_effects.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "vars.h"

/* ---- The boot -------------------------------------------------------------------------------- */

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

// $FD:C016 (the boot's work, after it sets the stack and the registers' widths)
void program_boot(GameState *g, Platform *pf)
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

/* ---- The interrupts' stubs ------------------------------------------------------------------- */

/* A stub is a long jump to the mode's handler, or a return where the mode has
 * turned the interrupt away. */
enum { STUB_LONG_JUMP = 0x5C, STUB_RETURN = 0x40, STUB_KIND_BITS = 0xFF };

static uint32_t stub_jumping_to(uint32_t handler) { return STUB_LONG_JUMP | handler << 8; }

static ProgramStub stub_of(uint32_t stub)
{
    switch (stub & STUB_KIND_BITS) {
    case STUB_LONG_JUMP: return (ProgramStub){ PROGRAM_STUB_JUMPS, stub >> 8 };
    case STUB_RETURN: return (ProgramStub){ PROGRAM_STUB_RETURNS, 0 };
    default: return (ProgramStub){ PROGRAM_STUB_UNSET, 0 };
    }
}

void program_install_vblank_handler(GameState *g, uint32_t handler) { set_nmi_vector(g, stub_jumping_to(handler)); }
void program_install_line_handler(GameState *g, uint32_t handler) { set_irq_vector(g, stub_jumping_to(handler)); }

ProgramStub program_vblank_stub(const GameState *g) { return stub_of(nmi_vector(g)); }
ProgramStub program_line_stub(const GameState *g) { return stub_of(irq_vector(g)); }

/* ---- The modes' handlers --------------------------------------------------------------------- */

/* The handlers the modes install, by the address their stubs name. A row's
 * `run` is the handler as a function of the game's state, which the driver
 * calls; none has one yet: each is still an entry that works on the machine's
 * registers and stack (it saves the interrupted code's, moves the data bank
 * and the page, calls out), and a row gets its function as its handler comes
 * off the machine. */
typedef void (*ProgramHandler)(GameState *g, Platform *pf);
static const struct {
    uint32_t handler;
    const char *name;
    ProgramHandler run;
} handlers[] = {
    { FIELD_VBLANK_HANDLER, "the field's vblank handler", field_vblank },
    { FIELD_LINE_HANDLER, "the field's line handler", field_line },
    { MENU_VBLANK_HANDLER, "the menus' vblank handler", menu_vblank },
    { MENU_LINE_HANDLER, "the menus' line handler", menu_line },
    { MENU_OBJECT_VBLANK_HANDLER, "the sprite-object mode's vblank handler", menu_object_vblank },
    { MENU_OBJECT_LINE_HANDLER, "the sprite-object mode's line handler", menu_object_line },
    { BATTLE_VBLANK_HANDLER, "the battle's vblank handler", battle_vblank },
    { BATTLE_LINE_HANDLER, "the battle's line handler", battle_line },
    { BATTLE_CLOSING_VBLANK_HANDLER, "the battle's closing vblank handler", battle_closing_vblank },
    { SCENE_BOOT_VBLANK, "a scene's boot's vblank handler", scene_boot_vblank },
    { SCENE_BOOT_LINE, "a scene's boot's line handler", scene_boot_line },
    { SCENE_FRAME_HANDLER, "a scene program's vblank handler", scene_vblank },
    { SCENE_HANDLER_WHILE_WORKING, "a scene program's vblank handler while it works", scene_vblank_while_working },
    { 0xFDDE15, "the warp's vblank handler", 0 },
    { 0xFDDE87, "the warp's line handler", 0 },
};
enum { HANDLERS = sizeof handlers / sizeof handlers[0] };

static void take(GameState *g, Platform *pf, ProgramStub stub, const char *unknown)
{
    if (stub.kind != PROGRAM_STUB_JUMPS)
        return;   /* turned away, or no mode has set the stub: the interrupt returns at once */
    for (unsigned i = 0; i < HANDLERS; i++) {
        if (handlers[i].handler != stub.handler)
            continue;
        if (handlers[i].run)
            handlers[i].run(g, pf);
        else
            program_needs_the_machine(g, stub.handler, handlers[i].name);
        return;
    }
    program_needs_the_machine(g, stub.handler, unknown);
}

void program_vblank(GameState *g, Platform *pf) { take(g, pf, program_vblank_stub(g), "a vblank handler"); }
void program_line(GameState *g, Platform *pf) { take(g, pf, program_line_stub(g), "a line handler"); }

/* ---- The main thread ------------------------------------------------------------------------- */

enum { FIELD_ENTER = 0xC0005D };

/* Where the program's next step is still a machine entry, it waits there,
 * frame after frame, for nothing: the build says where it stopped. */
static void go_no_further(GameState *g, Platform *pf, uint32_t routine, const char *what)
{
    program_needs_the_machine(g, routine, what);
    for (;;)
        tm_wait_frame(pf, PF_AT(routine));
}

/* After power-on the game goes between the field and the scenes: the field
 * runs until it ends into a scene (an event's), the scene until its script
 * ends it, and the field starts again. */
void program_main_thread(GameState *g, Platform *pf)
{
    program_boot(g, pf);
    field_power_on(g, pf);
    for (;;) {
        field_runs(g, pf);
        uint8_t scene;
        if (program_went_no_further(g) || !program_next_scene(g, &scene) || !scene_runs(g, pf, scene))
            break;
    }
    go_no_further(g, pf, FIELD_ENTER, "the field's entry");
}
