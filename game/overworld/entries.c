/* The overworld's machine forms: the routines its callers still under the
 * byte-exact gate run on the original's machine, and the contracts they keep.
 *
 * The overworld is off the gate (docs/REIMPLEMENTATION.md, "The overworld's
 * teardown"): game/registry.c lists none of its routines, the machine enters
 * none of them as ports, and in the shipping mode its work runs as C. Two of
 * its callers are the menus' and still under the gate, and their machine paths
 * run a routine where the original calls it, under the frame its call pushes,
 * with the registers it leaves: the mode's object loop, which calls each
 * object's handler with its slot in X (menu/objects.c), and the command
 * scripts' bridge, which calls the routines a scene's script names
 * (menu/script_bridge.c). Where they asked the registry for the port, they ask
 * here (overworld_machine_form), and elsewhere -- shadow mode's reference
 * play, the tools -- they make the original's call, as the cartridge's code
 * runs there, compared with nothing.
 *
 * Each contract is what the routine's registry row held: the registers'
 * widths and binary arithmetic, and the guard, the handler's machine half
 * (its slot in X, on the mode's page) and the state half overworld/handlers.c
 * has, which the program checks too. A guard turned away what the port didn't
 * take, and the original ran it; now it is an assertion, a claim that the game
 * never makes such a state, and a state that breaks it is reported
 * (program_breaks_a_contract) while the call still goes to the machine.
 *
 * A handler hands back the carry: done or not, the only thing the loop reads
 * of what it leaves (the loop sets the accumulator's width again before it
 * uses it, $C2:0545, $C2:054C). A walker runs an event's script through the
 * script interpreter's bridge (menu/script_bridge.h), called as from the
 * walker's own call to it. */
#include "overworld/entries.h"

#include "core/program.h"
#include "menu/map.h"
#include "menu/objects.h"
#include "menu/script_call.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "overworld/frames.h"
#include "overworld/handlers.h"
#include "overworld/music.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "overworld/setup.h"
#include "overworld/sign.h"
#include "overworld/vehicles.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- What the handlers leave --------------------------------------------------------------- */

static void handler_returns(Snes *s, bool done)
{
    m_set_flag(s, CPU_FLAG_C, done);
    m_rts(s);
}

static void handler_goes_on(Snes *s) { handler_returns(s, false); }

/* ---- A walker's script, on the machine ----------------------------------------------------- */

/* Where each walker calls the interpreter from: as it takes its script up,
 * and as it goes on with it. */
typedef struct {
    Snes *s;
    uint32_t calls[2];
} WalkerOnTheMachine;

static const uint32_t LEADER_CALLS[2] = { [WALKER_STARTS_ITS_SCRIPT] = 0xC2378B,
                                          [WALKER_GOES_ON_WITH_ITS_SCRIPT] = 0xC237A1 };
static const uint32_t FOLLOWER_CALLS[2] = { [WALKER_STARTS_ITS_SCRIPT] = 0xC23DBA,
                                            [WALKER_GOES_ON_WITH_ITS_SCRIPT] = 0xC23DC1 };

/* The walker's step table hands the interpreter an 8-bit accumulator and the
 * overflow clear from working out its row, which the first command's call of
 * a routine a script names would see. */
static WalkerScriptEnd run_on_the_machine(void *ctx, WalkerScriptRun run)
{
    WalkerOnTheMachine *w = ctx;
    m_sep(w->s, CPU_FLAG_M);
    m_set_flag(w->s, CPU_FLAG_V, false);
    switch (menu_run_object_script_from(w->s, w->calls[run])) {
    case SCRIPT_DONE: return WALKER_SCRIPT_DONE;
    case SCRIPT_GAVE_BACK: return WALKER_SCRIPT_HANDED_OVER;
    default: return WALKER_SCRIPT_WAITS;
    }
}

static WalkerScripts on_the_machine(WalkerOnTheMachine *w, Snes *s, const uint32_t calls[2])
{
    *w = (WalkerOnTheMachine){ s, { calls[0], calls[1] } };
    return (WalkerScripts){ run_on_the_machine, w };
}

// $C2:3404
static void lead(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject leader = menu_object_running(g);
    WalkerOnTheMachine w;
    WalkerScripts scripts = on_the_machine(&w, s, LEADER_CALLS);
    if (overworld_lead(g, leader, &scripts))
        handler_goes_on(s);
}

// $C2:3AE2
static void follow(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject self = menu_object_running(g);
    WalkerOnTheMachine w;
    WalkerScripts scripts = on_the_machine(&w, s, FOLLOWER_CALLS);
    if (overworld_follow(g, self, &scripts))
        handler_goes_on(s);
}

// $C2:3264
static void watch_the_ground(Snes *s)
{
    overworld_watch_the_ground(state_of(s));
    handler_goes_on(s);
}

// $C2:2F94
static void region_song(Snes *s)
{
    GameState *g = state_of(s);
    overworld_play_the_region_song(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:5628
static void sign(Snes *s)
{
    GameState *g = state_of(s);
    overworld_show_the_sign(g, menu_object_running(g));
    handler_goes_on(s);
}

/* ---- The vehicles -------------------------------------------------------------------------- */

// $C2:309E
static void vehicles(Snes *s)
{
    overworld_control_the_vehicles(state_of(s));
    handler_goes_on(s);
}

/* The Epoch runs its scenes' scripts through the interpreter's bridge, as
 * its own call at $C2:4823 calls it, with the accumulator its step's
 * dispatch leaves 8 bits wide. */
static ScriptEnd epoch_scene_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_sep(s, CPU_FLAG_M);
    return menu_run_object_script_from(s, 0xC24823);
}

// $C2:42DD
static void epoch(Snes *s)
{
    GameState *g = state_of(s);
    EpochScripts scripts = { epoch_scene_on_the_machine, s };
    if (overworld_fly_the_epoch(g, menu_object_running(g), &scripts))
        handler_goes_on(s);
}

// $C2:495E
static void epoch_second_sprite(Snes *s)
{
    GameState *g = state_of(s);
    handler_returns(s, overworld_draw_the_epoch_s_second_sprite(g, menu_object_running(g)));
}

// $C2:49CF
static void black_omen_watch(Snes *s)
{
    overworld_watch_for_the_black_omen(state_of(s));
    handler_goes_on(s);
}

/* ---- The dactyls ---------------------------------------------------------------------------- */

// $C2:4CC5
static void dactyl(Snes *s)
{
    GameState *g = state_of(s);
    overworld_fly_the_dactyl(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:504B
static void dactyl_second_sprite(Snes *s)
{
    GameState *g = state_of(s);
    overworld_draw_the_dactyl_s_second_sprite(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:5097
static void following_dactyl(Snes *s)
{
    GameState *g = state_of(s);
    overworld_fly_a_following_dactyl(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:5494
static void following_dactyl_second_sprite(Snes *s)
{
    GameState *g = state_of(s);
    overworld_draw_a_following_dactyl_s_second_sprite(g, menu_object_running(g));
    handler_goes_on(s);
}

/* ---- The routines a scene's script calls ---------------------------------------------------- */

// $C2:48A9
static void show_the_epoch_flying(Snes *s)
{
    GameState *g = state_of(s);
    overworld_show_the_epoch_flying(g, menu_object_running(g));
    m_rts(s);
}

// $C2:48CD
static void carry_the_epoch_round(Snes *s)
{
    GameState *g = state_of(s);
    overworld_carry_the_epoch_round(g, menu_object_running(g));
    m_rts(s);
}

// $C2:2F0F
static void start_the_location_song(Snes *s)
{
    overworld_start_the_location_song(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:5798
/* It comes back with an 8-bit accumulator. */
static void clear_the_sign_label(Snes *s)
{
    overworld_clear_the_sign_label(state_of(s));
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* ---- The contracts -------------------------------------------------------------------------- */

/* Each handler's guard is its slot in X on the mode's page (the machine
 * half) and the state half its row in overworld/handlers.c has. */
static bool leader_takes(Snes *s) { return menu_object_handler_takes(s, overworld_leader_takes); }
static bool follower_takes(Snes *s) { return menu_object_handler_takes(s, overworld_follower_takes); }
static bool watcher_takes(Snes *s) { return menu_object_handler_takes(s, overworld_watcher_takes); }
static bool region_song_takes(Snes *s) { return menu_object_handler_takes(s, overworld_region_song_takes); }
static bool sign_takes(Snes *s) { return menu_object_handler_takes(s, overworld_sign_takes); }
static bool vehicles_take(Snes *s) { return menu_object_handler_takes(s, overworld_vehicles_take); }
static bool epoch_takes(Snes *s) { return menu_object_handler_takes(s, overworld_epoch_takes); }
static bool epoch_second_sprite_takes(Snes *s) { return menu_object_handler_takes(s, overworld_epoch_second_sprite_takes); }
static bool dactyl_takes(Snes *s) { return menu_object_handler_takes(s, overworld_dactyl_takes); }
static bool dactyl_second_sprite_takes(Snes *s) { return menu_object_handler_takes(s, overworld_dactyl_second_sprite_takes); }

/* The followers read the heading's and the sine's tables through the data
 * bank, which reaches the ROM's first bank. */
static bool following_dactyl_takes(Snes *s)
{
    return m_bank_is_zero(s) && menu_object_handler_takes(s, overworld_following_dactyl_takes);
}

static bool following_dactyl_second_sprite_takes(Snes *s)
{
    return menu_object_handler_takes(s, overworld_following_dactyl_second_sprite_takes);
}

static bool black_omen_watch_takes(Snes *s) { return menu_object_handler_runs(s); }

/* The routines the Epoch's scenes call take the running object on the mode's
 * page, moving where its state says it can. */
static bool epoch_scene_call_takes(Snes *s)
{
    if (!menu_object_can_move(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = overworld_epoch_scene_call_takes(g, menu_object_running(g));
    vars_peek(false);
    return ok;
}

/* The song's look-up runs on the mode's page. */
static bool location_song_takes(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = overworld_location_song_takes(g, menu_object_running(g));
    vars_peek(false);
    return ok;
}

/* 8-bit A, 16-bit index registers, binary arithmetic; 16-bit index registers
 * and binary arithmetic, A either width. */
enum {
    M8_X16_BINARY = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D,
    X16_BINARY = CPU_FLAG_X | CPU_FLAG_D,
};

typedef struct {
    uint32_t routine;
    void (*form)(Snes *s);
    uint8_t p_mask, p_value;
    bool (*holds)(Snes *s);
    const char *what;
} MachineForm;

static const MachineForm FORMS[] = {
    /* the objects' handlers */
    { 0xC23404, lead, M8_X16_BINARY, CPU_FLAG_M, leader_takes, "the leader outside its contract" },
    { 0xC23AE2, follow, M8_X16_BINARY, CPU_FLAG_M, follower_takes, "a follower outside its contract" },
    { 0xC23264, watch_the_ground, M8_X16_BINARY, CPU_FLAG_M, watcher_takes, "the places' watcher outside its contract" },
    { 0xC22F94, region_song, M8_X16_BINARY, CPU_FLAG_M, region_song_takes, "the region's song outside its contract" },
    { 0xC25628, sign, M8_X16_BINARY, CPU_FLAG_M, sign_takes, "the sign outside its contract" },
    { 0xC2309E, vehicles, M8_X16_BINARY, CPU_FLAG_M, vehicles_take, "the vehicles outside their contract" },
    { 0xC242DD, epoch, M8_X16_BINARY, CPU_FLAG_M, epoch_takes, "the Epoch outside its contract" },
    { 0xC2495E, epoch_second_sprite, M8_X16_BINARY, CPU_FLAG_M, epoch_second_sprite_takes,
      "the Epoch's second sprite outside its contract" },
    { 0xC24CC5, dactyl, M8_X16_BINARY, CPU_FLAG_M, dactyl_takes, "the dactyl outside its contract" },
    { 0xC2504B, dactyl_second_sprite, M8_X16_BINARY, CPU_FLAG_M, dactyl_second_sprite_takes,
      "the dactyl's second sprite outside its contract" },
    { 0xC25097, following_dactyl, M8_X16_BINARY, CPU_FLAG_M, following_dactyl_takes,
      "a following dactyl outside its contract" },
    { 0xC25494, following_dactyl_second_sprite, M8_X16_BINARY, CPU_FLAG_M, following_dactyl_second_sprite_takes,
      "a following dactyl's second sprite outside its contract" },
    { 0xC249CF, black_omen_watch, M8_X16_BINARY, CPU_FLAG_M, black_omen_watch_takes,
      "the Black Omen's watch outside its contract" },
    /* the routines a scene's script calls */
    { 0xC248A9, show_the_epoch_flying, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, epoch_scene_call_takes,
      "the Epoch shown flying outside its contract" },
    { 0xC248CD, carry_the_epoch_round, X16_BINARY, 0, epoch_scene_call_takes,
      "the Epoch carried round outside its contract" },
    { 0xC22F0F, start_the_location_song, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, location_song_takes,
      "the location's song outside its contract" },
    { 0xC25798, clear_the_sign_label, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_map_page_is_menu,
      "the sign's label cleared outside its contract" },
    { 0xC227DE, overworld_load_the_map_tiles_on_the_machine, M8_X16_BINARY, CPU_FLAG_M, overworld_map_tiles_run,
      "the map's tiles loaded outside their contract" },
};

/* Selection alone; callers check the full contract on the constructed frame. */
void (*overworld_machine_entry(uint32_t routine))(Snes *)
{
    for (unsigned i = 0; i < sizeof FORMS / sizeof FORMS[0]; i++)
        if (FORMS[i].routine == routine)
            return FORMS[i].form;
    return NULL;
}

void (*overworld_machine_form(Snes *s, uint32_t routine))(Snes *s)
{
    if (!m_runs_retired_as_c(s))
        return NULL;
    for (unsigned i = 0; i < sizeof FORMS / sizeof FORMS[0]; i++) {
        const MachineForm *f = &FORMS[i];
        if (f->routine != routine)
            continue;
        if (!s->cpu.e && (s->cpu.p & f->p_mask) == f->p_value && f->holds(s))
            return f->form;
        program_breaks_a_contract(state_of(s), routine, f->what);
        return NULL;
    }
    return NULL;
}
