/* Scenes: routines of the program the scene boot ($C3:0014) decompresses to
 * $7E:3000. The same addresses hold other code and data at other times, so
 * every entry has a guard that checks, by hash, that RAM holds the code the
 * port was written for (see m_code_matches).
 *
 * The arithmetic works on the math scratch cells of the caller's page (the
 * scene's own, or a sequence channel's: scene/scratch.c) and the CPU's multiplier
 * (game/cpu_io.h); the registers the callers read afterwards are few, and
 * each entry's don't-care set says which are dead (liveness follows the
 * program's callers). The task lists call the program's tasks, which are still
 * the original's code, so their loop is a bridge on the machine. */
#include "scene/scene.h"

#include "cpu_io.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/sound.h"
#include "vars.h"

/* ---- Arithmetic -------------------------------------------------------------------------- */

/* The page the caller has D on, which the guards have checked is one. */
static SceneMathPage caller_page(const Snes *s)
{
    SceneMathPage page = scene_math_scene();
    scene_math_page_of(m_direct_page(s), &page);
    return page;
}

/* A 16-bit add with carry in and out, as ADC; `*overflow` if the caller wants it. */
static uint16_t add_carry(bool *carry, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b + (*carry ? 1 : 0);
    *carry = sum > 0xFFFF;
    return (uint16_t)sum;
}

/* A shift left through the carry, as ROL on a 32-bit pair of words. */
static uint32_t rotate_left(bool *carry, uint32_t v)
{
    bool out = v & 0x80000000u;
    v = v << 1 | (*carry ? 1u : 0u);
    *carry = out;
    return v;
}

/* 32 bits at a time through two 16-bit ADCs, the carry chained between them. */
static uint32_t add32(bool *carry, uint32_t a, uint32_t b)
{
    uint16_t low = add_carry(carry, (uint16_t)a, (uint16_t)b);
    return low | (uint32_t)add_carry(carry, (uint16_t)(a >> 16), (uint16_t)(b >> 16)) << 16;
}

bool scene_divide_present(Snes *s)
{
    return scene_on_math_page(s) && m_code_matches(s, 0x7E339F, 55, 0x03E628A5184CD799ull);
}

// $7E:339F
/* Unsigned 32-bit division by shift and subtract: the dividend (the two
 * operand words) by the divisor (the product cell), into the quotient and
 * remainder, over 32
 * rounds. The dividend is shifted left as it goes, taking in the bit each
 * round's quotient shift lets out (and the caller's carry in the first), and is
 * left that way. */
static void divide(GameState *g, SceneMathPage page, bool carry)
{
    uint32_t dividend = scene_scratch32(g, page, SCRATCH_A);
    uint32_t divisor = scene_scratch32(g, page, SCRATCH_PRODUCT);
    uint32_t quotient = 0, remainder = 0;
    for (int round = 0; round < 32; round++) {
        dividend = rotate_left(&carry, dividend);
        remainder = rotate_left(&carry, remainder);
        carry = true;
        remainder = add32(&carry, remainder, ~divisor);
        if (!carry) {
            remainder = add32(&carry, remainder, divisor);   /* didn't fit: put it back */
            carry = false;
        }
        quotient = rotate_left(&carry, quotient);
    }
    set_scene_scratch32(g, page, SCRATCH_A, dividend);
    set_scene_scratch32(g, page, SCRATCH_QUOTIENT, quotient);
    set_scene_scratch32(g, page, SCRATCH_REMAINDER, remainder);
}

/* Entered with 16-bit A and X; the caller reads none of the registers it
 * leaves but Y. */
void scene_divide(Snes *s)
{
    divide(state_of(s), caller_page(s), m_flag(s, CPU_FLAG_C));
    m_rts(s);
}

bool scene_divide_8_8_present(Snes *s)
{
    return scene_on_math_page(s) && m_code_matches(s, 0x7E32E2, 189, 0xD04B8C1274AF93C8ull);
}

// $7E:32E2
/* `dividend` by the divisor (the second operand) into an 8.8 fixed-point
 * quotient in the product cell's low word: eight rounds bring the dividend's bits into the
 * remainder from the top (rotating the dividend, left in scratch a, as they
 * go), eight more bring in zeros, and each subtracts the divisor where it
 * fits. The divisor minus one is kept in the quotient cell's low word, which the
 * subtraction uses with the carry the shift lets out, so a remainder whose top
 * bit was shifted out subtracts one less. Returns the last shift's carry,
 * which the caller reads. */
static bool divide_8_8(GameState *g, SceneMathPage page, uint16_t dividend, bool carry)
{
    set_scene_scratch16(g, page, SCRATCH_A, dividend);
    uint16_t divisor = scene_scratch16(g, page, SCRATCH_B);
    uint16_t less_one = (uint16_t)(divisor - 1);
    set_scene_scratch16(g, page, SCRATCH_QUOTIENT, less_one);
    uint16_t remainder = 0, quotient = 0;
    for (int round = 0; round < 16; round++) {
        if (round < 8) {
            bool out = dividend & 0x8000;
            dividend = (uint16_t)(dividend << 1 | (carry ? 1 : 0));
            carry = out;
        } else {
            carry = false;
        }
        bool top = remainder & 0x8000;
        remainder = (uint16_t)(remainder << 1 | (carry ? 1 : 0));
        carry = top;
        remainder = add_carry(&carry, remainder, (uint16_t)~less_one);
        if (!carry)
            remainder = (uint16_t)(remainder + divisor);
        top = quotient & 0x8000;
        quotient = (uint16_t)(quotient << 1 | (carry ? 1 : 0));
        carry = top;
    }
    set_scene_scratch16(g, page, SCRATCH_A, dividend);
    set_scene_scratch16(g, page, SCRATCH_PRODUCT, quotient);
    return carry;
}

/* Entered with 16-bit A, the dividend; back, only the carry is read. */
void scene_divide_8_8(Snes *s)
{
    m_set_flag(s, CPU_FLAG_C, divide_8_8(state_of(s), caller_page(s), s->cpu.a, m_flag(s, CPU_FLAG_C)));
    m_rts(s);
}

bool scene_multiply_present(Snes *s)
{
    return scene_on_math_page(s) && m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull);
}

// $7E:31BF
/* Unsigned 16x16 multiply through the multiplier: the two operands into the
 * 32-bit product, from the four 8x8 partial products. The middle sum's carry
 * goes into the product's top byte by a 16-bit rotate, which also shifts the
 * byte after the product (the quotient cell's first) left, as the original
 * does, and its top bit becomes the carry into the next sum. */
static void multiply(GameState *g, SceneMathPage page)
{
    uint16_t a = scene_scratch16(g, page, SCRATCH_A), b = scene_scratch16(g, page, SCRATCH_B);
    hw_set_multiplicand(g, (uint8_t)b);
    uint16_t low = hw_multiply_by(g, (uint8_t)a);
    uint16_t cross1 = hw_multiply_by(g, (uint8_t)(a >> 8));
    hw_set_multiplicand(g, (uint8_t)(b >> 8));
    uint16_t cross2 = hw_multiply_by(g, (uint8_t)a);
    uint16_t high = hw_multiply_by(g, (uint8_t)(a >> 8));

    bool carry = false;
    uint16_t cross = add_carry(&carry, cross1, cross2);
    uint8_t cross_carry = carry;
    uint8_t after = scene_scratch8(g, page, SCRATCH_QUOTIENT);
    carry = after & 0x80;   /* the rotate shifts the byte after the product */
    set_scene_scratch8(g, page, SCRATCH_QUOTIENT, (uint8_t)(after << 1));
    uint16_t middle = add_carry(&carry, cross, low >> 8);
    uint16_t top = add_carry(&carry, high, (uint16_t)(middle >> 8 | cross_carry << 8));
    set_scene_scratch32(g, page, SCRATCH_PRODUCT,
                        (uint32_t)(low & 0xFF) | (uint32_t)(middle & 0xFF) << 8 | (uint32_t)top << 16);
}

/* Entered with 16-bit A and X; P, the data bank, X and Y come back as they
 * were, and A isn't read. */
void scene_multiply(Snes *s)
{
    multiply(state_of(s), caller_page(s));
    m_rts(s);
}

/* ---- The task lists and a task's start -------------------------------------------------- */

bool scene_shift_c00_words_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E5FFE, 16, 0x0744D6BC8E4FFD3Cull);
}

// $7E:5FFE
/* The start of a scene task: the words of the tilemap buffer from $40 to $7D
 * move up by one word, from the top down (a history of values, presumably).
 * Returns the last word moved. */
enum { HISTORY_FIRST = 0x40, HISTORY_LAST = 0x7C };
static uint16_t shift_history(GameState *g)
{
    uint16_t word = 0;
    for (unsigned at = HISTORY_LAST; at >= HISTORY_FIRST; at -= 2) {
        word = (uint16_t)(scene_tilemap_buffer_byte(g, at) | scene_tilemap_buffer_byte(g, at + 1) << 8);
        set_scene_tilemap_buffer_byte(g, at + 2, (uint8_t)word);
        set_scene_tilemap_buffer_byte(g, at + 3, (uint8_t)(word >> 8));
    }
    return word;
}

/* Stops after the loop, at $7E:600E, with A the last word and X where the
 * loop's compare left it. */
void scene_shift_c00_words(Snes *s)
{
    s->cpu.a = shift_history(state_of(s));
    s->cpu.x = HISTORY_FIRST - 2;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    m_set_flag(s, CPU_FLAG_C, true);
    s->cpu.pc = 0x600E;
}

bool scene_run_tasks_950_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E398A, 29, 0xA8C5FD94195E6607ull);
}

bool scene_run_tasks_940_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E39BE, 32, 0xC12FC96AEC0DACA2ull);
}

/* One task, called as the list's loop calls it: PHB : PHX : PHP : PHD, for the
 * early list TDC : SEP #$20 (the direct page in 8-bit A), JSR through the
 * table, and all four back. A task that leaves by one of the list's removal
 * exits unwinds the call, and the original finishes the loop. A is what the
 * task leaves. */
static void call_task(Snes *s, uint16_t index, uint16_t task, uint16_t site, bool early)
{
    Cpu *c = &s->cpu;
    m_set_nz16(s, task);   /* the load that found it, before the PHP */
    m_push8(s, c->db);
    m_push16(s, index);
    m_push8(s, c->p);
    m_push16(s, c->d);
    c->x = index;
    c->a = task;
    if (early) {
        c->a = c->d;
        m_set_nz16(s, c->d);
        m_set_flag(s, CPU_FLAG_M, true);
    }
    m_jsr(s, 0x7E0000u | site, 0x7E0000u | task);
    c->d = m_pull16(s);
    c->p = m_pull8(s);
    m_pull16(s);
    c->db = m_pull8(s);
}

// $7E:398A, $7E:39BE
/* Every task in a list, in order: each nonzero word is a task of the program.
 * The list is read again after each task, which can change it. A is the last
 * task's, or the last word's where that was 0. */
static void run_tasks(Snes *s, bool early, uint16_t site)
{
    GameState *g = state_of(s);
    unsigned count = early ? SCENE_EARLY_TASKS_COUNT : SCENE_TASKS_RUNNING_COUNT;
    m_set_flag(s, CPU_FLAG_M, false);
    for (unsigned i = 0; i < count; i++) {
        uint16_t task = early ? scene_early_tasks(g, i) : scene_tasks_running(g, i);
        if (task)
            call_task(s, (uint16_t)(i * 2), task, site, early);
        else
            s->cpu.a = 0;
        m_set_flag(s, CPU_FLAG_C, i + 1 == count);   /* the loop's compare; its X, N and Z aren't read */
    }
    m_rtl(s);
}

/* The scene's 16 tasks listed at $0950 (the NMI handler's copy of $0920), each
 * called with 16-bit A. */
void scene_run_tasks_950(Snes *s) { run_tasks(s, false, 0x3998); }

/* The scene's 8 tasks listed at $0940, each called with 8-bit A and the direct
 * page in A. */
void scene_run_tasks_940(Snes *s) { run_tasks(s, true, 0x39CF); }

/* ---- The sound driver ---------------------------------------------------------------------- */

/* The guard also checks that absolute addressing reaches the APU ports, which
 * the audio service addresses in bank $00. */
bool scene_wait_apu_port3_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E5AAA, 7, 0x441A7AC228BA6C77ull);
}

/* The driver's status (port 3): its low nibble is nonzero once the driver is
 * ready. */
static uint8_t driver_status(Platform *pf) { return au_status(pf, PF_AT(0x7E5AAA), 3); }

// $7E:5AAA
/* One pass of the wait for the driver's status to be nonzero, then return.
 * Idle entry, 8-bit A holding the status. */
void scene_wait_apu_port3_loop(Snes *s)
{
    uint8_t status = driver_status(m_platform(s)) & 0x0F;
    m_set_a8(s, status);
    m_set_nz8(s, status);
    if (status != 0)
        m_rts(s);
}

/* The routine at $7E:377E, whose absolute addressing must reach $00:1E00 and the
 * APU ports. */
bool scene_send_sound_command_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E377E, 40, 0x4A45A38F11AEF81Bull);
}

// $7E:3780
/* The driver command in the sound command buffer to ports 3-0, the way
 * $C7:061C sends it but without the driver's dispatch. Returns the command,
 * which the echo wait compares with port 0. */
static uint8_t send_sound_command(const GameState *g, Platform *pf)
{
    static const uint32_t sites[4] = { 0x7E3786, 0x7E378C, 0x7E3792, 0x7E3798 };
    for (uint8_t port = 3; port > 0; port--)
        au_driver_write(pf, PF_AT(sites[3 - port]), port, sound_command_parameters(g, port - 1u));
    uint8_t command = sound_command(g);
    au_driver_write(pf, PF_AT(sites[3]), 0, command);
    return command;
}

/* Stops at the echo wait at $7E:379B, with 8-bit A the command and X the
 * number of passes the wait makes before it sends again. */
void scene_send_sound_command_again(Snes *s)
{
    uint8_t command = send_sound_command(state_of(s), m_platform(s));
    m_set_a8(s, command);
    m_set_nz8(s, command);
    s->cpu.x = 0x0400;
    s->cpu.pc = 0x379B;
}

// $7E:377E
/* The scene's direct send of the driver command in the buffer (its pause sound,
 * the race's sounds): SEP #$20, then the send. With APU-direct the send and its
 * echo wait run synchronously. */
void scene_send_sound_command(Snes *s)
{
    if (au_driver_run_direct(m_platform(s), 0x7E377E, 0x7E37A4))
        return;
    m_set_flag(s, CPU_FLAG_M, true);
    scene_send_sound_command_again(s);
}

// $7E:379B
/* One pass of the echo wait: to the RTS at $7E:37A5 when port 0 reads the
 * command, or after $0400 passes to $7E:37A3, which sends again. Idle entry,
 * 8-bit A and 16-bit X. */
void scene_wait_sound_command_echo_loop(Snes *s)
{
    uint8_t command = (uint8_t)s->cpu.a;
    uint8_t echo = au_driver_read(m_platform(s), PF_AT(0x7E379B), 0);
    m_set_flag(s, CPU_FLAG_C, command >= echo);
    m_set_nz8(s, (uint8_t)(command - echo));
    if (command == echo) {
        s->cpu.pc = 0x37A5;
        return;
    }
    uint16_t passes = (uint16_t)(s->cpu.x - 1);
    s->cpu.x = passes;
    m_set_nz16(s, passes);
    if (passes == 0)
        s->cpu.pc = 0x37A3;
}

/* The race's code at $7E:4BCB, whose absolute addressing must reach the APU
 * ports. */
bool scene_race_sound_present(Snes *s)
{
    return m_bank_reaches_io(s) && scene_on_scene_page(s) && m_code_matches(s, 0x7E4BCB, 17, 0x0E86BCEA28400770ull);
}

// $7E:4BD2
/* In the race, unless a song is being sent: the race's sound parameter to port
 * 1 and $89 to port 0, without waiting for an echo (presumably a parameter the
 * driver reads while port 0 holds $89). Continues at $7E:4BDC with 8-bit A
 * the $89. */
enum { RACE_SOUND_COMMAND = 0x89 };
void scene_race_send_sound_parameter(Snes *s)
{
    Platform *pf = m_platform(s);
    au_driver_write(pf, PF_AT(0x7E4BD4), 1, scene_race_sound_parameter(state_of(s)));
    au_driver_write(pf, PF_AT(0x7E4BD9), 0, RACE_SOUND_COMMAND);
    m_set_a8(s, RACE_SOUND_COMMAND);
    m_set_nz8(s, RACE_SOUND_COMMAND);
    s->cpu.pc = 0x4BDC;
}

bool scene_wait_39_clear_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E5211, 4, 0x19A0BC775B1B7057ull);
}

// $7E:5211
/* One pass of the race's wait for its pending tilemap upload, which the NMI's
 * work clears; then on at $7E:5215. Idle entry, 16-bit A. */
void scene_wait_39_clear_loop(Snes *s)
{
    uint16_t pending = scene_race_tilemap_pending(state_of(s));
    s->cpu.a = pending;
    m_set_nz16(s, pending);
    if (pending == 0)
        s->cpu.pc = 0x5215;
}
