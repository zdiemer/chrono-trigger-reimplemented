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
 * A pass of the loop is one sequence, which the original's machine runs a
 * frame at a time from the loop's head (its entry) and the program runs as C
 * (overworld_runs, the world map from the mode's start to the field it
 * leaves for). What only the machine can do is a step's callback: the waits
 * (on the machine, calls to the original's waits that the runtime holds over
 * the frame's end), the map's loading again (the unpacker), the screens the
 * loop opens, and the registers the original's code after a step reads. The
 * menus move the stack, which the shipping mode can't carry a port across,
 * so on the machine the loop starts again where the original comes back from
 * them (overworld_back_from_the_menus). In a mode it doesn't know, or
 * entering a place past the map's lists (neither a state the game makes),
 * the loop hands over to the original's. */
#include "overworld/main_loop.h"

#include "charge.h"
#include "core/program.h"
#include "menu/bridge.h"
#include "menu/display.h"
#include "menu/epoch.h"
#include "menu/menu.h"
#include "menu/vram.h"
#include "original_effects.h"
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
    ORIGINAL_EFFECT(charge_as(g, PUT_ASIDE, ACCESSES_A_BYTE * bytes));
}

static void bring_back(GameState *g, const Aside *what, unsigned n)
{
    uint32_t bytes = 0;
    for (unsigned i = 0; i < n; i++) {
        wram_copy(g, RAM_OVERWORLD_PUT_ASIDE + what[i].to, what[i].from, what[i].bytes);
        bytes += what[i].bytes;
    }
    ORIGINAL_EFFECT(charge_as(g, BRING_BACK, ACCESSES_A_BYTE * bytes));
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
    bool (*next_frame)(void *ctx);                             /* the loop's wait at its head */
    bool (*frames)(void *ctx, LoopWait wait);                  /* frames waited, the sound sent meanwhile */
    bool (*a_fade_frame)(void *ctx, uint16_t left);            /* a frame of the fade into a place */
    void (*arm_entered)(void *ctx, OverworldMode mode);
    void (*script_started)(void *ctx, uint32_t script);
    void (*cpu_stilled)(void *ctx);                            /* the CPU's interrupts masked */
    void (*flags_given_back)(void *ctx);
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
    l->steps->flags_given_back(l->ctx);
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

// $C2:251E
/* Open the menus (the party's exchange from its button): the map faded out
 * and put aside while they run. */
static Pass open_the_menus(const Loop *l, OverworldMode mode)
{
    fade_out_with(l, 0xC2251F, FADE_OUT, 0xC2252C);
    if (!l->steps->frames(l->ctx, WAIT_INTO_THE_MENUS))
        return PASS_STOPS;
    stop_the_screen(l, (StopSites){ 0xC22537, 0xC2253A, 0xC2253D }, true);
    put_aside(l->game, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    return l->steps->menus(l->ctx, mode);
}

// $C2:258D
/* The map view: the map faded out, the direct page and the objects put aside
 * while it runs, and the map faded in again. */
static Pass open_the_map_view(const Loop *l)
{
    GameState *g = l->game;
    fade_out_with(l, 0xC2258E, FADE_OUT_TO_THE_MAP_VIEW, 0xC2259B);
    if (!l->steps->frames(l->ctx, WAIT_INTO_THE_MAP_VIEW))
        return PASS_STOPS;
    stop_the_screen(l, (StopSites){ 0xC225A6, 0xC225A9, 0xC225AC }, true);
    put_aside(g, MAP_VIEW_ASIDE, MAP_VIEW_PIECES);
    if (!l->steps->map_view(l->ctx))
        return PASS_STOPS;
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

// $C2:261D
/* The time dial: the map faded out, the effects brought back up, the
 * overworld put aside while the dial runs; back on the map as before, unless
 * the dial sent the party on through time. */
static Pass run_the_time_dial(const Loop *l)
{
    GameState *g = l->game;
    fade_out_with(l, 0xC2261E, FADE_OUT, 0xC2262B);
    if (!l->steps->frames(l->ctx, WAIT_INTO_THE_DIAL))
        return PASS_STOPS;
    stop_the_screen(l, (StopSites){ 0xC22636, 0xC22639, 0xC2263C }, true);
    au_fade(l->pf, PF_CALL_DC(0xC2264E, 0xC70004, DRIVER_IGNORES), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    put_aside(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (!l->steps->time_dial(l->ctx))
        return PASS_STOPS;
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

// $C2:23CF
/* A pass of the loop: the wait for the next frame, then the mode's arm. */
static Pass run_a_pass(const Loop *l)
{
    GameState *g = l->game;
    if (!l->steps->next_frame(l->ctx))
        return PASS_STOPS;
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
} LoopAsC;

static bool next_frame(void *ctx)
{
    LoopAsC *c = ctx;
    menu_wait_for_the_next_frame(c->game, c->pf);
    return true;
}

static bool frames(void *ctx, LoopWait wait)
{
    LoopAsC *c = ctx;
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
    LoopAsC *c = ctx;
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
    LoopAsC *c = ctx;
    if (menu_mode_runs(c->game, c->pf, the_menus_screen(mode), 0) != MENUS_CLOSED)
        return PASS_STOPS;
    TIMED_AS(c->game, 0xC20043, menu_object_set_up_the_screen(c->game, c->pf));
    Pass p = PASS_STOPS;
    TIMED_AS(c->game, 0xC2255A, p = come_back_from_the_menus(c->loop));
    return p;
}

static bool map_view(void *ctx)
{
    LoopAsC *c = ctx;
    program_needs_the_machine(c->game, 0xC2631F, "the map view");
    return false;
}

static bool time_dial(void *ctx)
{
    LoopAsC *c = ctx;
    program_needs_the_machine(c->game, 0xC26A34, "the time dial");
    return false;
}

static void handed_over(void *ctx)
{
    LoopAsC *c = ctx;
    program_needs_the_machine(c->game, 0xC223D2, "a world map's mode the loop doesn't know");
}

static const LoopSteps as_c = {
    next_frame, frames, a_fade_frame, nothing_left, no_script_left, nothing_to_leave,
    nothing_to_leave, map_loaded, menus, map_view, time_dial, handed_over,
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
    LoopAsC c = { g, pf, NULL };
    Loop l = { g, pf, &as_c, &c };
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

static bool next_frame_on_the_machine(void *ctx) { return menu_call(ctx, 0xC223CF, 0xC2046E, READS_NOTHING | DC_DB); }

/* The wait for frames that sends the sound waiting meanwhile (menu/frame.c). */
static bool frames_on_the_machine(void *ctx, LoopWait wait)
{
    Snes *s = ctx;
    s->cpu.x = loop_waits[wait].frames;
    return menu_run_or_call(s, loop_waits[wait].site, 0xC20454, READS_X_AND_A_HIGH, menu_wait_frames_sending_the_sound_entry);
}

/* The original keeps its count on the stack across the fade's waits. */
static bool a_fade_frame_on_the_machine(void *ctx, uint16_t left)
{
    Snes *s = ctx;
    m_push16(s, left);
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

static void flags_given_back_on_the_machine(void *ctx) { ORIGINAL_EFFECT(menu_leave_a_high_byte_set((Snes *)ctx)); }

/* A map the loader can't take as it stands (none the game makes) is left to
 * the original's call. */
static bool map_loaded_on_the_machine(void *ctx, MapLoad load)
{
    Snes *s = ctx;
    vars_peek(true);
    bool loadable = overworld_map_can_be_loaded_again(state_of(s));
    vars_peek(false);
    if (!loadable)
        return menu_call(s, map_loads[load].site, 0xC22C93, READS_NOTHING);
    Unpacker u = overworld_unpacker_on_the_machine(s);
    bool loaded;
    NATIVE_TIME_AS(s, 0xC22C93, loaded = overworld_load_the_map_again(state_of(s), &u, m_platform(s)));
    if (loaded)
        m_sep(s, CPU_FLAG_M);
    return loaded;
}

/* The menus move the stack, which leaves the loop behind: it takes up again
 * where the original comes back from them and has set the mode's screen up
 * again ($C2:2556, $C2:0043). The menus take the screen in A and its argument
 * in X: the original hands them what its block moves left in X, whose low
 * byte, the argument these screens read, is 0. */
static Pass menus_on_the_machine(void *ctx, OverworldMode mode)
{
    Snes *s = ctx;
    s->cpu.a = the_menus_screen(mode);
    s->cpu.x = past_the_last_piece(OVERWORLD_ASIDE, OVERWORLD_PIECES);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_set_flag(s, CPU_FLAG_C, true);
    if (menu_call_long(s, 0xC22552, 0xC28000, DC_AH | DC_Y | DC_C))
        m_stop_at(s, 0xC22556, READS_NOTHING);
    return PASS_STOPS;
}

static bool map_view_on_the_machine(void *ctx) { return menu_run(ctx, 0xC225CF, 0xC2631F, menu_epoch_map_screen); }

static bool time_dial_on_the_machine(void *ctx) { return menu_run(ctx, 0xC22655, 0xC26A34, menu_epoch_time_dial_screen); }

/* The original's dispatch, with the machine as the wait left it. */
static void handed_over_on_the_machine(void *ctx) { m_stop_at(ctx, 0xC223D2, 0); }

static const LoopSteps on_the_machine = {
    next_frame_on_the_machine, frames_on_the_machine, a_fade_frame_on_the_machine, arm_entered_on_the_machine,
    script_started_on_the_machine, cpu_stilled_on_the_machine, flags_given_back_on_the_machine,
    map_loaded_on_the_machine, menus_on_the_machine, map_view_on_the_machine, time_dial_on_the_machine,
    handed_over_on_the_machine,
};

/* The loop's head, where each frame starts; the field's start, which the
 * world map leaves for; and the halt, where the original spins: the port
 * does a pass and comes back to it, which the runtime runs again as the
 * port. */
enum { NEXT_FRAME = 0xC223CF, THE_FIELD = 0xC00000, HALT = 0xC2250D };

static void after_the_pass(Snes *s, Pass p)
{
    switch (p) {
    case PASS_GOES_ON: m_stop_at(s, NEXT_FRAME, READS_NOTHING); break;
    case PASS_LEAVES: m_stop_at(s, THE_FIELD, READS_NOTHING); break;
    case PASS_HALTS: m_stop_at(s, HALT, READS_NOTHING); break;
    default: break;
    }
}

static Loop on_the_machine_loop(Snes *s) { return (Loop){ state_of(s), m_platform(s), &on_the_machine, s }; }

void overworld_run_a_frame(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, run_a_pass(&l));
}

void overworld_halt_entry(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, halt_the_game(&l));
}

void overworld_come_back_from_the_menus(Snes *s)
{
    Loop l = on_the_machine_loop(s);
    after_the_pass(s, come_back_from_the_menus(&l));
}

// $C2:2556
/* Back from the menus, which return here: the mode's screen set up again
 * first, with the CPU's interrupts masked, the menus having left theirs. */
void overworld_come_back_to_the_map_s_screen(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);
    if (menu_run_or_call(s, 0xC22557, 0xC20043, READS_NOTHING, menu_object_init_video))
        NATIVE_TIME_AS(s, 0xC2255A, overworld_come_back_from_the_menus(s));
}

// $C2:2E23
/* Put the overworld aside, as a routine of its own: its block moves leave X
 * past the last piece's source, which the menus read the low byte of. */
void overworld_put_aside_entry(Snes *s)
{
    put_aside(state_of(s), OVERWORLD_ASIDE, OVERWORLD_PIECES);
    s->cpu.x = past_the_last_piece(OVERWORLD_ASIDE, OVERWORLD_PIECES);
    m_rts(s);
}

// $C2:2E72
/* Bring it back: its block moves leave their count run out, A all ones. */
void overworld_bring_back_entry(Snes *s)
{
    bring_back(state_of(s), OVERWORLD_ASIDE, OVERWORLD_PIECES);
    ORIGINAL_EFFECT(s->cpu.a = 0xFFFF);
    m_rts(s);
}
