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
#include "field/field.h"
#include "field/line_effects.h"
#include "field/menus.h"
#include "field/party.h"
#include "field/requests.h"
#include "field/screen_effects.h"
#include "field/script_layers.h"
#include "field/sprites.h"
#include "field/start.h"
#include "field/walk.h"

#include "charge.h"
#include "original_effects.h"
#include "core/display.h"
#include "core/input.h"
#include "event/contact.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/parts.h"
#include "menu/party.h"
#include "menu/party_leaves.h"
#include "menu/saves.h"
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

/* The original's routines; the palettes' animation, through the original's
 * jump to it at $FD:FFF7, which isn't an entry; and the sound driver's start,
 * a port that waits on the sound processor across frames, which only a call
 * can run: it hands every register back as it found it, A's low byte and the
 * flags aside. */
enum {
    SOUND_DRIVER_BOOT = 0xC70000,
    THE_BOOT_IGNORES = DC_AL | DC_N | DC_V | DC_Z | DC_C,
    MENUS_SERVICES = 0xC28004,       /* the menus' services (menu/party.h), whose time the power-on's is */
    MENUS_SCREENS = 0xC20000,        /* the menus' mode that shows a screen with sprites */
    SPRITE_LISTS = 0xC0B192,         /* the objects' sprite lists set up */
    OBJECTS_SET_UP = 0xC056A6,       /* the location's objects started in their scripts */
    STEPS_FORGOTTEN = 0xC028AA,      /* the switches stepped on in the last location forgotten */
    FADE_IN = 0xC02848,              /* the screen set up and faded in */
    DIALOG_WINDOW = 0xC01F87,        /* the dialog window opened or closed a step */
    OBJECTS_DUE = 0xC059D9,          /* the objects whose scripts' waits are up queued for a turn */
    SCRIPTS = 0xC05A46,              /* the queued objects' scripts run, as the frame's time allows */
    SPRITE_FRAMES = 0xC0CA76,        /* the objects' new frames drawn, as the frame's time allows */
    PALETTE_ANIMATION = 0xFDFFF7,
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

/* For shadow mode: the sound driver's start is called with the field's
 * page's high byte in A's, which the original sets D through. */
static void leave_the_page_in_a(Snes *s) { s->cpu.a = (uint16_t)((FIELD_PAGE & 0xFF00) | (s->cpu.a & 0x00FF)); }

/* The field's entry reads none of the registers the power-on leaves. */
enum { ENTERING_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:000E
/* The field's first start, at power-on: the cartridge's saves looked at,
 * which starts a new game where there are none (menu/saves.h). The menus'
 * service keeps the X and Y it is given as its arguments, which this one
 * doesn't read: the original leaves the last clear's size in X. */
void field_first_start(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    take_the_interrupts(g);
    s->cpu.d = FIELD_PAGE;
    display_go_dark(s);
    for (unsigned i = 0; i < POWER_ON_CLEARS; i++)
        TIMED_AS(g, FIELD_FILL_MEMORY_ENTRY, field_clear_memory(g, pf, power_on_clears[i].from, power_on_clears[i].bytes));
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
        !bridge_call(s, 0xC0008B, SPRITE_LISTS, READS_NOTHING, page(s)) ||
        !bridge_call(s, 0xC0008E, OBJECTS_SET_UP, READS_NOTHING, page(s)) ||
        !bridge_call(s, 0xC00091, STEPS_FORGOTTEN, READS_NOTHING, page(s)) ||
        !bridge_call(s, 0xC00094, FADE_IN, READS_NOTHING, page(s)))
        return;
    m_stop_at(s, MAIN_LOOP, 0);
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

// $C0:0097
/* One pass of the main loop, up to the wait for the next frame. A's high byte
 * and the overflow pass from each step to the next as the steps leave them.
 * The pass stops at the wait rather than calling it: the wait spans the
 * frame's end, and a pass still running there would be a native routine held
 * over every frame, which a machine copy can only rebuild by replaying input
 * (ct_drive, docs/MOVIES.md). The wait sets A for the next pass itself. */
void field_frame(Snes *s)
{
    latch_presses(state_of(s));
    BridgeMachine m = page(s);
    if (field_run_or_hand_over(s, 0xC000A1, PAUSE_AND_MENUS, READS_A_HIGH_AND_THE_OVERFLOW, field_pause_and_menus, m) &&
        field_run_or_hand_over(s, 0xC000A4, REQUESTS, READS_A_HIGH_AND_THE_OVERFLOW, field_handle_requests, m) &&
        field_run_or_hand_over(s, 0xC000A7, WALK, READS_NOTHING, field_walk_the_leader, m) &&
        field_run_or_hand_over(s, 0xC000AA, CONFIRM, READS_THE_OVERFLOW, field_press_confirm, m) &&
        bridge_call_long(s, 0xC000AD, DIALOG_WINDOW, READS_THE_OVERFLOW, m) &&
        field_run_or_hand_over(s, 0xC000B1, SCREEN_EFFECT, READS_THE_OVERFLOW, field_move_screen_effect_on, m) &&
        field_run_or_hand_over(s, 0xC000B4, SCRIPT_LAYERS, READS_THE_OVERFLOW, field_do_script_layer_work, m) &&
        field_run_or_hand_over(s, 0xC000B7, FIELD_FRAME_WORK_ENTRY, READS_THE_OVERFLOW, field_frame_work, m))
        m_stop_at(s, WAIT_FOR_THE_NEXT_FRAME, READS_THE_OVERFLOW);
}

// $C0:00BF
/* The frame's work, on 8-bit index registers but for the palettes and the
 * sprite table: the objects' scripts and the line effects for the next
 * frame, the palettes animated, the objects moved and their new frames drawn,
 * and their sprites put in the table, which returns for this routine. */
void field_frame_work(Snes *s)
{
    m_sep(s, CPU_FLAG_X);
    if (!bridge_call(s, 0xC000C1, OBJECTS_DUE, READS_THE_OVERFLOW, page_x8(s)) ||
        !bridge_call(s, 0xC000C4, SCRIPTS, READS_THE_OVERFLOW, page_x8(s)) ||
        !field_run_long(s, 0xC000C7, LINE_EFFECTS, READS_THE_OVERFLOW, field_build_line_effect_tables, page_x8(s)))
        return;
    m_rep(s, CPU_FLAG_X);
    if (!bridge_call_long(s, 0xC000CD, PALETTE_ANIMATION, READS_THE_OVERFLOW, page(s)))
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
    if (bridge_call_long(s, 0xC000E6, PALETTE_ANIMATION, READS_NOTHING, page(s)))
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

/* Where the wait for the vblank goes on: an idle entry of its own
 * (field/frame.c), which reads the vblank's flag into A's low byte; the high
 * byte its callers may read is the count's, as the original leaves it
 * (field/leaves.c). */
enum { FRAME_WAIT_LOOP = 0xC0EC71 };

void field_finish_frame(Snes *s)
{
    GameState *g = state_of(s);
    finish_frame(g);
    m_sep(s, CPU_FLAG_M);
    s->cpu.a = field_frame_wait_leaves_a(g);
    m_stop_at(s, FRAME_WAIT_LOOP, DC_AL | DC_N | DC_Z);
}
