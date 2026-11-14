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
 * A pass of the loop is one sequence (run_a_pass), which overworld_runs
 * repeats from the mode's start until the world map leaves for the field. A mode
 * the loop doesn't know, or entering a place past the map's lists (neither
 * a state the game makes), breaks the loop's contract, and the program goes
 * no further. */
#include "overworld/main_loop.h"

#include "charge.h"
#include "field/main_loop.h"
#include "core/program.h"
#include "menu/display.h"
#include "menu/epoch.h"
#include "menu/menu.h"
#include "menu/objects.h"
#include "menu/vram.h"
#include "overworld/music.h"
#include "overworld/overworld_data.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "overworld/setup.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- Putting the overworld aside -------------------------------------------------------- */

/* What goes where in overworld_put_aside while the menus or the time dial
 * run: low RAM from $0700 (the objects among it), the mode's own cells,
 * bank $7E's from $B000 and from $4000 to the map's end, the map's regions
 * and its lists of places. The map view keeps the objects' slots alone, in
 * the same place, beside the mode's own cells. */
typedef struct {
    uint32_t from;
    uint16_t to;    /* bytes into overworld_put_aside */
    uint16_t bytes;
} Aside;

static const Aside OVERWORLD_ASIDE[] = {
    { 0x7E0000, 0x1700, 0x00F0 },                        /* the mode's own cells */
    { 0x7E0700, 0x0000, 0x1700 },                        /* low RAM from $0700 */
    { 0x7EB000, 0x1800, 0x0508 },
    { 0x7E4000, 0x1E00, 0x3000 },                        /* to the map's end */
    { RAM_OVERWORLD_REGIONS, 0x4E00, 0x0C00 },
    { RAM_OVERWORLD_EXITS, 0x5A00, 0x0800 },             /* the lists of places and the event scripts */
};

static const Aside MAP_VIEW_ASIDE[] = {
    { 0x7E0000, 0x1700, 0x00F0 },                        /* the mode's own cells */
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
 * field at the location left for; to the halt; or no further (a step the
 * port refused). */
typedef enum { PASS_GOES_ON, PASS_LEAVES, PASS_HALTS, PASS_STOPS } Pass;

/* The loop's waits while a screen of its own fades the map out or in, each
 * its frames, the sound sent meanwhile. */
enum {
    WAIT_INTO_THE_MENUS = 0x11, WAIT_BACK_FROM_THE_MENUS = 0x11, WAIT_INTO_THE_MAP_VIEW = 0x22,
    WAIT_BACK_FROM_THE_MAP_VIEW = 0x29, WAIT_INTO_THE_DIAL = 0x12, WAIT_BACK_FROM_THE_DIAL = 0x11,
};

static void wait_frames(GameState *g, Platform *pf, uint16_t frames)
{
    menu_wait_frames_sending_the_sound(g, pf, frames);
}

static void interrupts(Platform *pf, uint32_t site, bool on)
{
    tm_enable_interrupts(pf, PF_AT(site), on ? TM_INT_VBLANK | TM_INT_AUTO_JOYPAD : 0);
}

/* The scripts that fade the map out and in, and the map view's own: where
 * each starts in the fades' scripts. */
enum {
    FADE_OUT = 0x00,
    FADE_IN = 0x03,
    FADE_OUT_TO_THE_MAP_VIEW = 0x06,
    FADE_IN_FROM_THE_MAP_VIEW = 0x0F,
};

static void start_a_script(GameState *g, uint16_t fade)
{
    uint32_t script = overworld_fade_script(fade);
    TIMED_AS(g, 0xC204D9, (void)menu_start_script(g, script, MENU_ORDINARY_SLOT));
}

/* The map fades out over a script of its own, the interrupts back on while
 * it plays. */
static void fade_out_with(GameState *g, Platform *pf, uint32_t off, uint16_t script, uint32_t on)
{
    interrupts(pf, off, false);
    start_a_script(g, script);
    interrupts(pf, on, true);
}

/* The screen still before a screen of its own takes over: the interrupts,
 * the uploads and the line effects off, the mode's line effects too. */
typedef struct {
    uint32_t interrupts, uploads, line_effects;
} StopSites;

static void stop_the_screen(GameState *g, Platform *pf, StopSites at, bool its_line_effects)
{
    tm_enable_interrupts(pf, PF_AT(at.interrupts), 0);
    vid_stop_uploads(pf, PF_AT(at.uploads));
    vid_enable_line_effects(pf, PF_AT(at.line_effects), 0);
    if (its_line_effects)
        set_menu_object_line_effects(g, 0);
}

/* The screen shows again from the next frame: the mode sends its frame and
 * palette, and the interrupts are on. */
static void show_again(GameState *g, Platform *pf, uint32_t site)
{
    enum { SEND_FRAME_AND_PALETTE = 0x03 };
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | SEND_FRAME_AND_PALETTE));
    interrupts(pf, site, true);
}

/* The map loaded again (setup.c), where it is one the port loads. */
static bool map_loaded(GameState *g, Platform *pf)
{
    if (!PORT_CHECK(overworld_map_can_be_loaded_again(g))) {
        program_fail(g, 0xC22C93, "a world map loaded again that the port doesn't take");
        return false;
    }
    TIMED_AS(g, 0xC22C93, overworld_load_the_map_again(g, pf));
    return true;
}

/* The map's screen set up again: the upload queue reset and the map loaded. */
static bool load_the_map(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC203EF, menu_object_reset_the_upload_queue(g));
    return map_loaded(g, pf);
}

/* ---- The arms --------------------------------------------------------------------------- */

// $C2:23EF
/* On the map: a sound command a request left waiting goes to the driver. */
static void send_the_waiting_sound(GameState *g, Platform *pf)
{
    AudioCommand waiting;
    if (!overworld_take_the_waiting_sound(g, &waiting))
        return;
    au_command(pf, PF_CALL(0xC223F9, 0xC70004), waiting);
    overworld_waiting_sound_sent(g);
}

// $C2:24D5
/* Leave the world map for the location set: the screen stilled, the story's
 * flags given back, the effects brought back up, and the field takes over. */
static Pass leave_the_world_map(GameState *g, Platform *pf)
{
    stop_the_screen(g, pf, (StopSites){ 0xC224D7, 0xC224DA, 0xC224DD }, false);
    give_the_story_flags_back(g);
    au_fade(pf, PF_CALL(0xC224F0, 0xC70004), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    au_fade(pf, PF_CALL(0xC22501, 0xC70004), AU_FADE_MORE_EFFECTS, 0, AU_FULL_VOLUME);
    return PASS_LEAVES;
}

// $C2:2402
/* Enter the place whose exit the leader stands on: an exit to an event starts
 * the event's script on the map; any other fades the map out over eighteen
 * frames and leaves for its location. */
static Pass enter_the_place(GameState *g, Platform *pf)
{
    enum { FADE_FRAMES = 0x12 };
    interrupts(pf, 0xC22403, false);
    ExitTo to = overworld_exit_here(g);
    if (to.location == EXIT_TO_AN_EVENT) {
        uint16_t script = overworld_exit_here_event_script(g);
        set_overworld_event_script(g, script);
        overworld_start_event(g, script);
        set_overworld_event_over(g, 0);
        set_overworld_event(g, 1);
        overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
        interrupts(pf, 0xC2244C, true);
        return PASS_GOES_ON;
    }
    start_a_script(g, FADE_OUT);
    interrupts(pf, 0xC2245E, true);
    for (uint16_t frames = FADE_FRAMES; frames > 0; frames--)
        menu_wait_for_the_next_frame(g, pf);
    interrupts(pf, 0xC2246D, false);
    overworld_leave_for(g, to.location, to.arrival, to.x, to.y);
    return leave_the_world_map(g, pf);
}

// $C2:255A
/* Back from the menus: the map's screen, the overworld and its streams
 * brought back, and the map faded in, its sign set again. */
static Pass come_back_from_the_menus(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC20110, menu_object_take_the_interrupts(g));
    bring_back(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (!load_the_map(g, pf))
        return PASS_STOPS;
    start_a_script(g, FADE_IN);
    set_the_sign_again(g);
    show_again(g, pf, 0xC2257C);
    wait_frames(g, pf, WAIT_BACK_FROM_THE_MENUS);
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return PASS_GOES_ON;
}

/* The screen the menus open on: the main menu, or the party's exchange. */
enum { MAIN_MENU = 1, EXCHANGE_SCREEN = 5 };
static uint8_t the_menus_screen(OverworldMode mode) { return mode == OVERWORLD_OPENING_THE_MENUS ? MAIN_MENU : EXCHANGE_SCREEN; }

/* The menus as the program runs them (menu/display.c), the mode's screen set
 * up again when they close, and the overworld back. */
static Pass enter_the_menus(GameState *g, Platform *pf, OverworldMode mode)
{
    stop_the_screen(g, pf, (StopSites){ 0xC22537, 0xC2253A, 0xC2253D }, true);
    put_aside(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (menu_mode_runs(g, pf, the_menus_screen(mode), 0) != MENUS_CLOSED)
        return PASS_STOPS;
    TIMED_AS(g, 0xC20043, menu_object_set_up_the_screen(g, pf));
    Pass p = PASS_STOPS;
    TIMED_AS(g, 0xC2255A, p = come_back_from_the_menus(g, pf));
    return p;
}

// $C2:251E
static Pass open_the_menus(GameState *g, Platform *pf, OverworldMode mode)
{
    fade_out_with(g, pf, 0xC2251F, FADE_OUT, 0xC2252C);
    wait_frames(g, pf, WAIT_INTO_THE_MENUS);
    return enter_the_menus(g, pf, mode);
}

static Pass come_back_from_the_map_view(GameState *g, Platform *pf)
{
    bring_back(g, MAP_VIEW_ASIDE, MAP_VIEW_PIECES);
    if (!load_the_map(g, pf))
        return PASS_STOPS;
    start_a_script(g, FADE_IN_FROM_THE_MAP_VIEW);
    set_the_sign_again(g);
    show_again(g, pf, 0xC2260C);
    wait_frames(g, pf, WAIT_BACK_FROM_THE_MAP_VIEW);
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return PASS_GOES_ON;
}

static Pass enter_the_map_view(GameState *g, Platform *pf)
{
    stop_the_screen(g, pf, (StopSites){ 0xC225A6, 0xC225A9, 0xC225AC }, true);
    put_aside(g, MAP_VIEW_ASIDE, MAP_VIEW_PIECES);
    bool shown = false;
    TIMED_AS(g, 0xC2631F, shown = menu_epoch_map_view_shown(g, pf));
    if (!shown)
        return PASS_STOPS;
    return come_back_from_the_map_view(g, pf);
}

// $C2:258D
static Pass open_the_map_view(GameState *g, Platform *pf)
{
    fade_out_with(g, pf, 0xC2258E, FADE_OUT_TO_THE_MAP_VIEW, 0xC2259B);
    wait_frames(g, pf, WAIT_INTO_THE_MAP_VIEW);
    return enter_the_map_view(g, pf);
}

static Pass come_back_from_the_time_dial(GameState *g, Platform *pf)
{
    bring_back(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    TIMED_AS(g, 0xC203EF, menu_object_reset_the_upload_queue(g));
    bool sent_on = the_dial_sent_the_party_on(g);
    if (!sent_on) {
        if (!map_loaded(g, pf))
            return PASS_STOPS;
        start_a_script(g, FADE_IN);
        set_the_sign_again(g);
    }
    show_again(g, pf, 0xC22687);
    if (sent_on)
        return PASS_GOES_ON;
    wait_frames(g, pf, WAIT_BACK_FROM_THE_DIAL);
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return PASS_GOES_ON;
}

static Pass enter_the_time_dial(GameState *g, Platform *pf)
{
    stop_the_screen(g, pf, (StopSites){ 0xC22636, 0xC22639, 0xC2263C }, true);
    au_fade(pf, PF_CALL(0xC2264E, 0xC70004), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    put_aside(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    bool shown = false;
    TIMED_AS(g, 0xC26A34, shown = menu_epoch_time_dial_shown(g, pf));
    if (!shown)
        return PASS_STOPS;
    return come_back_from_the_time_dial(g, pf);
}

// $C2:261D
static Pass run_the_time_dial(GameState *g, Platform *pf)
{
    fade_out_with(g, pf, 0xC2261E, FADE_OUT, 0xC2262B);
    wait_frames(g, pf, WAIT_INTO_THE_DIAL);
    return enter_the_time_dial(g, pf);
}

// $C2:250D
/* Halted: the game stops for good. The screen is stilled with no interrupt
 * left to wake it and the story's flags are given back, over and over.
 * Nothing the player does gets out of it but a reset. */
static Pass halt_the_game(GameState *g, Platform *pf)
{
    stop_the_screen(g, pf, (StopSites){ 0xC2250F, 0xC22512, 0xC22515 }, false);
    give_the_story_flags_back(g);
    return PASS_HALTS;
}

/* ---- The loop --------------------------------------------------------------------------- */

// $C2:23D2
/* What a pass of the loop does once the next frame has come: the mode's arm. */
static Pass after_the_frame(GameState *g, Platform *pf)
{
    OverworldMode mode = overworld_mode(g);
    if (mode >= OVERWORLD_MODES || (mode == OVERWORLD_ENTERING_A_PLACE && !PORT_CHECK(overworld_exit_here_is_in_range(g)))) {
        program_fail(g, 0xC223D2, "a world map's mode the loop doesn't know");
        return PASS_STOPS;
    }
    switch (mode) {
    case OVERWORLD_STARTING:
    case OVERWORLD_ON_THE_MAP:
    case OVERWORLD_BACK_ON_THE_MAP:
        send_the_waiting_sound(g, pf);
        return PASS_GOES_ON;
    case OVERWORLD_LEAVING: return leave_the_world_map(g, pf);
    case OVERWORLD_HALTED: return halt_the_game(g, pf);
    case OVERWORLD_ENTERING_A_PLACE: return enter_the_place(g, pf);
    case OVERWORLD_OPENING_THE_MENUS:
    case OVERWORLD_EXCHANGING_THE_PARTY: return open_the_menus(g, pf, mode);
    case OVERWORLD_OPENING_THE_MAP_VIEW: return open_the_map_view(g, pf);
    case OVERWORLD_TRAVELLING_THROUGH_TIME: return run_the_time_dial(g, pf);
    default: return PASS_GOES_ON;
    }
}

// $C2:23CF
/* A pass of the loop: the wait for the next frame, then the mode's arm. */
static Pass run_a_pass(GameState *g, Platform *pf)
{
    menu_wait_for_the_next_frame(g, pf);
    return after_the_frame(g, pf);
}

// $C2:0000 (the mode's start, $C2:000F, the world map's setup, $C2:23A8, and its loop)
OverworldEnd overworld_runs(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2000F, menu_object_mode_starts(g, pf));
    if (!PORT_CHECK(overworld_can_be_set_up(g))) {
        program_fail(g, 0xC223A8, "a world map the port doesn't set up");
        return OVERWORLD_WENT_NO_FURTHER;
    }
    TIMED_AS(g, 0xC223A8, overworld_set_up(g, pf));
    Pass p = PASS_STOPS;
    do
        TIMED_AS(g, 0xC223CF, p = run_a_pass(g, pf));
    while (p == PASS_GOES_ON);
    if (p == PASS_LEAVES)
        return OVERWORLD_LEFT;
    /* halted, the game stops for good: the halt again every frame */
    while (p == PASS_HALTS) {
        tm_wait_frame(pf, PF_AT(0xC2250D));
        p = halt_the_game(g, pf);
    }
    return OVERWORLD_WENT_NO_FURTHER;
}
