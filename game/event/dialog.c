/* Event commands: dialog.
 *
 * $2B-$2D hold a 24-bit pointer to the dialog string table (set by $B8). A
 * dialog command opens a box on its first run: it marks the object as waiting
 * ($7F:0A00,X = 1), stores the string index ($2A), the speaking object's slot
 * ($2E) and the box position ($30: 1 top of the screen, 2 bottom, as seen in
 * the new-game movie; 0 not identified yet), clears $32 and bit 5 of $54, and
 * sets $29 = 1 to start the dialog. It stays on the command while $29 is nonzero;
 * once the dialog clears $29, the next run clears the mark and continues.
 *
 * The choice versions also set up the choices: $64 and $65 from the second
 * operand's bits 2-3 and 0-1, and $62 = 1; when done they copy the result in
 * $66 into the object's $7F:0A80 byte and clear $62.
 *
 * With no string table ($2D zero) the original jumps to an error handler; the
 * ports leave that to the original. */
#include "event/event.h"

#include "event/script.h"

#define WAITING 0x7F0A00u

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

static void stay(Snes *s)
{
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

static void next(Snes *s, int n)
{
    m_tyx(s);
    while (n--)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:3557
/* $B8: set the dialog string table pointer ($2B-$2D) from the 3 operands. */
void event_cmd_set_dialog_table(Snes *s)
{
    m_tyx(s);
    for (uint8_t cell = 0x2B; cell <= 0x2D; cell++) {
        m_inx(s);
        set_dp8(s, cell, m_lda8(s, script_byte(s, s->cpu.x)));
    }
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* LDA $2D : BNE ok : (error at `error_at`). Then LDA $29 : BNE stay : LDX $6D.
 * Returns false if the command has already been dealt with. */
static bool dialog_can_run(Snes *s, uint16_t error_at)
{
    if (m_lda8(s, dp8(s, 0x2D)) == 0) {
        s->cpu.pb = 0xC0;
        s->cpu.pc = error_at;
        return false;
    }
    if (m_lda8(s, dp8(s, 0x29)) != 0) {
        stay(s);
        return false;
    }
    ldx_current_object(s);
    return true;
}

/* LDA $6D : STA $2E : STZ $32 : LDA #$01 : STA $29 */
static void start_dialog(Snes *s)
{
    set_dp8(s, 0x2E, m_lda8(s, dp8(s, DP_OBJECT)));
    set_dp8(s, 0x32, 0x00);
    set_dp8(s, 0x29, m_lda8(s, 0x01));
}

/* LDA #$20 : TRB $54 : TYX : CLC : RTS */
static void finish_start(Snes *s)
{
    m_lda8(s, 0x20);
    m_trb8(s, m_dp(s, 0x54));
    stay(s);
}

/* $C0:3570 and copies: open a dialog box with the given $30 position value
 * (0 is written with STZ, others with LDA/STA). */
static void dialog(Snes *s, uint16_t error_at, uint8_t position)
{
    if (!dialog_can_run(s, error_at))
        return;
    if (m_lda8(s, m_read8(s, WAITING + s->cpu.x)) != 0) {
        m_lda8(s, 0x00);
        m_write8(s, WAITING + s->cpu.x, 0x00);
        next(s, 2);
        return;
    }
    m_lda8(s, 0x01);
    m_write8(s, WAITING + s->cpu.x, 0x01);
    m_tyx(s);
    m_inx(s);
    set_dp8(s, 0x2A, m_lda8(s, script_byte(s, s->cpu.x)));
    start_dialog(s);
    if (position == 0)
        set_dp8(s, 0x30, 0x00);
    else
        set_dp8(s, 0x30, m_lda8(s, position));
    finish_start(s);
}

// $C0:3570
/* $BB: show dialog string (operand 1), box position 0. */
void event_cmd_dialog(Snes *s) { dialog(s, 0x3574, 0); }

// $C0:35B5
/* $C1: show dialog string (operand 1) in a box at the top of the screen. */
void event_cmd_dialog_top(Snes *s) { dialog(s, 0x35B9, 1); }

// $C0:35FC
/* $C2: show dialog string (operand 1) in a box at the bottom of the screen. */
void event_cmd_dialog_bottom(Snes *s) { dialog(s, 0x3600, 2); }

/* $C0:3643 and copies: a dialog with choices. */
static void choice(Snes *s, uint16_t error_at, uint8_t position)
{
    if (!dialog_can_run(s, error_at))
        return;
    if (m_lda8(s, m_read8(s, WAITING + s->cpu.x)) != 0) {
        m_lda8(s, 0x00);
        m_write8(s, WAITING + s->cpu.x, 0x00);
        m_write8(s, 0x7F0A80u + s->cpu.x, m_lda8(s, dp8(s, 0x66)));
        set_dp8(s, 0x62, 0x00);
        next(s, 3);
        return;
    }
    if (position == 0)
        set_dp8(s, 0x30, 0x00);
    else
        set_dp8(s, 0x30, m_lda8(s, position));

    /* $C0:3674 */
    m_lda8(s, 0x01);
    m_write8(s, WAITING + s->cpu.x, 0x01);
    m_tyx(s);
    m_inx(s);
    set_dp8(s, 0x2A, m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    uint8_t layout = m_lda8(s, script_byte(s, s->cpu.x));
    set_dp8(s, 0xD9, layout);
    set_dp8(s, 0x65, m_lda8(s, layout & 0x03));
    uint8_t v = m_lda8(s, dp8(s, 0xD9));
    v = m_lsr8(s, v);
    v = m_lsr8(s, v);
    set_dp8(s, 0x64, m_lda8(s, v & 0x03));
    set_dp8(s, 0x62, m_lda8(s, 0x01));
    start_dialog(s);
    finish_start(s);
}

// $C0:3643
/* $C0: dialog string (operand 1) with choices (operand 2), box position 0. */
void event_cmd_choice(Snes *s) { choice(s, 0x3647, 0); }

// $C0:36AA
/* $C3: dialog with choices, at the top of the screen. */
void event_cmd_choice_top(Snes *s) { choice(s, 0x36AE, 1); }

// $C0:36DD
/* $C4: dialog with choices, at the bottom of the screen. */
void event_cmd_choice_bottom(Snes *s) { choice(s, 0x36E1, 2); }
