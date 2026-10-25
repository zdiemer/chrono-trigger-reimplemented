/* The layer over the world maps: how their scenes move it.
 *
 * The sprite-object mode shows a world map in its first two planes and a
 * layer over them (BG3). The scenes of four of the world maps start an
 * object that moves the layer every frame -- a slow drift, a drift by a
 * velocity of its own (one of them fast, over the 12,000 B.C. map), or a
 * drift with a wave down its lines -- and each
 * takes in the scroll the menu code has left pending (the map's own
 * scrolling moves the layer with it; menu/object_script.c). The wave's
 * values sit in a circular buffer, so the drift moves the wave's start round
 * it rather than rebuild it; the other wave, on another map's layer, is
 * menu/wave.c. While the mode holds its objects still, none of these moves.
 *
 * Each is an object's handler, never done. */
#include "menu/top_layer.h"

#include "menu/handlers.h"
#include "menu/line_effects.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/screen_objects.h"
#include "overworld/overworld.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* The scene holds still while the Epoch leaves through time. */
static bool held(const GameState *g) { return overworld_epoch_leaving_through_time(g); }

static void move_layer(GameState *g, int16_t across, int16_t down)
{
    set_menu_object_shadows_bg3_scroll_x(g, (uint16_t)(menu_object_shadows_bg3_scroll_x(g) + across));
    set_menu_object_shadows_bg3_scroll_y(g, (uint16_t)(menu_object_shadows_bg3_scroll_y(g) + down));
}

/* The scroll the menu code left pending, taken in and used up. */
static void take_pending_scroll(GameState *g)
{
    move_layer(g, (int16_t)menu_pending_scroll_x(g), (int16_t)menu_pending_scroll_y(g));
    set_menu_pending_scroll_x(g, 0);
    set_menu_pending_scroll_y(g, 0);
}

/* A count of the object's frames, in its slot. */
static uint16_t count_a_frame(GameState *g, MenuObject o)
{
    uint16_t frames = (uint16_t)(menu_object_slots_phase(g, o) + 1);
    set_menu_object_slots_phase(g, o, frames);
    return frames;
}

// $C2:75C3
/* A slow drift: a pixel left every fourth frame, and one down every eighth.
 * False where the objects are held. */
static bool slow_drift(GameState *g, MenuObject o)
{
    if (held(g))
        return false;
    uint16_t frames = count_a_frame(g, o);
    move_layer(g, frames % 4 == 0 ? -1 : 0, frames % 8 == 0 ? 1 : 0);
    take_pending_scroll(g);
    return true;
}

/* The drifts' velocities, 16.16 a frame: a little under four pixels left
 * and two down; and the fast one, a little over seventeen left and ten up. */
static const MenuPlace DRIFT = { 0xFFFC8931u, 0x00020000u };
static const MenuPlace FAST_DRIFT = { 0xFFEEADF4u, 0xFFF60000u };

// $C2:7702, $C2:77F2
/* A drift by a velocity: the object carries the layer's place, its fraction
 * of a pixel kept from frame to frame. */
static bool drift(GameState *g, MenuObject o, MenuPlace velocity)
{
    if (held(g))
        return false;
    if (menu_object_slots_step(g, o) == 0) {
        set_menu_object_slots_step(g, o, 1);
        menu_object_set_velocity(g, o, velocity);
    }
    set_menu_object_slots_x_pixel(g, o, menu_object_shadows_bg3_scroll_x(g));
    set_menu_object_slots_y_pixel(g, o, menu_object_shadows_bg3_scroll_y(g));
    menu_object_move(g, o);
    set_menu_object_shadows_bg3_scroll_x(g, menu_object_slots_x_pixel(g, o));
    set_menu_object_shadows_bg3_scroll_y(g, menu_object_slots_y_pixel(g, o));
    take_pending_scroll(g);
    return true;
}

/* The wave's circular buffer: 64 line pairs and a copy of them, so the
 * table can start its 224 lines anywhere in the first 64. */
enum { WAVE_PAIRS = 64, WAVE_BYTES = 2 * WAVE_PAIRS };
static uint16_t wave_first(void) { return (uint16_t)RAM_ABS(RAM_MENU_TOP_LAYER_WAVE_LINES); }

// $C2:75FD
/* The slow drift with the wave's start moved round its buffer as the layer
 * moves down: a pixel left and one down every fourth frame. */
static bool wave_drift(GameState *g, MenuObject o)
{
    if (held(g))
        return false;
    uint16_t frames = count_a_frame(g, o);
    if (frames % 4 == 0)
        move_layer(g, -1, 1);
    uint16_t pending_down = menu_pending_scroll_y(g);
    take_pending_scroll(g);
    uint16_t start = (uint16_t)(menu_top_layer_wave_at(g) + 2 * pending_down);
    if (start < wave_first())
        start = (uint16_t)(start + WAVE_BYTES);
    else if (start >= wave_first() + WAVE_BYTES)
        start = (uint16_t)(start - WAVE_BYTES);
    set_menu_top_layer_wave_at(g, start);
    return true;
}

/* The wave's table: three runs of 64 lines and one of 32, each reading its
 * values from where the vblank handler points it, on line-effect slot 1. */
enum { WAVE_SLOT = 1, RUN_OF_64_LINES = 0xC0, RUN_OF_32_LINES = 0xA0, WAVE_RUNS = 4 };
enum { WAVE_STEP = 0x10 };

/* A quarter of a signed value, rounded down, as an arithmetic shift does. */
static int16_t quarter(int16_t v) { return (int16_t)((uint16_t)v >> 2 | (v < 0 ? 0xC000 : 0)); }

static int16_t sine(const GameState *g, uint16_t phase)
{
    enum { STEPS = 0x400, HALF_TURN = 0x200 };
    uint16_t step = phase & (STEPS - 1);
    int16_t size = menu_wave_size(g, step);
    return step >= HALF_TURN ? (int16_t)-size : size;
}

/* What drawing the wave leaves in the mode's cells: its last value, and the
 * phase past it; and where its values are, long. */
static void leave_wave_drawn(GameState *g, int16_t last, uint16_t phase)
{
    enum { VALUE = 0x08, PHASE = 0x0A, VALUES_AT = 0x10 };
    set_menu_object_scratch_bytes_byte(g, VALUE, (uint8_t)last);
    set_menu_object_scratch_bytes_byte(g, VALUE + 1, (uint8_t)((uint16_t)last >> 8));
    set_menu_object_scratch_bytes_byte(g, PHASE, (uint8_t)phase);
    set_menu_object_scratch_bytes_byte(g, PHASE + 1, (uint8_t)(phase >> 8));
    set_menu_object_scratch_bytes_byte(g, VALUES_AT, (uint8_t)wave_first());
    set_menu_object_scratch_bytes_byte(g, VALUES_AT + 1, (uint8_t)(wave_first() >> 8));
    set_menu_object_scratch_bytes_byte(g, VALUES_AT + 2, (uint8_t)(RAM_MENU_TOP_LAYER_WAVE_LINES >> 16));
}

// $C2:7652
/* A wave down the layer's lines, redrawn every frame a step on from the
 * last: each line pair's scroll across is the layer's, and a quarter of a
 * sine. The wave goes off while the screen is, and holds while the objects
 * are held. False where it didn't draw. */
static bool wave(GameState *g, Platform *pf, MenuObject o)
{
    if (menu_screen_is_off(g)) {
        menu_turn_off_line_slots(g, 1u << WAVE_SLOT);
        return false;
    }
    if (held(g))
        return false;
    if (menu_object_slots_step(g, o) == 0) {
        set_menu_object_slots_step(g, o, 1);
        set_menu_object_slots_phase(g, o, 0);
        set_menu_top_layer_wave_at(g, wave_first());
    }
    static const uint8_t runs[WAVE_RUNS] = { RUN_OF_64_LINES, RUN_OF_64_LINES, RUN_OF_64_LINES, RUN_OF_32_LINES };
    for (unsigned r = 0; r < WAVE_RUNS; r++)
        set_menu_top_layer_wave_table(g, r, (menu_top_layer_wave_table(g, r) & 0xFFFF00u) | runs[r]);
    set_menu_top_layer_wave_table_end(g, 0);
    menu_program_line_channel(pf, 0xC2769A, WAVE_SLOT, VID_LINE_SCROLL_BG3_H, RAM_MENU_TOP_LAYER_WAVE_TABLE);
    menu_turn_on_line_slots(g, 1u << WAVE_SLOT);

    uint16_t phase = menu_object_slots_phase(g, o);
    set_menu_object_slots_phase(g, o, (uint16_t)(phase + 1));
    uint16_t layer = menu_object_shadows_bg3_scroll_x(g);
    int16_t last = 0;
    for (unsigned pair = 0; pair < WAVE_PAIRS; pair++, phase = (uint16_t)(phase + WAVE_STEP)) {
        last = sine(g, phase);
        set_menu_top_layer_wave_lines(g, pair, (uint16_t)(quarter(last) + layer));
    }
    for (unsigned pair = 0; pair < WAVE_PAIRS; pair++)
        set_menu_top_layer_wave_lines(g, WAVE_PAIRS + pair, menu_top_layer_wave_lines(g, pair));
    ORIGINAL_EFFECT(leave_wave_drawn(g, last, phase));
    return true;
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* They run on the mode's page with the data bank on the I/O banks, each
 * entered with its slot in X. */
bool menu_top_layer_object_runs(Snes *s)
{
    if (s->cpu.d != RAM_PAGE_MENU || s->cpu.db != 0x00)
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_object_address_names_one(menu_object_current(g)) && s->cpu.x == menu_object_current(g);
    vars_peek(false);
    return ok;
}

/* Each hands back that it goes on, with the carry clear, and 16 bits wide
 * where it moved the layer. */
static void goes_on(Snes *s, bool moved)
{
    m_set_flag(s, CPU_FLAG_C, false);
    if (moved)
        m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:75C3
void menu_top_layer_slow_drift(Snes *s)
{
    GameState *g = state_of(s);
    goes_on(s, slow_drift(g, menu_object_running(g)));
}

// $C2:7702
void menu_top_layer_drift(Snes *s)
{
    GameState *g = state_of(s);
    goes_on(s, drift(g, menu_object_running(g), DRIFT));
}

// $C2:77F2
void menu_top_layer_fast_drift(Snes *s)
{
    GameState *g = state_of(s);
    goes_on(s, drift(g, menu_object_running(g), FAST_DRIFT));
}

// $C2:75FD
void menu_top_layer_wave_drift(Snes *s)
{
    GameState *g = state_of(s);
    goes_on(s, wave_drift(g, menu_object_running(g)));
}

// $C2:7652
void menu_top_layer_wave(Snes *s)
{
    GameState *g = state_of(s);
    goes_on(s, wave(g, m_platform(s), menu_object_running(g)));
}

/* ---- As C ------------------------------------------------------------------------------------ */

/* Each takes its turn and goes on, never done (menu/handlers.c). */
MenuTurn menu_top_layer_slow_drift_turn(GameState *g, Platform *pf, MenuObject o)
{
    (void)pf;
    (void)slow_drift(g, o);
    return MENU_TURN_GOES_ON;
}

MenuTurn menu_top_layer_drift_turn(GameState *g, Platform *pf, MenuObject o)
{
    (void)pf;
    (void)drift(g, o, DRIFT);
    return MENU_TURN_GOES_ON;
}

MenuTurn menu_top_layer_fast_drift_turn(GameState *g, Platform *pf, MenuObject o)
{
    (void)pf;
    (void)drift(g, o, FAST_DRIFT);
    return MENU_TURN_GOES_ON;
}

MenuTurn menu_top_layer_wave_drift_turn(GameState *g, Platform *pf, MenuObject o)
{
    (void)pf;
    (void)wave_drift(g, o);
    return MENU_TURN_GOES_ON;
}

MenuTurn menu_top_layer_wave_turn(GameState *g, Platform *pf, MenuObject o)
{
    (void)wave(g, pf, o);
    return MENU_TURN_GOES_ON;
}
