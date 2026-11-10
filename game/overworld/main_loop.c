/* The overworld's main loop: a frame at a time, what the leader's buttons
 * and the scripts asked of it.
 *
 * While the party walks the world map the sprite-object mode's main loop
 * waits for each frame (the vblank handler runs the objects: the party, the
 * sign, the places' watcher, the music) and then looks at its mode
 * (OverworldMode, overworld.h):
 *
 * - on the map, it sends the sound command a request left waiting (music.c);
 * - entering a place, it fades the map out and leaves for the location the
 *   exit names, or, for an exit to an event, starts the event's script;
 * - leaving (a script's command chose the location), it leaves at once;
 * - for the menus or the party's exchange, the map view or the time dial, it
 *   fades the map out, puts the overworld aside, runs the screen, brings the
 *   overworld back and fades it in again, its sign set afresh;
 * - halted (the leader stepped onto a halt, places.c), it stops the game.
 *
 * A pass of the loop is one sequence, which the program runs as C
 * (overworld_runs, the world map from the mode's start to the field it
 * leaves for) and the shipping mode runs on the machine a frame at a time,
 * as the continuation of the loop's held wait for the next frame
 * (overworld_next_frame; the overworld is off the byte-exact gate, so
 * nowhere else: docs/REIMPLEMENTATION.md, "The overworld's teardown"). What
 * only the machine can do is a step's callback: the fades' waits (calls to
 * the mode's waits, which the runtime holds over the frame's end), the map's
 * loading again (the unpacker), the screens the loop opens, still under the
 * gate, and the registers they read. The menus move the stack, so on the
 * machine the pass waits in its call to them, and goes on where the original
 * comes back from them. A mode the loop doesn't know, or entering a place
 * past the map's lists (neither a state the game makes), breaks the loop's
 * contract, and the original's code goes on. */
#include "overworld/main_loop.h"

#include "charge.h"
#include "field/main_loop.h"
#include "holds.h"
#include "core/program.h"
#include "menu/bridge.h"
#include "menu/display.h"
#include "menu/epoch.h"
#include "menu/menu.h"
#include "menu/objects.h"
#include "menu/vram.h"
#include "overworld/music.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "overworld/setup.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* What the routines the loop calls read of the registers (tools/dontcare_check.py
 * checks each): nothing; the frames to wait in X and A's high byte, which the
 * wait hands on; and what the sound driver passes back. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_X_AND_A_HIGH = DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_AL,
    DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

/* ---- Putting the overworld aside -------------------------------------------------------- */

/* What goes where in overworld_put_aside while the menus or the time dial
 * run: low RAM from $0700 (the objects among it), the mode's direct page,
 * bank $7E's from $B000 and from $4000 to the map's end, the map's regions
 * and its lists of places. The map view keeps the objects' slots alone, in
 * the same place, beside the direct page. */
typedef struct {
    uint32_t from;
    uint16_t to;    /* bytes into overworld_put_aside */
    uint16_t bytes;
} Aside;

static const Aside OVERWORLD_ASIDE[] = {
    { 0x7E0000, 0x1700, 0x00F0 },                        /* the mode's direct page */
    { 0x7E0700, 0x0000, 0x1700 },                        /* low RAM from $0700 */
    { 0x7EB000, 0x1800, 0x0508 },
    { 0x7E4000, 0x1E00, 0x3000 },                        /* to the map's end */
    { RAM_OVERWORLD_REGIONS, 0x4E00, 0x0C00 },
    { RAM_OVERWORLD_EXITS, 0x5A00, 0x0800 },             /* the lists of places and the event scripts */
};

static const Aside MAP_VIEW_ASIDE[] = {
    { 0x7E0000, 0x1700, 0x00F0 },                        /* the mode's direct page */
    { RAM_MENU_OBJECT_SLOTS, 0x0000, 0x1000 },           /* the objects */
};

/* The original moves each piece with a block move of its own, a read and a
 * write a byte, timed as its putting the overworld aside's ($C2:2E23) or
 * bringing it back's ($C2:2E72) are: block moves run from bank C2, which
 * the menus' stub in work RAM doesn't time. */
enum { ACCESSES_A_BYTE = 2, PUT_ASIDE = 0xC22E23, BRING_BACK = 0xC22E72 };

static void put_aside(GameState *g, const Aside *what, unsigned n)
{
    uint32_t bytes = 0;
    for (unsigned i = 0; i < n; i++) {
        wram_copy(g, what[i].from, RAM_OVERWORLD_PUT_ASIDE + what[i].to, what[i].bytes);
        bytes += what[i].bytes;
    }
    charge_as(g, PUT_ASIDE, ACCESSES_A_BYTE * bytes);
}

static void bring_back(GameState *g, const Aside *what, unsigned n)
{
    uint32_t bytes = 0;
    for (unsigned i = 0; i < n; i++) {
        wram_copy(g, RAM_OVERWORLD_PUT_ASIDE + what[i].to, what[i].from, what[i].bytes);
        bytes += what[i].bytes;
    }
    charge_as(g, BRING_BACK, ACCESSES_A_BYTE * bytes);
}

enum { OVERWORLD_PIECES = sizeof OVERWORLD_ASIDE / sizeof OVERWORLD_ASIDE[0] };
enum { MAP_VIEW_PIECES = sizeof MAP_VIEW_ASIDE / sizeof MAP_VIEW_ASIDE[0] };

/* What the block moves leave in X: past the last piece's source. */
static uint16_t past_the_last_piece(const Aside *what, unsigned n)
{
    return (uint16_t)(what[n - 1].from + what[n - 1].bytes);
}

// $C2:26F0
/* The map screens keep their own copy of the story's flags (menu_story_flags)
 * as they run, and give it back as the overworld ends. */
enum { STORY_FLAGS_AT = 0x1F0, STORY_FLAGS = 0x10 };

static void give_the_story_flags_back(GameState *g)
{
    for (unsigned i = 0; i < STORY_FLAGS; i++)
        set_event_memory_byte(g, STORY_FLAGS_AT + i, menu_story_flags_bytes_byte(g, i));
}

/* Where the time dial left the story with the party going on through time
 * (event memory $1F4's bits 1 and 3, $1F5's bit 1), the loop takes up at once,
 * the map neither reloaded nor faded in. */
static bool the_dial_sent_the_party_on(const GameState *g)
{
    enum { FIRST = 0x1F4, FIRST_BITS = 0x0A, SECOND = 0x1F5, SECOND_BITS = 0x02 };
    return (event_memory_byte(g, FIRST) & FIRST_BITS) || (event_memory_byte(g, SECOND) & SECOND_BITS);
}

/* The sign sets its name again as the map comes back. */
static void set_the_sign_again(GameState *g)
{
    if (overworld_sign(g))
        set_overworld_sign_again(g, (uint8_t)(overworld_sign_again(g) + 1));
}

/* ---- The loop's steps ------------------------------------------------------------------- */

/* Where a pass goes: round to the next frame; out of the world map, for the
 * field at the location left for; to the halt; or no further (a step only
 * the machine runs, or one the original's code goes on from). */
typedef enum { PASS_GOES_ON, PASS_LEAVES, PASS_HALTS, PASS_STOPS } Pass;

/* The loop's waits while a screen of its own fades the map out or in, each
 * its call's site and its frames; and the map's loads again, each its call's
 * site. */
typedef enum {
    WAIT_INTO_THE_MENUS, WAIT_BACK_FROM_THE_MENUS, WAIT_INTO_THE_MAP_VIEW, WAIT_BACK_FROM_THE_MAP_VIEW,
    WAIT_INTO_THE_DIAL, WAIT_BACK_FROM_THE_DIAL, LOOP_WAITS,
} LoopWait;
static const struct {
    uint32_t site;
    uint16_t frames;
} loop_waits[LOOP_WAITS] = {
    [WAIT_INTO_THE_MENUS] = { 0xC22532, 0x11 },    [WAIT_BACK_FROM_THE_MENUS] = { 0xC22582, 0x11 },
    [WAIT_INTO_THE_MAP_VIEW] = { 0xC225A1, 0x22 }, [WAIT_BACK_FROM_THE_MAP_VIEW] = { 0xC22612, 0x29 },
    [WAIT_INTO_THE_DIAL] = { 0xC22631, 0x12 },     [WAIT_BACK_FROM_THE_DIAL] = { 0xC2269D, 0x11 },
};

typedef enum { LOAD_BACK_FROM_THE_MENUS, LOAD_BACK_FROM_THE_MAP_VIEW, LOAD_BACK_FROM_THE_DIAL, MAP_LOADS } MapLoad;
static const struct {
    uint32_t site;
} map_loads[MAP_LOADS] = {
    [LOAD_BACK_FROM_THE_MENUS] = { 0xC22563 },
    [LOAD_BACK_FROM_THE_MAP_VIEW] = { 0xC225F3 },
    [LOAD_BACK_FROM_THE_DIAL] = { 0xC2266E },
};

/* The steps that are the machine's on the development system, as C or on
 * the machine (below): each false where it went no further. */
typedef struct {
    bool (*frames)(void *ctx, LoopWait wait);                  /* frames waited, the sound sent meanwhile */
    bool (*a_fade_frame)(void *ctx, uint16_t left);            /* a frame of the fade into a place */
    void (*arm_entered)(void *ctx, OverworldMode mode);
    void (*script_started)(void *ctx, uint32_t script);
    void (*cpu_stilled)(void *ctx);                            /* the CPU's interrupts masked */
    bool (*map_loaded)(void *ctx, MapLoad load);               /* the map loaded again */
    Pass (*menus)(void *ctx, OverworldMode mode);              /* the menus run, the overworld put aside */
    bool (*map_view)(void *ctx);
    bool (*time_dial)(void *ctx);
    void (*handed_over)(void *ctx);                            /* a mode the loop doesn't know */
} LoopSteps;

typedef struct {
    GameState *game;
    Platform *pf;
    const LoopSteps *steps;
    void *ctx;
} Loop;

static void interrupts(const Loop *l, uint32_t site, bool on)
{
    tm_enable_interrupts(l->pf, PF_AT(site), on ? TM_INT_VBLANK | TM_INT_AUTO_JOYPAD : 0);
}

/* The scripts that fade the map out and in, and the map view's own. */
enum {
    FADE_OUT = 0xC22EC1,
    FADE_IN = 0xC22EC4,
    FADE_OUT_TO_THE_MAP_VIEW = 0xC22EC7,
    FADE_IN_FROM_THE_MAP_VIEW = 0xC22ED0,
};

static void start_a_script(const Loop *l, uint32_t script)
{
    TIMED_AS(l->game, 0xC204D9, (void)menu_start_script(l->game, script, MENU_ORDINARY_SLOT));
    l->steps->script_started(l->ctx, script);
}

/* The map fades out over a script of its own, the interrupts back on while
 * it plays. */
static void fade_out_with(const Loop *l, uint32_t off, uint32_t script, uint32_t on)
{
    interrupts(l, off, false);
    start_a_script(l, script);
    interrupts(l, on, true);
}

/* The screen still before a screen of its own takes over: the interrupts,
 * the uploads and the line effects off, the mode's line effects too. */
typedef struct {
    uint32_t interrupts, uploads, line_effects;
} StopSites;

static void stop_the_screen(const Loop *l, StopSites at, bool its_line_effects)
{
    l->steps->cpu_stilled(l->ctx);
    tm_enable_interrupts(l->pf, PF_AT(at.interrupts), 0);
    vid_stop_uploads(l->pf, PF_AT(at.uploads));
    vid_enable_line_effects(l->pf, PF_AT(at.line_effects), 0);
    if (its_line_effects)
        set_menu_object_line_effects(l->game, 0);
}

/* The screen shows again from the next frame: the mode sends its frame and
 * palette, and the interrupts are on. */
static void show_again(const Loop *l, uint32_t site)
{
    enum { SEND_FRAME_AND_PALETTE = 0x03 };
    set_menu_object_frame_state(l->game, (uint8_t)(menu_object_frame_state(l->game) | SEND_FRAME_AND_PALETTE));
    interrupts(l, site, true);
}

/* The map's screen set up again: the upload queue reset and the map loaded
 * (setup.c). */
static bool load_the_map(const Loop *l, MapLoad load)
{
    TIMED_AS(l->game, 0xC203EF, menu_object_reset_the_upload_queue(l->game));
    return l->steps->map_loaded(l->ctx, load);
}

/* ---- The arms --------------------------------------------------------------------------- */

// $C2:23EF
/* On the map: a sound command a request left waiting goes to the driver. */
static void send_the_waiting_sound(const Loop *l)
{
    AudioCommand waiting;
    if (!overworld_take_the_waiting_sound(l->game, &waiting))
        return;
    au_command(l->pf, PF_CALL_DC(0xC223F9, 0xC70004, DRIVER_IGNORES), waiting);
    overworld_waiting_sound_sent(l->game);
}

// $C2:24D5
/* Leave the world map for the location set: the screen stilled, the story's
 * flags given back, the effects brought back up, and the field takes over. */
static Pass leave_the_world_map(const Loop *l)
{
    stop_the_screen(l, (StopSites){ 0xC224D7, 0xC224DA, 0xC224DD }, false);
    give_the_story_flags_back(l->game);
    au_fade(l->pf, PF_CALL_DC(0xC224F0, 0xC70004, DRIVER_IGNORES), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    au_fade(l->pf, PF_CALL_DC(0xC22501, 0xC70004, DRIVER_IGNORES), AU_FADE_MORE_EFFECTS, 0, AU_FULL_VOLUME);
    return PASS_LEAVES;
}

// $C2:2402
/* Enter the place whose exit the leader stands on: an exit to an event starts
 * the event's script on the map; any other fades the map out over eighteen
 * frames and leaves for its location. */
static Pass enter_the_place(const Loop *l)
{
    enum { FADE_FRAMES = 0x12 };
    GameState *g = l->game;
    interrupts(l, 0xC22403, false);
    ExitTo to = overworld_exit_here(g);
    if (to.location == EXIT_TO_AN_EVENT) {
        uint16_t script = overworld_exit_here_event_script(g);
        set_overworld_event_script(g, script);
        overworld_start_event(g, script);
        set_overworld_event_over(g, 0);
        set_overworld_event(g, 1);
        overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
        interrupts(l, 0xC2244C, true);
        return PASS_GOES_ON;
    }
    start_a_script(l, FADE_OUT);
    interrupts(l, 0xC2245E, true);
    for (uint16_t frames = FADE_FRAMES; frames > 0; frames--)
        if (!l->steps->a_fade_frame(l->ctx, frames))
            return PASS_STOPS;
    interrupts(l, 0xC2246D, false);
    overworld_leave_for(g, to.location, to.arrival, to.x, to.y);
    return leave_the_world_map(l);
}

// $C2:255A
/* Back from the menus: the map's screen, the overworld and its streams
 * brought back, and the map faded in, its sign set again. */
static Pass come_back_from_the_menus(const Loop *l)
{
    GameState *g = l->game;
    TIMED_AS(g, 0xC20110, menu_object_take_the_interrupts(g));
    bring_back(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (!load_the_map(l, LOAD_BACK_FROM_THE_MENUS))
        return PASS_STOPS;
    start_a_script(l, FADE_IN);
    set_the_sign_again(g);
    show_again(l, 0xC2257C);
    if (!l->steps->frames(l->ctx, WAIT_BACK_FROM_THE_MENUS))
        return PASS_STOPS;
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return PASS_GOES_ON;
}

/* Screen transitions are phases around their display waits. The native
 * program calls them in sequence; held machine continuations resume the
 * same operations from saved frame boundaries. */
static Pass enter_the_menus(const Loop *l, OverworldMode mode)
{
    stop_the_screen(l, (StopSites){ 0xC22537, 0xC2253A, 0xC2253D }, true);
    put_aside(l->game, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    return l->steps->menus(l->ctx, mode);
}

// $C2:251E
static Pass open_the_menus(const Loop *l, OverworldMode mode)
{
    fade_out_with(l, 0xC2251F, FADE_OUT, 0xC2252C);
    if (!l->steps->frames(l->ctx, WAIT_INTO_THE_MENUS))
        return PASS_STOPS;
    return enter_the_menus(l, mode);
}

static Pass come_back_from_the_map_view(const Loop *l)
{
    GameState *g = l->game;
    bring_back(g, MAP_VIEW_ASIDE, MAP_VIEW_PIECES);
    if (!load_the_map(l, LOAD_BACK_FROM_THE_MAP_VIEW))
        return PASS_STOPS;
    start_a_script(l, FADE_IN_FROM_THE_MAP_VIEW);
    set_the_sign_again(g);
    show_again(l, 0xC2260C);
    if (!l->steps->frames(l->ctx, WAIT_BACK_FROM_THE_MAP_VIEW))
        return PASS_STOPS;
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return PASS_GOES_ON;
}

static Pass enter_the_map_view(const Loop *l)
{
    stop_the_screen(l, (StopSites){ 0xC225A6, 0xC225A9, 0xC225AC }, true);
    put_aside(l->game, MAP_VIEW_ASIDE, MAP_VIEW_PIECES);
    if (!l->steps->map_view(l->ctx))
        return PASS_STOPS;
    return come_back_from_the_map_view(l);
}

// $C2:258D
static Pass open_the_map_view(const Loop *l)
{
    fade_out_with(l, 0xC2258E, FADE_OUT_TO_THE_MAP_VIEW, 0xC2259B);
    if (!l->steps->frames(l->ctx, WAIT_INTO_THE_MAP_VIEW))
        return PASS_STOPS;
    return enter_the_map_view(l);
}

static Pass come_back_from_the_time_dial(const Loop *l)
{
    GameState *g = l->game;
    bring_back(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    TIMED_AS(g, 0xC203EF, menu_object_reset_the_upload_queue(g));
    bool sent_on = the_dial_sent_the_party_on(g);
    if (!sent_on) {
        if (!l->steps->map_loaded(l->ctx, LOAD_BACK_FROM_THE_DIAL))
            return PASS_STOPS;
        start_a_script(l, FADE_IN);
        set_the_sign_again(g);
    }
    show_again(l, 0xC22687);
    if (sent_on)
        return PASS_GOES_ON;
    if (!l->steps->frames(l->ctx, WAIT_BACK_FROM_THE_DIAL))
        return PASS_STOPS;
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return PASS_GOES_ON;
}

static Pass enter_the_time_dial(const Loop *l)
{
    stop_the_screen(l, (StopSites){ 0xC22636, 0xC22639, 0xC2263C }, true);
    au_fade(l->pf, PF_CALL_DC(0xC2264E, 0xC70004, DRIVER_IGNORES), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    put_aside(l->game, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (!l->steps->time_dial(l->ctx))
        return PASS_STOPS;
    return come_back_from_the_time_dial(l);
}

// $C2:261D
static Pass run_the_time_dial(const Loop *l)
{
    fade_out_with(l, 0xC2261E, FADE_OUT, 0xC2262B);
    if (!l->steps->frames(l->ctx, WAIT_INTO_THE_DIAL))
        return PASS_STOPS;
    return enter_the_time_dial(l);
}

// $C2:250D
/* Halted: the game stops for good. The screen is stilled with no interrupt
 * left to wake it and the story's flags are given back, over and over.
 * Nothing the player does gets out of it but a reset. */
static Pass halt_the_game(const Loop *l)
{
    stop_the_screen(l, (StopSites){ 0xC2250F, 0xC22512, 0xC22515 }, false);
    give_the_story_flags_back(l->game);
    return PASS_HALTS;
}

/* ---- The loop --------------------------------------------------------------------------- */

// $C2:23D2
/* What a pass of the loop does once the next frame has come: the mode's arm. */
static Pass after_the_frame(const Loop *l)
{
    GameState *g = l->game;
    OverworldMode mode = overworld_mode(g);
    if (mode >= OVERWORLD_MODES || (mode == OVERWORLD_ENTERING_A_PLACE && !overworld_exit_here_is_in_range(g))) {
        l->steps->handed_over(l->ctx);
        return PASS_STOPS;
    }
    l->steps->arm_entered(l->ctx, mode);
    switch (mode) {
    case OVERWORLD_STARTING:
    case OVERWORLD_ON_THE_MAP:
    case OVERWORLD_BACK_ON_THE_MAP:
        send_the_waiting_sound(l);
        return PASS_GOES_ON;
    case OVERWORLD_LEAVING: return leave_the_world_map(l);
    case OVERWORLD_HALTED: return halt_the_game(l);
    case OVERWORLD_ENTERING_A_PLACE: return enter_the_place(l);
    case OVERWORLD_OPENING_THE_MENUS:
    case OVERWORLD_EXCHANGING_THE_PARTY: return open_the_menus(l, mode);
    case OVERWORLD_OPENING_THE_MAP_VIEW: return open_the_map_view(l);
    case OVERWORLD_TRAVELLING_THROUGH_TIME: return run_the_time_dial(l);
    default: return PASS_GOES_ON;
    }
}

/* The menus take the screen in A and its argument in X. */
enum { MAIN_MENU = 1, EXCHANGE_SCREEN = 5 };
static uint8_t the_menus_screen(OverworldMode mode) { return mode == OVERWORLD_OPENING_THE_MENUS ? MAIN_MENU : EXCHANGE_SCREEN; }

/* ---- As C ---- */

typedef struct {
    GameState *game;
    Platform *pf;
    const Loop *loop;
} OverworldLoopContext;

static bool next_frame(void *ctx)
{
    OverworldLoopContext *c = ctx;
    menu_wait_for_the_next_frame(c->game, c->pf);
    return true;
}

// $C2:23CF
/* A pass of the loop: the wait for the next frame, then the mode's arm. */
static Pass run_a_pass(const Loop *l)
{
    next_frame(l->ctx);
    return after_the_frame(l);
}

static bool frames(void *ctx, LoopWait wait)
{
    OverworldLoopContext *c = ctx;
    menu_wait_frames_sending_the_sound(c->game, c->pf, loop_waits[wait].frames);
    return true;
}

static bool a_fade_frame(void *ctx, uint16_t left)
{
    (void)left;
    return next_frame(ctx);
}

static void nothing_left(void *ctx, OverworldMode mode) { (void)ctx, (void)mode; }
static void no_script_left(void *ctx, uint32_t script) { (void)ctx, (void)script; }
static void nothing_to_leave(void *ctx) { (void)ctx; }

static bool map_loaded(void *ctx, MapLoad load)
{
    (void)load;
    OverworldLoopContext *c = ctx;
    if (!overworld_map_can_be_loaded_again(c->game)) {
        program_needs_the_machine(c->game, 0xC22C93, "a world map loaded again that the port doesn't take");
        return false;
    }
    Unpacker u = overworld_unpacker(c->game);
    bool loaded = false;
    TIMED_AS(c->game, 0xC22C93, loaded = overworld_load_the_map_again(c->game, &u, c->pf));
    return loaded;
}

/* The menus as the program runs them (menu/display.c), the mode's screen set
 * up again when they close, and the overworld back. */
static Pass menus(void *ctx, OverworldMode mode)
{
    OverworldLoopContext *c = ctx;
    if (menu_mode_runs(c->game, c->pf, the_menus_screen(mode), 0) != MENUS_CLOSED)
        return PASS_STOPS;
    TIMED_AS(c->game, 0xC20043, menu_object_set_up_the_screen(c->game, c->pf));
    Pass p = PASS_STOPS;
    TIMED_AS(c->game, 0xC2255A, p = come_back_from_the_menus(c->loop));
    return p;
}

static bool map_view(void *ctx)
{
    OverworldLoopContext *c = ctx;
    bool shown = false;
    TIMED_AS(c->game, 0xC2631F, shown = menu_epoch_map_view_shown(c->game, c->pf));
    return shown;
}

static bool time_dial(void *ctx)
{
    OverworldLoopContext *c = ctx;
    bool shown = false;
    TIMED_AS(c->game, 0xC26A34, shown = menu_epoch_time_dial_shown(c->game, c->pf));
    return shown;
}

static void handed_over(void *ctx)
{
    OverworldLoopContext *c = ctx;
    program_needs_the_machine(c->game, 0xC223D2, "a world map's mode the loop doesn't know");
}

static const LoopSteps in_the_program = {
    frames, a_fade_frame, nothing_left, no_script_left, nothing_to_leave, map_loaded, menus, map_view, time_dial,
    handed_over,
};

// $C2:0000 (the mode's start, $C2:000F, the world map's setup, $C2:23A8, and its loop)
OverworldEnd overworld_runs(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2000F, menu_object_mode_starts(g, pf));
    if (!overworld_can_be_set_up(g)) {
        program_needs_the_machine(g, 0xC223A8, "a world map the port doesn't set up");
        return OVERWORLD_WENT_NO_FURTHER;
    }
    Unpacker u = overworld_unpacker(g);
    bool set_up = false;
    TIMED_AS(g, 0xC223A8, set_up = overworld_set_up(g, &u, pf));
    if (!set_up)
        return OVERWORLD_WENT_NO_FURTHER;
    OverworldLoopContext c = { g, pf, NULL };
    Loop l = { g, pf, &in_the_program, &c };
    c.loop = &l;
    Pass p = PASS_STOPS;
    do
        TIMED_AS(g, 0xC223CF, p = run_a_pass(&l));
    while (p == PASS_GOES_ON);
    if (p == PASS_LEAVES)
        return OVERWORLD_LEFT;
    /* halted, the game stops for good: the halt again every frame */
    while (p == PASS_HALTS) {
        tm_wait_frame(pf, PF_AT(0xC2250D));
        p = halt_the_game(&l);
    }
    return OVERWORLD_WENT_NO_FURTHER;
}

/* ---- On the machine ---- */

/* The original's dispatch hands an arm its mode doubled in A, a word, and in
 * X, with the flags of the doubling and the transfer. */
static void arm_entered_on_the_machine(void *ctx, OverworldMode mode)
{
    Snes *s = ctx;
    uint16_t doubled = (uint16_t)(mode * 2);
    s->cpu.a = doubled;
    s->cpu.x = doubled;
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_nz16(s, doubled);
}

/* The wait for frames that sends the sound waiting meanwhile (menu/frame.c). */
static bool frames_on_the_machine(void *ctx, LoopWait wait)
{
    Snes *s = ctx;
    s->cpu.x = loop_waits[wait].frames;
    return menu_run_checked(s, loop_waits[wait].site, 0xC20454, READS_X_AND_A_HIGH, menu_wait_frames_sending_the_sound_entry);
}

/* The original keeps its count on the stack across the fade's waits. */
static bool a_fade_frame_on_the_machine(void *ctx, uint16_t left)
{
    Snes *s = ctx;
    m_push16(s, left);
    if (m_runs_retired_as_c(s)) {
        if (menu_run_called(s, 0xC22465, 0xC20346, READS_NOTHING | DC_DB, menu_wait_for_frame))
            native_hold(s, HOLD_MENU_FRAME);
        return false;
    }
    bool back = menu_call(s, 0xC22465, 0xC20346, READS_NOTHING | DC_DB);
    (void)m_pull16(s);
    return back;
}

/* Starting a script leaves A its address's high byte over its bank (the
 * waits after it read it), the carry set and the overflow clear. */
static void script_started_on_the_machine(void *ctx, uint32_t script)
{
    Snes *s = ctx;
    s->cpu.a = (uint16_t)((script & 0xFF00) | (uint8_t)(script >> 16));
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_V, false);
}

/* The original masks the CPU's interrupts and clears A to write the
 * registers. */
static void cpu_stilled_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_set_flag(s, CPU_FLAG_I, true);
    s->cpu.a = 0;
}

/* A map the loader can't take as it stands (none the game makes) breaks the
 * loop's contract: reported, and left to the original's call. */
static bool map_loaded_on_the_machine(void *ctx, MapLoad load)
{
    Snes *s = ctx;
    vars_peek(true);
    bool loadable = overworld_map_can_be_loaded_again(state_of(s));
    vars_peek(false);
    if (!loadable) {
        program_breaks_a_contract(state_of(s), 0xC22C93, "a world map loaded again outside its contract");
        return menu_call(s, map_loads[load].site, 0xC22C93, READS_NOTHING);
    }
    Unpacker u = overworld_unpacker_on_the_machine(s);
    bool loaded;
    TIMED_AS(state_of(s), 0xC22C93, loaded = overworld_load_the_map_again(state_of(s), &u, m_platform(s)));
    if (loaded)
        m_sep(s, CPU_FLAG_M);
    return loaded;
}

static Pass come_back_to_the_map_s_screen(Snes *s);

/* Where the original comes back from the menus, and its dispatch on the mode. */
enum { BACK_FROM_THE_MENUS = 0xC22556, DISPATCH = 0xC223D2 };

/* The menus move the stack, which leaves the loop behind: it takes up again
 * where the original comes back from them and has set the mode's screen up
 * again ($C2:2556, $C2:0043). The menus take the screen in A and its argument
 * in X: the original hands them what its block moves left in X, whose low
 * byte, the argument these screens read, is 0. Menus that come back with
 * the machine as the loop doesn't take it break its contract: reported, and
 * the original's code goes on from their return. */
static Pass menus_on_the_machine(void *ctx, OverworldMode mode)
{
    Snes *s = ctx;
    s->cpu.a = the_menus_screen(mode);
    s->cpu.x = past_the_last_piece(OVERWORLD_ASIDE, OVERWORLD_PIECES);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_set_flag(s, CPU_FLAG_C, true);
    if (!menu_run_long_called(s, 0xC22552, 0xC28000, DC_AH | DC_Y | DC_C, menu_mode_enter))
        return PASS_STOPS;
    if (!overworld_loop_accepts_machine(s)) {
        program_breaks_a_contract(state_of(s), BACK_FROM_THE_MENUS, "the menus back to the world map outside its contract");
        m_stop_at(s, BACK_FROM_THE_MENUS, READS_NOTHING);
        return PASS_STOPS;
    }
    Pass p = PASS_STOPS;
    TIMED_AS(state_of(s), BACK_FROM_THE_MENUS, p = come_back_to_the_map_s_screen(s));
    return p;
}

static bool map_view_on_the_machine(void *ctx) { return menu_run_checked(ctx, 0xC225CF, 0xC2631F, READS_NOTHING, menu_epoch_map_screen); }

static bool time_dial_on_the_machine(void *ctx) { return menu_run_checked(ctx, 0xC22655, 0xC26A34, READS_NOTHING, menu_epoch_time_dial_screen); }

/* A mode the loop doesn't know breaks its contract: reported, and the
 * original's dispatch goes on, with the machine as the wait left it. */
static void handed_over_on_the_machine(void *ctx)
{
    program_breaks_a_contract(state_of(ctx), DISPATCH, "a world map's mode the loop doesn't know");
    m_stop_at(ctx, DISPATCH, 0);
}

static const LoopSteps on_the_machine = {
    frames_on_the_machine, a_fade_frame_on_the_machine, arm_entered_on_the_machine, script_started_on_the_machine,
    cpu_stilled_on_the_machine, map_loaded_on_the_machine, menus_on_the_machine, map_view_on_the_machine,
    time_dial_on_the_machine, handed_over_on_the_machine,
};

/* ---- The held waits ---- */

/* Each pass starts with the mode's wait for the next frame ($C2:23CF calls
 * $C2:046E, which jumps to $C2:0346): the call's return to the pass pushed,
 * the frame count it sees in A, and then its loop ($C2:0348) until the vblank
 * handler counts a frame past it. The main thread rests there at every frame
 * end on the map, so it is a held wait (holds.h, HOLD_OVERWORLD_FRAME;
 * docs/RUNTIME.md, "Held waits"): the port stops at the wait's loop with
 * nothing suspended, and the runtime goes on with overworld_next_frame once
 * the frame is counted. From the loop's head on, the code reads A's low byte,
 * the count it saw, and not X, Y or the flags the loop sets: the pass after
 * the wait loads A afresh (TDC) and X from the mode. (The wait's code returns
 * to the mode's other waits too, some of which read A's high byte and the
 * overflow, so those are left as they are.)
 *
 * The halt, where the original spins with the interrupts off, is a held wait
 * of its own (HOLD_OVERWORLD_HALT): each pass stops at its head and the
 * runtime runs overworld_halt_entry again. */
enum {
    FRAME_WAIT_LOOP = 0xC20348,
    AFTER_THE_WAIT = 0xC223D2,
    THE_WAIT_READS_A = DC_X | DC_Y | DC_N | DC_Z | DC_C,
};

/* The field's start, which the world map leaves for, and the halt. */
enum { HALT = 0xC2250D };

void overworld_wait_for_the_next_frame_on_the_machine(Snes *s)
{
    m_push16(s, (uint16_t)(AFTER_THE_WAIT - 1));
    m_lda8(s, menu_object_frame_counter(state_of(s)));
    m_hold_at_own(s, FRAME_WAIT_LOOP, THE_WAIT_READS_A, HOLD_OVERWORLD_FRAME);
}

static void after_the_pass(Snes *s, Pass p)
{
    switch (p) {
    case PASS_GOES_ON: overworld_wait_for_the_next_frame_on_the_machine(s); break;
    case PASS_LEAVES: field_enter_after_mode(s, READS_NOTHING); break;
    case PASS_HALTS: m_hold_at_own(s, HALT, READS_NOTHING, HOLD_OVERWORLD_HALT); break;
    default: break;
    }
}

static Loop on_the_machine_loop(Snes *s) { return (Loop){ state_of(s), m_platform(s), &on_the_machine, s }; }

// $C2:2468
/* The map's exit fade keeps its remaining frames on the saveable stack.
 * Its exit record stays selected while the fade disables party movement.
 * The entry already charged the record read; resuming only retrieves it. */
void overworld_exit_fade_finished(Snes *s)
{
    uint16_t left = (uint16_t)(m_pull16(s) - 1);
    s->cpu.x = left;
    if (left) {
        (void)a_fade_frame_on_the_machine(s, left);
        return;
    }
    Loop l = on_the_machine_loop(s);
    vars_peek(true);
    ExitTo to = overworld_exit_here(l.game);
    vars_peek(false);
    interrupts(&l, 0xC2246D, false);
    overworld_leave_for(l.game, to.location, to.arrival, to.x, to.y);
    after_the_pass(s, leave_the_world_map(&l));
}

bool overworld_exit_fade_can_resume(Snes *s)
{
    if (!overworld_loop_accepts_machine(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool valid = overworld_mode(g) == OVERWORLD_ENTERING_A_PLACE && overworld_exit_here_is_in_range(g);
    vars_peek(false);
    uint16_t left = m_read16(s, (uint16_t)(s->cpu.s + 1));
    return valid && left >= 1 && left <= 0x12;
}


/* The loop runs on the mode's page with the data bank on low RAM, an 8-bit
 * accumulator, 16-bit index registers and binary arithmetic. */
bool overworld_loop_accepts_machine(Snes *s)
{
    return !s->cpu.e && m_m8(s) && !m_x8(s) && !m_flag(s, CPU_FLAG_D) && overworld_main_loop_runs(s);
}

bool overworld_main_loop_runs(Snes *s) { return menu_objects_on_page(s); }

// $C2:0348 (the main loop's wait, the frame counted), with $C2:034C and $C2:23D2
/* The vblank handler has counted the next frame: the wait returns to the
 * pass, which does the mode's arm and waits again. The held wait's
 * continuation (holds.h). */
void overworld_next_frame(Snes *s)
{
    m_rts(s);
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, after_the_frame(&l));
}

// $C2:0348
/* A pass of the wait before the frame is counted: the held wait's other
 * continuation, an idle one, which goes round. */
void overworld_waits_for_the_frame(Snes *s) { (void)s; }

/* The wait's return on the stack is to the main loop's pass. */
static bool waits_in_the_main_loop(const Snes *s)
{
    uint16_t sp = s->cpu.s;
    uint16_t pushed = (uint16_t)(snes_bus_peek(&s->bus, (uint16_t)(sp + 1)) | snes_bus_peek(&s->bus, (uint16_t)(sp + 2)) << 8);
    return s->cpu.pb == (uint8_t)(AFTER_THE_WAIT >> 16) && pushed == (uint16_t)(AFTER_THE_WAIT - 1);
}

/* The held wait's continuations' guards: on the mode's page, the wait's
 * return to the main loop's pass, and the frame counted past the one the wait
 * saw (A's low byte), or not yet. Each is a claim the game keeps, which the
 * refusal measure checks over real play (tools/fallbacks.py --refusals). */
static bool frame_counted(Snes *s)
{
    vars_peek(true);
    bool counted = menu_object_frame_counter(state_of(s)) != (uint8_t)s->cpu.a;
    vars_peek(false);
    return counted;
}

bool overworld_frame_counted(Snes *s)
{
    return overworld_main_loop_runs(s) && waits_in_the_main_loop(s) && frame_counted(s);
}

bool overworld_frame_not_counted(Snes *s)
{
    return overworld_main_loop_runs(s) && waits_in_the_main_loop(s) && !frame_counted(s);
}

// $C2:250D
void overworld_halt_entry(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, halt_the_game(&l));
}

// $C2:2556, with $C2:255A
/* Back from the menus, which return here: the mode's screen set up again
 * first, with the CPU's interrupts masked, the menus having left theirs; then
 * the map back and faded in. */
static Pass come_back_to_the_map_s_screen(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);
    if (!menu_run_or_call(s, 0xC22557, 0xC20043, READS_NOTHING, menu_object_init_video))
        return PASS_STOPS;
    Loop l = on_the_machine_loop(s);
    Pass p = PASS_STOPS;
    TIMED_AS(state_of(s), 0xC2255A, p = come_back_from_the_menus(&l));
    return p;
}

// $C2:2556
/* The menus kept the caller's machine frame while their tasks ran. Restore
 * the map from that frame without keeping its opening pass suspended. */
void overworld_back_from_the_menus(Snes *s)
{
    after_the_pass(s, come_back_to_the_map_s_screen(s));
}

// $C2:2535
void overworld_menu_fade_finished(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, enter_the_menus(&l, overworld_mode(l.game)));
}

// $C2:2585
void overworld_menu_return_fade_finished(Snes *s)
{
    TIMED_AS(state_of(s), 0xC2255A, overworld_set_mode(state_of(s), OVERWORLD_ON_THE_MAP));
    after_the_pass(s, PASS_GOES_ON);
}

// $C2:25A4
void overworld_map_view_fade_finished(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, enter_the_map_view(&l));
}

// $C2:25D2
void overworld_back_from_the_map_view(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, come_back_from_the_map_view(&l));
}

// $C2:2615, $C2:26A0
void overworld_screen_return_fade_finished(Snes *s)
{
    overworld_set_mode(state_of(s), OVERWORLD_ON_THE_MAP);
    after_the_pass(s, PASS_GOES_ON);
}

// $C2:2634
void overworld_dial_fade_finished(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, enter_the_time_dial(&l));
}

// $C2:2658
void overworld_back_from_the_time_dial(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, come_back_from_the_time_dial(&l));
}
