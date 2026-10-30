/* The sprite-object mode's objects as the program runs them with no machine
 * (menu/handlers.h): the handlers by the address the slots name them by, the
 * command scripts' runner with its services, and the routines the scripts
 * call by address.
 *
 * Each is the same function of the game's state the machine's entry runs;
 * what differs is only how it is reached. The machine's entries check a
 * guard before they run, which hands a state the port doesn't take to the
 * original; here the guard's state half is checked first, and a state it
 * refuses -- or a handler, command or routine with no C yet -- stops the
 * program where it is met. */
#include "menu/handlers.h"

#include "charge.h"
#include "core/program.h"
#include "menu/map.h"
#include "menu/map_scenes.h"
#include "menu/menu_data.h"
#include "menu/screen_objects.h"
#include "menu/script.h"
#include "menu/script_call.h"
#include "menu/top_layer.h"
#include "overworld/music.h"
#include "platform/video.h"
#include "vars.h"

/* Handlers and routines are reached in bank C2 unless a far call names
 * another bank. */
enum { HANDLERS_BANK = 0xC20000 };

/* ---- The command scripts ------------------------------------------------------------------ */

/* The runner's state while a script runs: the machine it runs for, and the
 * command running (a register of the picture's a command sets is written as
 * that command's handler writes it). */
typedef struct {
    GameState *game;
    Platform *pf;
    uint8_t opcode;
    ScriptServices services;
} ObjectScriptContext;

static uint32_t handler_of(const GameState *g, uint8_t opcode) { return HANDLERS_BANK | menu_script_handler(g, opcode); }

/* The map's planes scroll as C where the scroll's guard takes the plane and
 * the strips it queues. */
static bool scroll_map(void *ctx, MenuAxis axis, uint8_t plane, int8_t step)
{
    ObjectScriptContext *c = ctx;
    uint32_t routine = axis == MENU_ACROSS ? 0xC20568 : 0xC2066C;
    if (!menu_map_can_scroll(c->game, plane, menu_map_scroll_records(axis, step))) {
        program_needs_the_machine(c->game, routine, "a scroll of the map the port doesn't take");
        return false;
    }
    TIMED_AS(c->game, routine, menu_map_scroll(c->game, plane, axis, step));
    return true;
}

/* A script names a plane 1-3; the first two are the map's. */
static bool redraw_map(void *ctx, uint8_t plane)
{
    ObjectScriptContext *c = ctx;
    uint8_t named = (uint8_t)(plane & 3);
    if (named == 0 || !menu_map_can_redraw(c->game, (uint8_t)(named - 1))) {
        program_needs_the_machine(c->game, 0xC209C5, "a redraw of the map the port doesn't take");
        return false;
    }
    menu_map_redraw_plane(c->game, c->pf, (uint8_t)(named - 1));
    return true;
}

static bool pass_on_sound(void *ctx)
{
    ObjectScriptContext *c = ctx;
    TIMED_AS(c->game, 0xC22ED9, (void)overworld_pass_on_sound(c->game));
    return true;
}

static const MenuScriptRoutine *script_routine(uint32_t routine);

static bool call_routine(void *ctx, uint32_t routine, bool far)
{
    ObjectScriptContext *c = ctx;
    uint32_t at = far ? routine & 0xFFFFFFu : HANDLERS_BANK | (uint16_t)routine;
    const MenuScriptRoutine *r = script_routine(at);
    MenuObject self = menu_object_running(c->game);
    if (!r || (r->takes && !r->takes(c->game, self))) {
        program_needs_the_machine(c->game, at, r ? r->name : "a routine a script calls with no C");
        return false;
    }
    bool back = false;
    TIMED_AS(c->game, at, back = r->run(c->game, c->pf, self, far));
    return back;
}

static void set_console_byte(void *ctx, uint16_t at, uint8_t v)
{
    ObjectScriptContext *c = ctx;
    script_set_the_consoles_byte(c->pf, handler_of(c->game, c->opcode), at, v);
}

/* Each command where its row says the port runs it as the original does; the
 * script pointer on it, as the routines the commands reach read it. */
static ScriptFlow run_a_command(void *ctx, MenuObject self, uint32_t at)
{
    ObjectScriptContext *c = ctx;
    GameState *g = c->game;
    set_menu_object_script(g, at);
    c->opcode = menu_script_byte(g, at);
    const ScriptOpcode *row = menu_script_opcode(c->opcode);
    ScriptCommand command = { 0 };
    if (row)
        command = menu_script_decode(g, row, self, at, &c->services);
    if (!row || !menu_script_runs_here(g, row, &command)) {
        program_needs_the_machine(g, row ? handler_of(g, c->opcode) : 0xC20F63, "a script command the port doesn't run");
        return handed_back();
    }
    ScriptFlow f = handed_back();
    TIMED_AS(g, handler_of(g, c->opcode), f = menu_script_perform(g, row, &command));
    return f;
}

MenuTurn menu_run_object_script(GameState *g, Platform *pf, MenuObject self)
{
    ObjectScriptContext c = { g, pf, 0, { 0 } };
    c.services = (ScriptServices){ scroll_map, redraw_map, pass_on_sound, call_routine, set_console_byte, &c };
    ScriptRunner r = { run_a_command, &c };
    uint32_t stopped = 0;
    switch (menu_run_script(g, self, &r, &stopped)) {
    case SCRIPT_DONE: return MENU_TURN_DONE;
    case SCRIPT_WAITING: return MENU_TURN_GOES_ON;
    default: return MENU_TURN_WENT_NO_FURTHER;
    }
}

/* ---- The handlers ------------------------------------------------------------------------- */

static MenuTurn run_the_script(GameState *g, Platform *pf, MenuObject self)
{
    MenuTurn t = MENU_TURN_WENT_NO_FURTHER;
    TIMED_AS(g, HANDLERS_BANK | MENU_SCRIPT_HANDLER, t = menu_run_object_script(g, pf, self));
    return t;
}

static bool script_takes(const GameState *g, MenuObject self)
{
    return menu_object_script_can_run(g, menu_object_slots_script(g, self));
}

/* The screen's objects say themselves when they are done. */
#define DONE_WHEN(fn)                                                       \
    static MenuTurn fn##_turn(GameState *g, Platform *pf, MenuObject self) \
    {                                                                       \
        (void)pf;                                                           \
        return menu_turn_done_if(fn(g, self));                              \
    }
DONE_WHEN(menu_stream_tiles)
DONE_WHEN(menu_load_palette_row)
DONE_WHEN(menu_fade_palette_row)
DONE_WHEN(menu_fade_out)
DONE_WHEN(menu_fade_in)
DONE_WHEN(menu_clear_mosaic)
DONE_WHEN(menu_grow_mosaic)

/* The scenes' objects that never finish of themselves. */
static MenuTurn drift_the_map(GameState *g, Platform *pf, MenuObject self)
{
    (void)pf;
    menu_scene_drift_the_map(g, self);
    return MENU_TURN_GOES_ON;
}

MENU_HANDLERS(menu_handlers,
    { MENU_SCRIPT_HANDLER, "a command script", script_takes, run_the_script },
    { MENU_TILE_STREAMER, "the tile streamer", menu_tile_streamer_takes, menu_stream_tiles_turn },
    { MENU_PALETTE_LOADER, "a palette row's load", menu_palette_loader_takes, menu_load_palette_row_turn },
    { MENU_PALETTE_FADE, "a palette row's fade", menu_palette_fade_takes, menu_fade_palette_row_turn },
    { MENU_FADE_OUT, "the screen's fade out", menu_fade_out_takes, menu_fade_out_turn },
    { MENU_FADE_IN, "the screen's fade in", menu_fade_in_takes, menu_fade_in_turn },
    { MENU_MOSAIC_CLEARS, "the mosaic clearing", menu_mosaic_takes, menu_clear_mosaic_turn },
    { MENU_MOSAIC_GROWS, "the mosaic growing", menu_mosaic_takes, menu_grow_mosaic_turn },
    { 0x75C3, "the layer over the map, drifting slowly", NULL, menu_top_layer_slow_drift_turn },
    { 0x75FD, "the layer over the map, drifting in a wave", NULL, menu_top_layer_wave_drift_turn },
    { 0x7652, "the wave down the layer over the map", NULL, menu_top_layer_wave_turn },
    { 0x7702, "the layer over the map, drifting", NULL, menu_top_layer_drift_turn },
    { 0x7759, "the layer over the map, shimmering", NULL, menu_shimmer_turn },
    { 0x77F2, "the layer over the map, drifting fast", NULL, menu_top_layer_fast_drift_turn },
    { 0x7849, "the map drifting along", menu_scene_drift_takes, drift_the_map },
);

static const MenuHandlers *const HANDLER_TABLES[] = { &menu_handlers, &menu_epoch_handlers, &overworld_handlers };
enum { HANDLER_TABLES_COUNT = sizeof HANDLER_TABLES / sizeof HANDLER_TABLES[0] };

const MenuHandler *menu_handler(uint16_t handler)
{
    for (unsigned t = 0; t < HANDLER_TABLES_COUNT; t++)
        for (unsigned i = 0; i < HANDLER_TABLES[t]->count; i++)
            if (HANDLER_TABLES[t]->rows[i].handler == handler)
                return &HANDLER_TABLES[t]->rows[i];
    return NULL;
}

/* ---- The routines the scripts call ----------------------------------------------------------- */

static const MenuScriptRoutines *const ROUTINE_TABLES[] = {
    &menu_scene_routines,
    &menu_line_effect_routines,
    &menu_map_scene_routines,
    &menu_map_view_routines,
    &menu_turning_world_routines,
    &overworld_routines,
};
enum { ROUTINE_TABLES_COUNT = sizeof ROUTINE_TABLES / sizeof ROUTINE_TABLES[0] };

static const MenuScriptRoutine *script_routine(uint32_t routine)
{
    for (unsigned t = 0; t < ROUTINE_TABLES_COUNT; t++)
        for (unsigned i = 0; i < ROUTINE_TABLES[t]->count; i++)
            if (ROUTINE_TABLES[t]->rows[i].routine == routine)
                return &ROUTINE_TABLES[t]->rows[i];
    return NULL;
}
