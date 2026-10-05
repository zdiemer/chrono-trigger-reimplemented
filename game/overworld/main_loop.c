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
 *   overworld back and fades it in again, its sign set afresh.
 *
 * The screens and the map's loaders are still the original's, so the loop is
 * a bridge on the original's machine; what it does between them (putting
 * the overworld aside and back, starting the event, the location left for)
 * is the game's and plain C. The menus move the stack, which the shipping
 * mode can't carry a port across, so the loop starts again where the
 * original comes back from them (overworld_back_from_the_menus). Halted, or
 * in a mode it doesn't know, the loop hands over to the original's. */
#include "overworld/main_loop.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/epoch.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "overworld/music.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/native.h"
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

/* ---- The loop's machine ------------------------------------------------------------------- */

/* The original's dispatch hands an arm its mode doubled in A, a word, and in
 * X, with the flags of the doubling and the transfer. */
static void enter_the_arm(Snes *s, OverworldMode mode)
{
    uint16_t doubled = (uint16_t)(mode * 2);
    s->cpu.a = doubled;
    s->cpu.x = doubled;
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_nz16(s, doubled);
}

static bool wait_for_the_next_frame(Snes *s) { return menu_call(s, 0xC223CF, 0xC2046E, READS_NOTHING | DC_DB); }

/* The wait for frames that sends the sound waiting meanwhile. */
static bool wait_frames(Snes *s, uint32_t site, uint16_t frames)
{
    s->cpu.x = frames;
    return menu_call(s, site, 0xC20454, READS_X_AND_A_HIGH);
}

/* Starting a script leaves A its address's high byte over its bank (the
 * waits after it read it), the carry set and the overflow clear. */
static void start_a_script(Snes *s, uint32_t script)
{
    NATIVE_TIME_AS(s, 0xC204D9, (void)menu_start_script(state_of(s), script, MENU_ORDINARY_SLOT));
    s->cpu.a = (uint16_t)((script & 0xFF00) | (uint8_t)(script >> 16));
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_V, false);
}

/* The scripts that fade the map out and in, and the map view's own. */
enum {
    FADE_OUT = 0xC22EC1,
    FADE_IN = 0xC22EC4,
    FADE_OUT_TO_THE_MAP_VIEW = 0xC22EC7,
    FADE_IN_FROM_THE_MAP_VIEW = 0xC22ED0,
};

static void interrupts(Snes *s, uint32_t site, bool on)
{
    tm_enable_interrupts(m_platform(s), PF_AT(site), on ? TM_INT_VBLANK | TM_INT_AUTO_JOYPAD : 0);
}

/* The screen still before a screen of its own takes over: the interrupts,
 * the uploads and the line effects off, the mode's line effects too. The
 * original clears A to write them. */
typedef struct {
    uint32_t interrupts, uploads, line_effects;
} StopSites;

static void stop_the_screen(Snes *s, StopSites at, bool its_line_effects)
{
    Platform *pf = m_platform(s);
    m_set_flag(s, CPU_FLAG_I, true);
    s->cpu.a = 0;
    tm_enable_interrupts(pf, PF_AT(at.interrupts), 0);
    vid_stop_uploads(pf, PF_AT(at.uploads));
    vid_enable_line_effects(pf, PF_AT(at.line_effects), 0);
    if (its_line_effects)
        set_menu_object_line_effects(state_of(s), 0);
}

/* The screen shows again from the next frame: the mode sends its frame and
 * palette, and the interrupts are on. */
static void show_again(Snes *s, uint32_t site)
{
    enum { SEND_FRAME_AND_PALETTE = 0x03 };
    GameState *g = state_of(s);
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | SEND_FRAME_AND_PALETTE));
    interrupts(s, site, true);
}

/* The map's screen set up again, the original's loaders still: the upload
 * queue reset and the map's streams loaded. */
static bool reset_the_uploads(Snes *s, uint32_t site) { return menu_call(s, site, 0xC203EF, READS_NOTHING | DC_DB); }
static bool load_the_map(Snes *s, uint32_t site) { return menu_call(s, site, 0xC22C93, READS_NOTHING); }

/* ---- The arms --------------------------------------------------------------------------- */

// $C2:23EF
/* On the map: a sound command a request left waiting goes to the driver. */
static void send_the_waiting_sound(Snes *s)
{
    GameState *g = state_of(s);
    if (!overworld_sound_waits(g))
        return;
    overworld_mark_sound_sending(g, true);
    AudioCommand waiting = { sound_command(g), sound_command_parameters(g, 0), sound_command_parameters(g, 1),
                             sound_command_parameters(g, 2), 3 };
    au_command(m_platform(s), PF_CALL_DC(0xC223F9, 0xC70004, DRIVER_IGNORES), waiting);
    overworld_mark_sound_sending(g, false);
}

// $C2:24D5
/* Leave the world map for the location set: the screen stilled, the story's
 * flags given back, the effects brought back up, and the field takes over. */
static void leave_the_world_map(Snes *s)
{
    Platform *pf = m_platform(s);
    stop_the_screen(s, (StopSites){ 0xC224D7, 0xC224DA, 0xC224DD }, false);
    give_the_story_flags_back(state_of(s));
    ORIGINAL_EFFECT(menu_leave_a_high_byte_set(s));
    au_fade(pf, PF_CALL_DC(0xC224F0, 0xC70004, DRIVER_IGNORES), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    au_fade(pf, PF_CALL_DC(0xC22501, 0xC70004, DRIVER_IGNORES), AU_FADE_MORE_EFFECTS, 0, AU_FULL_VOLUME);
    m_stop_at(s, 0xC00000, READS_NOTHING);
}

// $C2:2402
/* Enter the place whose exit the leader stands on: an exit to an event starts
 * the event's script on the map; any other fades the map out over eighteen
 * frames and leaves for its location. False where the loop is done. */
static bool enter_the_place(Snes *s)
{
    enum { FADE_FRAMES = 0x12 };
    GameState *g = state_of(s);
    interrupts(s, 0xC22403, false);
    ExitTo to = overworld_exit_here(g);
    if (to.location == EXIT_TO_AN_EVENT) {
        uint16_t script = overworld_exit_here_event_script(g);
        set_overworld_event_script(g, script);
        overworld_start_event(g, script);
        set_overworld_event_over(g, 0);
        set_overworld_event(g, 1);
        overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
        interrupts(s, 0xC2244C, true);
        return true;
    }
    start_a_script(s, FADE_OUT);
    interrupts(s, 0xC2245E, true);
    for (uint16_t frames = FADE_FRAMES; frames > 0; frames--) {
        m_push16(s, frames);   /* the original keeps its count on the stack across the wait */
        bool back = menu_call(s, 0xC22465, 0xC20346, READS_NOTHING | DC_DB);
        (void)m_pull16(s);
        if (!back)
            return false;
    }
    interrupts(s, 0xC2246D, false);
    overworld_leave_for(g, to.location, to.arrival, to.x, to.y);
    leave_the_world_map(s);
    return false;
}

// $C2:255A
/* Back from the menus: the map's screen, the overworld and its streams
 * brought back, and the map faded in, its sign set again. */
static bool come_back_from_the_menus(Snes *s)
{
    GameState *g = state_of(s);
    if (!menu_call(s, 0xC2255A, 0xC20110, READS_NOTHING))
        return false;
    bring_back(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (!reset_the_uploads(s, 0xC22560) || !load_the_map(s, 0xC22563))
        return false;
    start_a_script(s, FADE_IN);
    set_the_sign_again(g);
    show_again(s, 0xC2257C);
    if (!wait_frames(s, 0xC22582, 0x11))
        return false;
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return true;
}

// $C2:251E
/* Open the menus (the party's exchange from its button): the map faded out
 * and put aside. The menus move the stack, which leaves the loop behind: it
 * takes up again where the original comes back from them and has set the
 * mode's screen up again ($C2:2556, $C2:0043). */
static void open_the_menus(Snes *s, OverworldMode mode)
{
    enum { MAIN_MENU = 1, EXCHANGE_SCREEN = 5 };
    GameState *g = state_of(s);
    interrupts(s, 0xC2251F, false);
    start_a_script(s, FADE_OUT);
    interrupts(s, 0xC2252C, true);
    if (!wait_frames(s, 0xC22532, 0x11))
        return;
    stop_the_screen(s, (StopSites){ 0xC22537, 0xC2253A, 0xC2253D }, true);
    put_aside(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    /* The menus take the screen in A and its argument in X: the original
     * hands them what its block moves left in X, whose low byte, the argument
     * these screens read, is 0. */
    s->cpu.a = mode == OVERWORLD_OPENING_THE_MENUS ? MAIN_MENU : EXCHANGE_SCREEN;
    s->cpu.x = past_the_last_piece(OVERWORLD_ASIDE, OVERWORLD_PIECES);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_set_flag(s, CPU_FLAG_C, true);
    if (menu_call_long(s, 0xC22552, 0xC28000, DC_AH | DC_Y | DC_C))
        m_stop_at(s, 0xC22556, READS_NOTHING);
}

// $C2:258D
/* The map view: the map faded out, the direct page and the objects put aside
 * while it runs, and the map faded in again. */
static bool open_the_map_view(Snes *s)
{
    GameState *g = state_of(s);
    interrupts(s, 0xC2258E, false);
    start_a_script(s, FADE_OUT_TO_THE_MAP_VIEW);
    interrupts(s, 0xC2259B, true);
    if (!wait_frames(s, 0xC225A1, 0x22))
        return false;
    stop_the_screen(s, (StopSites){ 0xC225A6, 0xC225A9, 0xC225AC }, true);
    put_aside(g, MAP_VIEW_ASIDE, MAP_VIEW_PIECES);
    if (!menu_run(s, 0xC225CF, 0xC2631F, menu_epoch_map_screen))
        return false;
    bring_back(g, MAP_VIEW_ASIDE, MAP_VIEW_PIECES);
    if (!reset_the_uploads(s, 0xC225F0) || !load_the_map(s, 0xC225F3))
        return false;
    start_a_script(s, FADE_IN_FROM_THE_MAP_VIEW);
    set_the_sign_again(g);
    show_again(s, 0xC2260C);
    if (!wait_frames(s, 0xC22612, 0x29))
        return false;
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return true;
}

// $C2:261D
/* The time dial: the map faded out, the effects brought back up, the
 * overworld put aside while the dial runs; back on the map as before, unless
 * the dial sent the party on through time. */
static bool run_the_time_dial(Snes *s)
{
    GameState *g = state_of(s);
    interrupts(s, 0xC2261E, false);
    start_a_script(s, FADE_OUT);
    interrupts(s, 0xC2262B, true);
    if (!wait_frames(s, 0xC22631, 0x12))
        return false;
    stop_the_screen(s, (StopSites){ 0xC22636, 0xC22639, 0xC2263C }, true);
    au_fade(m_platform(s), PF_CALL_DC(0xC2264E, 0xC70004, DRIVER_IGNORES), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    put_aside(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (!menu_run(s, 0xC22655, 0xC26A34, menu_epoch_time_dial_screen))
        return false;
    bring_back(g, OVERWORLD_ASIDE, OVERWORLD_PIECES);
    if (!reset_the_uploads(s, 0xC2265B))
        return false;
    bool sent_on = the_dial_sent_the_party_on(g);
    if (!sent_on) {
        if (!load_the_map(s, 0xC2266E))
            return false;
        start_a_script(s, FADE_IN);
        set_the_sign_again(g);
    }
    show_again(s, 0xC22687);
    if (sent_on)
        return true;
    if (!wait_frames(s, 0xC2269D, 0x11))
        return false;
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    return true;
}

/* ---- The loop ----------------------------------------------------------------------------- */

/* The loop's head, where each frame starts. */
enum { NEXT_FRAME = 0xC223CF };

void overworld_run_a_frame(Snes *s)
{
    GameState *g = state_of(s);
    if (!wait_for_the_next_frame(s))
        return;
    OverworldMode mode = overworld_mode(g);
    if (mode == OVERWORLD_HALTED || mode >= OVERWORLD_MODES
        || (mode == OVERWORLD_ENTERING_A_PLACE && !overworld_exit_here_is_in_range(g))) {
        /* halted, a mode the loop doesn't know, or an exit past the lists:
         * the original's dispatch, with the machine as the wait left it */
        m_stop_at(s, 0xC223D2, 0);
        return;
    }
    enter_the_arm(s, mode);
    switch (mode) {
    case OVERWORLD_STARTING:
    case OVERWORLD_ON_THE_MAP:
    case OVERWORLD_BACK_ON_THE_MAP:
        send_the_waiting_sound(s);
        break;
    case OVERWORLD_LEAVING:
        leave_the_world_map(s);
        return;
    case OVERWORLD_ENTERING_A_PLACE:
        if (!enter_the_place(s))
            return;
        break;
    case OVERWORLD_OPENING_THE_MENUS:
    case OVERWORLD_EXCHANGING_THE_PARTY:
        open_the_menus(s, mode);
        return;
    case OVERWORLD_OPENING_THE_MAP_VIEW:
        if (!open_the_map_view(s))
            return;
        break;
    case OVERWORLD_TRAVELLING_THROUGH_TIME:
        if (!run_the_time_dial(s))
            return;
        break;
    default:
        break;
    }
    m_stop_at(s, NEXT_FRAME, READS_NOTHING);
}

void overworld_come_back_from_the_menus(Snes *s)
{
    if (come_back_from_the_menus(s))
        m_stop_at(s, NEXT_FRAME, READS_NOTHING);
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
