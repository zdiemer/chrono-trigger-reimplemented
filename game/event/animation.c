/* Event commands: object animation.
 *
 * Object tables, as these commands use them: $1680 animation number, $1780
 * animation mode (0; 1 looping; 2 during a timed play; 3 a static frame from
 * $1301), $1781 the animation a timed play was started with, $1601 and
 * $1681 cleared whenever an animation starts, and $7F:0B01 a countdown other
 * code decrements while a timed animation plays. */
#include "event/event.h"

#include "event/script.h"

#define COUNTDOWN 0x7F0B01u

static void set8(Snes *s, uint16_t table, uint8_t v) { m_write8(s, m_abs(s, table) + s->cpu.x, v); }
static uint8_t get8(Snes *s, uint16_t table) { return m_read8(s, m_abs(s, table) + s->cpu.x); }

/* $C0:2E74, X = own slot, A = mode: STA $1780,X : STZ $1601,X : LDA #$00 :
 * STA $7F0B01,X : STZ $1681,X : TYX : INX : SEC : RTS */
static void start_mode(Snes *s)
{
    set8(s, 0x1780, (uint8_t)s->cpu.a);
    set8(s, 0x1601, 0x00);
    m_lda8(s, 0x00);
    m_write8(s, COUNTDOWN + s->cpu.x, 0x00);
    set8(s, 0x1681, 0x00);
    m_tyx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* $C0:2E6D, A = animation: LDX $6D : STA $1680,X : LDA #$01, then start_mode. */
static void loop_animation(Snes *s)
{
    uint8_t animation = (uint8_t)s->cpu.a;
    ldx_current_object(s);
    set8(s, 0x1680, animation);
    m_lda8(s, 0x01);
    start_mode(s);
}

// $C0:2E67
/* $AA: loop animation (operand 1). */
void event_cmd_loop_animation(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    loop_animation(s);
}

// $C0:2E87
/* $B3: loop animation 0. */
void event_cmd_loop_animation_0(Snes *s)
{
    m_lda8(s, 0x00);
    loop_animation(s);
}

// $C0:2E8B
/* $B4: loop animation 1. */
void event_cmd_loop_animation_1(Snes *s)
{
    m_lda8(s, 0x01);
    loop_animation(s);
}

// $C0:2E8F
/* $AE: reset the animation (number 0, mode 0). */
void event_cmd_reset_animation(Snes *s)
{
    ldx_current_object(s);
    m_lda8(s, 0x00);
    set8(s, 0x1680, 0x00);
    m_lda8(s, 0x00);
    start_mode(s);
}

/* A = animation, X = own slot: STA $1781,X : STZ $1681,X : STZ $1601,X : if
 * mode is 0, $1680,X = $FF : mode 2. */
static void begin_timed(Snes *s)
{
    set8(s, 0x1781, (uint8_t)s->cpu.a);
    set8(s, 0x1681, 0x00);
    set8(s, 0x1601, 0x00);
    if (m_lda8(s, get8(s, 0x1780)) == 0)
        set8(s, 0x1680, m_lda8(s, 0xFF));
    set8(s, 0x1780, m_lda8(s, 0x02));
}

/* The countdown reached 1: clear it, $1601 and $1681, and restore mode 1
 * (or mode 0 with animation 0 if $1680 is $FF). Continue after the command. */
static void end_timed(Snes *s, int command_length)
{
    m_lda8(s, 0x00);
    m_write8(s, COUNTDOWN + s->cpu.x, 0x00);
    set8(s, 0x1601, 0x00);
    set8(s, 0x1681, 0x00);
    uint8_t animation = m_lda8(s, get8(s, 0x1680));
    m_cmp8(s, animation, 0xFF);
    if (animation == 0xFF) {
        set8(s, 0x1680, 0x00);
        m_lda8(s, 0x00);
    } else {
        m_lda8(s, 0x01);
    }
    set8(s, 0x1780, (uint8_t)s->cpu.a);
    m_tyx(s);
    while (command_length--)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* LDX $6D : LDA $7F0B01,X -- the timed play's countdown (0: not playing). */
static uint8_t countdown(Snes *s)
{
    ldx_current_object(s);
    return m_lda8(s, m_read8(s, COUNTDOWN + s->cpu.x));
}

// $C0:2E9A
/* $AB: play animation (operand 1) until the countdown, started at 2, runs
 * out; then go back to looping (see end_timed). Stays on the command while it
 * plays. */
void event_cmd_play_animation(Snes *s)
{
    uint8_t count = countdown(s);
    if (count == 0) {
        m_tyx(s);
        m_inx(s);
        m_lda8(s, script_byte(s, s->cpu.x));
        ldx_current_object(s);
    } else if (m_lda8(s, (uint8_t)(count - 1)) == 0) {
        end_timed(s, 2);
        return;
    } else {
        m_tyx(s);
        m_inx(s);
        uint8_t animation = m_lda8(s, script_byte(s, s->cpu.x));
        ldx_current_object(s);
        uint8_t playing = get8(s, 0x1781);
        m_cmp8(s, animation, playing);
        if (animation == playing) {
            m_tyx(s);
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
    }
    begin_timed(s);
    m_write8(s, COUNTDOWN + s->cpu.x, m_lda8(s, 0x02));
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:2F01
/* $B7: play animation (operand 1) with the countdown started at operand 2 + 1. */
void event_cmd_play_animation_for(Snes *s)
{
    uint8_t count = countdown(s);
    if (count == 0) {
        m_tyx(s);
        m_inx(s);
        m_lda8(s, script_byte(s, s->cpu.x));
        ldx_current_object(s);
    } else if (m_lda8(s, (uint8_t)(count - 1)) == 0) {
        end_timed(s, 3);
        return;
    } else {
        m_tyx(s);
        m_inx(s);
        uint8_t animation = m_lda8(s, script_byte(s, s->cpu.x));
        ldx_current_object(s);
        uint8_t playing = get8(s, 0x1781);
        m_cmp8(s, animation, playing);
        if (animation == playing) {
            m_tyx(s);
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
    }
    begin_timed(s);
    m_tyx(s);
    m_inx(s);
    m_inx(s);
    uint8_t frames = m_lda8(s, script_byte(s, s->cpu.x));
    frames = m_lda8(s, (uint8_t)(frames + 1));
    ldx_current_object(s);
    m_write8(s, COUNTDOWN + s->cpu.x, frames);
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:2F71
/* $AC: show static frame (operand 1): $1301,X = operand, mode 3. Ends the turn. */
void event_cmd_static_frame(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    uint8_t frame = m_lda8(s, script_byte(s, s->cpu.x));
    ldx_current_object(s);
    set8(s, 0x1301, frame);
    set8(s, 0x1681, 0x00);
    set8(s, 0x1601, 0x00);
    if (m_lda8(s, get8(s, 0x1780)) == 0)
        set8(s, 0x1680, m_lda8(s, 0xFF));
    set8(s, 0x1780, m_lda8(s, 0x03));
    m_tyx(s);
    m_inx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}
