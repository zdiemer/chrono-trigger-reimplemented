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
 */
#include "field/main_loop.h"
#include "menu/display.h"
#include "scene/scene.h"
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
#include "core/program.h"
#include "core/palette.h"
#include "core/display.h"
#include "core/sound_commands.h"
#include "core/input.h"
#include "event/contact.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/parts.h"
#include "menu/party.h"
#include "warp/warp.h"
#include "menu/saves.h"
#include "overworld/main_loop.h"
#include "platform/timing.h"
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

/* The frame done: the vblank handler takes it, and the main loop waits
 * for it to. */
void field_finish_the_frame(GameState *g, Platform *pf)
{
    finish_frame(g);
    while (field_frame_pending(g))
        tm_wait_frame(pf, PF_AT(FIELD_FRAME_WAIT_LOOP));
}

/* ---- The timing keys of the work it does ---------------------------------------------------------- */

/* The keys (charge.h, TIMED_AS) of the routines whose work the field's steps
 * do: the sound driver's start, the menus' services, the world map and the
 * palettes' animation. */
enum {
    MENUS_SERVICES = 0xC28004, /* the menus' services (menu/party.h), whose time the power-on's is */
    WORLD_MAP = 0xC20000, /* the world map, in the menus' sprite-object mode */
    PALETTE_ANIMATION = 0xFDFFF7,
};

/* The frame's two budgets: each works through the objects for as long as the
 * frame's time allows (platform/timing.h, tm_frame_time_left). */
enum {
    SCRIPT_TURNS = 0xC05A46,         /* the queued objects' turns run, as the frame's time allows */
    SPRITE_FRAMES = 0xC0CA76,        /* the objects' new frames drawn, as the frame's time allows */
};

/* The objects' update: each object that comes into view or leaves it. */
enum { UPDATE_OBJECTS = 0xC0A810 };

/* The steps it runs, by the routines they stand for. */
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

/* ---- The field's start ---------------------------------------------------------------------------- */

/* The timing keys of the field's start, its main loop and its frame wait. */
enum { ENTER = 0xC0005D };

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
 * (menu/saves.h). Here the driver is one a host plays (core/sound_commands.h),
 * and the field's entry follows. */
void field_power_on(GameState *g, Platform *pf)
{
    wake(g, pf);
    (void)sound_driver_host_boots(g, pf);
    TIMED_AS(g, MENUS_SERVICES, start_from_the_saves(g, pf));
}

typedef enum { START_STEP, LOAD_STEP, SPRITES_STEP, OBJECTS_STEP, FORGET_STEP, FADE_IN_STEP, ENTERING_STEPS } EnterStep;

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
    program_breaks_a_contract(g, FIELD_LOAD_LOCATION_ENTRY, "a location whose map or script the port doesn't take");
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

static const struct { uint32_t site, routine; bool (*run)(GameState *, Platform *); } entering[ENTERING_STEPS] = {
    [START_STEP] = { 0xC00085, START, start },
    [LOAD_STEP] = { 0xC00088, FIELD_LOAD_LOCATION_ENTRY, load },
    [SPRITES_STEP] = { 0xC0008B, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, sprites_afresh },
    [OBJECTS_STEP] = { 0xC0008E, FIELD_SET_UP_THE_OBJECTS_ENTRY, field_objects_set_up },
    [FORGET_STEP] = { 0xC00091, FIELD_FORGET_THE_STEPS_ENTRY, forget_the_steps },
    [FADE_IN_STEP] = { 0xC00094, FIELD_FADE_IN_ENTRY, field_fade_in },
};

/* The world map, which the menus' sprite-object mode shows, takes over:
 * true where it left for the field again. */
static bool to_the_world_map(GameState *g, Platform *pf)
{
    OverworldEnd end = OVERWORLD_WENT_NO_FURTHER;
    TIMED_AS(g, WORLD_MAP, end = overworld_runs(g, pf));
    return end == OVERWORLD_LEFT;
}

// $C0:005D (and $C0:0000, the field's start for the other modes)
/* The field entered, the screen dark: its handlers taken, and for a map its
 * steps; the locations from $1F0 are the world map's instead, which comes
 * back to the field's start for the place it leaves for. False where it went
 * no further. */
enum { LOCATION_SIGN = 0x8000 };

bool field_enter_the_location(GameState *g, Platform *pf)
{
    if (location_id(g) & LOCATION_SIGN) {
        program_breaks_a_contract(g, ENTER, "a field location outside its unsigned range");
        return false;
    }
    for (;;) {
        TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf));
        take_the_interrupts(g);
        if (!location_is_the_menus(g))
            break;
        if (!to_the_world_map(g, pf))
            return false;
    }
    for (EnterStep step = START_STEP; step < ENTERING_STEPS; step++) {
        bool done = false;
        TIMED_AS(g, entering[step].routine, done = entering[step].run(g, pf));
        if (!done)
            return false;
    }
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

/* The main loop's steps, in order. */

static bool turns_due(GameState *g, Platform *pf)
{
    (void)pf;
    field_queue_the_due_turns(g);
    return true;
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

static const struct { uint32_t site, routine; bool (*run)(GameState *, Platform *); } frame_work[] = {
    { 0xC000C1, TURNS_DUE, turns_due },
    { 0xC000C4, SCRIPT_TURNS, field_queued_turns_run },
    { 0xC000C7, LINE_EFFECTS, line_effects },
    { 0xC000CD, PALETTE_ANIMATION, palettes },
    { 0xC000D3, UPDATE_OBJECTS, objects },
    { 0xC000D6, SPRITE_FRAMES, frames },
};

enum { FRAME_WORK_STEPS = sizeof frame_work / sizeof frame_work[0] };

// $C0:00BF
/* The frame's work: the objects' scripts and the line effects for the next
 * frame, the palettes animated, the objects moved and their new frames
 * drawn, and their sprites put in the table. False where a step went no
 * further. */
bool field_frame_work_done(GameState *g, Platform *pf)
{
    for (unsigned step = 0; step < FRAME_WORK_STEPS; step++) {
        bool done = false;
        TIMED_AS(g, frame_work[step].routine, done = frame_work[step].run(g, pf));
        if (!done)
            return false;
    }
    bool built = false;
    TIMED_AS(g, SPRITE_TABLE, built = field_sprite_table_built(g));
    if (!built)
        program_breaks_a_contract(g, SPRITE_TABLE, "a sprite table the port doesn't build");
    return built;
}

/* ---- The main loop's pass ------------------------------------------------------------------- */

/* Its steps: where the pass goes after each. */
static FieldPass goes_on(bool back) { return back ? FIELD_PASS_GOES_ON : FIELD_PASS_STOPS; }

static FieldPass walk(GameState *g, Platform *pf)
{
    (void)pf;
    return goes_on(field_the_leader_walks(g));
}

/* The pass's steps, in order. */
typedef enum {
    PAUSE_AND_MENUS_STEP, REQUESTS_STEP, WALK_STEP, CONFIRM_STEP, DIALOG_WINDOW_STEP, SCREEN_EFFECT_STEP,
    SCRIPT_LAYERS_STEP, FRAME_WORK_STEP, PASS_STEPS,
} PassStep;

static FieldPass pause_and_menus(GameState *g, Platform *pf) { return goes_on(field_pause_and_menus(g, pf)); }

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

static const struct { uint32_t site, routine; FieldPass (*run)(GameState *, Platform *); } the_pass[PASS_STEPS] = {
    [PAUSE_AND_MENUS_STEP] = { 0xC000A1, PAUSE_AND_MENUS, pause_and_menus },
    [REQUESTS_STEP] = { 0xC000A4, REQUESTS, field_requests_handled },
    [WALK_STEP] = { 0xC000A7, WALK, walk },
    [CONFIRM_STEP] = { 0xC000AA, CONFIRM, confirm },
    [DIALOG_WINDOW_STEP] = { 0xC000AD, FIELD_DIALOG_WINDOW_STEP_ENTRY, dialog_window },
    [SCREEN_EFFECT_STEP] = { 0xC000B1, SCREEN_EFFECT, screen_effect },
    [SCRIPT_LAYERS_STEP] = { 0xC000B4, SCRIPT_LAYERS, script_layers },
    [FRAME_WORK_STEP] = { 0xC000B7, FIELD_FRAME_WORK_ENTRY, frame_work_done },
};

// $C0:0097
/* A pass of the main loop, up to the wait for the next frame: the frame's
 * presses latched, then the steps. Where the pass goes after a step that
 * doesn't go on: the field started anew for a new place, or no further. */
static FieldPass run_the_pass(GameState *g, Platform *pf)
{
    latch_presses(g);
    for (PassStep step = PAUSE_AND_MENUS_STEP; step < PASS_STEPS; step++) {
        FieldPass after = FIELD_PASS_STOPS;
        TIMED_AS(g, the_pass[step].routine, after = the_pass[step].run(g, pf));
        if (after != FIELD_PASS_GOES_ON)
            return after;
    }
    return FIELD_PASS_GOES_ON;
}

/* The pass goes through the wait for the next frame, unless a step
 * sent it round the main loop itself. */
FieldPass field_runs_a_pass(GameState *g, Platform *pf)
{
    FieldPass after = run_the_pass(g, pf);
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

// $C0:00DE
/* The effects refreshed for the next frame: the line effects' tables built
 * and the palettes animated, each a routine of its own; false where the
 * palettes' went no further. */
bool field_effects_refreshed(GameState *g)
{
    TIMED_AS(g, LINE_EFFECTS, field_line_effects_build(g));
    bool done = false;
    TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g));
    return done;
}

// $C0:00EB
/* A frame of the leader's walk alone, while the field waits on something of
 * its own: the walk, the effects refreshed, and the wait for the next frame. */
bool field_walked_a_frame(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, WALK, done = field_the_leader_walks(g));
    if (!done)
        return false;
    TIMED_AS(g, FIELD_REFRESH_EFFECTS_ENTRY, done = field_effects_refreshed(g));
    if (!done)
        return false;
    TIMED_AS(g, FIELD_FRAME_WAIT, field_finish_the_frame(g, pf));
    return true;
}

/* The scenes an event command asks for: below $80 the scene engine's
 * (scene.h); from $80 the time warp's tunnel (warp/), as long as the trip:
 * the longest for $80, then shorter for $81 and $82, and the one that
 * flickers for the rest. */
enum { SCENE_FIELD_CLOSES_INTO = 0x85 };

/* The scene the field ends into, the scene engine's or a time gate's
 * tunnel, is the program's next step (core/program.c: scene_runs,
 * warp_runs). */
void field_scene_starts(GameState *g, uint8_t scene) { program_goes_on_to_a_scene(g, scene); }

// $C0:2643
/* The field closes into the scene its close leads to (field/screen_effects.c),
 * always the scene engine's, whatever its number. */
void field_closed_into_the_scene(GameState *g) { program_goes_on_to_a_scene(g, SCENE_FIELD_CLOSES_INTO); }
