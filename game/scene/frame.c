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
 * The lists run the tasks that are ports (the title's, the screen's fades and
 * the frame's own sprites and palette, the credit pages', the palace
 * rising's and the planet's) from a C table, and any other task, and the program's
 * own send of a sound command, as a call on the machine; so the frame's work
 * is a bridge around the logic here: it makes those calls with the registers
 * the original has there, takes a task off its list where it leaves through
 * the list's exits, and stops where the original waits. */
#include "scene/scene.h"

#include "charge.h"
#include "core/input.h"
#include "core/program.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The NMI's two handlers, in bank C3: the frame's, and the one that only
 * acknowledges while the frame's work runs. */
enum { FRAME_HANDLER = 0x08B3, HANDLER_WHILE_WORKING = 0x08C9 };

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

// $C3:0CE2, $C3:0CF8
bool scene_add_task(GameState *g, SceneTaskList list, SceneTask task) { return replace_task(g, list, NO_TASK, task); }

// $C3:0CB8, $C3:0CCD
bool scene_remove_task(GameState *g, SceneTaskList list, SceneTask task) { return replace_task(g, list, task, NO_TASK); }

/* ---- The frame's work, as logic -------------------------------------------------------------- */

static void install_handler(GameState *g, uint16_t handler) { set_scene_nmi_stub_handler_in_bank(g, handler); }

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
    install_handler(g, HANDLER_WHILE_WORKING);
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

typedef enum { PAD_READ, PAD_BUSY, PAD_RESET } PadRead;

// $C3:0A8E
/* The controller into the scene's copy of it, once the console has read it.
 * All four reset buttons at once, where the scene allows it, blank the screen
 * and stop the interrupts and the per-line effects for the reset. */
static PadRead read_controller(GameState *g, Platform *pf)
{
    if (!in_ready(pf, PF_AT(0xC30A90)))
        return PAD_BUSY;
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

/* ---- The bridge: the original's code it calls, and the registers it calls it with ------------ */

/* After a call-out, the code that follows runs in native mode with 16-bit index
 * registers and reaches the registers through a data bank that reaches them;
 * otherwise the port stops there. */
static bool can_continue(Snes *s) { return !s->cpu.e && !m_flag(s, CPU_FLAG_X) && m_bank_reaches_io(s); }

/* Where the original's copy of the task list leaves Y (past the copy), which
 * the frame's tasks and the controller read's stops see. */
enum { PAST_THE_COPY = 0x0970 };

// $7E:39BE, $7E:398A
/* A task list, called as the frame's work calls it: the vblank list with 8-bit
 * A, the frame's list with 16-bit A, and A the pause word it tested (0, to get
 * there), which the tasks may read. */
static bool run_task_list(Snes *s, SceneTaskList list)
{
    bool vblank = list == SCENE_VBLANK_TASKS;
    m_set_flag(s, CPU_FLAG_M, vblank);
    s->cpu.a = 0x0000;
    m_jsl_dc(s, vblank ? 0xC308F7 : 0xC30938, vblank ? 0x7E39BE : 0x7E398A,
             DC_X | DC_N | DC_Z | (vblank ? DC_A : 0));
    return can_continue(s);
}

/* The program's send of the command in the sound command buffer ($7E:377A). */
static bool send_pause_sound(Snes *s, uint32_t site)
{
    m_jsl_dc(s, site, 0x7E377A, DC_A | DC_X | DC_N | DC_Z);
    return can_continue(s);
}

/* The JSR to the controller read pushes this, and it is on the stack at the
 * read's stops and its call-out. */
enum { READ_CONTROLLER_RETURN = 0x0921 };

typedef enum { READ_STOPPED, READ_DONE, READ_SENT_SOUND } ReadEnd;

// $C3:0A8E
/* The controller and the pause, as the frame's work calls them. The port stops
 * where the console is still reading (the wait is the original's loop), at the
 * reset, or where the pause's sound comes back with other widths. At the
 * sound's call the carry is the controller's compare with the reset buttons. */
static ReadEnd read_controller_and_pause(Snes *s)
{
    GameState *g = state_of(s);
    m_push16(s, READ_CONTROLLER_RETURN);
    switch (read_controller(g, m_platform(s))) {
    case PAD_BUSY:
        m_set_flag(s, CPU_FLAG_Z, false);
        m_stop_at_own(s, 0xC30A95, DC_A | DC_X | DC_N | DC_C);
        return READ_STOPPED;
    case PAD_RESET:
        m_stop_at_own(s, 0x00FF00, DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C | DC_DB | DC_D);   /* JML to the reset code */
        return READ_STOPPED;
    case PAD_READ:
        break;
    }
    m_set_flag(s, CPU_FLAG_M, false);
    PauseChange change = press_pause(g, scene_joypad(g));
    if (change == PAUSE_UNCHANGED) {
        m_pull16(s);
        return READ_DONE;
    }
    m_set_flag(s, CPU_FLAG_C, scene_joypad(g) >= RESET_BUTTONS);
    if (!send_pause_sound(s, change == PAUSE_ON ? 0xC30AE1 : 0xC30AF3))
        return READ_STOPPED;
    m_pull16(s);
    return READ_SENT_SOUND;
}

// $C3:08D0
/* The frame's work (see the top of the file). False where the port stopped. */
static bool frame_work(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    s->cpu.d = 0x0000;
    s->cpu.db = 0x00;
    bool quiet = begin_frame(g, pf);
    if (scene_flags(g) & SCENE_FLAG_TITLE_WITH_LINE_IRQ)
        m_set_flag(s, CPU_FLAG_I, false);   /* the line interrupt may interrupt the work */
    if (!is_paused(g)) {
        m_set_flag(s, CPU_FLAG_C, quiet);   /* the acknowledge's compare, which the tasks see */
        if (!run_task_list(s, SCENE_VBLANK_TASKS))
            return false;
    }
    after_vblank_tasks(g, pf);
    ORIGINAL_EFFECT(s->cpu.y = PAST_THE_COPY);
    m_set_flag(s, CPU_FLAG_M, true);

    ReadEnd read = read_controller_and_pause(s);
    if (read == READ_STOPPED)
        return false;
    if (is_paused(g)) {
        dim_for_pause(g, pf);
        ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_C, scene_brightness(g) & 1));   /* the halving's */
        s->cpu.a &= 0x00FF;   /* the pause word's high byte, 0 */
    } else {
        if (read == READ_DONE)
            m_set_flag(s, CPU_FLAG_C, scene_joypad(g) >= RESET_BUTTONS);   /* the controller's compare */
        if (!run_task_list(s, SCENE_FRAME_TASKS))
            return false;
    }
    m_set_flag(s, CPU_FLAG_M, true);
    install_handler(g, FRAME_HANDLER);
    return true;
}

/* The scene keeps its pause as 0 or 1, which the port takes as a switch; the
 * original tests the word, and its low byte, which agree only then. */
bool scene_pause_is_a_switch(Snes *s)
{
    vars_peek(true);
    bool ok = scene_paused(state_of(s)) <= 1;
    vars_peek(false);
    return ok;
}

/* Entered with 16-bit index registers; back, the callers read A's high byte and
 * the carry as the original leaves them. */
void scene_frame_work(Snes *s)
{
    if (frame_work(s))
        m_rtl(s);
}

/* The song's send, as the NMI handler makes it, with the data bank the frame's
 * work leaves (0, which reaches the sound command buffer and the driver; the
 * handler goes on only where it does). False where the port stops. */
static bool send_song(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_M, true);
    uint8_t song;
    if (!song_to_send(g, &song))
        return true;
    au_command(m_platform(s), PF_CALL_DC(0xC309D1, 0xC70004, DC_AL | DC_X | DC_N | DC_Z | DC_C),
               (AudioCommand){ .command = PLAY_SONG, .p1 = song, .p2 = SONG_VOLUME_AS_IS, .p3 = SONG_ECHO_AS_IS,
                               .count = 3 });
    if (!can_continue(s) || !m_flag(s, CPU_FLAG_M))
        return false;
    set_sending_song(g, false);
    return true;
}

/* The JSR to the song's send pushes this. */
enum { SEND_SONG_RETURN = 0x08C0 };

// $C3:08B3
/* The NMI handler: the registers saved, the frame's work (a call-out, so that
 * its own entry is compared on its own), a changed song sent, and back. */
void scene_nmi(Snes *s)
{
    Cpu *c = &s->cpu;
    m_set_flag(s, CPU_FLAG_M | CPU_FLAG_X, false);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push16(s, c->d);
    m_push8(s, c->db);

    m_jsl(s, 0xC308BA, 0xC308D0);
    if (!can_continue(s))
        return;

    m_push16(s, SEND_SONG_RETURN);
    if (!send_song(s))
        return;
    m_pull16(s);

    m_set_flag(s, CPU_FLAG_M | CPU_FLAG_X, false);
    c->db = m_pull8(s);
    c->d = m_pull16(s);
    c->y = m_pull16(s);
    c->x = m_pull16(s);
    c->a = m_pull16(s);
    m_rti(s);
}

// $C3:08C9
/* The NMI while the frame's work runs: acknowledge it, and back. */
void scene_nmi_while_working(Snes *s)
{
    tm_ack_vblank(m_platform(s), PF_AT(0xC308CB));
    m_rti(s);
}

// $C3:0945
/* A line interrupt handler that only acknowledges; nothing installs it. */
void scene_line_irq_ack(Snes *s)
{
    tm_ack_line_irq(m_platform(s), PF_AT(0xC30947));
    m_rti(s);
}

/* ---- The task lists' entries ---------------------------------------------------------------- */

bool scene_frame_tasks_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E398A, 29, 0xA8C5FD94195E6607ull);
}

bool scene_vblank_tasks_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E39BE, 32, 0xC12FC96AEC0DACA2ull);
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

/* What a task leaves for the code after it: Y for the next task, and A (A's
 * high byte is what the frame's work reads after the list's last task; a slot
 * handed over leaves the task that takes it). */
static void leave_y(Snes *s, TaskLeftovers l)
{
    if (l.y_set)
        s->cpu.y = l.y;
}

static void leave_a(Snes *s, TaskLeftovers l, TaskEnd end)
{
    if (end.outcome == TASK_HANDS_OVER)
        s->cpu.a = end.next;
    else if (l.a_set)
        s->cpu.a = l.a_high_as_called ? (uint16_t)((s->cpu.a & 0xFF00) | (l.a & 0x00FF)) : l.a;
}

/* The lists' exits, in their own code, where a task that is done jumps: the
 * frame list's two, and the vblank list's, which takes a task off it. */
enum { TASK_LEAVES = 0x7E39A7, TASK_HANDS_OVER_EXIT = 0x7E39B1, VBLANK_TASK_LEAVES = 0x7E39DE };

static bool is_an_exit(SceneTaskList list, uint32_t at)
{
    if (list == SCENE_VBLANK_TASKS)
        return at == VBLANK_TASK_LEAVES;
    return at == TASK_LEAVES || at == TASK_HANDS_OVER_EXIT;
}

/* A list's loop calls a task from its own code (the frame list's at
 * $7E:3998, the vblank list's at $7E:39CF), with the data bank, the slot, P
 * and D saved under the call, and for the vblank list the direct page in
 * 8-bit A. A task that is done jumps to one of two exits in the loop, which
 * take it off the list, or hand its slot to the task in X; they unwind the
 * frames under the call and go on with the list. */
enum { FRAME_TASK_CALL = 0x7E3998, VBLANK_TASK_CALL = 0x7E39CF };
enum { THE_TASKS_READ_ANYTHING = 0, JSR_SIZE = 3 };

static void keep_the_list_s_frames(Snes *s, uint16_t index)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, index);
    m_push8(s, c->p);
    m_push16(s, c->d);
}

static void unwind_the_task_s_frames(Snes *s)
{
    Cpu *c = &s->cpu;
    c->d = m_pull16(s);
    c->p = m_pull8(s);
    c->x = m_pull16(s);
    c->db = m_pull8(s);
}

/* The tasks whose ports take the machine, by the program's routine the value
 * on the list is the address of: run as the list's call reaches them, timed
 * as their own routines, where their guards hold (scene_run); a task with no
 * row here or in the tables above is the original's. The rows are the tasks
 * the library's runs reach, in shadow mode and in the shipping mode. */
static const struct {
    uint32_t routine;
    void (*entry)(Snes *);
} machine_tasks[] = {
    /* the scripts' channels' first steps and the vblank list's uploads */
    { 0x7E3749, scene_run_channel_0_script },
    { 0x7E3754, scene_run_channel_1_script },
    { 0x7E3843, scene_clear_sprite_table },
    { 0x7E388B, scene_upload_palette },
    /* the jet bike race's */
    { 0x7E3D72, scene_race_sweep_curtain },
    { 0x7E416D, scene_race_settle_camera },
    { 0x7E424D, scene_race_wait_to_leave },
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
    { 0x7E5654, scene_send_the_flat_plane_entry },
    { 0x7E56A9, scene_turn_lavos },
    { 0x7E56EB, scene_draw_the_planet },
    { 0x7E579B, scene_stars_fly },
    { 0x7E5855, scene_swing_the_field },
    /* the planet's and the rising palace's */
    { 0x7E63D5, scene_wait_for_a_button_to_restart },
    { 0x7E63F5, scene_count_down_to_the_restart },
    { 0x7E64BF, scene_show_the_planet_s_caption },
    { 0x7E6844, scene_sweep_the_palace_curtain },
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
    { 0x7E81C4, scene_send_the_plane_s_place_entry },
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
    { 0x7E9D48, scene_twinkle_the_lights },
    { 0x7E9DBC, scene_send_the_flight_view },
    { 0x7E9E2B, scene_send_the_picture },
    { 0x7E9E7B, scene_roll_credits },
    { 0x7E9EFC, scene_upload_credits },
};

static void (*machine_task(SceneTask task))(Snes *)
{
    for (unsigned i = 0; i < sizeof machine_tasks / sizeof machine_tasks[0]; i++)
        if (machine_tasks[i].routine == (PROGRAM_BANK | task))
            return machine_tasks[i].entry;
    return NULL;
}

static TaskEnd run_the_original_task(Snes *s, SceneTaskList list, unsigned slot, SceneTask task)
{
    Cpu *c = &s->cpu;
    bool vblank = list == SCENE_VBLANK_TASKS;
    uint16_t index = (uint16_t)(slot * 2);
    m_set_nz16(s, task);
    keep_the_list_s_frames(s, index);
    uint16_t below_the_call = c->s;
    c->x = index;
    c->a = task;
    if (vblank) {
        c->a = c->d;
        m_set_nz16(s, c->d);
        m_set_flag(s, CPU_FLAG_M, true);
    }
    uint32_t call = vblank ? VBLANK_TASK_CALL : FRAME_TASK_CALL;
    if (scene_run(s, call, PROGRAM_BANK | task, THE_TASKS_READ_ANYTHING, machine_task(task))) {
        unwind_the_task_s_frames(s);
        c->x = index;
        return task_goes_on();
    }
    uint32_t at = cpu_pc24(c);
    bool at_an_exit = is_an_exit(list, at) && c->s == (uint16_t)(below_the_call - (JSR_SIZE - 1));
    if (!at_an_exit)
        return (TaskEnd){ TASK_LEFT_TO_THE_ORIGINAL, NO_TASK };
    native_dont_care_at_stop(s, 0, 0);   /* the stop is the loop's, which goes on as C */
    TaskEnd end = at == TASK_HANDS_OVER_EXIT ? (TaskEnd){ TASK_HANDS_OVER, c->x } : task_is_done();
    if (end.outcome == TASK_HANDS_OVER) {
        m_set_flag(s, CPU_FLAG_M, false);
        c->a = end.next;
    }
    (void)m_pull16(s);
    unwind_the_task_s_frames(s);
    end_the_task(state_of(s), list, slot, end);
    return end;
}

/* The list calls a task with A the task (the vblank list: the direct page),
 * which a task's own calls may read. */
static void as_the_list_calls(Snes *s, SceneTaskList list, SceneTask task)
{
    s->cpu.a = list == SCENE_VBLANK_TASKS ? s->cpu.d : task;
}

/* A task the port has as C runs where the program's code for it is the code
 * the port stands for, in binary mode, as every list runs. */
static bool runs_as_c(Snes *s, const SceneTaskPort *port)
{
    return port && !m_flag(s, CPU_FLAG_D) && port->present(s);
}

/* One task's frame, on a list's slot. As C, it is timed as the program's
 * routine for it; the list then has P back as it saved it, with the flags of
 * the slot's load, and the slot in X. A task the port doesn't have is the
 * original's, called through the machine. */
static TaskEnd run_task(Snes *s, SceneTaskList list, unsigned slot, SceneTask task)
{
    const SceneTaskPort *port = port_of(task, list);
    if (!runs_as_c(s, port))
        return run_the_original_task(s, list, slot, task);
    GameState *g = state_of(s);
    TaskEnd end = task_goes_on();
    uint16_t index = (uint16_t)(slot * 2);
    m_set_nz16(s, task);
    if (port->calls_out) {
        keep_the_list_s_frames(s, index);
        scene_bridge_enter(s, list == SCENE_VBLANK_TASKS ? VBLANK_TASK_CALL : FRAME_TASK_CALL);
        m_set_flag(s, CPU_FLAG_M, true);
    }
    ORIGINAL_EFFECT(as_the_list_calls(s, list, task));
    NATIVE_TIME_AS(s, PROGRAM_BANK | task, end = port->run(g, m_platform(s)));
    if (port->calls_out) {
        scene_bridge_leave(s);
        unwind_the_task_s_frames(s);
    }
    TaskLeftovers l = port->leaves(g, end);
    leave_y(s, l);
    ORIGINAL_EFFECT(leave_a(s, l, end));
    end_the_task(g, list, slot, end);
    s->cpu.x = index;
    return end;
}

// $7E:398A, $7E:39BE
/* Every task on a list from slot `first` on, in order; the list is read again
 * after each task, which can change it. A is the last task's, or 0 where the
 * last slot was empty; the carry is the loop's compare. A task the original
 * runs that leaves some other way than the list's own is the original's from
 * there. */
static void run_tasks_from(Snes *s, SceneTaskList list, unsigned first)
{
    GameState *g = state_of(s);
    unsigned count = list_length(list);
    m_set_flag(s, CPU_FLAG_M, false);
    for (unsigned slot = first; slot < count; slot++) {
        SceneTask task = list == SCENE_VBLANK_TASKS ? scene_early_tasks(g, slot) : scene_tasks_running(g, slot);
        if (task == NO_TASK)
            s->cpu.a = 0;
        else if (run_task(s, list, slot, task).outcome == TASK_LEFT_TO_THE_ORIGINAL)
            return;
        m_set_flag(s, CPU_FLAG_C, slot + 1 == count);   /* the loop's compare; its X, N and Z aren't read */
    }
    m_set_flag(s, CPU_FLAG_C, true);   /* the last compare's, at the list's end, from wherever it went on */
    m_rtl(s);
}

/* The registers a task leaves as it ends, as its row's leftovers work them
 * out: Y, A (scaffolding), A's width and the overflow. */
void scene_leave_task_registers(Snes *s, const SceneTaskPort *port, TaskEnd end)
{
    TaskLeftovers l = port->leaves(state_of(s), end);
    leave_y(s, l);
    ORIGINAL_EFFECT(leave_a(s, l, (TaskEnd){ end.outcome == TASK_HANDS_OVER ? TASK_GOES_ON : end.outcome, NO_TASK }));
    if (l.a_8bit)
        m_set_flag(s, CPU_FLAG_M, true);
    if (l.overflow_clear)
        m_set_flag(s, CPU_FLAG_V, false);
}

/* A task reached by its address, alone: its frame as the list runs it, and
 * back to the list as the original's would come back -- returning, or done
 * through the list's exit that empties its slot, or that hands it over, which
 * read A's high byte and Y (the exits put P back as the list saved it). */
enum { THE_EXITS_READ_A_HIGH_AND_Y = DC_AL | DC_X | DC_N | DC_V | DC_Z | DC_C,
       THE_HAND_OVER_READS_THE_NEXT_TASK_TOO = DC_AL | DC_N | DC_V | DC_Z | DC_C };

void scene_task_alone(Snes *s, const SceneTaskPort *port)
{
    GameState *g = state_of(s);
    if (port->calls_out)
        m_set_flag(s, CPU_FLAG_M, true);
    TaskEnd end = port->run(g, m_platform(s));
    scene_leave_task_registers(s, port, end);
    if (end.outcome == TASK_GOES_ON) {
        m_rts(s);
    } else if (end.outcome == TASK_IS_DONE) {
        m_stop_at_own(s, TASK_LEAVES, THE_EXITS_READ_A_HIGH_AND_Y);
    } else {
        s->cpu.x = end.next;
        m_stop_at_own(s, TASK_HANDS_OVER_EXIT, THE_HAND_OVER_READS_THE_NEXT_TASK_TOO);
    }
}

/* ---- The frame as C, with no machine ------------------------------------------------------------ */

/* The program's routines on the lists the port has as C beside the tasks'
 * own: the channels' turns, the sprites hidden and the palette sent. Each
 * stays on its list. */
static bool program_routine_as_c(GameState *g, Platform *pf, SceneTask task)
{
    switch (PROGRAM_BANK | task) {
    case 0x7E3749: return scene_channel_turn(g, pf, 0) == SCENE_TURN_ENDED;
    case 0x7E3754: return scene_channel_turn(g, pf, 1) == SCENE_TURN_ENDED;
    case 0x7E3843: scene_hide_every_sprite(pf); return true;
    case 0x7E388B: scene_send_palette(pf); return true;
    default:
        program_needs_the_machine(g, PROGRAM_BANK | task, "a scene's task only the machine runs");
        return false;
    }
}

/* A list's tasks in order, the list read again after each, which can change
 * it; false where a task is the machine's yet. */
static bool run_tasks_as_c(GameState *g, Platform *pf, SceneTaskList list)
{
    unsigned count = list_length(list);
    for (unsigned slot = 0; slot < count; slot++) {
        SceneTask task = list == SCENE_VBLANK_TASKS ? scene_early_tasks(g, slot) : scene_tasks_running(g, slot);
        if (task == NO_TASK)
            continue;
        const SceneTaskPort *port = port_of(task, list);
        if (port) {
            TaskEnd end = task_goes_on();
            TIMED_AS(g, PROGRAM_BANK | task, end = port->run(g, pf));
            end_the_task(g, list, slot, end);
            continue;
        }
        bool done = false;
        TIMED_AS(g, PROGRAM_BANK | task, done = program_routine_as_c(g, pf, task));
        if (!done)
            return false;
    }
    return true;
}

// $C3:08D0
bool scene_frame_work_as_c(GameState *g, Platform *pf)
{
    (void)begin_frame(g, pf);
    if (!is_paused(g) && !run_tasks_as_c(g, pf, SCENE_VBLANK_TASKS))
        return false;
    after_vblank_tasks(g, pf);
    switch (read_controller(g, pf)) {
    case PAD_BUSY: break;
    case PAD_RESET:
        program_needs_the_machine(g, 0x00FF00, "the reset the controller asks for in a scene");
        return false;
    case PAD_READ:
        if (press_pause(g, scene_joypad(g)) != PAUSE_UNCHANGED)
            scene_sends_the_command(g, pf);
        break;
    }
    if (is_paused(g))
        dim_for_pause(g, pf);
    else if (!run_tasks_as_c(g, pf, SCENE_FRAME_TASKS))
        return false;
    install_handler(g, FRAME_HANDLER);
    return true;
}

// $C3:08B3
void scene_vblank(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, 0xC308D0, done = scene_frame_work_as_c(g, pf));
    uint8_t song;
    if (!done || !song_to_send(g, &song))
        return;
    au_command(pf, PF_CALL_DC(0xC309D1, 0xC70004, DC_AL | DC_X | DC_N | DC_Z | DC_C),
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

/* The frame's tasks, from the copy of the list the frame's work made, each
 * called with 16-bit A. */
static void run_tasks(Snes *s, SceneTaskList list) { run_tasks_from(s, list, 0); }

void scene_run_frame_tasks(Snes *s) { run_tasks(s, SCENE_FRAME_TASKS); }

/* The vblank tasks, each called with 8-bit A and the direct page in A. */
void scene_run_vblank_tasks(Snes *s) { run_tasks(s, SCENE_VBLANK_TASKS); }

// $7E:399B, $7E:39D2
/* A list going on after a task it called came back to it itself, frames
 * later (a channel's script that waited for the music, scene/script.c): the
 * frames the list kept under the call unwound, the slot's among them, and the
 * list's tasks after it. */
static void go_on_after_the_task(Snes *s, SceneTaskList list)
{
    unwind_the_task_s_frames(s);
    run_tasks_from(s, list, s->cpu.x / 2u + 1u);
}

void scene_frame_tasks_go_on(Snes *s) { go_on_after_the_task(s, SCENE_FRAME_TASKS); }
void scene_vblank_tasks_go_on(Snes *s) { go_on_after_the_task(s, SCENE_VBLANK_TASKS); }
