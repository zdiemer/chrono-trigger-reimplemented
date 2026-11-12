/* Scenes: the frame -- what a scene program does every frame, from its NMI.
 *
 * The program's NMI handler ($C3:08B3) does the frame's work and then sends the
 * sound driver a song the scene has changed to. The frame's work:
 *
 * - blanks the screen and turns the per-line effects off while it works, and
 *   points a second NMI at a handler that only acknowledges it;
 * - unless the scene is paused, runs the vblank tasks: the uploads (the
 *   sprites, the palette, a tilemap), made while the screen is blank;
 * - puts the per-line effects and the brightness back, and counts the frame;
 * - copies the task list and reads the controller: Start pauses and unpauses
 *   (with a sound), and L+R+Select+Start resets the console where the scene
 *   allows it;
 * - then, paused, shows the picture at half brightness; otherwise it runs the
 *   frame's tasks, the scene's per-frame logic.
 *
 * A task is a routine of the program, run every frame while it is on a list
 * (SceneTask). Scripts put tasks on and take them off (scene/script.c); a task
 * can take itself off, or hand its slot to another, when it is done.
 *
 * The lists find each task in the files' tables (the title's, the screen's
 * fades and the frame's own sprites and palette, the credit pages', the
 * palace rising's and the planet's) or in the table below, by the program's
 * routine the list's value is the address of. A task the port doesn't have
 * stops the scene there (program_fail). */
#include "scene/scene.h"

#include "charge.h"
#include "core/input.h"
#include "core/program.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* The sound driver commands pausing sends: the effects stop, and unpausing
 * plays sound effect $44, panned to the middle. */
enum { STOP_EFFECTS = 0xF2, PLAY_EFFECT = 0x18, UNPAUSE_EFFECT = 0x44, PAN_MIDDLE = 0x80 };
enum { PLAY_SONG = 0x10, SONG_VOLUME_AS_IS = 0xFF, SONG_ECHO_AS_IS = 0xFF };

/* ---- Tasks ------------------------------------------------------------------------------------ */

static unsigned list_length(SceneTaskList list)
{
    return list == SCENE_VBLANK_TASKS ? SCENE_EARLY_TASKS_COUNT : SCENE_TASKS_COUNT;
}

static SceneTask task_at(const GameState *g, SceneTaskList list, unsigned slot)
{
    return list == SCENE_VBLANK_TASKS ? scene_early_tasks(g, slot) : scene_tasks(g, slot);
}

static void set_task_at(GameState *g, SceneTaskList list, unsigned slot, SceneTask task)
{
    if (list == SCENE_VBLANK_TASKS)
        set_scene_early_tasks(g, slot, task);
    else
        set_scene_tasks(g, slot, task);
}

/* The first slot holding `from` becomes `to`. */
static bool replace_task(GameState *g, SceneTaskList list, SceneTask from, SceneTask to)
{
    for (unsigned slot = 0; slot < list_length(list); slot++) {
        if (task_at(g, list, slot) == from) {
            set_task_at(g, list, slot, to);
            return true;
        }
    }
    return false;
}

bool scene_task_listed(const GameState *g, SceneTaskList list, SceneTask task)
{
    for (unsigned slot = 0; slot < list_length(list); slot++)
        if (task_at(g, list, slot) == task)
            return true;
    return false;
}

/* Past the list's end, the original's search goes on a word at a time through
 * the scripts' bank, for as far as its index reaches: its 16-bit index runs
 * on from the list's start, and back to it after $FFFE, where it would look
 * again for ever. */
enum { SEARCH_BANK = SCENE_LOWER_WORK_RAM, SEARCH_WORDS = 0x8000 };

static uint32_t list_start(SceneTaskList list)
{
    return list == SCENE_VBLANK_TASKS ? RAM_SCENE_EARLY_TASKS : RAM_SCENE_TASKS;
}

/* The first word past the list holding `from`, or 0 for none in reach. */
static uint32_t word_past_the_list(const GameState *g, SceneTaskList list, SceneTask from)
{
    uint32_t start = (uint32_t)SEARCH_BANK << 16 | (list_start(list) & 0xFFFF);
    for (uint32_t word = list_length(list); word < SEARCH_WORDS; word++)
        if (wram_read16(g, start + word * 2) == from)
            return start + word * 2;
    return 0;
}

// $C3:0CB8, $C3:0CCD, $C3:0CE2, $C3:0CF8
/* A script's command putting a task on a list or taking one off, as the
 * original's search does it: where the list has no slot holding `from`, the
 * search goes on past its end and changes the first word that holds it,
 * whatever that word is -- a slot of the vblank list after the frame's, or,
 * as the shared endings' script takes the fireworks off in a mode that never
 * put them on, a word of the script's own. False only where no word in reach
 * holds it, where the original would search for ever. */
bool scene_script_replace_task(GameState *g, SceneTaskList list, SceneTask from, SceneTask to)
{
    if (replace_task(g, list, from, to))
        return true;
    uint32_t at = word_past_the_list(g, list, from);
    if (!at)
        return false;
    wram_write16(g, at, to);
    return true;
}

bool scene_script_can_replace_task(const GameState *g, SceneTaskList list, SceneTask from)
{
    return scene_task_listed(g, list, from) || word_past_the_list(g, list, from) != 0;
}

// $C3:0CE2, $C3:0CF8
bool scene_add_task(GameState *g, SceneTaskList list, SceneTask task) { return replace_task(g, list, NO_TASK, task); }

// $C3:0CB8, $C3:0CCD
bool scene_remove_task(GameState *g, SceneTaskList list, SceneTask task) { return replace_task(g, list, task, NO_TASK); }

/* ---- The frame's work, as logic -------------------------------------------------------------- */

static void install_handler(GameState *g, ProgramHandler handler)
{
    program_install_vblank_handler(g, handler);
    charge_accesses(g, 2);
}

/* The brightness the scene keeps, where 0 means the screen blank. */
static VidDisplay display_at(uint8_t brightness)
{
    return brightness ? vid_display_decode(brightness) : (VidDisplay){ .blank = true };
}

/* The screen blanked and the per-line effects off while the work runs, and a
 * second NMI pointed at the handler that only acknowledges. Returns whether
 * the NMI was acknowledged with nothing pending (its status 0). */
static bool begin_frame(GameState *g, Platform *pf)
{
    bool quiet = tm_ack_vblank(pf, PF_AT(0xC308D9)) == 0;
    vid_enable_line_effects(pf, PF_AT(0xC308DC), 0);
    install_handler(g, scene_vblank_while_working);
    vid_set_display(pf, PF_AT(0xC308EF), &(VidDisplay){ .blank = true });
    return quiet;
}

/* After the vblank tasks: the per-line effects and the brightness back, the
 * frame counted, and the task list copied to the copy the frame's tasks run
 * from (so a task that changes the list changes next frame's). */
static void after_vblank_tasks(GameState *g, Platform *pf)
{
    vid_enable_line_effects(pf, PF_AT(0xC30900), scene_line_effects(g));
    VidDisplay display = display_at(scene_brightness(g));
    vid_set_display(pf, PF_AT(0xC3090A), &display);
    set_scene_frame_counter(g, (uint16_t)(scene_frame_counter(g) + 1));
    for (unsigned i = 0; i < SCENE_TASKS_COUNT; i++)
        set_scene_tasks_running(g, i, scene_tasks(g, i));
}

/* ---- The scene's switches: how each is kept ---------------------------------------------------- */

/* Pausing: whether Start may pause the scene, whether it is paused (a word the
 * pause keeps 0 or 1), and whether Start was already down last frame, so
 * holding it pauses once. */
static bool pause_allowed(const GameState *g) { return scene_pause_enabled(g) != 0; }
static bool is_paused(const GameState *g) { return scene_paused(g) != 0; }
static void set_paused(GameState *g, bool paused) { set_scene_paused(g, paused ? 1 : 0); }
static bool start_was_down(const GameState *g) { return scene_start_held(g) != 0; }
static void set_start_was_down(GameState *g, bool down) { set_scene_start_held(g, down ? 1 : 0); }

/* Whether the reset buttons reset the console during this scene. */
static bool reset_allowed(const GameState *g) { return scene_reset_disabled(g) == 0; }

/* Whether a song is on its way to the sound driver. */
static bool sending_song(const GameState *g) { return scene_song_sending(g) != 0; }
static void set_sending_song(GameState *g, bool sending) { set_scene_song_sending(g, sending ? 1 : 0); }

/* ---- The controller, pausing and the song --------------------------------------------------- */

typedef enum { PAD_READ, PAD_RESET } PadRead;

// $C3:0A8E
/* The controller into the scene's copy of it, once the console has read it.
 * All four reset buttons at once, where the scene allows it, blank the screen
 * and stop the interrupts and the per-line effects for the reset. */
static PadRead read_controller(GameState *g, Platform *pf)
{
    in_wait_ready(pf, PF_AT(0xC30A90));
    PadBits pad = in_read(pf, PF_AT(0xC30A99), 0);
    set_scene_joypad(g, pad);
    if (pad != RESET_BUTTONS || !reset_allowed(g))
        return PAD_READ;
    vid_set_display(pf, PF_AT(0xC30AAC), &(VidDisplay){ .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xC30AAF), 0);
    vid_enable_line_effects(pf, PF_AT(0xC30AB2), 0);
    return PAD_RESET;
}

typedef enum { PAUSE_UNCHANGED, PAUSE_ON, PAUSE_OFF } PauseChange;

/* Start, pressed anew where the scene allows pausing, pauses or unpauses it and
 * puts the sound that goes with it in the sound command buffer. */
static PauseChange press_pause(GameState *g, PadBits pad)
{
    if (!pause_allowed(g))
        return PAUSE_UNCHANGED;
    if (!(pad & IN_BUTTON_START)) {
        set_start_was_down(g, false);
        return PAUSE_UNCHANGED;
    }
    if (start_was_down(g))
        return PAUSE_UNCHANGED;
    set_start_was_down(g, true);
    bool paused = !is_paused(g);
    set_paused(g, paused);
    if (paused) {
        set_sound_command(g, STOP_EFFECTS);
        set_sound_command_parameters(g, 0, 0);
        return PAUSE_ON;
    }
    set_sound_command(g, PLAY_EFFECT);
    set_sound_command_parameters(g, 0, UNPAUSE_EFFECT);
    set_sound_command_parameters(g, 1, PAN_MIDDLE);
    set_sound_command_parameters(g, 2, 0);
    return PAUSE_OFF;
}

/* Paused: the picture at half the scene's brightness. */
static void dim_for_pause(const GameState *g, Platform *pf)
{
    VidDisplay dimmed = display_at(scene_brightness(g) >> 1);
    vid_set_display(pf, PF_AT(0xC30931), &dimmed);
}

// $C3:09A4
/* The song the scene wants, when it has changed and no song is on its way
 * (and the scene sends songs at all): marked playing, and on its way. */
static bool song_to_send(GameState *g, uint8_t *song)
{
    if (sending_song(g) || (scene_flags(g) & SCENE_FLAG_NO_SONGS))
        return false;
    *song = scene_song(g);
    if (*song == scene_song_playing(g))
        return false;
    set_scene_song_playing(g, *song);
    set_sending_song(g, true);
    return true;
}

/* ---- The tasks ------------------------------------------------------------------------------ */

/* The tasks that are C (scene/title.c, scene/screen.c, scene/credit_pages.c,
 * scene/rising_palace.c, scene/planet.c), looked up by the value on the list and the list it
 * is on. */
static const SceneTaskPort *port_of(SceneTask task, SceneTaskList list)
{
    static const struct {
        const SceneTaskPort *ports;
        const unsigned *count;
    } files[] = { { scene_title_tasks, &scene_title_task_count }, { scene_screen_tasks, &scene_screen_task_count },
                  { scene_credit_pages_tasks, &scene_credit_pages_task_count },
                  { scene_palace_tasks, &scene_palace_task_count }, { scene_planet_tasks, &scene_planet_task_count } };
    for (unsigned f = 0; f < sizeof files / sizeof files[0]; f++)
        for (unsigned n = 0; n < *files[f].count; n++)
            if (files[f].ports[n].task == task && files[f].ports[n].list == list)
                return &files[f].ports[n];
    return NULL;
}

/* The program's routines, which the lists' values are the addresses of. */
enum { PROGRAM_BANK = 0x7E0000 };

/* A task done takes itself off its list (for the frame's tasks, its slot in
 * the list the scripts keep; the frame's copy is the frame's work's), or
 * hands its slot to the task that follows it. */
static void end_the_task(GameState *g, SceneTaskList list, unsigned slot, TaskEnd end)
{
    if (end.outcome == TASK_IS_DONE || end.outcome == TASK_HANDS_OVER)
        set_task_at(g, list, slot, end.next);
}

TaskEnd scene_task_goes_no_further(GameState *g, uint32_t routine, const char *what)
{
    program_fail(g, routine, what);
    return (TaskEnd){ TASK_GOES_NO_FURTHER, NO_TASK };
}

/* The rest of the program's tasks, by the program's routine the value on the
 * list is the address of, run as the list reaches them. A task with no row
 * here or in the files' tables is one the port doesn't have. */

static TaskEnd channel_turn(GameState *g, Platform *pf, SceneChannel ch)
{
    if (scene_channel_turn(g, pf, ch) != SCENE_TURN_ENDED)
        return (TaskEnd){ TASK_GOES_NO_FURTHER, NO_TASK };
    return task_goes_on();
}

static TaskEnd channel_0_turn(GameState *g, Platform *pf, unsigned slot) { (void)slot; return channel_turn(g, pf, 0); }

static TaskEnd channel_1_turn(GameState *g, Platform *pf, unsigned slot) { (void)slot; return channel_turn(g, pf, 1); }

static TaskEnd hide_every_sprite(GameState *g, Platform *pf, unsigned slot)
{
    (void)g;
    (void)slot;
    scene_hide_every_sprite(pf);
    return task_goes_on();
}

static TaskEnd send_palette(GameState *g, Platform *pf, unsigned slot)
{
    (void)g;
    (void)slot;
    scene_send_palette(pf);
    return task_goes_on();
}

static TaskEnd send_the_flat_plane(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    scene_send_the_flat_plane(g, pf);
    return task_goes_on();
}

static TaskEnd nothing_this_frame(GameState *g, Platform *pf, unsigned slot)
{
    (void)g;
    (void)pf;
    (void)slot;
    return task_goes_on();
}

static TaskEnd send_the_plane_s_place(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    scene_send_the_plane_s_place(g, pf);
    return task_goes_on();
}

static const struct { uint32_t routine; SceneTaskRun run; } program_tasks[] = {
    /* the scripts' channels' first steps and the vblank list's uploads */
    { 0x7E3176, nothing_this_frame },
    { 0x7E3749, channel_0_turn },
    { 0x7E3754, channel_1_turn },
    { 0x7E3843, hide_every_sprite },
    { 0x7E388B, send_palette },
    /* the jet bike race's */
    { 0x7E3D72, scene_race_sweep_curtain },
    { 0x7E416D, scene_race_settle_camera },
    { 0x7E424D, scene_race_wait_to_leave },
    { 0x7E4283, scene_race_leave },
    { 0x7E428B, scene_race_move_along },
    { 0x7E42BD, scene_race_keep_controller },
    { 0x7E42CE, scene_race_check_perspective },
    { 0x7E4339, scene_race_turn_camera },
    { 0x7E43F2, scene_race_move_camera },
    { 0x7E4447, scene_race_aim_camera },
    { 0x7E449C, scene_race_follow_finish },
    { 0x7E44CE, scene_race_drain_score },
    { 0x7E44FE, scene_race_set_horizon },
    { 0x7E4643, scene_race_upload_column_rom },
    { 0x7E464D, scene_race_upload_column_ram },
    { 0x7E4677, scene_race_coast },
    { 0x7E4755, scene_race_await_victory_song },
    { 0x7E485D, scene_race_ride },
    { 0x7E4D0D, scene_race_place_bikes },
    { 0x7E4F54, scene_race_draw },
    { 0x7E5221, scene_race_upload_tilemap },
    /* Lavos falling to the planet's */
    { 0x7E55F9, scene_turn_the_field_s_angle },
    { 0x7E5654, send_the_flat_plane },
    { 0x7E56A9, scene_turn_lavos },
    { 0x7E56EB, scene_draw_the_planet },
    { 0x7E579B, scene_stars_fly },
    { 0x7E5855, scene_swing_the_field },
    /* the planet's and the rising palace's */
    { 0x7E63D5, scene_wait_for_a_button_to_restart },
    { 0x7E63F5, scene_count_down_to_the_restart },
    { 0x7E64BF, scene_show_the_planet_s_caption },
    { 0x7E6844, scene_sweep_the_palace_curtain },
    /* the turning-map scene */
    { 0x7E7193, scene_step_turn_mosaic },
    { 0x7E72FF, scene_spread_turn_figures },
    { 0x7E7325, scene_size_turn_windows },
    { 0x7E737E, scene_show_turn_mosaic },
    { 0x7E73AA, scene_scroll_turn_wave },
    { 0x7E76A5, scene_orbit_turn_figures },
    { 0x7E7721, scene_update_turn_figures },
    { 0x7E7868, scene_send_turn_figure },
    { 0x7E78AB, scene_cycle_turn_colors },
    /* the Epoch's dive's */
    { 0x7E79F8, scene_set_the_screen_outside },
    { 0x7E7A35, scene_set_the_screen_in_the_cockpit },
    { 0x7E7BF2, scene_open_the_light },
    { 0x7E7F12, scene_pass_the_shell_across },
    { 0x7E7F35, scene_pass_the_shell_down },
    { 0x7E7F58, scene_follow_the_panel },
    { 0x7E7F7D, scene_shake_the_cockpit },
    { 0x7E7FC4, scene_size_the_trail },
    { 0x7E7FCB, scene_draw_the_trail },
    { 0x7E8079, scene_flicker_the_lights },
    { 0x7E81C4, send_the_plane_s_place },
    { 0x7E81F1, scene_let_the_shell_fall_away },
    { 0x7E8205, scene_draw_the_epoch },
    { 0x7E824A, scene_send_the_epoch },
    { 0x7E82D1, scene_check_the_shell },
    { 0x7E835B, scene_fly_the_epoch_on },
    /* the endings' pictures and credits */
    { 0x7E8850, scene_ending_place_picture },
    { 0x7E891B, scene_ending_pick_picture_frame },
    { 0x7E89C0, scene_ending_upload_picture },
    { 0x7E8C05, scene_clear_sprite_high_bits },
    { 0x7E8D56, scene_ending_show_credits },
    { 0x7E8B52, scene_ending_panorama_horizon },
    { 0x7E8E44, scene_ending_show_last_scene },
    { 0x7E8E88, scene_ending_scroll_bg2 },
    /* the flight beyond time's, the star fields' drift, the captions' and the staff roll's */
    { 0x7E9080, scene_set_the_flight_horizon },
    { 0x7E90C1, scene_pass_the_map },
    { 0x7E9897, scene_stars_start_drift },
    { 0x7E98AC, scene_stars_keep_drifting },
    { 0x7E992A, scene_circle_the_epoch },
    { 0x7E997D, scene_sway_the_epoch },
    { 0x7E999C, scene_stars_fly_after_captions },
    { 0x7E99B5, scene_keep_the_orbit },
    { 0x7E99FA, scene_ending_draw_the_end },
    { 0x7E9A0C, scene_ending_sweep_window },
    { 0x7E9ABD, scene_check_the_flight_view },
    { 0x7E9AF3, scene_place_the_flight_view },
    { 0x7E9B4F, scene_turn_captions },
    { 0x7E9BF9, scene_burst_the_fireworks },
    { 0x7E9C58, scene_show_the_mosaic },
    { 0x7E9CE2, scene_send_the_mosaic_frame },
    { 0x7E9D45, scene_turn_back_and_twinkle },
    { 0x7E9D48, scene_twinkle_the_lights },
    { 0x7E9DBC, scene_send_the_flight_view },
    { 0x7E9E19, scene_send_the_picture_and_bg2 },
    { 0x7E9E2B, scene_send_the_picture },
    { 0x7E9E7B, scene_roll_credits },
    { 0x7E9EFC, scene_upload_credits },
    { 0x7E9F2B, scene_send_the_credits_picture },
    { 0x7E9F39, scene_send_the_pattern_s_map },
    { 0x7E9F47, scene_send_the_pattern_s_tiles },
    { 0x7E9F73, scene_blank_the_map },
};

static int program_task_row(SceneTask task)
{
    for (unsigned i = 0; i < sizeof program_tasks / sizeof program_tasks[0]; i++)
        if (program_tasks[i].routine == (PROGRAM_BANK | task))
            return (int)i;
    return -1;
}

/* ---- The frame ------------------------------------------------------------------------------------ */

/* A program's routine on a list's slot, where a row has it. */
static TaskEnd program_routine(GameState *g, Platform *pf, SceneTask task, unsigned slot)
{
    int row = program_task_row(task);
    if (row >= 0 && program_tasks[row].run)
        return program_tasks[row].run(g, pf, slot);
    return scene_task_goes_no_further(g, PROGRAM_BANK | task, "a scene's task the port doesn't have");
}

/* A list's tasks in order, the list read again after each, which can change
 * it; false where a task goes no further. */
static bool run_tasks(GameState *g, Platform *pf, SceneTaskList list)
{
    unsigned count = list_length(list);
    for (unsigned slot = 0; slot < count; slot++) {
        SceneTask task = list == SCENE_VBLANK_TASKS ? scene_early_tasks(g, slot) : scene_tasks_running(g, slot);
        if (task == NO_TASK)
            continue;
        const SceneTaskPort *port = port_of(task, list);
        TaskEnd end = task_goes_on();
        if (port)
            TIMED_AS(g, PROGRAM_BANK | task, end = port->run(g, pf));
        else
            TIMED_AS(g, PROGRAM_BANK | task, end = program_routine(g, pf, task, slot));
        if (end.outcome == TASK_GOES_NO_FURTHER || program_went_no_further(g))
            return false;
        end_the_task(g, list, slot, end);
    }
    return true;
}

// $C3:08D0
bool scene_frame_work(GameState *g, Platform *pf)
{
    (void)begin_frame(g, pf);
    if (!is_paused(g) && !run_tasks(g, pf, SCENE_VBLANK_TASKS))
        return false;
    after_vblank_tasks(g, pf);
    switch (read_controller(g, pf)) {
    case PAD_RESET:
        program_resets_the_console(g);
        return true;
    case PAD_READ:
        if (press_pause(g, scene_joypad(g)) != PAUSE_UNCHANGED)
            scene_sends_the_command(g, pf);
        break;
    }
    if (is_paused(g))
        dim_for_pause(g, pf);
    else if (!run_tasks(g, pf, SCENE_FRAME_TASKS))
        return false;
    install_handler(g, scene_vblank);
    return true;
}

// $C3:08B3
void scene_vblank(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, 0xC308D0, done = scene_frame_work(g, pf));
    uint8_t song;
    if (!done || !song_to_send(g, &song))
        return;
    au_command(pf, PF_CALL(0xC309D1, 0xC70004),
               (AudioCommand){ .command = PLAY_SONG, .p1 = song, .p2 = SONG_VOLUME_AS_IS, .p3 = SONG_ECHO_AS_IS,
                               .count = 3 });
    set_sending_song(g, false);
}

// $C3:08C9
void scene_vblank_while_working(GameState *g, Platform *pf)
{
    (void)g;
    (void)tm_ack_vblank(pf, PF_AT(0xC308CB));
}
