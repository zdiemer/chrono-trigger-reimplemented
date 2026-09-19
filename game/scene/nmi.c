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
 * uses the line interrupt); unless paused, run the task list at $0940; the
 * per-line effects and brightness back; count the frame; copy the task list at
 * $0920 to $0950; read the controller, where Start toggles pause (with its
 * sound) and L+R+Select+Start resets; then, paused, show the screen at half
 * brightness, or run the tasks at $0950. The task lists and the pause sound
 * are routines of the program, called out to. */
#include "scene/scene.h"

#include "platform/audio.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"
#include "scene/display.h"

/* The NMI stub's target: the handler, and the one that runs while it works. */
enum { NMI_HANDLER = 0x08B3, NMI_WHILE_WORKING = 0x08C9 };
static void set_nmi_handler(Snes *s, uint16_t handler)
{
    m_write16(s, m_abs(s, RAM_ABS(RAM_NMI_VECTOR) + 1), handler);
}

/* RAM_SCENE_FLAGS: bit 7, the scene uses the line interrupt (so interrupts are
 * enabled again while the NMI works); bit 5, song changes aren't sent. */
enum { SCENE_FLAG_LINE_IRQ = 0x80, SCENE_FLAG_NO_SONGS = 0x20 };

/* The controller: Start (in the high byte) toggles pause, and these four
 * together reset. */
enum { PAD_START_HIGH = 0x10, PAD_RESET_COMBINATION = 0x3030 };

/* Sound driver commands the pause sends through the program ($7E:377A): the
 * pause sound, and on unpausing the command with its parameters. */
enum { PAUSE_SOUND = 0x00F2, UNPAUSE_COMMAND = 0x4418, UNPAUSE_PARAMETERS = 0x0080 };
enum { SONG_COMMAND = 0x10 };

static uint8_t byte_at(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static uint16_t word_at(Snes *s, uint16_t addr) { return m_read16(s, m_abs(s, addr)); }
static void set_byte_at(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }
static void set_word_at(Snes *s, uint16_t addr, uint16_t v) { m_write16(s, m_abs(s, addr), v); }

/* The brightness the work restores: 0 means blank. */
static VidDisplay display_for(uint8_t brightness)
{
    return brightness ? vid_display_decode(brightness) : (VidDisplay){ .blank = true };
}

/* ---- Where the port meets the original's registers ---------------------------------- */

static void set_m8(Snes *s, bool m8)
{
    s->cpu.p = (uint8_t)(m8 ? s->cpu.p | CPU_FLAG_M : s->cpu.p & ~CPU_FLAG_M);
}

/* After a call-out: the code that follows runs in native mode with 16-bit index
 * registers and addresses the registers through a data bank that reaches them;
 * otherwise the port stops there. */
static bool can_continue(Snes *s)
{
    return !s->cpu.e && !(s->cpu.p & CPU_FLAG_X) && m_io_bank(s->cpu.db);
}

/* A task list of the program, called out to with 8-bit A (list $0940) or
 * 16-bit A (list $0950). */
static bool run_task_list(Snes *s, uint32_t site, uint32_t list, bool m8)
{
    set_m8(s, m8);
    m_jsl_dc(s, site, list, DC_X | DC_N | DC_Z | (m8 ? DC_A | DC_Y : 0));
    return can_continue(s);
}

/* The program's send of the command in $1E00-$1E03 ($7E:377A), with 16-bit A
 * holding the word it stored last (neither negative nor zero). */
static bool send_pause_sound(Snes *s, uint32_t site, uint16_t stored)
{
    s->cpu.a = stored;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_jsl(s, site, 0x7E377A);
    return can_continue(s);
}

// $C3:0A8E
/* Read the controller into $0002 once the automatic read is done, and toggle
 * pause on Start while pausing is allowed. `carry` is the compare with the
 * reset combination, which the callers see. False where the port stops: the
 * automatic read still busy (at the loop's branch: the wait on it is the
 * original's), the reset, or a call-out coming back with other widths. */
static bool read_pause_button(Snes *s, bool *carry)
{
    Platform *pf = m_platform(s);
    uint8_t status = tm_video_status(pf, PF_AT(0xC30A90));
    if (status & TM_STATUS_JOYPAD_BUSY) {
        s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | status);
        m_set_flag(s, CPU_FLAG_Z, false);
        m_stop_at(s, 0xC30A95, DC_N);
        return false;
    }
    set_m8(s, false);
    PadBits pad = in_read(pf, PF_AT(0xC30A99), 0);
    set_word_at(s, RAM_ABS(RAM_SCENE_JOYPAD), pad);
    *carry = pad >= PAD_RESET_COMBINATION;
    if (pad == PAD_RESET_COMBINATION && byte_at(s, RAM_ABS(RAM_SCENE_RESET_DISABLED)) == 0) {
        vid_set_display(pf, PF_AT(0xC30AAC), &(VidDisplay){ .blank = true });
        tm_enable_interrupts(pf, PF_AT(0xC30AAF), 0);
        vid_enable_line_effects(pf, PF_AT(0xC30AB2), 0);
        s->cpu.a = 0x3080;   /* the reset combination's high byte, then the blank */
        m_set_flag(s, CPU_FLAG_C, true);
        set_m8(s, true);
        m_stop_at(s, 0x00FF00, DC_N | DC_Z);   /* JML to the reset code */
        return false;
    }
    if (word_at(s, RAM_ABS(RAM_SCENE_PAUSE_ENABLED)) == 0)
        return true;
    if (!(pad >> 8 & PAD_START_HIGH)) {
        set_word_at(s, RAM_ABS(RAM_SCENE_START_HELD), 0);
        return true;
    }
    if (word_at(s, RAM_ABS(RAM_SCENE_START_HELD)) != 0)
        return true;
    set_word_at(s, RAM_ABS(RAM_SCENE_START_HELD), 1);
    uint16_t paused = (uint16_t)((word_at(s, RAM_ABS(RAM_SCENE_PAUSED)) + 1) & 1);
    set_word_at(s, RAM_ABS(RAM_SCENE_PAUSED), paused);
    m_set_flag(s, CPU_FLAG_C, *carry);
    bool ok;
    if (paused) {
        set_word_at(s, RAM_ABS(RAM_SOUND_COMMAND), PAUSE_SOUND);
        ok = send_pause_sound(s, 0xC30AE1, PAUSE_SOUND);
    } else {
        set_word_at(s, RAM_ABS(RAM_SOUND_COMMAND), UNPAUSE_COMMAND);
        set_word_at(s, RAM_ABS(RAM_SOUND_COMMAND) + 2, UNPAUSE_PARAMETERS);
        ok = send_pause_sound(s, 0xC30AF3, UNPAUSE_PARAMETERS);
    }
    *carry = m_flag(s, CPU_FLAG_C);
    return ok;
}

/* The JSR to $C3:0A8E pushes this, and the frame is there at its call-outs. */
enum { READ_PAD_RETURN = 0x0921 };

// $C3:08D0
/* The frame's work (see the top of the file). Returns false where the port
 * stopped; otherwise the caller returns with A, the carry and the overflow as
 * the original leaves them. */
static bool run_frame_work(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    c->d = 0x0000;
    c->db = 0x00;
    uint8_t ack = tm_ack_vblank(pf, PF_AT(0xC308D9));
    vid_enable_line_effects(pf, PF_AT(0xC308DC), 0);
    set_nmi_handler(s, NMI_WHILE_WORKING);
    if (byte_at(s, RAM_ABS(RAM_SCENE_FLAGS)) & SCENE_FLAG_LINE_IRQ)
        c->p &= (uint8_t)~CPU_FLAG_I;
    vid_set_display(pf, PF_AT(0xC308EF), &(VidDisplay){ .blank = true });
    c->x = NMI_WHILE_WORKING;
    m_set_flag(s, CPU_FLAG_C, ack == 0);   /* the acknowledge's compare with the TDC's 0 */
    if (byte_at(s, RAM_ABS(RAM_SCENE_PAUSED)) == 0) {
        c->a = 0x0000;   /* the paused byte, over the TDC's 0 */
        if (!run_task_list(s, 0xC308F7, 0x7E39BE, true))
            return false;
    }
    vid_enable_line_effects(pf, PF_AT(0xC30900), byte_at(s, RAM_ABS(RAM_SCENE_LINE_EFFECTS)));
    VidDisplay display = display_for(byte_at(s, RAM_ABS(RAM_SCENE_BRIGHTNESS)));
    vid_set_display(pf, PF_AT(0xC3090A), &display);
    set_word_at(s, RAM_ABS(RAM_SCENE_FRAME_COUNTER), (uint16_t)(word_at(s, RAM_ABS(RAM_SCENE_FRAME_COUNTER)) + 1));

    /* MVN $00,$00: the task list to its running copy */
    for (uint16_t i = 0; i < RAM_SCENE_TASKS_COUNT * RAM_SCENE_TASKS_STRIDE; i++)
        m_write8(s, RAM_ABS(RAM_SCENE_TASKS_RUNNING) + i, m_read8(s, RAM_ABS(RAM_SCENE_TASKS) + i));
    c->a = 0xFFFF;
    c->x = RAM_ABS(RAM_SCENE_TASKS) + RAM_SCENE_TASKS_COUNT * RAM_SCENE_TASKS_STRIDE;
    c->y = RAM_ABS(RAM_SCENE_TASKS_RUNNING) + RAM_SCENE_TASKS_COUNT * RAM_SCENE_TASKS_STRIDE;
    c->db = 0x00;
    set_m8(s, true);

    bool carry = m_flag(s, CPU_FLAG_C);
    m_push16(s, READ_PAD_RETURN);
    if (!read_pause_button(s, &carry))
        return false;
    m_pull16(s);

    uint16_t paused = word_at(s, RAM_ABS(RAM_SCENE_PAUSED));
    if (paused != 0) {
        uint8_t brightness = byte_at(s, RAM_ABS(RAM_SCENE_BRIGHTNESS));
        uint8_t half = brightness >> 1;
        VidDisplay dimmed = display_for(half);
        vid_set_display(pf, PF_AT(0xC30931), &dimmed);
        c->a = (uint16_t)((paused & 0xFF00) | (half ? half : 0x80));
        m_set_flag(s, CPU_FLAG_C, brightness & 1);
    } else {
        c->a = 0x0000;
        m_set_flag(s, CPU_FLAG_C, carry);
        if (!run_task_list(s, 0xC30938, 0x7E398A, false))
            return false;
    }
    set_m8(s, true);
    c->x = NMI_HANDLER;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    set_nmi_handler(s, NMI_HANDLER);
    return true;
}

// $C3:08D0
void scene_frame_work(Snes *s)
{
    if (run_frame_work(s))
        m_rtl(s);
}

// $C3:09A4
/* Send the song in $038A to the sound driver (command $10) when it changed,
 * unless a send is already under way ($0388) or the scene sends none. False
 * where the port stops. */
static bool update_song(Snes *s)
{
    Cpu *c = &s->cpu;
    set_m8(s, true);
    if (byte_at(s, RAM_ABS(RAM_SCENE_SONG_SENDING)) != 0)
        return true;
    if (byte_at(s, RAM_ABS(RAM_SCENE_FLAGS)) & SCENE_FLAG_NO_SONGS)
        return true;
    uint8_t song = byte_at(s, RAM_ABS(RAM_SCENE_SONG));
    uint8_t playing = byte_at(s, RAM_ABS(RAM_SCENE_SONG_PLAYING));
    if (song == playing)
        return true;
    set_byte_at(s, RAM_ABS(RAM_SCENE_SONG_PLAYING), song);
    set_byte_at(s, RAM_ABS(RAM_SCENE_SONG_SENDING), (uint8_t)(byte_at(s, RAM_ABS(RAM_SCENE_SONG_SENDING)) + 1));
    if (!m_io_bank(c->db)) {
        /* the command buffer isn't where the audio service writes it */
        c->a = (uint16_t)((c->a & 0xFF00) | song);
        m_set_flag(s, CPU_FLAG_C, song >= playing);
        m_stop_at(s, 0xC309C0, DC_N | DC_Z);
        return false;
    }
    /* the registers at the original's JSL: A the song, X the two parameters
     * it sends as $FF, N and Z from that load, the carry from the compare */
    c->a = (uint16_t)((c->a & 0xFF00) | song);
    c->x = 0xFFFF;
    m_set_flag(s, CPU_FLAG_N, true);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_set_flag(s, CPU_FLAG_C, song >= playing);
    au_command(m_platform(s), PF_CALL(0xC309D1, 0xC70004),
               (AudioCommand){ .command = SONG_COMMAND, .p1 = song, .p2 = 0xFF, .p3 = 0xFF, .count = 3 });
    if (!can_continue(s) || !(c->p & CPU_FLAG_M))
        return false;
    set_byte_at(s, RAM_ABS(RAM_SCENE_SONG_SENDING), 0);
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
    c->p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X);
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

    c->p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X);
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
void scene_boot_nmi(Snes *s)
{
    Platform *pf = m_platform(s);
    tm_ack_vblank(pf, PF_AT(0xC3054B));
    vid_set_display(pf, PF_AT(0xC30551), &(VidDisplay){ .brightness = 15 });
    m_rti(s);
}

// $C3:0529
/* The boot's line interrupt: acknowledge it, then blank the screen at the
 * start of the next hblank. The two waits for the beam are the original's
 * loops, idle entries, and the blank the entry after them (docs/TODO.md). The
 * handler saves A's low byte, which is on the stack across the waits. */
void scene_boot_line_irq(Snes *s)
{
    s->cpu.p |= CPU_FLAG_M;
    m_push8(s, (uint8_t)s->cpu.a);
    /* A holds the acknowledge's status until the wait reloads it; its other
     * bits are open bus, which the original reads as the LDA's last operand
     * byte, 0 */
    uint8_t ack = tm_ack_line_irq(m_platform(s), PF_AT(0xC3052C));
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | (ack & TM_LINE_IRQ_PENDING));
    m_stop_at(s, 0xC30530, DC_N | DC_Z);
}

/* The status the waits load into A: bits 1-5 are open bus, which the original
 * reads as the LDA's last operand byte, 0. The load sets N, and the BIT that
 * tests hblank Z. */
enum { STATUS_BITS = TM_STATUS_VBLANK | TM_STATUS_HBLANK | TM_STATUS_JOYPAD_BUSY };
static void load_status(Snes *s, uint8_t status)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | (status & STATUS_BITS));
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
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | m_pull8(s));
    m_rti(s);
}
