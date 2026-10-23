/* Scenes: the program's start and its end.
 *
 * Every scene program starts the same way. Its state is put as a scene starts
 * it -- the fades a step every four frames, no race won, no pause, no song on
 * its way, the screen dark, no per-line effects -- and its work RAM cleared;
 * then, unless it is the title coming back with its line interrupt (which
 * keeps what that clear spares), the sound is put back to normal
 * (scene/sound.c), every sprite hidden and the screen set to the one every
 * scene starts from (scene/screen.c). Then the scene's own setup, by its mode:
 * the title's (scene/title.c), the race's, the attract sequence's pages of
 * credits (scene/credit_pages.c), the palace rising (scene/rising_palace.c),
 * Lavos falling to the planet (scene/lavos_falls.c), the Epoch's dive
 * (scene/epoch_dive.c), the shared endings' and the staff roll's are C, the
 * others still the original's (the setups' table below); and its first frame: the palette the setup made sent, a frame
 * waited out, the frame's work done once (scene/frame.c) and the interrupts
 * on. The program's main loop then runs
 * (attract/main_loop.c) until a script ends the scene, when the program blanks
 * the screen (scene/screen.c), puts the sound back to normal and hands the
 * game back to the field's start, on the game's fresh stack.
 *
 * The start is cut where the original waits on something a port can't hold:
 * the sound driver taking the last of the commands that put the sound back,
 * and a setup's own wait (the title waits for its music to start). The start
 * goes on from each wait's return, which is an entry of its own.
 *
 * All of it is the program's code, in work RAM, so every entry is guarded by
 * a hash of its code. */
#include "scene/scene.h"

#include "charge.h"
#include "original_effects.h"
#include "core/console.h"
#include "core/program.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The program's state, as a scene starts it ------------------------------------------------ */

/* The fades step every four frames until a scene sets its own pace; the
 * window shapes' last corner starts at the screen's left. */
enum { FADE_PACE = 4, DARK = 0, NO_LINE_EFFECTS = 0, NO_SONG = 0, LAST_CORNER = 3 };

/* The scene's own direct page, where the program's cells are. */
enum { SCENE_PAGE = 0x0000 };

// $7E:3063
static void reset_the_program(GameState *g)
{
    set_scene_fade_period(g, FADE_PACE);
    set_scene_fade_countdown(g, FADE_PACE);
    set_scene_race_winner(g, 0);
    set_scene_race_prize(g, 0);
    set_scene_pause_enabled(g, 0);
    set_scene_paused(g, 0);
    set_scene_start_held(g, 0);
    set_scene_reset_disabled(g, 0);
    set_scene_song_sending(g, 0);
    set_scene_brightness(g, DARK);
    set_scene_line_effects(g, NO_LINE_EFFECTS);
    set_field_window_shape_corners_x(g, LAST_CORNER, 0);
}

/* Every sprite hidden, and the screen every scene starts from. */
static void start_from_the_first_screen(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0x7E3843, scene_hide_every_sprite(pf));
    TIMED_AS(g, 0x7E30F9, scene_set_up_first_screen(g, pf));
}

/* The screen's setup is written through a direct page on the register block
 * and leaves 8-bit A at 0, with 16-bit index registers, as the program's
 * setups take it. */
static void as_the_screen_s_setup_leaves_it(Snes *s)
{
    s->cpu.p = (uint8_t)((s->cpu.p | CPU_FLAG_M) & ~CPU_FLAG_X);
    s->cpu.a = 0x0000;
}

/* ---- The scene's setup, by its mode ------------------------------------------------------------ */

/* The low bits of scene_flags are the scene's mode, which picks its setup:
 * the race's (scene/race.c; the attract sequence's race is mode 9), the
 * attract sequence's pages of credits (scene/credit_pages.c), the title's
 * (scene/title.c), the palace rising (scene/rising_palace.c), Lavos falling
 * to the planet (scene/lavos_falls.c), the Epoch's dive (scene/epoch_dive.c),
 * the planet's (scene/planet.c), the shared endings' (scene/endings.c)
 * and the staff roll's (scene/staff_roll.c) are C;
 * the attract sequence's other scenes, and the ones no movie reaches, are
 * still the original's, called by the routine the program's own table names
 * for the mode. */
enum { SCENE_MODE_BITS = 0x1F, SCENE_MODES = 16, PROGRAM_BANK = 0x7E0000 };

typedef enum {
    MODE_RACE = 0, MODE_CREDITS_PAGE = 1, MODE_TITLE = 2, MODE_RISING_PALACE = 3, MODE_LAVOS_FALLS = 4, MODE_EPOCH_DIVE = 6,
    MODE_PLANET_LOST = 7, MODE_PLANET_ENDING = 8, MODE_ATTRACT_RACE = 9,
    MODE_ENDING = 10, MODE_ENDING_TOO = 11, MODE_STAFF_ROLL = 13, MODE_ENDING_AGAIN = 14, MODE_LAST_ENDING = 15,
} SceneMode;

/* The setups' routines, by which their time is reckoned. */
enum {
    RACE_SET_UP = 0x7E3B48, CREDIT_PAGES_SET_UP = 0x7E6B04, TITLE_SET_UP = 0x7E5978, PALACE_SET_UP = 0x7E671F,
    ENDINGS_SET_UP = 0x7E8538, STAFF_ROLL_SET_UP = 0x7E8EA8, LAVOS_FALLS_SET_UP = 0x7E53BB, EPOCH_DIVE_SET_UP = 0x7E78DD,
    PLANET_SET_UP = 0x7E6248,
};

static const struct {
    SceneMode mode;
    uint32_t routine;
    bool (*set_up)(Snes *s);
    bool (*present)(Snes *s);
    bool binary;   /* its code adds, so it runs in binary mode */
} setups[] = {
    { MODE_RACE, RACE_SET_UP, scene_race_set_up_scene, scene_race_set_up_present, false },
    { MODE_CREDITS_PAGE, CREDIT_PAGES_SET_UP, scene_credit_pages_set_up_scene, scene_credit_pages_set_up_present, false },
    { MODE_TITLE, TITLE_SET_UP, scene_title_set_up_scene, scene_title_set_up_present, false },
    { MODE_RISING_PALACE, PALACE_SET_UP, scene_palace_set_up_scene, scene_palace_set_up_present, false },
    { MODE_LAVOS_FALLS, LAVOS_FALLS_SET_UP, scene_lavos_falls_set_up_scene, scene_lavos_falls_set_up_present, false },
    { MODE_EPOCH_DIVE, EPOCH_DIVE_SET_UP, scene_dive_set_up_scene, scene_dive_set_up_present, true },
    { MODE_PLANET_LOST, PLANET_SET_UP, scene_planet_set_up_scene, scene_planet_set_up_present, false },
    { MODE_PLANET_ENDING, PLANET_SET_UP, scene_planet_set_up_scene, scene_planet_set_up_present, false },
    { MODE_ATTRACT_RACE, RACE_SET_UP, scene_race_set_up_scene, scene_race_set_up_present, false },
    { MODE_ENDING, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
    { MODE_ENDING_TOO, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
    { MODE_STAFF_ROLL, STAFF_ROLL_SET_UP, scene_staff_roll_set_up_scene, scene_staff_roll_set_up_present, false },
    { MODE_ENDING_AGAIN, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
    { MODE_LAST_ENDING, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
};
enum { SETUP_PORTS = sizeof setups / sizeof setups[0] };

/* What the program's dispatch leaves a setup: the mode doubled in A and X,
 * the flags of the index's move to X, and the carry its doubling left clear.
 * The setups are reached through a table in RAM, which liveness can't follow,
 * so every register is as the original has it; Y and the overflow are as the
 * program was started with them, which none of the start changes. */
static void enter_as_the_dispatch_does(Snes *s, uint8_t mode)
{
    uint16_t index = (uint16_t)(mode * 2);
    s->cpu.a = index;
    s->cpu.x = index;
    m_set_nz16(s, index);
    m_set_flag(s, CPU_FLAG_C, false);
}

/* The dispatch's call, whose return the setups run under; a scene whose
 * setup is the original's is a call of the routine the program's own table
 * names for its mode. */
enum { SETUP_DISPATCH = 0x7E300A, THE_SETUPS_MAY_READ_ANYTHING = 0 };

static void first_frame(Snes *s);

/* The scene's setup as C where it is a port and the program's code for it is
 * the code the port stands for, then its first frame. Every setup still
 * calls the original's code (the unpacker's decompressor, a script's first
 * step, the sound driver), whose calls the original makes with the dispatch's
 * return under them, so the setup runs with it there, as the stack is at
 * those calls; and the title's setup ends in the original's wait for its song
 * to start, which returns through it to the first frame ($7E:300D). */
static void set_the_scene_up(Snes *s)
{
    uint8_t mode = scene_flags(state_of(s)) & SCENE_MODE_BITS;
    enter_as_the_dispatch_does(s, mode);
    for (unsigned n = 0; n < SETUP_PORTS; n++) {
        if (setups[n].mode != mode || (setups[n].binary && m_flag(s, CPU_FLAG_D)) || !setups[n].present(s))
            continue;
        bool done = false;
        scene_bridge_enter(s, SETUP_DISPATCH);
        NATIVE_TIME_AS(s, setups[n].routine, done = setups[n].set_up(s));
        if (!done)
            return;
        if (mode == MODE_TITLE) {
            scene_title_wait_for_its_song(s);
            return;
        }
        scene_bridge_leave(s);
        first_frame(s);
        return;
    }
    uint16_t original_setup = scene_program_setups(state_of(s), mode);
    if (scene_run(s, SETUP_DISPATCH, PROGRAM_BANK | original_setup, THE_SETUPS_MAY_READ_ANYTHING, NULL))
        first_frame(s);
}

/* ---- The scene's first frame ------------------------------------------------------------------- */

enum {
    FIRST_WAIT = 0x7E3010, FRAME_WAIT = 0x7E31B0, FRAME_WORK_CALL = 0x7E3013, FRAME_WORK = 0xC308D0,
    MAIN_LOOP = 0x7E3023,
};
enum {
    THE_WAIT_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    THE_FRAME_WORK_READS_Y_AND_THE_OVERFLOW = DC_A | DC_X | DC_N | DC_Z | DC_C,
    THE_LOOP_READS_THE_CARRY = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z,
};

// $7E:300D
/* The palette the setup made sent, a frame waited out, the frame's work done
 * once, and the vblank and line interrupts and the controllers' reading on;
 * the scene neither over nor paused, the main loop runs (16-bit A). */
static void first_frame(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    TIMED_AS(g, 0x7E388B, scene_send_palette(pf));
    tm_wait_frame(pf, PF_CALL_DC(FIRST_WAIT, FRAME_WAIT, THE_WAIT_READS_NOTHING));
    if (!scene_run_long(s, FRAME_WORK_CALL, FRAME_WORK, THE_FRAME_WORK_READS_Y_AND_THE_OVERFLOW, scene_frame_work))
        return;
    tm_enable_interrupts(pf, PF_AT(0x7E3019), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    set_scene_program_over(g, 0);
    set_scene_paused(g, 0);
    m_set_flag(s, CPU_FLAG_M, false);
    m_stop_at(s, MAIN_LOOP, THE_LOOP_READS_THE_CARRY);
}

/* ---- The entries --------------------------------------------------------------------------------- */

/* The start's own routine ($7E:3063), which the program calls first: its
 * return is on the stack under everything it calls, and it returns to the
 * setup's dispatch. */
enum { PROGRAM_START = 0x7E3000, SOUND_CALL = 0x7E30F3, SOUND_BACK_TO_NORMAL = 0x7E3177 };
enum { THE_SOUND_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The clear works through the DMA registers' page, which it leaves in A's
 * high byte; the sound's commands hand it back to the start untouched. */
static void leave_the_clear_s_page_in_a(Snes *s) { s->cpu.a = (uint16_t)(M_DP_DMA | (s->cpu.a & 0x00FF)); }

// $7E:3000
/* The program's start, from the boot on the field's page. The clear's matrix
 * value is what A holds there, the fades' pace. */
void scene_program_start(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    scene_bridge_enter(s, PROGRAM_START);
    reset_the_program(g);
    bool title_coming_back = false;
    TIMED_AS(g, 0x7E3092, title_coming_back = scene_clear_the_work_ram(g, pf, FADE_PACE));
    s->cpu.d = SCENE_PAGE;
    if (!title_coming_back) {
        if (!(scene_flags(g) & SCENE_FLAG_NO_SONGS)) {
            set_scene_song(g, NO_SONG);
            ORIGINAL_EFFECT(leave_the_clear_s_page_in_a(s));
            if (!scene_run(s, SOUND_CALL, SOUND_BACK_TO_NORMAL, THE_SOUND_READS_NOTHING, scene_sound_back_to_normal))
                return;
        }
        start_from_the_first_screen(g, pf);
    }
    scene_bridge_leave(s);
    set_the_scene_up(s);
}

// $7E:30F6
/* The start's own routine goes on where the sound's last command returns:
 * every sprite hidden and the first screen, and back to the program. */
void scene_program_goes_on(Snes *s)
{
    start_from_the_first_screen(state_of(s), m_platform(s));
    as_the_screen_s_setup_leaves_it(s);
    m_rts(s);
}

// $7E:3003
/* The scene set up and its first frame, where the start's own routine returns
 * after the sound's commands. */
void scene_setup_and_first_frame(Snes *s) { set_the_scene_up(s); }

/* The first frame, where a setup that waited returns. */
void scene_first_frame(Snes *s) { first_frame(s); }

/* ---- The program's end ------------------------------------------------------------------------------ */

enum { FIELD_STARTS = 0xC00000, THE_FIELD_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The game back to the field's start, on the game's fresh stack, which the
 * original sets through X. */
void scene_hand_back_to_the_field(Snes *s)
{
    s->cpu.x = CONSOLE_STACK_TOP;
    s->cpu.s = CONSOLE_STACK_TOP;
    m_stop_at(s, FIELD_STARTS, THE_FIELD_READS_NOTHING);
}

// $7E:303B
/* Where the sound's last command returns at the program's end. */
void scene_program_hands_back(Snes *s) { scene_hand_back_to_the_field(s); }

/* ---- The guards ---------------------------------------------------------------------------------- */

/* The start, its table of setups and the routines it runs inline as one
 * stretch of the program; the sprites' hiding and the palette's sending.
 * The main loop's call of the frame's own routine is left out: the race's
 * setup points it at the race's (the loop is the original's, past where the
 * start stops). The program's stores reach low RAM and the I/O registers
 * through the data bank; and the mode is one of the table's. */
static bool start_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E3000, 0x24, 0x86AB875D97E1ED1Cull) &&
           m_code_matches(s, 0x7E3026, 0x18A, 0x60EA7B591D51F309ull) &&
           m_code_matches(s, 0x7E3843, 45, 0x36CEE4DC10A522A8ull) &&
           m_code_matches(s, 0x7E388B, 36, 0x84B3B18FAFEEAE44ull);
}

bool scene_program_start_present(Snes *s)
{
    if (!start_present(s))
        return false;
    vars_peek(true);
    bool ok = (scene_flags(state_of(s)) & SCENE_MODE_BITS) < SCENE_MODES;
    vars_peek(false);
    return ok;
}

/* Where the sound's last command returns, the setups' dispatch takes A's high
 * byte into its index with the mode: the start's routine leaves it 0. */
bool scene_setup_present(Snes *s) { return (s->cpu.a & 0xFF00) == 0 && scene_program_start_present(s); }

bool scene_program_end_present(Snes *s) { return m_code_matches(s, 0x7E303B, 8, 0xF200A66DE169CF3Aull); }

/* ---- The program as C, with no machine ------------------------------------------------------------ */

/* The setups the port has as C for a program with no machine: the title's. */
static bool set_up_as_c(GameState *g, Platform *pf, uint8_t mode)
{
    if (mode == MODE_TITLE) {
        bool done = false;
        TIMED_AS(g, TITLE_SET_UP, done = scene_title_set_up_as_c(g, pf));
        return done;
    }
    program_needs_the_machine(g, PROGRAM_BANK | scene_program_setups(g, mode), "a scene's setup only the machine runs");
    return false;
}

/* The title's setup waits for its song to start playing ($7E:5AAA), a frame
 * at a time where a host has the driver's time pass. */
enum { MUSIC_WAIT = 0x7E5AAA };

// $7E:300D
static bool first_frame_as_c(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0x7E388B, scene_send_palette(pf));
    tm_wait_frame(pf, PF_CALL_DC(FIRST_WAIT, FRAME_WAIT, THE_WAIT_READS_NOTHING));
    bool done = false;
    TIMED_AS(g, FRAME_WORK, done = scene_frame_work_as_c(g, pf));
    if (!done)
        return false;
    tm_enable_interrupts(pf, PF_AT(0x7E3019), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    set_scene_program_over(g, 0);
    set_scene_paused(g, 0);
    return true;
}

// $7E:3000 (and the boot, $C3:0000, before it; the main loop, $7E:3023, and the end, $7E:302A, after)
bool scene_runs(GameState *g, Platform *pf, uint8_t flags)
{
    (void)scene_boots(g, pf, flags);
    reset_the_program(g);
    bool title_coming_back = false;
    TIMED_AS(g, 0x7E3092, title_coming_back = scene_clear_the_work_ram(g, pf, FADE_PACE));
    if (!title_coming_back) {
        if (!(scene_flags(g) & SCENE_FLAG_NO_SONGS)) {
            set_scene_song(g, NO_SONG);
            TIMED_AS(g, SOUND_BACK_TO_NORMAL, scene_sound_back_to_normal_as_c(g, pf));
        }
        start_from_the_first_screen(g, pf);
    }
    uint8_t mode = scene_flags(g) & SCENE_MODE_BITS;
    if (!set_up_as_c(g, pf, mode))
        return false;
    if (mode == MODE_TITLE)
        while (!scene_music_is_playing(pf))
            tm_wait_frame(pf, PF_AT(MUSIC_WAIT));
    if (!first_frame_as_c(g, pf))
        return false;
    while (!scene_program_over(g))
        tm_wait_frame(pf, PF_AT(MAIN_LOOP));
    scene_stops_the_display(pf);
    TIMED_AS(g, SOUND_BACK_TO_NORMAL, scene_sound_back_to_normal_as_c(g, pf));
    return true;
}
