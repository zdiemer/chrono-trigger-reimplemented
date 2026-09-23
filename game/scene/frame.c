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
 * The tasks and the program's own send of a sound command are the original's
 * code, so the frame's work is a bridge on the machine around the logic here:
 * it makes those calls with the registers the original has there, and stops
 * where the original waits. */
#include "scene/scene.h"

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

/* The controller's buttons (PadBits), and the four that reset together. */
enum { BUTTON_START = 0x1000, BUTTON_SELECT = 0x2000, BUTTON_L = 0x0020, BUTTON_R = 0x0010 };
enum { RESET_BUTTONS = BUTTON_L | BUTTON_R | BUTTON_SELECT | BUTTON_START };

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
    if (pad != RESET_BUTTONS || scene_reset_disabled(g) != 0)
        return PAD_READ;
    vid_set_display(pf, PF_AT(0xC30AAC), &(VidDisplay){ .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xC30AAF), 0);
    vid_enable_line_effects(pf, PF_AT(0xC30AB2), 0);
    return PAD_RESET;
}

typedef enum { PAUSE_UNCHANGED, PAUSE_ON, PAUSE_OFF } PauseChange;

/* Start, pressed anew where the scene allows pausing, pauses or unpauses it and
 * puts the sound that goes with it in the sound command buffer. The pause flag
 * is the low bit of its cell. */
static PauseChange press_pause(GameState *g, PadBits pad)
{
    if (scene_pause_enabled(g) == 0)
        return PAUSE_UNCHANGED;
    if (!(pad & BUTTON_START)) {
        set_scene_start_held(g, false);
        return PAUSE_UNCHANGED;
    }
    if (scene_start_held(g) != 0)
        return PAUSE_UNCHANGED;
    set_scene_start_held(g, true);
    bool paused = !(scene_paused(g) & 1);
    set_scene_paused(g, paused);
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
/* The song the scene wants, when it has changed and no send is under way (and
 * the scene sends songs at all): marked playing, and the send under way. */
static bool song_to_send(GameState *g, uint8_t *song)
{
    if (scene_song_sending(g) != 0 || (scene_flags(g) & SCENE_FLAG_NO_SONGS))
        return false;
    *song = scene_song(g);
    if (*song == scene_song_playing(g))
        return false;
    set_scene_song_playing(g, *song);
    set_scene_song_sending(g, (uint8_t)(scene_song_sending(g) + 1));
    return true;
}

/* ---- The bridge: the original's code it calls, and the registers it calls it with ------------ */

/* After a call-out, the code that follows runs in native mode with 16-bit index
 * registers and reaches the registers through a data bank that reaches them;
 * otherwise the port stops there. */
static bool can_continue(Snes *s) { return !s->cpu.e && !m_flag(s, CPU_FLAG_X) && m_bank_reaches_io(s); }

// $7E:39BE, $7E:398A
/* A task list, called as the frame's work calls it: the vblank list with 8-bit
 * A, the frame's list with 16-bit A. */
static bool run_task_list(Snes *s, SceneTaskList list)
{
    bool vblank = list == SCENE_VBLANK_TASKS;
    m_set_flag(s, CPU_FLAG_M, vblank);
    s->cpu.a = 0x0000;   /* the pause flag the frame's work tested, which was 0 */
    m_jsl_dc(s, vblank ? 0xC308F7 : 0xC30938, vblank ? 0x7E39BE : 0x7E398A,
             DC_X | DC_N | DC_Z | (vblank ? DC_A | DC_Y : 0));
    return can_continue(s);
}

/* The registers the task list's copy leaves (a block move of it: A run down to
 * $FFFF, X and Y past the two lists), which the controller read's stops and its
 * call-out see. */
static void leave_registers_of_the_copy(Snes *s)
{
    s->cpu.a = 0xFFFF;
    s->cpu.x = 0x0940;
    s->cpu.y = 0x0970;
}

/* The program's send of the command in the sound command buffer ($7E:377A),
 * with 16-bit A the word stored last (neither negative nor zero). */
static bool send_pause_sound(Snes *s, uint32_t site, uint16_t stored)
{
    s->cpu.a = stored;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_jsl(s, site, 0x7E377A);
    return can_continue(s);
}

/* The JSR to the controller read pushes this, and it is on the stack at the
 * read's stops and its call-out. */
enum { READ_CONTROLLER_RETURN = 0x0921 };

// $C3:0A8E
/* The controller and the pause, as the frame's work calls them. The carry is
 * the compare with the reset buttons, which the frame's tasks see. False where
 * the port stops: the console still reading (the wait is the original's loop),
 * the reset, or a call-out coming back with other widths. */
static bool read_controller_and_pause(Snes *s, bool *carry)
{
    GameState *g = state_of(s);
    m_push16(s, READ_CONTROLLER_RETURN);
    switch (read_controller(g, m_platform(s))) {
    case PAD_BUSY:
        m_set_flag(s, CPU_FLAG_Z, false);
        m_stop_at_own(s, 0xC30A95, DC_A | DC_X | DC_N | DC_C);
        return false;
    case PAD_RESET:
        m_stop_at_own(s, 0x00FF00, DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C | DC_DB | DC_D);   /* JML to the reset code */
        return false;
    case PAD_READ:
        break;
    }
    m_set_flag(s, CPU_FLAG_M, false);
    *carry = scene_joypad(g) >= RESET_BUTTONS;
    PauseChange change = press_pause(g, scene_joypad(g));
    if (change != PAUSE_UNCHANGED) {
        m_set_flag(s, CPU_FLAG_C, *carry);
        bool ok = change == PAUSE_ON ? send_pause_sound(s, 0xC30AE1, STOP_EFFECTS)
                                     : send_pause_sound(s, 0xC30AF3, PAN_MIDDLE);
        *carry = m_flag(s, CPU_FLAG_C);
        if (!ok)
            return false;
    }
    m_pull16(s);
    return true;
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
    s->cpu.x = HANDLER_WHILE_WORKING;
    m_set_flag(s, CPU_FLAG_C, quiet);
    if (!(scene_paused(g) & 0xFF) && !run_task_list(s, SCENE_VBLANK_TASKS))
        return false;
    after_vblank_tasks(g, pf);
    leave_registers_of_the_copy(s);
    m_set_flag(s, CPU_FLAG_M, true);

    bool carry = m_flag(s, CPU_FLAG_C);
    if (!read_controller_and_pause(s, &carry))
        return false;

    uint16_t paused = scene_paused(g);
    if (paused != 0) {
        dim_for_pause(g, pf);
        s->cpu.a = (uint16_t)(paused & 0xFF00);
        m_set_flag(s, CPU_FLAG_C, scene_brightness(g) & 1);
    } else {
        m_set_flag(s, CPU_FLAG_C, carry);
        if (!run_task_list(s, SCENE_FRAME_TASKS))
            return false;
    }
    m_set_flag(s, CPU_FLAG_M, true);
    install_handler(g, FRAME_HANDLER);
    return true;
}

/* Entered with 16-bit index registers; back, the callers read A's high byte and
 * the carry as the original leaves them. */
void scene_frame_work(Snes *s)
{
    if (frame_work(s))
        m_rtl(s);
}

/* The song's send, as the NMI handler makes it: A the song, X the two
 * parameters it sends as $FF, N and Z from that load and the carry from the
 * compare. False where the port stops. */
static bool send_song(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_M, true);
    uint8_t song;
    uint8_t playing = scene_song_playing(g);
    if (!song_to_send(g, &song))
        return true;
    m_set_a8(s, song);
    m_set_flag(s, CPU_FLAG_C, song >= playing);
    if (!m_bank_reaches_io(s)) {
        /* the command buffer isn't where the audio service writes it */
        m_stop_at(s, 0xC309C0, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_D | DC_AL);
        return false;
    }
    s->cpu.x = 0xFFFF;
    m_set_flag(s, CPU_FLAG_N, true);
    m_set_flag(s, CPU_FLAG_Z, false);
    au_command(m_platform(s), PF_CALL(0xC309D1, 0xC70004),
               (AudioCommand){ .command = PLAY_SONG, .p1 = song, .p2 = SONG_VOLUME_AS_IS, .p3 = SONG_ECHO_AS_IS,
                               .count = 3 });
    if (!can_continue(s) || !m_flag(s, CPU_FLAG_M))
        return false;
    set_scene_song_sending(g, 0);
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

/* One task, called as the list's loop calls it: the data bank, the slot, P and
 * D saved around a call through the list, and for the vblank list the direct
 * page in 8-bit A. A task that takes itself off the list, or hands its slot to
 * another, leaves through the list's own code, which unwinds the call; the
 * original then finishes the loop. The slot's load that found the task sets
 * N and Z before P is saved. */
static void call_task(Snes *s, SceneTaskList list, unsigned slot, SceneTask task)
{
    Cpu *c = &s->cpu;
    bool vblank = list == SCENE_VBLANK_TASKS;
    uint16_t index = (uint16_t)(slot * 2);
    m_set_nz16(s, task);
    m_push8(s, c->db);
    m_push16(s, index);
    m_push8(s, c->p);
    m_push16(s, c->d);
    c->x = index;
    c->a = task;
    if (vblank) {
        c->a = c->d;
        m_set_nz16(s, c->d);
        m_set_flag(s, CPU_FLAG_M, true);
    }
    m_jsr(s, vblank ? 0x7E39CF : 0x7E3998, 0x7E0000u | task);
    c->d = m_pull16(s);
    c->p = m_pull8(s);
    m_pull16(s);
    c->db = m_pull8(s);
}

// $7E:398A, $7E:39BE
/* Every task on a list, in order; the list is read again after each task,
 * which can change it. A is the last task's, or 0 where the last slot was
 * empty; the carry is the loop's compare. */
static void run_tasks(Snes *s, SceneTaskList list)
{
    GameState *g = state_of(s);
    unsigned count = list_length(list);
    m_set_flag(s, CPU_FLAG_M, false);
    for (unsigned slot = 0; slot < count; slot++) {
        SceneTask task = list == SCENE_VBLANK_TASKS ? scene_early_tasks(g, slot) : scene_tasks_running(g, slot);
        if (task != NO_TASK)
            call_task(s, list, slot, task);
        else
            s->cpu.a = 0;
        m_set_flag(s, CPU_FLAG_C, slot + 1 == count);   /* the loop's compare; its X, N and Z aren't read */
    }
    m_rtl(s);
}

/* The frame's tasks, from the copy of the list the frame's work made, each
 * called with 16-bit A. */
void scene_run_frame_tasks(Snes *s) { run_tasks(s, SCENE_FRAME_TASKS); }

/* The vblank tasks, each called with 8-bit A and the direct page in A. */
void scene_run_vblank_tasks(Snes *s) { run_tasks(s, SCENE_VBLANK_TASKS); }
