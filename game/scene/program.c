/* Scenes: routines of the program the scene boot ($C3:0014) decompresses to
 * $7E:3000. The same addresses hold other code and data at other times, so
 * every entry has a guard that checks, by hash, that RAM holds the code the
 * port was written for (see m_code_matches). */
#include "scene/scene.h"

#include "platform/audio.h"
#include "port/machine.h"
#include "scene/sound.h"

static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* ROL on a direct-page word, with N, Z and C. */
static void rol_dp16(Snes *s, uint8_t cell)
{
    uint16_t v = dp16(s, cell);
    uint16_t r = (uint16_t)(v << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
    set_dp16(s, cell, r);
    m_set_flag(s, CPU_FLAG_C, v & 0x8000);
    m_set_nz16(s, r);
}

bool scene_divide_present(Snes *s) { return m_code_matches(s, 0x7E339F, 55, 0x03E628A5184CD799ull); }

// $7E:339F
/* Unsigned 32-bit division by shift and subtract: dp $F0-$F3 divided by dp
 * $F4-$F7, quotient in dp $F8-$FB and remainder in dp $FC-$FF, over 32 rounds.
 * The dividend is shifted left as it goes (taking the carry the previous
 * round's quotient shift left, and the caller's carry in the first). Entered
 * with 16-bit A and X; returns with RTS. */
void scene_divide(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp16(s, 0xFA, 0x0000);
    set_dp16(s, 0xF8, 0x0000);
    set_dp16(s, 0xFE, 0x0000);
    set_dp16(s, 0xFC, 0x0000);
    m_ldx16(s, 0x0020);
    do {
        rol_dp16(s, 0xF0);
        rol_dp16(s, 0xF2);
        rol_dp16(s, 0xFC);
        rol_dp16(s, 0xFE);
        m_set_flag(s, CPU_FLAG_C, true);
        m_lda16(s, m_sbc16(s, dp16(s, 0xFC), dp16(s, 0xF4)));
        set_dp16(s, 0xFC, c->a);
        m_lda16(s, m_sbc16(s, dp16(s, 0xFE), dp16(s, 0xF6)));
        set_dp16(s, 0xFE, c->a);
        if (!m_flag(s, CPU_FLAG_C)) {
            m_lda16(s, m_adc16(s, dp16(s, 0xFC), dp16(s, 0xF4)));
            set_dp16(s, 0xFC, c->a);
            m_lda16(s, m_adc16(s, dp16(s, 0xFE), dp16(s, 0xF6)));
            set_dp16(s, 0xFE, c->a);
            m_set_flag(s, CPU_FLAG_C, false);
        }
        rol_dp16(s, 0xF8);
        rol_dp16(s, 0xFA);
        m_ldx16(s, (uint16_t)(c->x - 1));
    } while (c->x != 0);
    m_rts(s);
}

bool scene_divide_8_8_present(Snes *s) { return m_code_matches(s, 0x7E32E2, 189, 0xD04B8C1274AF93C8ull); }

// $7E:32E2
/* Divides A by dp $F2 into an 8.8 fixed-point quotient in dp $F4 (16 bits,
 * unrolled): A goes to dp $F0 and dp $F8 = $F2 - 1. Eight rounds shift the
 * remainder in A left taking bits from $F0 (ROL $F0 : ROL A), eight more
 * shift in zeros (ASL A); each subtracts $F8 with the shifted-out carry
 * (so a divisor of $F2), adds $F2 back and clears the carry when it didn't
 * fit, and rotates the carry into $F4. The remainder is left in A. Entered
 * with 16-bit A; returns with RTS. */
void scene_divide_8_8(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp16(s, 0xF0, c->a);
    m_lda16(s, dp16(s, 0xF2));
    m_lda16(s, (uint16_t)(c->a - 1));
    set_dp16(s, 0xF8, c->a);
    m_lda16(s, 0x0000);
    set_dp16(s, 0xF4, c->a);
    for (int round = 0; round < 16; round++) {
        if (round < 8) {
            rol_dp16(s, 0xF0);
            uint16_t a = c->a;                        /* ROL A */
            c->a = (uint16_t)(a << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
            m_set_flag(s, CPU_FLAG_C, a & 0x8000);
            m_set_nz16(s, c->a);
        } else {
            c->a = m_asl16(s, c->a);
        }
        m_lda16(s, m_sbc16(s, c->a, dp16(s, 0xF8)));
        if (!m_flag(s, CPU_FLAG_C)) {
            m_lda16(s, m_adc16(s, c->a, dp16(s, 0xF2)));
            m_set_flag(s, CPU_FLAG_C, false);
        }
        rol_dp16(s, 0xF4);
    }
    m_rts(s);
}

bool scene_multiply_present(Snes *s) { return m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull); }

// $7E:31BF
/* Unsigned 16x16 multiply through the hardware multiplier, with the data bank
 * set to 0: dp $F0-$F1 times dp $F2-$F3 into dp $F4-$F7, from the four 8x8
 * partial products (16-bit reads of $4216). The carry of the middle sum is
 * rotated into $F7 with a 16-bit ROL, which also shifts $F8 left as the
 * original does. Saves and restores P, the data bank, X and Y. Entered with
 * 16-bit A and X; returns with RTS. */
void scene_multiply(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_push8(s, c->db);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_sep(s, CPU_FLAG_X);
    m_ldy(s, 0x00);
    m_push8(s, (uint8_t)c->y);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);

    m_ldx(s, m_read8(s, m_dp(s, 0xF2)));
    m_ldy(s, m_read8(s, m_dp(s, 0xF0)));
    m_write8(s, m_abs(s, 0x4202), (uint8_t)c->x);
    m_write8(s, m_abs(s, 0x4203), (uint8_t)c->y);
    m_ldy(s, m_read8(s, m_dp(s, 0xF1)));
    set_dp16(s, 0xF6, 0x0000);
    m_lda16(s, m_read16(s, m_abs(s, 0x4216)));
    m_write8(s, m_abs(s, 0x4203), (uint8_t)c->y);
    set_dp16(s, 0xF4, c->a);

    m_ldx(s, m_read8(s, m_dp(s, 0xF3)));
    m_ldy(s, m_read8(s, m_dp(s, 0xF0)));
    m_lda16(s, m_read16(s, m_abs(s, 0x4216)));
    m_write8(s, m_abs(s, 0x4202), (uint8_t)c->x);
    m_write8(s, m_abs(s, 0x4203), (uint8_t)c->y);
    m_ldy(s, m_read8(s, m_dp(s, 0xF1)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, m_adc16(s, c->a, m_read16(s, m_abs(s, 0x4216))));
    rol_dp16(s, 0xF7);
    m_write8(s, m_abs(s, 0x4203), (uint8_t)c->y);
    m_lda16(s, m_adc16(s, c->a, dp16(s, 0xF5)));
    set_dp16(s, 0xF5, c->a);
    m_lda16(s, m_read16(s, m_abs(s, 0x4216)));
    m_lda16(s, m_adc16(s, c->a, dp16(s, 0xF6)));
    set_dp16(s, 0xF6, c->a);

    m_rep(s, CPU_FLAG_X);
    c->y = m_pull16(s);
    m_set_nz16(s, c->y);
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->p = m_pull8(s);
    m_rts(s);
}

bool scene_shift_c00_words_present(Snes *s) { return m_code_matches(s, 0x7E5FFE, 16, 0x0744D6BC8E4FFD3Cull); }

// $7E:5FFE
/* The loop at the start of a scene task: moves the words at $0C40-$0C7D up by
 * two bytes (LDX #$007C, then LDA $0C00,X : STA $0C02,X down to X = $40),
 * presumably a history of values. Stops after the loop, at $7E:600E. Entered
 * with 16-bit A and X, as the task list at $0950 calls its tasks. */
void scene_shift_c00_words(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx16(s, 0x007C);
    do {
        m_lda16(s, m_read16(s, m_abs(s, 0x0C00) + c->x));
        m_write16(s, m_abs(s, 0x0C02) + c->x, c->a);
        m_ldx16(s, (uint16_t)(c->x - 2));
        m_cmp16(s, c->x, 0x003E);
    } while (c->x != 0x003E);
    c->pc = 0x600E;
}

bool scene_run_tasks_950_present(Snes *s) { return m_code_matches(s, 0x7E398A, 29, 0xA8C5FD94195E6607ull); }
bool scene_run_tasks_940_present(Snes *s) { return m_code_matches(s, 0x7E39BE, 32, 0xC12FC96AEC0DACA2ull); }

/* The shared loop of the two task lists: for each nonzero word in the table
 * (read through the data bank), PHB : PHX : PHP : PHD, for the $0940 list
 * TDC : SEP #$20, then JSR (table,X) (through the program bank), then PLD :
 * PLP : PLX : PLB; X steps by 2 up to `end`. A task that leaves by one of the
 * list's removal exits unwinds the call, and the original finishes the loop. */
static void run_tasks(Snes *s, uint16_t table, uint16_t end, uint16_t call_site, bool pass_dp)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M);
    m_ldx16(s, 0x0000);
    do {
        if (m_lda16(s, m_read16(s, m_abs(s, table) + c->x)) != 0) {
            m_push8(s, c->db);
            m_push16(s, c->x);
            m_push8(s, c->p);
            m_push16(s, c->d);
            if (pass_dp) {
                m_tdc(s);
                m_sep(s, CPU_FLAG_M);
            }
            uint16_t task = m_jump_table(s, 0x7E, table, c->x);
            m_jsr(s, 0x7E0000u | call_site, 0x7E0000u | task);
            c->d = m_pull16(s);
            m_set_nz16(s, c->d);
            c->p = m_pull8(s);   /* as pushed: 16-bit X */
            c->x = m_pull16(s);
            m_set_nz16(s, c->x);
            c->db = m_pull8(s);
            m_set_nz8(s, c->db);
        }
        m_ldx16(s, (uint16_t)(c->x + 2));
        m_cmp16(s, c->x, end);
    } while (c->x != end);
    m_rtl(s);
}

// $7E:398A
/* Runs the scene's 16 tasks listed at $0950 (the NMI handler's copy of $0920),
 * each called with 16-bit A. Returns with RTL. */
void scene_run_tasks_950(Snes *s) { run_tasks(s, 0x0950, 0x0020, 0x3998, false); }

// $7E:39BE
/* Runs the scene's 8 tasks listed at $0940, each called with 8-bit A and the
 * direct page in A. Returns with RTL. */
void scene_run_tasks_940(Snes *s) { run_tasks(s, 0x0940, 0x0010, 0x39CF, true); }

/* The guard also checks that absolute addressing reaches the APU ports, which
 * the audio service addresses in bank $00. */
bool scene_wait_apu_port3_present(Snes *s)
{
    return m_io_bank(s->cpu.db) && m_code_matches(s, 0x7E5AAA, 7, 0x441A7AC228BA6C77ull);
}

// $7E:5AAA
/* Waits until the low nibble of APU port 3 (the driver's status, au_status) is
 * nonzero, then returns. Idle entry, 8-bit A. */
void scene_wait_apu_port3_loop(Snes *s)
{
    m_lda8(s, au_status(m_platform(s), PF_AT(0x7E5AAA), 3));
    if (m_lda8(s, (uint8_t)(s->cpu.a & 0x0F)) != 0) {
        s->cpu.pc = 0x5AB1;
        m_rts(s);
    }
}

/* The routine at $7E:377E, whose absolute addressing must reach $00:1E00 and the
 * APU ports. */
bool scene_send_sound_command_present(Snes *s)
{
    return m_io_bank(s->cpu.db) && m_code_matches(s, 0x7E377E, 40, 0x4A45A38F11AEF81Bull);
}

// $7E:3780
/* LDX #$0400, then $1E03, $1E02, $1E01 and $1E00 to ports 3-0: the driver
 * command in the buffer, sent the way $C7:061C sends it but without the
 * driver's dispatch. Stops at the echo wait at $7E:379B. */
void scene_send_sound_command_again(Snes *s)
{
    Platform *pf = m_platform(s);
    m_ldx(s, 0x0400);
    static const struct { uint16_t buffer, write; } bytes[] = {
        { 0x1E03, 0x3786 }, { 0x1E02, 0x378C }, { 0x1E01, 0x3792 }, { 0x1E00, 0x3798 },
    };
    for (uint8_t i = 0; i < 4; i++)
        au_driver_write(pf, PF_AT(0x7E0000 | bytes[i].write), (uint8_t)(3 - i),
                        m_lda8(s, m_read8(s, m_abs(s, bytes[i].buffer))));
    s->cpu.pc = 0x379B;
}

// $7E:377E
/* The scene's direct send of the driver command in $1E00-$1E03 (its pause sound,
 * the race's sounds): SEP #$20, then scene_send_sound_command_again. With
 * APU-direct the send and its echo wait run synchronously. */
void scene_send_sound_command(Snes *s)
{
    if (au_driver_run_direct(m_platform(s), 0x7E377E, 0x7E37A4))
        return;
    m_sep(s, CPU_FLAG_M);
    scene_send_sound_command_again(s);
}

// $7E:379B
/* CMP $2140 : BEQ : DEX : BNE loop -- the echo wait: exits to the RTS at $7E:37A5
 * when port 0 reads A, or after $0400 passes to $7E:37A3, which sends again.
 * Idle entry, 8-bit A and 16-bit X. */
void scene_wait_sound_command_echo_loop(Snes *s)
{
    uint8_t a = (uint8_t)s->cpu.a;
    uint8_t v = au_driver_read(m_platform(s), PF_AT(0x7E379B), 0);
    m_cmp8(s, a, v);
    if (a == v) {
        s->cpu.pc = 0x37A5;
        return;
    }
    if (m_set_index(s, &s->cpu.x, (uint16_t)(s->cpu.x - 1)) == 0)
        s->cpu.pc = 0x37A3;
}

/* The race's code at $7E:4BCB, whose absolute addressing must reach the APU
 * ports. */
bool scene_race_sound_present(Snes *s)
{
    return m_io_bank(s->cpu.db) && m_code_matches(s, 0x7E4BCB, 17, 0x0E86BCEA28400770ull);
}

// $7E:4BD2
/* LDA $4B : STA $2141 : LDA #$89 : STA $2140 -- in the race, unless $0388 is
 * set: dp $4B to port 1 and $89 to port 0, without waiting for an echo
 * (presumably a sound parameter the driver reads while port 0 holds $89).
 * Continues at $7E:4BDC. */
void scene_race_send_sound_parameter(Snes *s)
{
    Platform *pf = m_platform(s);
    au_driver_write(pf, PF_AT(0x7E4BD4), 1, m_lda8(s, m_read8(s, m_dp(s, 0x4B))));
    au_driver_write(pf, PF_AT(0x7E4BD9), 0, m_lda8(s, 0x89));
    s->cpu.pc = 0x4BDC;
}

bool scene_wait_39_clear_present(Snes *s) { return m_code_matches(s, 0x7E5211, 4, 0x19A0BC775B1B7057ull); }

// $7E:5211
/* Waits for dp $39 (16-bit) to become 0, which the scene's NMI work does; then
 * continues at $7E:5215. Idle entry, 16-bit A. */
void scene_wait_39_clear_loop(Snes *s)
{
    if (m_lda16(s, dp16(s, 0x39)) == 0)
        s->cpu.pc = 0x5215;
}
