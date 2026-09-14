/* Battle: synchronizing with the display. */
#include "battle/battle.h"

#include "port/machine.h"

/* Guard for battle_wait_for_frame_loop: each pass only does nothing while
 * $CD02 is clear (otherwise $CD:3E44 talks to the sound driver). */
bool battle_frame_wait_is_idle(Snes *s)
{
    return snes_bus_peek(&s->bus, m_abs(s, 0xCD02)) == 0;
}

// $C1:0080
/* The loop in the wait-for-next-frame routine at $C1:007E: calls $CD:0036
 * (which jumps to $CD:04A6, which calls $CD:3E44, which returns at once with
 * $CD02 clear) until the NMI handler clears $9E. Idle entry, 8-bit A; exits
 * to the RTS at $C1:0088. One pass, including the stack bytes its calls
 * write. */
void battle_wait_for_frame_loop(Snes *s)
{
    /* JSL $CD0036 ... JMP $04A6 : JSR $3E44 */
    uint16_t s0 = s->cpu.s;
    m_push8(s, 0xC1);
    m_push16(s, 0x0083);
    m_push16(s, 0x04A8);
    /* LDA $CD02 : BEQ : RTS : RTL */
    m_lda8(s, m_read8(s, m_abs(s, 0xCD02)));
    s->cpu.s = s0;
    /* LDA $9E : BNE loop */
    if (m_lda8(s, m_read8(s, m_dp(s, 0x9E))) == 0)
        s->cpu.pc = 0x0088;
}

// $CD:3E81
/* The loop in $CD:3E7D, which waits for the NMI handler to clear $CA20: calls
 * $CD:3E44 (which returns at once with $CD02 clear) until it has. Idle entry,
 * guarded like battle_wait_for_frame_loop; exits to $CD:3E89. */
void battle_wait_for_nmi_loop(Snes *s)
{
    /* JSR $3E44 : LDA $CD02 : BEQ : RTS */
    uint16_t s0 = s->cpu.s;
    m_push16(s, 0x3E83);
    m_lda8(s, m_read8(s, m_abs(s, 0xCD02)));
    s->cpu.s = s0;
    /* LDA $CA20 : BNE loop */
    if (m_lda8(s, m_read8(s, m_abs(s, 0xCA20))) == 0)
        s->cpu.pc = 0x3E89;
}

// $C1:003C
/* A loop that calls $CD:0012 (a jump to $CD:0534, which returns $CA1F clamped
 * to 0 or 1) until $CA1F is clear, which interrupt code does. Idle entry, 8-bit
 * A; exits to $C1:0042. */
void battle_wait_ca1f_loop(Snes *s)
{
    /* JSL $CD0012 ... LDA $CA1F : BEQ : LDA #$01 : RTL */
    uint16_t s0 = s->cpu.s;
    m_push8(s, 0xC1);
    m_push16(s, 0x003F);
    uint8_t v = m_lda8(s, m_read8(s, m_abs(s, 0xCA1F)));
    s->cpu.s = s0;
    if (v != 0)
        m_lda8(s, 0x01);   /* BNE loop */
    else
        s->cpu.pc = 0x0042;
}

/* LDA $004212 */
static uint8_t read_hvbjoy(Snes *s) { return m_lda8(s, m_read8(s, 0x004212)); }

// $CD:3B84
/* The first loop of the wait-for-scanline routine at $CD:3B82: wait while in
 * vblank. Idle entry, 8-bit A; continues at $CD:3B8A. */
void battle_wait_vblank_end_loop(Snes *s)
{
    if (!(read_hvbjoy(s) & 0x80))
        s->cpu.pc = 0x3B8A;
}

// $CD:3B8A
/* Second loop: wait for hblank. Idle entry, 8-bit A; continues at $CD:3B92. */
void battle_wait_hblank_loop(Snes *s)
{
    uint8_t v = read_hvbjoy(s);
    if (m_lda8(s, v & 0x40) != 0)
        s->cpu.pc = 0x3B92;
}

// $D1:FEB8
/* In the battle IRQ handler: wait for hblank. Idle entry, 8-bit A; continues
 * at $D1:FEBF. */
void battle_irq_wait_hblank_loop(Snes *s)
{
    uint8_t v = m_lda8(s, m_read8(s, m_abs(s, 0x4212)));
    if (m_lda8(s, v & 0x40) != 0)
        s->cpu.pc = 0xFEBF;
}

// $CD:3B92
/* Third loop: wait for hblank to end, then latch the counters and compare the
 * vertical position with the line in $45; loop until it's reached. Idle
 * entry, 8-bit A; returns through the RTS at $CD:3BAA. */
void battle_wait_line_loop(Snes *s)
{
    uint8_t v = read_hvbjoy(s);
    if (m_lda8(s, v & 0x40) != 0)
        return;
    m_lda8(s, m_read8(s, 0x002137));
    m_lda8(s, m_read8(s, 0x00213F));
    uint8_t line = m_lda8(s, m_read8(s, 0x00213D));
    uint8_t target = m_read8(s, m_dp(s, 0x45));
    m_cmp8(s, line, target);
    if (line < target)
        return;   /* BCC back to the loop head */
    s->cpu.pc = 0x3BAA;
    m_rts(s);
}
