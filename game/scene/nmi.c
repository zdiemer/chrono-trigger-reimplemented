/* Scenes: the interrupt handlers of bank C3.
 *
 * The scene program points the NMI stub at $0500 to $C3:08B3, which does the
 * frame's work ($C3:08D0, an entry of its own that the program also calls
 * directly) and then sends a changed song to the sound driver. While the work
 * runs, the stub points at $C3:08C9, which only acknowledges a second NMI.
 * The boot ($C3:0000, scene/boot.c) installs $C3:0548 and $C3:0529 until the
 * program installs its own.
 *
 * The frame's work: acknowledge the NMI, per-line effects off and the screen
 * blanked while the NMI works (with interrupts enabled again where the scene
 * uses the line interrupt); unless paused, run the early task list; the
 * per-line effects and brightness back; count the frame; copy the task list to
 * its running copy; read the controller, where Start toggles pause (with its
 * sound) and L+R+Select+Start resets; then, paused, show the screen at half
 * brightness, or run the tasks.
 *
 * What the frame's work does is logic on the game's state and the platform.
 * The task lists and the pause sound are routines of the program, still the
 * original's code, and the song goes to the sound driver the same way, so the
 * handlers that call them are bridges on the machine: they make those calls with
 * the registers the original has there, and stop where it waits. */
#include "scene/scene.h"

#include "platform/audio.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/display.h"
#include "vars.h"

/* The NMI stub's target: the handler, and the one that runs while it works. */
enum { NMI_HANDLER = 0x08B3, NMI_WHILE_WORKING = 0x08C9 };

/* scene_flags: bit 7, the scene uses the line interrupt (so interrupts are
 * enabled again while the NMI works); bit 5, song changes aren't sent. */
enum { SCENE_FLAG_LINE_IRQ = 0x80, SCENE_FLAG_NO_SONGS = 0x20 };

/* The controller: Start (in the high byte) toggles pause, and these four
 * together reset. */
enum { PAD_START_HIGH = 0x10, PAD_RESET_COMBINATION = 0x3030 };

/* Sound driver commands the pause sends through the program ($7E:377A): the
 * pause sound, and on unpausing the command with its parameters. */
enum { PAUSE_SOUND = 0xF2, UNPAUSE_COMMAND = 0x18, UNPAUSE_PARAMETER = 0x44, UNPAUSE_VOLUME = 0x80 };
enum { SONG_COMMAND = 0x10 };

/* ---- The frame's work, as logic ---------------------------------------------------- */

/* The stub's target is the JML's operand's low word. */
static void set_nmi_handler(GameState *g, uint16_t handler)
{
    set_nmi_vector(g, (nmi_vector(g) & 0xFF0000FFu) | (uint32_t)handler << 8);
}

/* The brightness the work restores: 0 means blank. */
static VidDisplay display_for(uint8_t brightness)
{
    return brightness ? vid_display_decode(brightness) : (VidDisplay){ .blank = true };
}

// $C3:08D0
/* The start of the frame's work: the NMI acknowledged, the per-line effects off,
 * a second NMI pointed at the handler that only acknowledges, and the screen
 * blanked. Returns the acknowledge's status. */
static uint8_t begin_frame_work(GameState *g, Platform *pf)
{
    uint8_t ack = tm_ack_vblank(pf, PF_AT(0xC308D9));
    vid_enable_line_effects(pf, PF_AT(0xC308DC), 0);
    set_nmi_handler(g, NMI_WHILE_WORKING);
    vid_set_display(pf, PF_AT(0xC308EF), &(VidDisplay){ .blank = true });
    return ack;
}

/* After the early tasks: the per-line effects and the brightness back, the
 * frame counted, and the task list copied to the copy the tasks run from. */
static void after_early_tasks(GameState *g, Platform *pf)
{
    vid_enable_line_effects(pf, PF_AT(0xC30900), scene_line_effects(g));
    VidDisplay display = display_for(scene_brightness(g));
    vid_set_display(pf, PF_AT(0xC3090A), &display);
    set_scene_frame_counter(g, (uint16_t)(scene_frame_counter(g) + 1));
    for (unsigned i = 0; i < SCENE_TASKS_COUNT; i++)
        set_scene_tasks_running(g, i, scene_tasks(g, i));
}

typedef enum { PAD_READ, PAD_BUSY, PAD_RESET } PadRead;

// $C3:0A8E
/* Read the controller into the scene's joypad cell once the automatic read is
 * done; L+R+Select+Start, where resetting is allowed, blanks the screen and
 * stops interrupts and per-line effects for the reset. `*status` is the video
 * status the wait read. */
static PadRead read_pad(GameState *g, Platform *pf, uint8_t *status, uint16_t *pad)
{
    *status = tm_video_status(pf, PF_AT(0xC30A90));
    if (*status & TM_STATUS_JOYPAD_BUSY)
        return PAD_BUSY;
    *pad = in_read(pf, PF_AT(0xC30A99), 0);
    set_scene_joypad(g, *pad);
    if (*pad != PAD_RESET_COMBINATION || scene_reset_disabled(g) != 0)
        return PAD_READ;
    vid_set_display(pf, PF_AT(0xC30AAC), &(VidDisplay){ .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xC30AAF), 0);
    vid_enable_line_effects(pf, PF_AT(0xC30AB2), 0);
    return PAD_RESET;
}

typedef enum { PAUSE_UNCHANGED, PAUSE_ON, PAUSE_OFF } PauseChange;

/* Start, pressed anew while pausing is allowed, toggles pause and puts its
 * sound in the sound command buffer. */
static PauseChange toggle_pause(GameState *g, uint16_t pad)
{
    if (scene_pause_enabled(g) == 0)
        return PAUSE_UNCHANGED;
    if (!(pad >> 8 & PAD_START_HIGH)) {
        set_scene_start_held(g, 0);
        return PAUSE_UNCHANGED;
    }
    if (scene_start_held(g) != 0)
        return PAUSE_UNCHANGED;
    set_scene_start_held(g, 1);
    uint16_t paused = (uint16_t)((scene_paused(g) + 1) & 1);
    set_scene_paused(g, paused);
    if (paused) {
        set_sound_command(g, PAUSE_SOUND);
        set_sound_command_parameters(g, 0, 0);
        return PAUSE_ON;
    }
    set_sound_command(g, UNPAUSE_COMMAND);
    set_sound_command_parameters(g, 0, UNPAUSE_PARAMETER);
    set_sound_command_parameters(g, 1, UNPAUSE_VOLUME);
    set_sound_command_parameters(g, 2, 0);
    return PAUSE_OFF;
}

/* Paused: the screen at half the scene's brightness. Returns the brightness. */
static uint8_t dim_for_pause(const GameState *g, Platform *pf)
{
    uint8_t brightness = scene_brightness(g);
    VidDisplay dimmed = display_for(brightness >> 1);
    vid_set_display(pf, PF_AT(0xC30931), &dimmed);
    return brightness;
}

// $C3:09A4
/* A song to send the sound driver: the scene's song when it changed, unless a
 * send is already under way or the scene sends none. Marks it playing and the
 * send under way. */
static bool song_to_send(GameState *g, uint8_t *song, uint8_t *playing)
{
    if (scene_song_sending(g) != 0 || (scene_flags(g) & SCENE_FLAG_NO_SONGS))
        return false;
    *song = scene_song(g);
    *playing = scene_song_playing(g);
    if (*song == *playing)
        return false;
    set_scene_song_playing(g, *song);
    set_scene_song_sending(g, (uint8_t)(scene_song_sending(g) + 1));
    return true;
}

/* ---- Where the logic meets the original's registers -------------------------------- */

static void set_m8(Snes *s, bool m8) { m_set_flag(s, CPU_FLAG_M, m8); }

/* After a call-out: the code that follows runs in native mode with 16-bit index
 * registers and addresses the registers through a data bank that reaches them;
 * otherwise the port stops there. */
static bool can_continue(Snes *s)
{
    return !s->cpu.e && !m_flag(s, CPU_FLAG_X) && m_bank_reaches_io(s);
}

/* A task list of the program, called out to with 8-bit A (the early list) or
 * 16-bit A. */
static bool run_task_list(Snes *s, uint32_t site, uint32_t list, bool m8)
{
    set_m8(s, m8);
    m_jsl_dc(s, site, list, DC_X | DC_N | DC_Z | (m8 ? DC_A | DC_Y : 0));
    return can_continue(s);
}

/* The program's send of the command in the sound command buffer ($7E:377A),
 * with 16-bit A holding the word it stored last (neither negative nor zero). */
static bool send_pause_sound(Snes *s, uint32_t site, uint16_t stored)
{
    s->cpu.a = stored;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_jsl(s, site, 0x7E377A);
    return can_continue(s);
}

// $C3:0A8E
/* The controller read and the pause, as the frame's work calls them. `carry` is
 * the compare with the reset combination, which the callers see. False where
 * the port stops: the automatic read still busy (at the loop's branch: the wait
 * on it is the original's), the reset, or a call-out coming back with other
 * widths. */
static bool read_pause_button(Snes *s, bool *carry)
{
    GameState *g = state_of(s);
    uint8_t status;
    uint16_t pad = 0;
    switch (read_pad(g, m_platform(s), &status, &pad)) {
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
    set_m8(s, false);
    *carry = pad >= PAD_RESET_COMBINATION;
    PauseChange change = toggle_pause(g, pad);
    if (change == PAUSE_UNCHANGED)
        return true;
    m_set_flag(s, CPU_FLAG_C, *carry);
    bool ok = change == PAUSE_ON ? send_pause_sound(s, 0xC30AE1, PAUSE_SOUND)
                                 : send_pause_sound(s, 0xC30AF3, UNPAUSE_VOLUME);
    *carry = m_flag(s, CPU_FLAG_C);
    return ok;
}

/* The JSR to $C3:0A8E pushes this, and the frame is there at its call-outs. */
enum { READ_PAD_RETURN = 0x0921 };

// $C3:08D0
/* The frame's work (see the top of the file). Returns false where the port
 * stopped; otherwise the caller returns with A's high byte and the carry as
 * the original leaves them. */
static bool run_frame_work(Snes *s)
{
    Cpu *c = &s->cpu;
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    c->d = 0x0000;
    c->db = 0x00;
    uint8_t ack = begin_frame_work(g, pf);
    if (scene_flags(g) & SCENE_FLAG_LINE_IRQ)
        m_set_flag(s, CPU_FLAG_I, false);   /* CLI */
    c->x = NMI_WHILE_WORKING;
    m_set_flag(s, CPU_FLAG_C, ack == 0);   /* the acknowledge's compare with the TDC's 0 */
    if (scene_paused(g) == 0) {
        c->a = 0x0000;   /* the paused byte, over the TDC's 0 */
        if (!run_task_list(s, 0xC308F7, 0x7E39BE, true))
            return false;
    }
    after_early_tasks(g, pf);
    /* the registers the task list's block move leaves, which the pad read's
     * stops and call-out see */
    c->a = 0xFFFF;
    c->x = 0x0940;
    c->y = 0x0970;
    set_m8(s, true);

    bool carry = m_flag(s, CPU_FLAG_C);
    m_push16(s, READ_PAD_RETURN);
    if (!read_pause_button(s, &carry))
        return false;
    m_pull16(s);

    uint16_t paused = scene_paused(g);
    if (paused != 0) {
        uint8_t brightness = dim_for_pause(g, pf);
        c->a = (uint16_t)(paused & 0xFF00);
        m_set_flag(s, CPU_FLAG_C, brightness & 1);
    } else {
        c->a = 0x0000;   /* the paused word */
        m_set_flag(s, CPU_FLAG_C, carry);
        if (!run_task_list(s, 0xC30938, 0x7E398A, false))
            return false;
    }
    set_m8(s, true);
    set_nmi_handler(g, NMI_HANDLER);
    return true;
}

/* Entered with 16-bit index registers. */
void scene_frame_work(Snes *s)
{
    if (run_frame_work(s))
        m_rtl(s);
}

/* The song's send, as the NMI handler makes it: the registers at the original's
 * JSL are A the song, X the two parameters it sends as $FF, N and Z from that
 * load and the carry from the compare. False where the port stops. */
static bool update_song(Snes *s)
{
    Cpu *c = &s->cpu;
    GameState *g = state_of(s);
    set_m8(s, true);
    uint8_t song, playing;
    if (!song_to_send(g, &song, &playing))
        return true;
    m_set_a8(s, song);
    m_set_flag(s, CPU_FLAG_C, song >= playing);
    if (!m_bank_reaches_io(s)) {
        /* the command buffer isn't where the audio service writes it */
        m_stop_at(s, 0xC309C0, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_D | DC_AL);
        return false;
    }
    c->x = 0xFFFF;
    m_set_flag(s, CPU_FLAG_N, true);
    m_set_flag(s, CPU_FLAG_Z, false);
    au_command(m_platform(s), PF_CALL(0xC309D1, 0xC70004),
               (AudioCommand){ .command = SONG_COMMAND, .p1 = song, .p2 = 0xFF, .p3 = 0xFF, .count = 3 });
    if (!can_continue(s) || !m_flag(s, CPU_FLAG_M))
        return false;
    set_scene_song_sending(g, 0);
    return true;
}

/* The JSR to $C3:09A4 pushes this. */
enum { UPDATE_SONG_RETURN = 0x08C0 };

// $C3:08B3
/* The NMI handler: save the registers, do the frame's work (a call-out, so
 * that its own entry is compared on its own), send a changed song, restore. */
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

    m_push16(s, UPDATE_SONG_RETURN);
    if (!update_song(s))
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

// $C3:0548
/* The boot's NMI handler: acknowledge, and the screen on at full brightness. */
static void boot_vblank(Platform *pf)
{
    tm_ack_vblank(pf, PF_AT(0xC3054B));
    vid_set_display(pf, PF_AT(0xC30551), &(VidDisplay){ .brightness = 15 });
}

void scene_boot_nmi(Snes *s)
{
    boot_vblank(m_platform(s));
    m_rti(s);
}

// $C3:0529
/* The boot's line interrupt: acknowledge it, then blank the screen at the
 * start of the next hblank. The two waits for the beam are the original's
 * loops, idle entries, and the blank the entry after them (docs/TODO.md). The
 * handler saves A's low byte, which is on the stack across the waits. */
void scene_boot_line_irq(Snes *s)
{
    set_m8(s, true);
    m_push8(s, (uint8_t)s->cpu.a);
    /* A holds the acknowledge's status until the wait reloads it; its other
     * bits are open bus, which the original reads as the LDA's last operand
     * byte, 0 */
    uint8_t ack = tm_ack_line_irq(m_platform(s), PF_AT(0xC3052C));
    m_set_a8(s, ack & TM_LINE_IRQ_PENDING);
    m_stop_at(s, 0xC30530, DC_N | DC_Z);
}

/* The status the waits load into A: bits 1-5 are open bus, which the original
 * reads as the LDA's last operand byte, 0. The load sets N, and the BIT that
 * tests hblank Z. */
enum { STATUS_BITS = TM_STATUS_VBLANK | TM_STATUS_HBLANK | TM_STATUS_JOYPAD_BUSY };
static void load_status(Snes *s, uint8_t status)
{
    m_set_a8(s, status & STATUS_BITS);
    m_set_flag(s, CPU_FLAG_N, status & 0x80);
    m_set_flag(s, CPU_FLAG_Z, !(status & TM_STATUS_HBLANK));
}

// $C3:0530
/* One pass of the wait for the current hblank to end. Idle entry. */
void scene_boot_line_irq_wait_hblank_end_loop(Snes *s)
{
    uint8_t status = tm_video_status(m_platform(s), PF_AT(0xC30530));
    load_status(s, status);
    if (!(status & TM_STATUS_HBLANK))
        s->cpu.pc = 0x0538;
}

// $C3:0538
/* One pass of the wait for the next hblank to start. Idle entry. */
void scene_boot_line_irq_wait_hblank_loop(Snes *s)
{
    uint8_t status = tm_video_status(m_platform(s), PF_AT(0xC30538));
    load_status(s, status);
    if (status & TM_STATUS_HBLANK)
        s->cpu.pc = 0x0540;
}

// $C3:0540
/* In hblank: blank the screen, A's low byte back, and return. */
void scene_boot_line_irq_blank(Snes *s)
{
    vid_set_display(m_platform(s), PF_AT(0xC30542), &(VidDisplay){ .blank = true });
    m_set_a8(s, m_pull8(s));
    m_rti(s);
}
