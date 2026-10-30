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
 *   interrupt handlers installed. The locations from $1F0 are not the
 *   field's maps but the world maps, drawn with sprites, which the menus'
 *   sprite-object mode shows (overworld/setup.c). A map's location has the
 *   field's state started (field/start.c), the location loaded
 *   (field/location.c), its objects and their sprites set up, and the screen
 *   faded in.
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
#include "field/frames.h"
#include "field/line_effects.h"
#include "field/object_update.h"
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
#include "core/program.h"
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
#include "overworld/main_loop.h"
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

/* The frame done, as C: the vblank handler takes it, and the main loop waits
 * for it to. */
void field_finish_the_frame(GameState *g, Platform *pf)
{
    finish_frame(g);
    while (field_frame_pending(g))
        tm_wait_frame(pf, PF_AT(FIELD_FRAME_WAIT_LOOP));
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
    WORLD_MAP = 0xC20000,            /* the world map, in the menus' sprite-object mode */
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

/* ---- The field entered ---------------------------------------------------------------------- */

/* Its steps after the handlers, which are entries of their own: the field's
 * state started, the location loaded, its sprites built afresh, its objects
 * set up, its steps forgotten and its screen faded in. As C, each is false
 * where it is a step the port doesn't take, the program going no further
 * there; on the machine each is called from the original's call of it. */
static bool start(GameState *g, Platform *pf)
{
    (void)pf;
    field_start_state(g);
    return true;
}

static bool load(GameState *g, Platform *pf)
{
    if (field_load_the_location(g, pf))
        return true;
    program_needs_the_machine(g, FIELD_LOAD_LOCATION_ENTRY, "a location whose map or script the port doesn't take");
    return false;
}

static bool sprites_afresh(GameState *g, Platform *pf)
{
    (void)pf;
    sprites_all_to_build_afresh(g);
    return true;
}

static bool forget_the_steps(GameState *g, Platform *pf)
{
    (void)pf;
    field_forget_the_steps(g);
    return true;
}

typedef enum { START_STEP, LOAD_STEP, SPRITES_STEP, OBJECTS_STEP, FORGET_STEP, FADE_IN_STEP, ENTERING_STEPS } EnterStep;

static const struct {
    uint32_t site, routine;
    void (*entry)(Snes *);
    bool (*run)(GameState *, Platform *);
} entering[ENTERING_STEPS] = {
    [START_STEP] = { 0xC00085, START, field_start, start },
    [LOAD_STEP] = { 0xC00088, FIELD_LOAD_LOCATION_ENTRY, field_load_location, load },
    [SPRITES_STEP] = { 0xC0008B, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, field_build_the_sprites_afresh,
                       sprites_afresh },
    [OBJECTS_STEP] = { 0xC0008E, FIELD_SET_UP_THE_OBJECTS_ENTRY, field_set_up_the_objects, field_objects_set_up },
    [FORGET_STEP] = { 0xC00091, FIELD_FORGET_THE_STEPS_ENTRY, field_forget_the_steps_entry, forget_the_steps },
    [FADE_IN_STEP] = { 0xC00094, FIELD_FADE_IN_ENTRY, field_fade_in_entry, field_fade_in },
};

/* A step of the entry run, or the world map taking over, which the menus'
 * sprite-object mode shows: each false where the field goes no further (the
 * world map true where it left for the field again). */
typedef struct {
    bool (*step)(void *ctx, EnterStep step);
    bool (*to_the_world_map)(void *ctx);
} EnterSteps;

// $C0:005D (and $C0:0000, the field's start for the other modes)
/* The field entered, the screen dark: its handlers taken, and for a map its
 * steps; the locations from $1F0 are the world map's instead, which comes
 * back to the field's start for the place it leaves for. False where it went
 * no further. */
static bool enter_the_location(GameState *g, Platform *pf, const EnterSteps *steps, void *ctx)
{
    for (;;) {
        TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf));
        take_the_interrupts(g);
        if (!location_is_the_menus(g))
            break;
        if (!steps->to_the_world_map(ctx))
            return false;
    }
    for (EnterStep step = START_STEP; step < ENTERING_STEPS; step++)
        if (!steps->step(ctx, step))
            return false;
    return true;
}

typedef struct {
    GameState *game;
    Platform *pf;
} FieldPassContext;

static bool enter_step(void *ctx, EnterStep step)
{
    FieldPassContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, entering[step].routine, done = entering[step].run(c->game, c->pf));
    return done;
}

static bool to_the_world_map(void *ctx)
{
    FieldPassContext *c = ctx;
    OverworldEnd end = OVERWORLD_WENT_NO_FURTHER;
    TIMED_AS(c->game, WORLD_MAP, end = overworld_runs(c->game, c->pf));
    return end == OVERWORLD_LEFT;
}

bool field_enter_the_location(GameState *g, Platform *pf)
{
    static const EnterSteps in_the_program = { enter_step, to_the_world_map };
    FieldPassContext c = { g, pf };
    return enter_the_location(g, pf, &in_the_program, &c);
}

static bool enter_step_on_the_machine(void *ctx, EnterStep step)
{
    Snes *s = ctx;
    return field_run(s, entering[step].site, entering[step].routine, READS_NOTHING, entering[step].entry, page(s));
}

/* What the menus' mode reads of the registers is the data bank alone. */
static bool to_the_world_map_on_the_machine(void *ctx)
{
    m_stop_at(ctx, WORLD_MAP, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_D);
    return false;
}

void field_enter(Snes *s)
{
    static const EnterSteps on_the_machine = { enter_step_on_the_machine, to_the_world_map_on_the_machine };
    display_leaves_the_machine_dark(s);
    s->cpu.d = FIELD_PAGE;
    if (enter_the_location(state_of(s), m_platform(s), &on_the_machine, s))
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

/* ---- The frame's work ----------------------------------------------------------------------- */

/* Its steps, as C, false where one is a step the port doesn't take (a build
 * with no machine calls the budgets' turns and frames, and the objects'
 * update, as C too). */
static bool turns_due(GameState *g, Platform *pf)
{
    (void)pf;
    field_queue_the_due_turns(g);
    return true;
}

static bool line_effects(GameState *g, Platform *pf)
{
    (void)pf;
    field_line_effects_build(g);
    return true;
}

static bool palettes(GameState *g, Platform *pf)
{
    (void)pf;
    return palette_animated(g);
}

static bool objects(GameState *g, Platform *pf)
{
    (void)pf;
    return field_objects_updated(g);
}

static bool frames(GameState *g, Platform *pf)
{
    field_queued_frames_drawn(g, pf);
    return true;
}

/* In order, each with the original's call of it: its entry where the port
 * runs it as C under the call, none where it is a port called on the machine
 * as the original's call (a budget, or the update), and the index
 * registers' width it is called with. The line effects' tables and the
 * palettes are reached by long calls. */
static const struct {
    uint32_t site, routine;
    void (*entry)(Snes *);
    bool long_call, x8;
    bool (*run)(GameState *, Platform *);
} frame_work[] = {
    { 0xC000C1, TURNS_DUE, field_queue_due_turns, false, true, turns_due },
    { 0xC000C4, SCRIPT_TURNS, NULL, false, true, field_queued_turns_run },
    { 0xC000C7, LINE_EFFECTS, field_build_line_effect_tables, true, true, line_effects },
    { 0xC000CD, PALETTE_ANIMATION, palette_animate, true, false, palettes },
    { 0xC000D3, UPDATE_OBJECTS, NULL, false, true, objects },
    { 0xC000D6, SPRITE_FRAMES, NULL, false, true, frames },
};
enum { FRAME_WORK_STEPS = sizeof frame_work / sizeof frame_work[0] };

// $C0:00BF
/* The frame's work: the objects' scripts and the line effects for the next
 * frame, the palettes animated, the objects moved and their new frames
 * drawn, and their sprites put in the table, which `table` does. False where
 * a step went no further. */
typedef bool (*FrameWorkStep)(void *ctx, unsigned step);

static bool do_the_frame_work(FrameWorkStep step, void *ctx)
{
    for (unsigned i = 0; i < FRAME_WORK_STEPS; i++)
        if (!step(ctx, i))
            return false;
    return true;
}

static bool frame_work_step(void *ctx, unsigned step)
{
    FieldPassContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, frame_work[step].routine, done = frame_work[step].run(c->game, c->pf));
    return done;
}

bool field_frame_work_done(GameState *g, Platform *pf)
{
    FieldPassContext c = { g, pf };
    if (!do_the_frame_work(frame_work_step, &c))
        return false;
    bool built = false;
    TIMED_AS(g, SPRITE_TABLE, built = field_sprite_table_built(g));
    if (!built)
        program_needs_the_machine(g, SPRITE_TABLE, "a sprite table the port doesn't build");
    return built;
}

static bool frame_work_step_on_the_machine(void *ctx, unsigned step)
{
    Snes *s = ctx;
    if (frame_work[step].x8 != m_x8(s)) {
        if (frame_work[step].x8)
            m_sep(s, CPU_FLAG_X);
        else
            m_rep(s, CPU_FLAG_X);
    }
    BridgeMachine m = frame_work[step].x8 ? page_x8(s) : page(s);
    if (!frame_work[step].entry)
        return bridge_call(s, frame_work[step].site, frame_work[step].routine, READS_THE_OVERFLOW, m);
    if (frame_work[step].long_call)
        return field_run_long(s, frame_work[step].site, frame_work[step].routine, READS_THE_OVERFLOW,
                              frame_work[step].entry, m);
    return field_run(s, frame_work[step].site, frame_work[step].routine, READS_THE_OVERFLOW, frame_work[step].entry, m);
}

/* On 8-bit index registers but for the palettes and the sprite table, which
 * returns for this routine. */
void field_frame_work(Snes *s)
{
    if (!do_the_frame_work(frame_work_step_on_the_machine, s))
        return;
    m_rep(s, CPU_FLAG_X);
    if (!field_go_on_with(s, SPRITE_TABLE, field_build_sprite_table))
        m_stop_at(s, SPRITE_TABLE, 0);
}

/* ---- The main loop's pass ------------------------------------------------------------------- */

/* Its steps, as C: where the pass goes after each. */
static FieldPass goes_on(bool back) { return back ? FIELD_PASS_GOES_ON : FIELD_PASS_STOPS; }

static FieldPass pause_and_menus(GameState *g, Platform *pf) { return goes_on(field_pause_and_menus(g, pf)); }

static FieldPass walk(GameState *g, Platform *pf)
{
    (void)pf;
    return goes_on(field_the_leader_walks(g));
}

static FieldPass confirm(GameState *g, Platform *pf)
{
    (void)pf;
    field_confirm_pressed(g);
    return FIELD_PASS_GOES_ON;
}

static FieldPass dialog_window(GameState *g, Platform *pf)
{
    (void)pf;
    field_dialog_window_step(g);
    return FIELD_PASS_GOES_ON;
}

static FieldPass screen_effect(GameState *g, Platform *pf)
{
    return goes_on(field_screen_effect_moved_on(g, pf));
}

static FieldPass script_layers(GameState *g, Platform *pf)
{
    (void)pf;
    field_script_layers_worked(g);
    return FIELD_PASS_GOES_ON;
}

static FieldPass frame_work_done(GameState *g, Platform *pf) { return goes_on(field_frame_work_done(g, pf)); }

/* The pass's steps in order, each called from its site. The dialog
 * window's step is reached by a long call. A's high byte and the overflow
 * pass from each step to the next as the steps leave them. */
typedef enum {
    PAUSE_AND_MENUS_STEP, REQUESTS_STEP, WALK_STEP, CONFIRM_STEP, DIALOG_WINDOW_STEP, SCREEN_EFFECT_STEP,
    SCRIPT_LAYERS_STEP, FRAME_WORK_STEP, PASS_STEPS,
} PassStep;

static const struct {
    uint32_t site, routine;
    uint16_t reads;
    void (*entry)(Snes *);
    bool long_call;
    FieldPass (*run)(GameState *, Platform *);
} the_pass[PASS_STEPS] = {
    [PAUSE_AND_MENUS_STEP] = { 0xC000A1, PAUSE_AND_MENUS, READS_A_HIGH_AND_THE_OVERFLOW, field_pause_and_menus_entry, false,
                               pause_and_menus },
    [REQUESTS_STEP] = { 0xC000A4, REQUESTS, READS_A_HIGH_AND_THE_OVERFLOW, field_handle_requests, false,
                        field_requests_handled },
    [WALK_STEP] = { 0xC000A7, WALK, READS_NOTHING, field_walk_the_leader, false, walk },
    [CONFIRM_STEP] = { 0xC000AA, CONFIRM, READS_THE_OVERFLOW, field_press_confirm, false, confirm },
    [DIALOG_WINDOW_STEP] = { 0xC000AD, FIELD_DIALOG_WINDOW_STEP_ENTRY, READS_THE_OVERFLOW, field_dialog_window_step_entry,
                             true, dialog_window },
    [SCREEN_EFFECT_STEP] = { 0xC000B1, SCREEN_EFFECT, READS_THE_OVERFLOW, field_move_screen_effect_on, false,
                             screen_effect },
    [SCRIPT_LAYERS_STEP] = { 0xC000B4, SCRIPT_LAYERS, READS_THE_OVERFLOW, field_do_script_layer_work, false,
                             script_layers },
    [FRAME_WORK_STEP] = { 0xC000B7, FIELD_FRAME_WORK_ENTRY, READS_THE_OVERFLOW, field_frame_work, false,
                          frame_work_done },
};

// $C0:0097
/* A pass of the main loop from its `first` step on, up to the wait for the
 * next frame: the frame's presses latched where it starts, then the steps.
 * Where the pass goes after a step that doesn't go on: the field started
 * anew for a new place, or no further. */
typedef FieldPass (*PassStepRun)(void *ctx, PassStep step);

static FieldPass run_the_pass_from(GameState *g, PassStep first, PassStepRun run, void *ctx)
{
    if (first == PAUSE_AND_MENUS_STEP)
        latch_presses(g);
    for (PassStep step = first; step < PASS_STEPS; step++) {
        FieldPass after = run(ctx, step);
        if (after != FIELD_PASS_GOES_ON)
            return after;
    }
    return FIELD_PASS_GOES_ON;
}

static FieldPass pass_step(void *ctx, PassStep step)
{
    FieldPassContext *c = ctx;
    FieldPass after = FIELD_PASS_STOPS;
    TIMED_AS(c->game, the_pass[step].routine, after = the_pass[step].run(c->game, c->pf));
    return after;
}

/* As C the pass goes through the wait for the next frame, unless a step
 * sent it round the main loop itself. */
FieldPass field_runs_a_pass(GameState *g, Platform *pf)
{
    FieldPassContext c = { g, pf };
    FieldPass after = run_the_pass_from(g, PAUSE_AND_MENUS_STEP, pass_step, &c);
    if (after == FIELD_PASS_GOES_ROUND)
        return FIELD_PASS_GOES_ON;
    if (after == FIELD_PASS_GOES_ON)
        TIMED_AS(g, FIELD_FRAME_WAIT, field_finish_the_frame(g, pf));
    return after;
}

// $C0:005D (and $C0:0000, its start again for a new place)
void field_runs(GameState *g, Platform *pf)
{
    for (;;) {
        if (!field_enter_the_location(g, pf))
            return;
        FieldPass pass;
        do
            pass = field_runs_a_pass(g, pf);
        while (pass == FIELD_PASS_GOES_ON);
        if (pass == FIELD_PASS_STOPS && (program_went_no_further(g) || !program_field_starts_again(g)))
            return;
    }
}

/* On the machine, a step that hands its routine over to the original (a
 * guard refused it, or it stopped where the original goes on) ends the
 * port's part, and the pass goes on with the next step from the step's
 * return (field_frame_goes_on). */
static FieldPass pass_step_on_the_machine(void *ctx, PassStep step)
{
    Snes *s = ctx;
    bool back = the_pass[step].long_call
                    ? field_run_long(s, the_pass[step].site, the_pass[step].routine, the_pass[step].reads,
                                     the_pass[step].entry, page(s))
                    : field_run_or_hand_over(s, the_pass[step].site, the_pass[step].routine, the_pass[step].reads,
                                             the_pass[step].entry, page(s));
    return goes_on(back);
}

static void pass_on_the_machine_from(Snes *s, PassStep first)
{
    if (run_the_pass_from(state_of(s), first, pass_step_on_the_machine, s) == FIELD_PASS_GOES_ON)
        m_stop_at(s, WAIT_FOR_THE_NEXT_FRAME, READS_THE_OVERFLOW);
}

/* On the machine the pass stops at the wait rather than calling it: the
 * wait spans the frame's end, and a pass still running there would be a
 * native routine held over every frame, which a machine copy can only
 * rebuild by replaying input (ct_drive, docs/MOVIES.md). The wait is an
 * entry of its own, and sets A for the next pass itself. */
void field_frame(Snes *s) { pass_on_the_machine_from(s, PAUSE_AND_MENUS_STEP); }

/* The pass going on after a step whose routine came back to it itself: a
 * request answered over frames, which ends by jumping to the frame's wait
 * that returns to the pass, or a step the original ran. */
// $C0:00A4
void field_frame_goes_on_with_the_requests(Snes *s) { pass_on_the_machine_from(s, REQUESTS_STEP); }
// $C0:00A7
void field_frame_goes_on_with_the_walk(Snes *s) { pass_on_the_machine_from(s, WALK_STEP); }
// $C0:00AA
void field_frame_goes_on_with_confirm(Snes *s) { pass_on_the_machine_from(s, CONFIRM_STEP); }
// $C0:00AD
void field_frame_goes_on_with_the_dialog_window(Snes *s) { pass_on_the_machine_from(s, DIALOG_WINDOW_STEP); }
// $C0:00B1
void field_frame_goes_on_with_the_screen_effect(Snes *s) { pass_on_the_machine_from(s, SCREEN_EFFECT_STEP); }
// $C0:00B4
void field_frame_goes_on_with_the_script_layers(Snes *s) { pass_on_the_machine_from(s, SCRIPT_LAYERS_STEP); }
// $C0:00B7
void field_frame_goes_on_with_the_frame_work(Snes *s) { pass_on_the_machine_from(s, FRAME_WORK_STEP); }

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

// $C0:00DE
/* The effects refreshed for the next frame: the line effects' tables built
 * and the palettes animated, each a routine of its own, run as C or on the
 * machine; false where one went no further. */
typedef struct {
    bool (*line_effects)(void *ctx);
    bool (*palettes)(void *ctx);
} EffectsSteps;

static bool refresh_effects(const EffectsSteps *steps, void *ctx)
{
    return steps->line_effects(ctx) && steps->palettes(ctx);
}

static bool refreshed_line_effects(void *ctx)
{
    TIMED_AS((GameState *)ctx, LINE_EFFECTS, field_line_effects_build(ctx));
    return true;
}

static bool refreshed_palettes(void *ctx)
{
    bool done = false;
    TIMED_AS((GameState *)ctx, PALETTE_ANIMATION, done = palette_animated(ctx));
    return done;
}

bool field_effects_refreshed(GameState *g)
{
    static const EffectsSteps in_the_program = { refreshed_line_effects, refreshed_palettes };
    return refresh_effects(&in_the_program, g);
}

/* On the machine each is called from the original's call of it, the line
 * effects' tables with 8-bit index registers. */
static bool line_effects_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_sep(s, CPU_FLAG_X);
    if (!field_run_long(s, 0xC000E0, LINE_EFFECTS, READS_NOTHING, field_build_line_effect_tables, page_x8(s)))
        return false;
    m_rep(s, CPU_FLAG_X);
    return true;
}

static bool palettes_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return field_run_long(s, 0xC000E6, PALETTE_ANIMATION, READS_NOTHING, palette_animate, page(s));
}

void field_refresh_effects(Snes *s)
{
    static const EffectsSteps on_the_machine = { line_effects_on_the_machine, palettes_on_the_machine };
    if (refresh_effects(&on_the_machine, s))
        m_rts(s);
}

// $C0:00EB
/* A frame of the leader's walk alone, while the field waits on something of
 * its own: the walk, the effects refreshed, and the wait for the next frame,
 * which returns for this routine (and sets A and its flags first). */
typedef struct {
    bool (*walk)(void *ctx);
    bool (*refresh)(void *ctx);
    bool (*wait)(void *ctx);
} WalkFrameSteps;

static bool walk_a_frame(const WalkFrameSteps *steps, void *ctx)
{
    return steps->walk(ctx) && steps->refresh(ctx) && steps->wait(ctx);
}

static bool leader_takes_a_step(void *ctx)
{
    FieldPassContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, WALK, done = field_the_leader_walks(c->game));
    return done;
}

static bool effects_refreshed(void *ctx)
{
    FieldPassContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, FIELD_REFRESH_EFFECTS_ENTRY, done = field_effects_refreshed(c->game));
    return done;
}

static bool frame_waited(void *ctx)
{
    FieldPassContext *c = ctx;
    TIMED_AS(c->game, FIELD_FRAME_WAIT, field_finish_the_frame(c->game, c->pf));
    return true;
}

bool field_walked_a_frame(GameState *g, Platform *pf)
{
    static const WalkFrameSteps in_the_program = { leader_takes_a_step, effects_refreshed, frame_waited };
    FieldPassContext c = { g, pf };
    return walk_a_frame(&in_the_program, &c);
}

static bool walked_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return field_run(s, 0xC000EB, WALK, READS_NOTHING, field_walk_the_leader, page(s));
}

static bool refreshed_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return field_run(s, 0xC000EE, FIELD_REFRESH_EFFECTS_ENTRY, READS_NOTHING, field_refresh_effects, page(s));
}

enum { THE_WAIT_SETS_A = DC_A | DC_N | DC_Z };

static bool waited_on_the_machine(void *ctx)
{
    m_stop_at(ctx, FIELD_FRAME_WAIT, THE_WAIT_SETS_A);
    return false;
}

void field_walk_a_frame(Snes *s)
{
    static const WalkFrameSteps on_the_machine = { walked_on_the_machine, refreshed_on_the_machine,
                                                   waited_on_the_machine };
    (void)walk_a_frame(&on_the_machine, s);
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

/* As C: the scene the field ends into, the scene engine's or a time gate's
 * tunnel, is the program's next step (core/program.c: scene_runs,
 * warp_runs). */
void field_scene_starts(GameState *g, uint8_t scene) { program_goes_on_to_a_scene(g, scene); }

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
void field_closed_into_the_scene(GameState *g) { program_goes_on_to_a_scene(g, SCENE_FIELD_CLOSES_INTO); }

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
