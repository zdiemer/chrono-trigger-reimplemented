/* Field mode: its start, and its frame.
 *
 * The field is the game's main mode: the party on a map, the scripts running
 * its objects. It starts in two ways:
 * - at power-on, once the console is set up (core/console.c): work RAM is
 *   cleared but for the interrupts' stubs and the stack, the sound driver
 *   started, and the menus told to look whether the cartridge holds a saved
 *   game; then the field is entered at location 0, the title's;
 * - entered, from power-on and from every mode that hands back to it (the
 *   menus, the scenes, a game over): the screen dark and the field's
 *   interrupt handlers installed. The locations from $1F0 are not maps but
 *   the menus' screens drawn with sprites (the Epoch's flights and the like),
 *   which the menus' own mode shows. A map's location has the field's state
 *   started (field/start.c), the location loaded (field/location.c), its
 *   objects and their sprites set up, and the screen faded in.
 *
 * Then the main loop runs, a pass a frame (field_frame): the presses that
 * arrived latched for the scripts; the pause and the menus; what the scripts
 * and the walk asked of the loop; the leader walked with the pad; the
 * confirm button; the dialog window opening or closing; the screen effect
 * and the scripts' layer work moved on; the frame's work -- the objects'
 * scripts and moves, the line effects and the palettes for the next frame,
 * the objects' sprites (field_frame_work); and the wait for the vblank to
 * take the frame. While the field waits on something of its own (a fade, a
 * game over's button), it runs a frame with only the leader's walk and the
 * effects' refresh (field_walk_a_frame).
 *
 * The field's own steps that are ports run as C; the rest are the original's
 * routines, called from the original's calls (field/bridge.h). */
#include "field/main_loop.h"
#include "field/dialog_window.h"
#include "field/field.h"
#include "field/line_effects.h"
#include "field/menus.h"
#include "field/party.h"
#include "field/requests.h"
#include "field/screen_effects.h"
#include "field/script_layers.h"
#include "field/objects_start.h"
#include "field/screen_setup.h"
#include "field/sprites.h"
#include "field/switches.h"
#include "field/start.h"
#include "field/walk.h"

#include "charge.h"
#include "original_effects.h"
#include "core/console.h"
#include "core/palette.h"
#include "core/display.h"
#include "core/sound_driver.h"
#include "core/input.h"
#include "event/contact.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/parts.h"
#include "menu/party.h"
#include "menu/party_leaves.h"
#include "menu/saves.h"
#include "platform/timing.h"
#include "port/native.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The game in the field ---------------------------------------------------------------------- */

/* Locations from here on are the menus' screens rather than maps. */
enum { FIRST_MENUS_SCREEN = 0x1F0 };
static bool location_is_the_menus(const GameState *g) { return location_id(g) >= FIRST_MENUS_SCREEN; }

/* Work RAM cleared at power-on: the first bank but the interrupts' stubs and
 * the stack ($0500-$06FF), and a stretch of the second ($7F:5080-$A0FF). */
static const struct {
    uint32_t from;
    uint16_t bytes;
} power_on_clears[] = {
    { 0x7E0000, 0x0500 },
    { 0x7E0700, 0xE900 },
    { 0x7F5080, 0x5080 },
};
enum { POWER_ON_CLEARS = sizeof power_on_clears / sizeof power_on_clears[0] };

/* The presses that arrived this frame, latched for the scripts to take one at
 * a time (event/input.c): the assignments' in the low byte, the game's order
 * in the high one. */
static void latch_presses(GameState *g)
{
    uint8_t assigned = (uint8_t)input_mapped(g, ASSIGNED_PRESSED);
    uint8_t game_order = (uint8_t)input_pressed(g);
    set_field_input_latched(g, (uint16_t)(field_input_latched(g) | game_order << 8 | assigned));
}

// $C0:EC60
/* The main loop has finished a frame: the vblank may take it, and one more
 * frame of the party standing about is counted (party_trail.c). */
static void finish_frame(GameState *g)
{
    party_stands_another_frame(g);
    set_field_frame_pending(g, (uint8_t)(field_frame_pending(g) + 1));
}

/* ---- The routines it calls on the machine ------------------------------------------------------- */

/* The original's routines; the palettes' animation, a port called through
 * the original's jump to it at $FD:FFF7, an entry of its own; and the sound driver's start,
 * a port that waits on the sound processor across frames, which only a call
 * can run: it hands every register back as it found it, A's low byte and the
 * flags aside. */
enum {
    SOUND_DRIVER_BOOT = 0xC70000,
    THE_BOOT_IGNORES = DC_AL | DC_N | DC_V | DC_Z | DC_C,
    MENUS_SERVICES = 0xC28004,       /* the menus' services (menu/party.h), whose time the power-on's is */
    MENUS_SCREENS = 0xC20000,        /* the menus' mode that shows a screen with sprites */
    PALETTE_ANIMATION = 0xFDFFF7,
};

/* The frame's two budgets, ports called on the machine: each reads where the
 * beam is (platform/timing.h, tm_frame_time_left) and calls out per object,
 * and shadow mode compares them a call at a time, with only a short wait for
 * the original's latch, where a C call would stretch the comparison across
 * the frame's work and leave more of it interrupted. A build with no machine
 * calls them as C. */
enum {
    SCRIPT_TURNS = 0xC05A46,         /* the queued objects' turns run, as the frame's time allows */
    SPRITE_FRAMES = 0xC0CA76,        /* the objects' new frames drawn, as the frame's time allows */
};

/* The objects' update, a port whose call-outs (each object that comes into
 * view or leaves it) are some of the original's calls from one site, which
 * shadow mode pairs with the original's only where the update is a call of
 * its own (docs/PORTING.md, "Call-outs"). */
enum { UPDATE_OBJECTS = 0xC0A810 };

/* The ports it runs as C, by the routines they stand for. */
enum {
    START = 0xC00B86,
    PAUSE_AND_MENUS = 0xC018D9,
    REQUESTS = 0xC00C76,
    WALK = 0xC0881E,
    CONFIRM = 0xC01AAC,
    SCREEN_EFFECT = 0xC021E1,
    SCRIPT_LAYERS = 0xC0274D,
    LINE_EFFECTS = 0xFDC2C1,
    SPRITE_TABLE = 0xC0B271,
    TURNS_DUE = 0xC059D9,
};

enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
    READS_A_HIGH_AND_THE_OVERFLOW = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C,
};

static BridgeMachine page(const Snes *s) { return on_the_field_page(s, false); }
static BridgeMachine page_x8(const Snes *s) { return on_the_field_page(s, true); }

/* The field's own handlers back on the interrupts. */
/* The game starts from what the cartridge holds (menu/saves.h): a saved
 * game to load, or a new one; then, as every one of the menus' services
 * ends, what the party wears is shown. */
static void start_from_the_saves(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC28D6A, (void)saves_power_on(g, pf));
    party_wear_it_shows(g);
}

static void take_the_interrupts(GameState *g)
{
    TIMED_AS(g, FIELD_INSTALL_NMI_ENTRY, field_take_the_vblank_interrupt(g));
    TIMED_AS(g, FIELD_INSTALL_IRQ_ENTRY, field_take_the_line_interrupt(g));
}

/* ---- The entries ------------------------------------------------------------------------------- */

/* Where the field's entry and its main loop start; the loop reads A's high
 * byte and the overflow as the last pass, or the field's entry, left them. */
enum { ENTER = 0xC0005D, MAIN_LOOP = 0xC00097, WAIT_FOR_THE_NEXT_FRAME = 0xC000BA };
enum { THE_LOOP_READS_A_HIGH_AND_THE_OVERFLOW = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* For shadow mode: the sound driver's start is called with the field's
 * page's high byte in A's, which the original sets D through. */
static void leave_the_page_in_a(Snes *s) { s->cpu.a = (uint16_t)((FIELD_PAGE & 0xFF00) | (s->cpu.a & 0x00FF)); }

/* The field's entry reads none of the registers the power-on leaves. */
enum { ENTERING_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* At power-on, before the sound driver starts: the field's handlers on the
 * interrupts, the screen dark, and work RAM cleared. */
static void wake(GameState *g, Platform *pf)
{
    take_the_interrupts(g);
    TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf));
    for (unsigned i = 0; i < POWER_ON_CLEARS; i++)
        TIMED_AS(g, FIELD_FILL_MEMORY_ENTRY, field_clear_memory(g, pf, power_on_clears[i].from, power_on_clears[i].bytes));
}

// $C0:000E
/* The field's first start, at power-on: the sound driver started, and the
 * cartridge's saves looked at, which starts a new game where there are none
 * (menu/saves.h). Here the driver is one a host plays (core/sound_driver.h),
 * and the field's entry follows. */
void field_power_on(GameState *g, Platform *pf)
{
    wake(g, pf);
    TIMED_AS(g, SOUND_DRIVER_BOOT, (void)sound_driver_host_boots(g, pf));
    TIMED_AS(g, MENUS_SERVICES, start_from_the_saves(g, pf));
}

/* On the machine the driver may be the original's, whose boot waits on the
 * sound processor across frames: a call the machine makes. The menus'
 * service keeps the X and Y it is given as its arguments, which this one
 * doesn't read: the original leaves the last clear's size in X. */
void field_first_start(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    s->cpu.d = FIELD_PAGE;
    display_leaves_the_machine_dark(s);
    wake(g, pf);
    s->cpu.x = power_on_clears[POWER_ON_CLEARS - 1].bytes;
    ORIGINAL_EFFECT(leave_the_page_in_a(s));
    if (!bridge_call_long(s, 0xC00053, SOUND_DRIVER_BOOT, THE_BOOT_IGNORES, page(s)))
        return;
    PartyServiceCall call = { 0 };
    ORIGINAL_EFFECT(call = party_service_called(g, (uint8_t)s->cpu.y, (uint8_t)s->cpu.x));
    NATIVE_TIME_AS(s, MENUS_SERVICES, start_from_the_saves(g, pf));
    ORIGINAL_EFFECT(party_service_answered(g, SERVICE_POWER_ON, &call));
    m_stop_at(s, ENTER, ENTERING_READS_NOTHING);
}

// $C0:005D (and $C0:0000, the field's start for the other modes)
/* The field entered. For the menus' screens, the menus' mode takes the
 * machine; what it reads of the registers is the data bank alone. */
void field_enter(Snes *s)
{
    GameState *g = state_of(s);
    display_go_dark(s);
    take_the_interrupts(g);
    if (location_is_the_menus(g)) {
        m_stop_at(s, MENUS_SCREENS, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_D);
        return;
    }
    s->cpu.d = FIELD_PAGE;
    if (!field_run(s, 0xC00085, START, READS_NOTHING, field_start, page(s)) ||
        !field_run(s, 0xC00088, FIELD_LOAD_LOCATION_ENTRY, READS_NOTHING, field_load_location, page(s)) ||
        !field_run(s, 0xC0008B, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, READS_NOTHING, field_build_the_sprites_afresh,
                   page(s)) ||
        !field_run(s, 0xC0008E, FIELD_SET_UP_THE_OBJECTS_ENTRY, READS_NOTHING, field_set_up_the_objects, page(s)) ||
        !field_run(s, 0xC00091, FIELD_FORGET_THE_STEPS_ENTRY, READS_NOTHING, field_forget_the_steps_entry, page(s)) ||
        !field_run(s, 0xC00094, FIELD_FADE_IN_ENTRY, READS_NOTHING, field_fade_in, page(s)))
        return;
    m_stop_at(s, MAIN_LOOP, THE_LOOP_READS_A_HIGH_AND_THE_OVERFLOW);
}

/* The guard: a location the game keeps, its number in the word's low bits
 * (the original reads the word as a signed number, and a negative one would
 * take neither of the field's ways in). */
enum { LOCATION_SIGN = 0x8000 };
bool field_location_is_a_number(Snes *s)
{
    vars_peek(true);
    bool ok = !(location_id(state_of(s)) & LOCATION_SIGN);
    vars_peek(false);
    return ok;
}

/* The main loop's pass: its steps in order, each called from its site.
 * The dialog window's step is reached by a long call. A's high byte and the
 * overflow pass from each step to the next as the steps leave them. */
typedef enum {
    PAUSE_AND_MENUS_STEP, REQUESTS_STEP, WALK_STEP, CONFIRM_STEP, DIALOG_WINDOW_STEP, SCREEN_EFFECT_STEP,
    SCRIPT_LAYERS_STEP, FRAME_WORK_STEP, PASS_STEPS,
} PassStep;

static const struct {
    uint32_t site, routine;
    uint16_t reads;
    void (*entry)(Snes *);
    bool long_call;
} the_pass[PASS_STEPS] = {
    [PAUSE_AND_MENUS_STEP] = { 0xC000A1, PAUSE_AND_MENUS, READS_A_HIGH_AND_THE_OVERFLOW, field_pause_and_menus, false },
    [REQUESTS_STEP] = { 0xC000A4, REQUESTS, READS_A_HIGH_AND_THE_OVERFLOW, field_handle_requests, false },
    [WALK_STEP] = { 0xC000A7, WALK, READS_NOTHING, field_walk_the_leader, false },
    [CONFIRM_STEP] = { 0xC000AA, CONFIRM, READS_THE_OVERFLOW, field_press_confirm, false },
    [DIALOG_WINDOW_STEP] = { 0xC000AD, FIELD_DIALOG_WINDOW_STEP_ENTRY, READS_THE_OVERFLOW, field_dialog_window_step_entry,
                             true },
    [SCREEN_EFFECT_STEP] = { 0xC000B1, SCREEN_EFFECT, READS_THE_OVERFLOW, field_move_screen_effect_on, false },
    [SCRIPT_LAYERS_STEP] = { 0xC000B4, SCRIPT_LAYERS, READS_THE_OVERFLOW, field_do_script_layer_work, false },
    [FRAME_WORK_STEP] = { 0xC000B7, FIELD_FRAME_WORK_ENTRY, READS_THE_OVERFLOW, field_frame_work, false },
};

/* The pass from its `first` step on, up to the wait for the next frame. A
 * step that hands its routine over to the original (a guard refused it, or it
 * stopped where the original goes on) ends the port's part, and the pass goes
 * on with the next step from the step's return (field_frame_goes_on). */
static void run_the_pass_from(Snes *s, PassStep first)
{
    BridgeMachine m = page(s);
    for (PassStep step = first; step < PASS_STEPS; step++) {
        bool back = the_pass[step].long_call
                        ? field_run_long(s, the_pass[step].site, the_pass[step].routine, the_pass[step].reads,
                                         the_pass[step].entry, m)
                        : field_run_or_hand_over(s, the_pass[step].site, the_pass[step].routine, the_pass[step].reads,
                                                 the_pass[step].entry, m);
        if (!back)
            return;
    }
    m_stop_at(s, WAIT_FOR_THE_NEXT_FRAME, READS_THE_OVERFLOW);
}

// $C0:0097
/* One pass of the main loop, up to the wait for the next frame: the frame's
 * presses taken, then the steps. The pass stops at the wait rather than
 * calling it: the wait spans the frame's end, and a pass still running there
 * would be a native routine held over every frame, which a machine copy can
 * only rebuild by replaying input (ct_drive, docs/MOVIES.md). The wait is an
 * entry of its own, and sets A for the next pass itself. */
void field_frame(Snes *s)
{
    latch_presses(state_of(s));
    run_the_pass_from(s, PAUSE_AND_MENUS_STEP);
}

/* The pass going on after a step whose routine came back to it itself: a
 * request answered over frames, which ends by jumping to the frame's wait
 * that returns to the pass, or a step the original ran. */
// $C0:00A4
void field_frame_goes_on_with_the_requests(Snes *s) { run_the_pass_from(s, REQUESTS_STEP); }
// $C0:00A7
void field_frame_goes_on_with_the_walk(Snes *s) { run_the_pass_from(s, WALK_STEP); }
// $C0:00AA
void field_frame_goes_on_with_confirm(Snes *s) { run_the_pass_from(s, CONFIRM_STEP); }
// $C0:00AD
void field_frame_goes_on_with_the_dialog_window(Snes *s) { run_the_pass_from(s, DIALOG_WINDOW_STEP); }
// $C0:00B1
void field_frame_goes_on_with_the_screen_effect(Snes *s) { run_the_pass_from(s, SCREEN_EFFECT_STEP); }
// $C0:00B4
void field_frame_goes_on_with_the_script_layers(Snes *s) { run_the_pass_from(s, SCRIPT_LAYERS_STEP); }
// $C0:00B7
void field_frame_goes_on_with_the_frame_work(Snes *s) { run_the_pass_from(s, FRAME_WORK_STEP); }

// $C0:00BA
/* The main loop's wait for the next frame (field_finish_frame), and once the
 * vblank has taken the frame the loop's next pass; in the shipping mode the
 * wait stops at the loop it waits in, which returns to the next pass itself.
 * Nothing the pass leaves but A's high byte and the overflow is read after it
 * (the entry's set). */
void field_wait_for_the_next_frame(Snes *s)
{
    if (field_run_or_hand_over(s, WAIT_FOR_THE_NEXT_FRAME, FIELD_FRAME_WAIT, READS_THE_OVERFLOW, field_finish_frame,
                               page(s)))
        field_main_loop_goes_round(s);
}

// $C0:00BD
/* Back from the wait, the main loop goes round. */
void field_main_loop_goes_round(Snes *s) { m_stop_at(s, MAIN_LOOP, THE_LOOP_READS_A_HIGH_AND_THE_OVERFLOW); }

// $C0:00BF
/* The frame's work, on 8-bit index registers but for the palettes and the
 * sprite table: the objects' scripts and the line effects for the next
 * frame, the palettes animated, the objects moved and their new frames drawn,
 * and their sprites put in the table, which returns for this routine. */
void field_frame_work(Snes *s)
{
    m_sep(s, CPU_FLAG_X);
    if (!field_run(s, 0xC000C1, TURNS_DUE, READS_THE_OVERFLOW, field_queue_due_turns, page_x8(s)) ||
        !bridge_call(s, 0xC000C4, SCRIPT_TURNS, READS_THE_OVERFLOW, page_x8(s)) ||
        !field_run_long(s, 0xC000C7, LINE_EFFECTS, READS_THE_OVERFLOW, field_build_line_effect_tables, page_x8(s)))
        return;
    m_rep(s, CPU_FLAG_X);
    if (!field_run_long(s, 0xC000CD, PALETTE_ANIMATION, READS_THE_OVERFLOW, palette_animate, page(s)))
        return;
    m_sep(s, CPU_FLAG_X);
    if (!bridge_call(s, 0xC000D3, UPDATE_OBJECTS, READS_THE_OVERFLOW, page_x8(s)) ||
        !bridge_call(s, 0xC000D6, SPRITE_FRAMES, READS_THE_OVERFLOW, page_x8(s)))
        return;
    m_rep(s, CPU_FLAG_X);
    if (!field_go_on_with(s, SPRITE_TABLE, field_build_sprite_table))
        m_stop_at(s, SPRITE_TABLE, 0);
}

// $C0:00DE
/* The effects refreshed for the next frame: the line effects' tables built
 * and the palettes animated. */
void field_refresh_effects(Snes *s)
{
    m_sep(s, CPU_FLAG_X);
    if (!field_run_long(s, 0xC000E0, LINE_EFFECTS, READS_NOTHING, field_build_line_effect_tables, page_x8(s)))
        return;
    m_rep(s, CPU_FLAG_X);
    if (field_run_long(s, 0xC000E6, PALETTE_ANIMATION, READS_NOTHING, palette_animate, page(s)))
        m_rts(s);
}

// $C0:00EB
/* A frame of the leader's walk alone, while the field waits on something of
 * its own: the walk, the effects refreshed, and the wait for the next frame,
 * which returns for this routine (and sets A and its flags first). */
enum { THE_WAIT_SETS_A = DC_A | DC_N | DC_Z };

void field_walk_a_frame(Snes *s)
{
    if (!field_run(s, 0xC000EB, WALK, READS_NOTHING, field_walk_the_leader, page(s)) ||
        !field_run(s, 0xC000EE, FIELD_REFRESH_EFFECTS_ENTRY, READS_NOTHING, field_refresh_effects, page(s)))
        return;
    m_stop_at(s, FIELD_FRAME_WAIT, THE_WAIT_SETS_A);
}

/* ---- Starting afresh ------------------------------------------------------------------------- */

/* Where the field goes when it is done with the game it was playing: back to
 * its own start for a new location, or out to a scene, each on the game's
 * fresh stack. The original sets the stack through X, which keeps its top. */
static void on_a_fresh_stack(Snes *s)
{
    s->cpu.x = CONSOLE_STACK_TOP;
    s->cpu.s = CONSOLE_STACK_TOP;
}

enum { FIELD_STARTS = 0xC00000, THE_FIELD_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:0CC0
/* The field starts again, for the location the party has moved to. */
void field_start_again(Snes *s)
{
    on_a_fresh_stack(s);
    m_stop_at(s, FIELD_STARTS, THE_FIELD_READS_NOTHING);
}

/* The scenes an event command asks for: below $80 the scene engine's (its
 * flags, scene.h, in A); from $80 the time warp's tunnel (warp/), as long as
 * the trip: the longest for $80, then shorter for $81 and $82, and the one
 * that flickers for the rest. Each starts the warp's setup and runs its
 * frames, still the original's code from its start. What each reads first it
 * sets, the flags the look at the scene's number left among it. */
enum { SCENE_ENGINE = 0xC30000, SCENE_FIELD_CLOSES_INTO = 0x85 };
enum { THE_SCENE_READS_A = DC_X | DC_Y | DC_N | DC_Z | DC_C };
enum { SCENE_LONG_WARP = 0x80, SCENE_SHORT_WARP = 0x81, SCENE_SHORTEST_WARP = 0x82 };
enum { LONG_WARP = 0xFDDA00, SHORT_WARP = 0xFDDA5F, SHORTEST_WARP = 0xFDDABE, FLICKERING_WARP = 0xFDDB1D };

static void start_the_scene(Snes *s, uint8_t scene)
{
    if (scene == SCENE_LONG_WARP)
        m_stop_at(s, LONG_WARP, THE_SCENE_READS_A);
    else if (scene == SCENE_SHORT_WARP)
        m_stop_at(s, SHORT_WARP, THE_SCENE_READS_A);
    else if (scene == SCENE_SHORTEST_WARP)
        m_stop_at(s, SHORTEST_WARP, THE_SCENE_READS_A);
    else if (scene > SCENE_SHORTEST_WARP)
        m_stop_at(s, FLICKERING_WARP, THE_SCENE_READS_A);
    else
        m_stop_at(s, SCENE_ENGINE, THE_SCENE_READS_A);
}

/* The scene's number in A, over the field's page's low byte (which the
 * original moves into A's high byte on the way). */
static void hand_over_the_scene(Snes *s, uint8_t scene)
{
    on_a_fresh_stack(s);
    s->cpu.a = (uint16_t)((FIELD_PAGE & 0x00FF) << 8 | scene);
}

// $C0:3FD6
/* The field ends for the scene an event command asked for (event/screen.c). */
void field_start_the_scene(Snes *s)
{
    uint8_t scene = field_scene_to_start(state_of(s));
    hand_over_the_scene(s, scene);
    start_the_scene(s, scene);
}

// $C0:2643
/* The field closes into the scene its close leads to (field/screen_effects.c),
 * always the scene engine's, whatever its number. */
void field_close_into_the_scene(Snes *s)
{
    hand_over_the_scene(s, SCENE_FIELD_CLOSES_INTO);
    m_stop_at(s, SCENE_ENGINE, THE_SCENE_READS_A);
}

/* The frame done, the main loop waits for the vblank handler to take it.
 * The wait is the one the field is in at nearly every frame's end, so a
 * saved machine has to hold it: the shipping mode leaves it to the loop's
 * idle entry (field/frame.c), which goes on as this does, and the port stops
 * at the loop (tm_wait_for_handlers_held). The loop reads the count into A's
 * low byte each pass; the high byte its callers may read is the count's, as
 * the original leaves it (field/leaves.c). */
enum { THE_LOOP_LOADS_A_LOW = DC_AL | DC_N | DC_Z };

void field_finish_frame(Snes *s)
{
    GameState *g = state_of(s);
    finish_frame(g);
    m_sep(s, CPU_FLAG_M);
    s->cpu.a = field_frame_wait_leaves_a(g);
    while (field_frame_pending(g))
        if (!tm_wait_for_handlers_held(m_platform(s), PF_LOOP_HELD(FIELD_FRAME_WAIT_LOOP, FIELD_FRAME_WAIT_RETURN, THE_LOOP_LOADS_A_LOW)))
            return;
    m_set_a8(s, 0);   /* the count the loop read last, taken */
    m_set_nz8(s, 0);
    m_rts(s);
}
