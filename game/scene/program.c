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
#include "core/program.h"
#include "field/main_loop.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The program's state, as a scene starts it ------------------------------------------------ */

/* The fades step every four frames until a scene sets its own pace; the
 * window shapes' last corner starts at the screen's left. */
enum { FADE_PACE = 4, DARK = 0, NO_LINE_EFFECTS = 0, NO_SONG = 0, LAST_CORNER = 3 };

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
enum { SCENE_MODE_BITS = 0x1F, PROGRAM_BANK = 0x7E0000 };

/* The setups' routines, by which their time is reckoned. */
enum {
    RACE_SET_UP = 0x7E3B48, CREDIT_PAGES_SET_UP = 0x7E6B04, TITLE_SET_UP = 0x7E5978, PALACE_SET_UP = 0x7E671F,
    ENDINGS_SET_UP = 0x7E8538, STAFF_ROLL_SET_UP = 0x7E8EA8, LAVOS_FALLS_SET_UP = 0x7E53BB, EPOCH_DIVE_SET_UP = 0x7E78DD,
    TURN_SET_UP = 0x7E7092, PLANET_SET_UP = 0x7E6248, SLOW_STAFF_ROLL_SET_UP = 0x7E8E95,
};

/* ---- The scene's first frame ------------------------------------------------------------------- */

enum { FIRST_WAIT = 0x7E3010, FRAME_WAIT = 0x7E31B0, FRAME_WORK = 0xC308D0, MAIN_LOOP = 0x7E3023 };

/* ---- The program's start ------------------------------------------------------------------------- */

enum { CLEAR_WORK_RAM = 0x7E3092, SOUND_BACK_TO_NORMAL = 0x7E3177 };

/* The sound put back to normal as a scene starts, unless the scene has no
 * songs: no song on its way, then the driver's commands (scene/sound.c). */
static void put_the_sound_back(GameState *g, Platform *pf)
{
    if (scene_flags(g) & SCENE_FLAG_NO_SONGS)
        return;
    set_scene_song(g, NO_SONG);
    TIMED_AS(g, SOUND_BACK_TO_NORMAL, scene_sound_back_to_normal(g, pf));
}

/* ---- The main loop ---------------------------------------------------------------------------- */

static bool main_loop_pass(GameState *g, Platform *pf);

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
    program_fail(g, PROGRAM_BANK | scene_program_setups(g, mode), "a scene's setup only the machine runs");
    return false;
}

/* The title's setup waits for its song to start playing ($7E:5AAA), a frame
 * at a time where a host has the driver's time pass. */
enum { MUSIC_WAIT = 0x7E5AAA };

// $7E:300D
static bool first_frame(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0x7E388B, scene_send_palette(pf));
    tm_wait_frame(pf, PF_CALL(FIRST_WAIT, FRAME_WAIT));
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
        program_fail(g, PROGRAM_BANK | call, "a scene's main loop call only the machine runs");
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
