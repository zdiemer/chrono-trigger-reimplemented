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
 * (scene/epoch_dive.c), the shared endings', the unused panoramas and turning
 * figures, and the staff roll's are all C; and its first frame: the palette
 * the setup made sent, a frame waited out, the frame's work done once
 * (scene/frame.c) and the interrupts on. The program's main loop then makes
 * the call the setup named each pass until a script ends the scene, when the
 * program blanks the screen (scene/screen.c), puts the sound back to normal
 * and hands the game back to the field's start, on the game's fresh stack.
 *
 * The same sequence runs twice here: with no machine under it (scene_runs, the
 * program from the boot to its end), and on the machine in the shipping mode,
 * where the boot starts it, the main loop is a held wait whose passes are the
 * runtime's continuations. Setups, frame work and scene routines are checked
 * C calls, all retired from byte-exact comparison (docs/REIMPLEMENTATION.md,
 * "The scene engine's teardown").
 *
 * All of it is the program's code, in work RAM, where other code and data are
 * at other times, so its contracts check a hash of its code. */
#include "scene/scene.h"

#include "charge.h"
#include "core/console.h"
#include "core/program.h"
#include "field/main_loop.h"
#include "holds.h"
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

/* ---- The scene's setup, by its mode ------------------------------------------------------------ */

/* The low bits of scene_flags are the scene's mode, which picks its setup:
 * the race's (scene/race.c; the attract sequence's race is mode 9), the
 * attract sequence's pages of credits (scene/credit_pages.c), the title's
 * (scene/title.c), the palace rising (scene/rising_palace.c), Lavos falling
 * to the planet (scene/lavos_falls.c), the Epoch's dive (scene/epoch_dive.c),
 * the planet's (scene/planet.c), the shared endings' (scene/endings.c)
 * and the staff roll's, fast and slow (scene/staff_roll.c). All sixteen
 * modes have C setups, including the ones no movie reaches. */
enum { SCENE_MODE_BITS = 0x1F, SCENE_MODES = 16, PROGRAM_BANK = 0x7E0000 };

/* The setups' routines, by which their time is reckoned. */
enum {
    RACE_SET_UP = 0x7E3B48, CREDIT_PAGES_SET_UP = 0x7E6B04, TITLE_SET_UP = 0x7E5978, PALACE_SET_UP = 0x7E671F,
    ENDINGS_SET_UP = 0x7E8538, STAFF_ROLL_SET_UP = 0x7E8EA8, LAVOS_FALLS_SET_UP = 0x7E53BB, EPOCH_DIVE_SET_UP = 0x7E78DD,
    TURN_SET_UP = 0x7E7092, PLANET_SET_UP = 0x7E6248, SLOW_STAFF_ROLL_SET_UP = 0x7E8E95,
};

static const struct {
    SceneMode mode;
    uint32_t routine;
    bool (*set_up)(Snes *s);
    bool (*present)(Snes *s);
    bool binary;   /* its code adds, so it runs in binary mode */
} setups[] = {
    { SCENE_MODE_TURNING_MAP, TURN_SET_UP, scene_turn_set_up_scene, scene_turn_set_up_present, true },
    { SCENE_MODE_RACE, RACE_SET_UP, scene_race_set_up_scene, scene_race_set_up_present, false },
    { SCENE_MODE_CREDITS_PAGE, CREDIT_PAGES_SET_UP, scene_credit_pages_set_up_scene, scene_credit_pages_set_up_present, false },
    { SCENE_MODE_TITLE, TITLE_SET_UP, scene_title_set_up_scene, scene_title_set_up_present, false },
    { SCENE_MODE_RISING_PALACE, PALACE_SET_UP, scene_palace_set_up_scene, scene_palace_set_up_present, false },
    { SCENE_MODE_LAVOS_FALLS, LAVOS_FALLS_SET_UP, scene_lavos_falls_set_up_scene, scene_lavos_falls_set_up_present, false },
    { SCENE_MODE_EPOCH_DIVE, EPOCH_DIVE_SET_UP, scene_dive_set_up_scene, scene_dive_set_up_present, true },
    { SCENE_MODE_PLANET_LOST, PLANET_SET_UP, scene_planet_set_up_scene, scene_planet_set_up_present, false },
    { SCENE_MODE_PLANET_ENDING, PLANET_SET_UP, scene_planet_set_up_scene, scene_planet_set_up_present, false },
    { SCENE_MODE_ATTRACT_RACE, RACE_SET_UP, scene_race_set_up_scene, scene_race_set_up_present, false },
    { SCENE_MODE_ENDING_CAPTIONED, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
    { SCENE_MODE_ENDING_CAPTIONED_TOO, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
    { SCENE_MODE_STAFF_ROLL, STAFF_ROLL_SET_UP, scene_staff_roll_set_up_scene, scene_staff_roll_set_up_present, false },
    { SCENE_MODE_SLOW_STAFF_ROLL, SLOW_STAFF_ROLL_SET_UP, scene_slow_staff_roll_set_up_scene, scene_slow_staff_roll_set_up_present,
      false },
    { SCENE_MODE_ENDING_AGAIN, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
    { SCENE_MODE_ENDING_WITHOUT_CREDITS, ENDINGS_SET_UP, scene_ending_set_up_scene, scene_ending_set_up_present, true },
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

/* The dispatch's return frame, retained across a setup's held wait. */
enum { SETUP_DISPATCH = 0x7E300A, THE_SETUPS_MAY_READ_ANYTHING = 0 };

static void first_frame_on_the_machine(Snes *s);

/* Run the selected setup under the dispatch's return frame. The title's
 * held song wait retains that frame and resumes at the first frame. */
static void set_the_scene_up(Snes *s)
{
    uint8_t mode = scene_flags(state_of(s)) & SCENE_MODE_BITS;
    enter_as_the_dispatch_does(s, mode);
    for (unsigned n = 0; n < SETUP_PORTS; n++) {
        if (setups[n].mode != mode || (setups[n].binary && m_flag(s, CPU_FLAG_D)) || !setups[n].present(s))
            continue;
        bool done = false;
        uint16_t stack = s->cpu.s;
        scene_bridge_enter(s, SETUP_DISPATCH);
        NATIVE_TIME_AS(s, setups[n].routine, done = setups[n].set_up(s));
        if (!done)
            return;
        if (mode == SCENE_MODE_TITLE) {
            scene_title_wait_for_its_song(s);
            if (m_came_back(s, SETUP_DISPATCH + 3, stack))
                first_frame_on_the_machine(s);
            return;
        }
        scene_bridge_leave(s);
        first_frame_on_the_machine(s);
        return;
    }
    uint16_t original_setup = scene_program_setups(state_of(s), mode);
    if (scene_run(s, SETUP_DISPATCH, PROGRAM_BANK | original_setup, THE_SETUPS_MAY_READ_ANYTHING, NULL))
        first_frame_on_the_machine(s);
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
 * the scene neither over nor paused, and the main loop's wait held (16-bit
 * A). */
static void first_frame_on_the_machine(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    TIMED_AS(g, 0x7E388B, scene_send_palette(pf));
    tm_wait_frame(pf, PF_CALL_DC(FIRST_WAIT, FRAME_WAIT, THE_WAIT_READS_NOTHING));
    if (!scene_run_long(s, FRAME_WORK_CALL, FRAME_WORK, THE_FRAME_WORK_READS_Y_AND_THE_OVERFLOW, scene_frame_work_entry))
        return;
    tm_enable_interrupts(pf, PF_AT(0x7E3019), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    set_scene_program_over(g, 0);
    set_scene_paused(g, 0);
    m_set_flag(s, CPU_FLAG_M, false);
    m_hold_at(s, MAIN_LOOP, THE_LOOP_READS_THE_CARRY, HOLD_SCENE_MAIN_LOOP);
}

/* ---- The program's start, on the machine ------------------------------------------------------- */

enum { PROGRAM_START = 0x7E3000, CLEAR_WORK_RAM = 0x7E3092, SOUND_BACK_TO_NORMAL = 0x7E3177 };

/* The sound put back to normal as a scene starts, unless the scene has no
 * songs: no song on its way, then the driver's commands (scene/sound.c). */
static void put_the_sound_back(GameState *g, Platform *pf)
{
    if (scene_flags(g) & SCENE_FLAG_NO_SONGS)
        return;
    set_scene_song(g, NO_SONG);
    TIMED_AS(g, SOUND_BACK_TO_NORMAL, scene_sound_back_to_normal(g, pf));
}

/* The program's start, from the boot on the field's page: the start's own
 * routine ($7E:3063), whose return is on the stack under everything it calls,
 * then the setup and the first frame. The clear's matrix value is the fades'
 * pace, which the original has in A there. */
static void start_the_program(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    scene_bridge_enter(s, PROGRAM_START);
    reset_the_program(g);
    bool title_coming_back = false;
    TIMED_AS(g, CLEAR_WORK_RAM, title_coming_back = scene_clear_the_work_ram(g, pf, FADE_PACE));
    s->cpu.d = SCENE_PAGE;
    if (!title_coming_back) {
        put_the_sound_back(g, pf);
        start_from_the_first_screen(g, pf);
    }
    scene_bridge_leave(s);
    set_the_scene_up(s);
}

/* The program's contract: entered from the boot with an 8-bit accumulator and
 * 16-bit index registers; its start, the routines it runs inline, its main
 * loop's test, its end and its sound routine the code the port stands for
 * (the main loop's call is left out of the hash: a setup points it at its
 * scene's routine); its stores reaching low RAM and the I/O registers through
 * the data bank; and the mode one of the table's. */
static bool start_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E3000, 0x24, 0x86AB875D97E1ED1Cull) &&
           m_code_matches(s, 0x7E3026, 0x18A, 0x60EA7B591D51F309ull) &&
           m_code_matches(s, 0x7E3843, 45, 0x36CEE4DC10A522A8ull) &&
           m_code_matches(s, 0x7E388B, 36, 0x84B3B18FAFEEAE44ull);
}

static bool start_takes(Snes *s)
{
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M || !start_present(s))
        return false;
    vars_peek(true);
    bool ok = (scene_flags(state_of(s)) & SCENE_MODE_BITS) < SCENE_MODES;
    vars_peek(false);
    return ok;
}

// $7E:3000
/* Start the retired C program at its checked boundary. Invalid callers
 * report a broken contract and stop at the original entry. */
void scene_program_starts_on_the_machine(Snes *s)
{
    m_jump_to(s, PROGRAM_START);
    if (!start_takes(s)) {
        program_breaks_a_contract(state_of(s), PROGRAM_START, "a scene program's start outside its contract");
        m_stop_at_own(s, PROGRAM_START, 0);
        return;
    }
    NATIVE_TIME_AS_SIBLING(s, PROGRAM_START, start_the_program(s));
}

/* ---- The main loop, on the machine ------------------------------------------------------------ */

/* The routines a pass calls as C (main_loop_pass, below), each checked
 * against its widths, arithmetic mode, page, bank and code image. The race uses a called machine form because its
 * upload wait holds the caller's frame between racers. */
static const struct {
    ScenePass call;
    void (*entry)(Snes *s);
} PASS_ENTRIES[] = {
    { SCENE_PASS_ENDING_SCALING, scene_ending_scale_picture_entry },
    { SCENE_PASS_DIVE_SCALING, scene_scale_the_epoch_entry },
    { SCENE_PASS_TURN_MAP, scene_turn_map_entry },
};
enum { PASS_ENTRY_COUNT = sizeof PASS_ENTRIES / sizeof PASS_ENTRIES[0] };

static bool main_loop_pass(GameState *g, Platform *pf);

/* Keep the race's return frame for its held upload wait. As with the old
 * call-out, the frame belongs to the caller's time, and the scaling's work
 * is settled at its own rate before returning or handing off the wait. */
static bool race_scaling_on_the_machine(Snes *s)
{
    uint16_t stack = s->cpu.s;
    uint32_t routine = PROGRAM_BANK | SCENE_PASS_RACE_SCALING;
    m_push16(s, (uint16_t)(MAIN_LOOP + 2));
    if (!scene_accept_continuation(s, routine, scene_race_scale_pictures_entry))
        return false;
    GameState *g = state_of(s);
    charge_settle(g);
    TIMED_AS(g, routine, {
        scene_race_scale_pictures_entry(s);
        charge_settle(g);
    });
    return m_came_back(s, MAIN_LOOP + 3, stack);
}

/* Run the pass's checked C operation. False when it holds a wait or reports
 * a broken contract at the original call boundary. */
static bool pass_on_the_machine(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t call = scene_main_loop_call(g);
    if (call == SCENE_PASS_NOTHING)
        return true;
    if (call == SCENE_PASS_RACE_SCALING)
        return race_scaling_on_the_machine(s);
    for (unsigned n = 0; n < PASS_ENTRY_COUNT; n++) {
        if (PASS_ENTRIES[n].call != call)
            continue;
        const NativeEntry *e = native_hand_entry_at(s, PROGRAM_BANK | call);
        if ((!e || e->fn != PASS_ENTRIES[n].entry) && !scene_machine_form_takes(s, PASS_ENTRIES[n].entry))
            break;
        if (!main_loop_pass(g, m_platform(s)))
            program_breaks_a_contract(g, PROGRAM_BANK | call, "a scene's main loop call its guard took and its C didn't");
        return true;
    }
    program_breaks_a_contract(g, PROGRAM_BANK | call, "a scene main-loop call outside its contract");
    scene_bridge_enter(s, MAIN_LOOP);
    /* No C target is permitted: the common boundary leaves every register
     * live and stops at the rejected call's entry with its frame intact. */
    (void)scene_accept_continuation(s, PROGRAM_BANK | call, NULL);
    return false;
}

/* The program's end ($7E:302A, and its hand-back $7E:303B): its code in
 * place, and the field's location a number for the field's start the game
 * goes back to. */
enum { PROGRAM_END = 0x7E302A, FIELD_STARTS = 0xC00000 };

static bool end_present(Snes *s)
{
    return m_code_matches(s, PROGRAM_END, 14, 0x614033C5F8C83156ull) &&
           m_code_matches(s, 0x7E303B, 8, 0xF200A66DE169CF3Aull) && field_location_is_a_number(s);
}

/* The game back to the field's start, on the game's fresh stack, which the
 * original sets through X. */
static void hand_back_to_the_field(Snes *s)
{
    s->cpu.x = CONSOLE_STACK_TOP;
    s->cpu.s = CONSOLE_STACK_TOP;
    m_jump_to(s, FIELD_STARTS);
    /* Field loading keeps its own timing report across this tail call. */
    if (scene_accept_continuation(s, FIELD_STARTS, field_enter))
        NATIVE_TIME_AS_SIBLING(s, FIELD_STARTS, field_enter(s));
}

// $7E:302A
/* A script has ended the scene: interrupts and per-line effects off and the
 * screen blanked, the sound put back to normal, and the game handed back to
 * the field's start. An end not in place or with 8-bit index registers (its code takes
 * 16-bit ones), which the game never makes, is a
 * contract broken, and the cartridge's end runs instead. */
static void end_on_the_machine(Snes *s)
{
    GameState *g = state_of(s);
    if (m_flag(s, CPU_FLAG_X) || !end_present(s)) {
        program_breaks_a_contract(g, PROGRAM_END, "a scene program's end outside its contract");
        m_stop_at_own(s, PROGRAM_END, 0);
        return;
    }
    TIMED_AS(g, PROGRAM_END, scene_stops_the_display(m_platform(s)));
    TIMED_AS(g, SOUND_BACK_TO_NORMAL, scene_sound_back_to_normal(g, m_platform(s)));
    m_set_flag(s, CPU_FLAG_M, true);   /* the field's start takes an 8-bit accumulator, as the end leaves it */
    hand_back_to_the_field(s);
}

// $7E:3023
/* The main thread rests in the program's main loop at every frame end of a
 * scene, the frame's work done by the vblank handler, so the loop is a held
 * wait (holds.h): the first frame holds it, and in the shipping mode the
 * runtime runs these two continuations where the machine names it (the
 * subsystem is retired, so elsewhere the cartridge's loop runs). Each pass
 * makes the call a setup named -- nothing, or the scaling of a picture the
 * scene draws as it goes -- then looks whether a sequence script has ended
 * the scene (scene_program_over; scene/script.c), and ends the program once
 * one has. */
void scene_program_next_pass(Snes *s)
{
    if (!pass_on_the_machine(s))
        return;
    scene_program_finishes_pass(s);
}

// $7E:3026
/* A scaling pass has returned, possibly after a held upload wait. Finish
 * this pass before choosing the next one, so the scene's end is observed at
 * the same boundary as an immediate return. */
void scene_program_finishes_pass(Snes *s)
{
    if (scene_program_over(state_of(s))) {
        end_on_the_machine(s);
        return;
    }
    m_hold_at(s, MAIN_LOOP, THE_LOOP_READS_THE_CARRY, HOLD_SCENE_MAIN_LOOP);
}

/* A pass with nothing to call and the scene going on: the idle continuation,
 * which goes round. */
void scene_program_waits_entry(Snes *s) { (void)s; }

// $7E:3023
/* Rest while the handlers run the scene. The saved head contains all the
 * state this loop needs; waking rechecks the code image and the next pass. */
void scene_program_waits(Snes *s)
{
    do {
        scene_program_waits_entry(s);
    } while (native_idle_held(s));
}

/* The continuations' guards: the program's loop -- its call, and its test
 * after it -- on the scene's page; the idle pass where there is nothing to
 * call and the scene isn't over, the other everywhere else. Each is a claim
 * the game keeps, which the refusal measure checks over real play
 * (the original hand-back paths --refusals). */
enum { JSR = 0x20 };

static bool loop_present(Snes *s)
{
    return snes_bus_peek(&s->bus, MAIN_LOOP) == JSR && m_code_matches(s, MAIN_LOOP + 3, 4, 0x2B0A967642BA802Bull) &&
           m_direct_page(s) == SCENE_PAGE;
}

static bool nothing_to_do(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool idle = scene_main_loop_call(g) == SCENE_PASS_NOTHING && !scene_program_over(g);
    vars_peek(false);
    return idle;
}

bool scene_program_passes(Snes *s) { return loop_present(s) && !nothing_to_do(s); }

bool scene_program_pass_finished(Snes *s) { return loop_present(s); }

bool scene_program_waits_for_the_frame(Snes *s) { return loop_present(s) && nothing_to_do(s); }

/* ---- The program as C, with no machine ------------------------------------------------------------ */

/* The setups the port has as C for a program with no machine: the title's,
 * the credits' page's, the race's (the attract sequence's too), the rising
 * palace's, Lavos falling's and the planet's. */
static const struct {
    SceneMode mode;
    uint32_t routine;
    bool (*set_up)(GameState *g, Platform *pf);
} c_setups[] = {
    { SCENE_MODE_TURNING_MAP, TURN_SET_UP, scene_turn_set_up },
    { SCENE_MODE_TITLE, TITLE_SET_UP, scene_title_set_up },
    { SCENE_MODE_CREDITS_PAGE, CREDIT_PAGES_SET_UP, scene_credit_pages_set_up },
    { SCENE_MODE_RACE, RACE_SET_UP, scene_race_set_up },
    { SCENE_MODE_ATTRACT_RACE, RACE_SET_UP, scene_race_set_up },
    { SCENE_MODE_RISING_PALACE, PALACE_SET_UP, scene_palace_set_up },
    { SCENE_MODE_LAVOS_FALLS, LAVOS_FALLS_SET_UP, scene_lavos_falls_set_up },
    { SCENE_MODE_EPOCH_DIVE, EPOCH_DIVE_SET_UP, scene_dive_set_up },
    { SCENE_MODE_ENDING_CAPTIONED, ENDINGS_SET_UP, scene_ending_set_up },
    { SCENE_MODE_ENDING_CAPTIONED_TOO, ENDINGS_SET_UP, scene_ending_set_up },
    { SCENE_MODE_ENDING_AGAIN, ENDINGS_SET_UP, scene_ending_set_up },
    { SCENE_MODE_ENDING_WITHOUT_CREDITS, ENDINGS_SET_UP, scene_ending_set_up },
    { SCENE_MODE_STAFF_ROLL, STAFF_ROLL_SET_UP, scene_staff_roll_set_up },
    { SCENE_MODE_SLOW_STAFF_ROLL, SLOW_STAFF_ROLL_SET_UP, scene_slow_staff_roll_set_up },
    { SCENE_MODE_PLANET_LOST, PLANET_SET_UP, scene_planet_set_up },
    { SCENE_MODE_PLANET_ENDING, PLANET_SET_UP, scene_planet_set_up },
};
enum { C_SETUPS = sizeof c_setups / sizeof c_setups[0] };

static bool set_up(GameState *g, Platform *pf, uint8_t mode)
{
    for (unsigned n = 0; n < C_SETUPS; n++) {
        if (c_setups[n].mode != mode)
            continue;
        bool done = false;
        TIMED_AS(g, c_setups[n].routine, done = c_setups[n].set_up(g, pf));
        return done;
    }
    program_needs_the_machine(g, PROGRAM_BANK | scene_program_setups(g, mode), "a scene's setup only the machine runs");
    return false;
}

/* The title's setup waits for its song to start playing ($7E:5AAA), a frame
 * at a time where a host has the driver's time pass. */
enum { MUSIC_WAIT = 0x7E5AAA };

// $7E:300D
static bool first_frame(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0x7E388B, scene_send_palette(pf));
    tm_wait_frame(pf, PF_CALL_DC(FIRST_WAIT, FRAME_WAIT, THE_WAIT_READS_NOTHING));
    bool done = false;
    TIMED_AS(g, FRAME_WORK, done = scene_frame_work(g, pf));
    if (!done)
        return false;
    tm_enable_interrupts(pf, PF_AT(0x7E3019), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    set_scene_program_over(g, 0);
    set_scene_paused(g, 0);
    return true;
}

// $7E:3023
/* One pass of the main loop's call (ScenePass): nothing, or the scaling of
 * the race's racers, the endings' zooming picture or the Epoch seen from
 * outside. False where the program goes no further. */
static bool main_loop_pass(GameState *g, Platform *pf)
{
    uint16_t call = scene_main_loop_call(g);
    bool done = false;
    switch (call) {
    case SCENE_PASS_NOTHING: return true;
    case SCENE_PASS_RACE_SCALING: TIMED_AS(g, PROGRAM_BANK | call, done = scene_race_scale_pictures(g, pf)); return done;
    case SCENE_PASS_ENDING_SCALING: TIMED_AS(g, PROGRAM_BANK | call, done = scene_ending_scale_picture(g, pf)); return done;
    case SCENE_PASS_DIVE_SCALING: TIMED_AS(g, PROGRAM_BANK | call, done = scene_scale_the_epoch(g, pf)); return done;
    case SCENE_PASS_TURN_MAP: TIMED_AS(g, PROGRAM_BANK | call, done = scene_turn_map(g)); return done;
    default:
        program_needs_the_machine(g, PROGRAM_BANK | call, "a scene's main loop call only the machine runs");
        return false;
    }
}

// $7E:3000 (and the boot, $C3:0000, before it; the main loop, $7E:3023, and the end, $7E:302A, after)
bool scene_runs(GameState *g, Platform *pf, uint8_t flags)
{
    (void)scene_boots(g, pf, flags);
    reset_the_program(g);
    bool title_coming_back = false;
    TIMED_AS(g, CLEAR_WORK_RAM, title_coming_back = scene_clear_the_work_ram(g, pf, FADE_PACE));
    if (!title_coming_back) {
        put_the_sound_back(g, pf);
        start_from_the_first_screen(g, pf);
    }
    uint8_t mode = scene_flags(g) & SCENE_MODE_BITS;
    if (!set_up(g, pf, mode))
        return false;
    if (mode == SCENE_MODE_TITLE)
        while (!scene_music_is_playing(pf))
            tm_wait_frame(pf, PF_AT(MUSIC_WAIT));
    if (!first_frame(g, pf))
        return false;
    for (;;) {
        if (!main_loop_pass(g, pf))
            return false;
        if (scene_program_over(g))
            break;
        tm_wait_frame(pf, PF_AT(MAIN_LOOP));
        if (program_console_is_resetting(g))
            return true;
    }
    scene_stops_the_display(pf);
    TIMED_AS(g, SOUND_BACK_TO_NORMAL, scene_sound_back_to_normal(g, pf));
    return true;
}
