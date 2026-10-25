/* The overworld on the original's machine: the objects' handlers and the main
 * loop as their registered entries, what each leaves the original's code,
 * and the guards that keep them to the states the game makes.
 *
 * Each object's handler is entered from the mode's object loop
 * (menu/objects.c) with its slot in X and an 8-bit accumulator, and hands
 * back the carry: done or not, the only thing the loop reads of what it
 * leaves (the registry declares the rest, the accumulator's width among
 * them). A walker runs an event's script through the script
 * interpreter's bridge (menu/script_bridge.h), called as from the walker's
 * own call to it. */
#include "overworld/entries.h"

#include "menu/map.h"
#include "menu/objects.h"
#include "menu/script_call.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "overworld/frames.h"
#include "overworld/handlers.h"
#include "overworld/main_loop.h"
#include "overworld/music.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "overworld/sign.h"
#include "overworld/terrain.h"
#include "overworld/vehicles.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- What the handlers leave --------------------------------------------------------------- */

/* A handler says whether it is done with the carry, which the objects' loop
 * reads; the loop sets the accumulator's width again before it uses it
 * ($C2:0545, $C2:054C), so the width a handler leaves is declared don't-care
 * (DC_WIDTH) and the wrappers leave it as it is. */
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
void overworld_lead_entry(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject leader = menu_object_running(g);
    WalkerOnTheMachine w;
    WalkerScripts scripts = on_the_machine(&w, s, LEADER_CALLS);
    if (overworld_lead(g, leader, &scripts))
        handler_goes_on(s);
}

// $C2:3AE2
void overworld_follow_entry(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject self = menu_object_running(g);
    WalkerOnTheMachine w;
    WalkerScripts scripts = on_the_machine(&w, s, FOLLOWER_CALLS);
    if (overworld_follow(g, self, &scripts))
        handler_goes_on(s);
}

// $C2:3264
void overworld_watch_the_ground_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_watch_the_ground(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:2F94
void overworld_region_song_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_play_the_region_song(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:5628
void overworld_sign_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_show_the_sign(g, menu_object_running(g));
    handler_goes_on(s);
}

/* ---- The vehicles -------------------------------------------------------------------------- */

// $C2:309E
void overworld_vehicles_entry(Snes *s)
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
void overworld_epoch_entry(Snes *s)
{
    GameState *g = state_of(s);
    EpochScripts scripts = { epoch_scene_on_the_machine, s };
    if (overworld_fly_the_epoch(g, menu_object_running(g), &scripts))
        handler_goes_on(s);
}

// $C2:495E
void overworld_epoch_second_sprite_entry(Snes *s)
{
    GameState *g = state_of(s);
    handler_returns(s, overworld_draw_the_epoch_s_second_sprite(g, menu_object_running(g)));
}

// $C2:49CF
void overworld_black_omen_watch_entry(Snes *s)
{
    overworld_watch_for_the_black_omen(state_of(s));
    handler_goes_on(s);
}

/* ---- The dactyls ---------------------------------------------------------------------------- */

// $C2:4CC5
void overworld_dactyl_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_fly_the_dactyl(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:504B
void overworld_dactyl_second_sprite_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_draw_the_dactyl_s_second_sprite(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:5097
void overworld_following_dactyl_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_fly_a_following_dactyl(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:5494
void overworld_following_dactyl_second_sprite_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_draw_a_following_dactyl_s_second_sprite(g, menu_object_running(g));
    handler_goes_on(s);
}

/* The test's operands are in the mode's cells: the two spots, and long
 * pointers to their reaches' records. It answers in the carry, which its
 * callers branch on; and it leaves the accumulator 16 bits wide, which
 * $C2:4917's caller goes on with, reading the pad at $C2:44CE. */
static ReachRecord reach_record(uint32_t pointer) { return (ReachRecord)(uint16_t)pointer; }

// $C2:2345
void overworld_within_reach_entry(Snes *s)
{
    GameState *g = state_of(s);
    WorldSpot first = { overworld_scratch_reach_first_x(g), overworld_scratch_reach_first_y(g) };
    WorldSpot second = { overworld_scratch_reach_second_x(g), overworld_scratch_reach_second_y(g) };
    bool within = overworld_within_reach(g, first, reach_record(overworld_scratch_reach_first(g)), second,
                                         reach_record(overworld_scratch_reach_second(g)));
    m_set_flag(s, CPU_FLAG_C, within);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

/* The routines the Epoch's scenes' scripts call leave the running object's
 * slot in X and a carry: the script's call command goes on to its length,
 * 3, and the interpreter after it reads neither (it loads X again and clears
 * the carry for the next command), but liveness follows the interpreter's
 * branch for a length of 0, which the call commands never return, and keeps
 * them live there: they are left for shadow mode's comparison at the
 * command's return. */
static void leave_a_scene_call(Snes *s, bool carry)
{
    s->cpu.x = menu_object_current(state_of(s));
    m_set_flag(s, CPU_FLAG_C, carry);
}

// $C2:48A9
/* Its carry is its sprite script's place doubled, clear for the game's. */
void overworld_show_the_epoch_flying_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_show_the_epoch_flying(g, menu_object_running(g));
    ORIGINAL_EFFECT(leave_a_scene_call(s, false));
    m_rts(s);
}

/* The wrap onto the map leaves the carry of its last sum: carried round
 * forward, set where it went past the map's right edge (so it is now in the
 * map's first third); carried back, set where it went past the left (so it
 * is now in the last third). */
static bool wrapped_round(const GameState *g, MenuObject epoch)
{
    uint16_t across = menu_object_slots_x_pixel(g, epoch);
    if (overworld_pace(g, epoch, MENU_ACROSS) < 0)
        return across >= MENU_MAP_WIDTH - EPOCH_CARRIED_ROUND;
    return across < EPOCH_CARRIED_ROUND;
}

// $C2:48CD
void overworld_carry_the_epoch_round_entry(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject self = menu_object_running(g);
    overworld_carry_the_epoch_round(g, self);
    ORIGINAL_EFFECT(leave_a_scene_call(s, wrapped_round(g, self)));
    m_rts(s);
}

// $C2:23CF
void overworld_main_loop_entry(Snes *s) { overworld_run_a_frame(s); }

// $C2:255A
void overworld_back_from_the_menus_entry(Snes *s) { overworld_come_back_from_the_menus(s); }

/* ---- The guards ---------------------------------------------------------------------------- */

/* Each is the handler's machine half (its slot in X, on the mode's page) and
 * the state half overworld/handlers.c has, which the program checks too. */
bool overworld_leader_runs(Snes *s) { return menu_object_handler_takes(s, overworld_leader_takes); }
bool overworld_follower_runs(Snes *s) { return menu_object_handler_takes(s, overworld_follower_takes); }
bool overworld_watcher_runs(Snes *s) { return menu_object_handler_takes(s, overworld_watcher_takes); }
bool overworld_region_song_runs(Snes *s) { return menu_object_handler_takes(s, overworld_region_song_takes); }
bool overworld_sign_runs(Snes *s) { return menu_object_handler_takes(s, overworld_sign_takes); }
bool overworld_vehicles_run(Snes *s) { return menu_object_handler_takes(s, overworld_vehicles_take); }
bool overworld_epoch_runs(Snes *s) { return menu_object_handler_takes(s, overworld_epoch_takes); }
bool overworld_epoch_second_sprite_runs(Snes *s) { return menu_object_handler_takes(s, overworld_epoch_second_sprite_takes); }
bool overworld_dactyl_runs(Snes *s) { return menu_object_handler_takes(s, overworld_dactyl_takes); }
bool overworld_dactyl_second_sprite_runs(Snes *s)
{
    return menu_object_handler_takes(s, overworld_dactyl_second_sprite_takes);
}

/* The followers read the heading's and the sine's tables through the data
 * bank, which reaches the ROM's first bank. */
bool overworld_following_dactyl_runs(Snes *s)
{
    return m_bank_is_zero(s) && menu_object_handler_takes(s, overworld_following_dactyl_takes);
}

bool overworld_following_dactyl_second_sprite_runs(Snes *s)
{
    return menu_object_handler_takes(s, overworld_following_dactyl_second_sprite_takes);
}

bool overworld_black_omen_watch_runs(Snes *s) { return menu_object_handler_runs(s); }

bool overworld_reach_test_runs(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    bool ok = overworld_reach_test_takes(state_of(s));
    vars_peek(false);
    return ok;
}

bool overworld_epoch_scene_call_runs(Snes *s)
{
    if (!menu_object_can_move(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = overworld_epoch_scene_call_takes(g, menu_object_running(g));
    vars_peek(false);
    return ok;
}

/* The loop runs on the mode's page with the data bank on low RAM. */
bool overworld_main_loop_runs(Snes *s) { return menu_objects_on_page(s); }

/* ---- The location's song, from a scene's script ------------------------------------------ */

/* The script's call command passes X and the carry on, so the wrapper leaves
 * them as the lookup did: X the region (the song table's index), the carry
 * the lookup's last shift, and A's high byte the region word's, clear (the
 * driver hands them back as it found them). A song a screen keeps leaves
 * them as they came. */
static void leave_the_lookup(void *ctx, LocationSong song)
{
    Snes *s = ctx;
    if (!song.of_a_region)
        return;
    s->cpu.x = song.region;
    s->cpu.a = (uint16_t)(s->cpu.a & 0x00FF);
    m_set_flag(s, CPU_FLAG_C, overworld_region_lookup_carry_at_the_location(state_of(s)));
}

// $C2:2F0F
static void looked_up(void *ctx, LocationSong song)
{
    ORIGINAL_EFFECT(leave_the_lookup(ctx, song));
}

void overworld_start_the_location_song_entry(Snes *s)
{
    overworld_start_the_location_song(state_of(s), m_platform(s), looked_up, s);
    m_rts(s);
}

/* It runs on the mode's page. */
bool overworld_location_song_runs(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = overworld_location_song_takes(g, menu_object_running(g));
    vars_peek(false);
    return ok;
}

/* ---- The sound request, from the original's callers ---------------------------------------- */

// $C2:2ED9
/* The carry says the request couldn't be passed on; X is left as it came. */
void overworld_pass_on_sound_entry(Snes *s)
{
    m_set_flag(s, CPU_FLAG_C, !overworld_pass_on_sound(state_of(s)));
    m_rts(s);
}

// $C2:5798
/* Its block move leaves nothing a caller reads (the registry declares it),
 * and an 8-bit accumulator. */
void overworld_clear_the_sign_label_entry(Snes *s)
{
    overworld_clear_the_sign_label(state_of(s));
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}
