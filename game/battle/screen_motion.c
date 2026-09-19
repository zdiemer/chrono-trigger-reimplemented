/* Battle: screen motion in bank D1 -- the screen scripts' layer offsets, and
 * the settings of the screen shake and the palette presets.
 *
 * Each screen script channel (screen.h) has a record of 16 bytes at
 * battle_screen_motion ($CA52): a direction flag and a base, then the jitter
 * and the offset BG3's moving scroll adds (battle_bg3_scroll and
 * battle_bg3_scroll_offset are the first channel's), and a second record at
 * battle_screen_swing ($CA7A) that drives the offset: a speed, how fast it
 * changes, and a 32-bit position. The channel is in battle_screen_channel. */
#include "battle/screen.h"

#include "battle/screen_data.h"
#include "port/machine.h"
#include "ram.h"

static void set_a(Snes *s, uint16_t a)
{
    s->cpu.a = a;
    s->cpu.p |= CPU_FLAG_M;
}

static uint32_t script_at(Snes *s) { return m_read16(s, RAM_BATTLE_SCREEN_SCRIPT) | (uint32_t)m_read8(s, RAM_BATTLE_SCREEN_SCRIPT + 2) << 16; }
static uint8_t operand(Snes *s, uint16_t n) { return m_read8(s, script_at(s) + n); }

static uint8_t scratch(Snes *s, uint8_t cell) { return m_read8(s, RAM_BATTLE_SCRATCH + cell); }
static void set_scratch(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, RAM_BATTLE_SCRATCH + cell, v); }

// $D1:F474
/* A random byte: the table entry at A + the frame count + the carry (its
 * index taking A's high byte along, as the original's TAX does). Returns the
 * byte; `index` gets the index the original leaves in X. */
static uint8_t random_byte(Snes *s, uint16_t a, bool carry, uint16_t *index)
{
    uint8_t sum = (uint8_t)((uint8_t)a + m_read8(s, RAM_BATTLE_FRAME_COUNT) + carry);
    *index = (uint16_t)((a & 0xFF00) | sum);
    return battle_random_byte(s, *index);
}

void battle_screen_random_byte(Snes *s)
{
    uint16_t index;
    uint8_t v = random_byte(s, s->cpu.a, m_flag(s, CPU_FLAG_C), &index);
    s->cpu.x = index;
    m_set_a8(s, v);
    m_rts(s);
}

// $D1:F47C
/* Screen command $DF (three operands: x, y, mask): jitter the channel's layer
 * by a random amount, x plus a random byte masked, and y likewise, as negative
 * offsets. The random picks start from the carry the caller left. */
static void jitter(Snes *s, bool carry)
{
    uint8_t mask = operand(s, 2);
    set_scratch(s, 4, mask);
    uint16_t a_high = s->cpu.a & 0xFF00, index;
    uint8_t r = random_byte(s, (uint16_t)(a_high | mask), carry, &index) & mask;
    uint16_t sum = (uint16_t)(r + operand(s, 0));
    uint8_t x = (uint8_t)sum;
    set_scratch(s, 0, x);
    r = random_byte(s, (uint16_t)(a_high | x), sum > 0xFF, &index) & mask;
    uint8_t y = (uint8_t)(r + operand(s, 1));
    set_scratch(s, 2, y);
    uint16_t channel = m_read16(s, RAM_BATTLE_SCREEN_CHANNEL);
    m_write16(s, RAM_BATTLE_BG3_SCROLL + channel, (uint16_t)-(uint16_t)x);
    m_write16(s, RAM_BATTLE_BG3_SCROLL + 2 + channel, (uint16_t)-(uint16_t)y);
    m_write16(s, RAM_BATTLE_SCREEN_SCRIPT, (uint16_t)(m_read16(s, RAM_BATTLE_SCREEN_SCRIPT) + 2));
}

void battle_screen_jitter(Snes *s)
{
    jitter(s, m_flag(s, CPU_FLAG_C));
    set_a(s, 0);
    m_rtl(s);
}

// $D1:F559
/* Screen command $CE: swing the channel's layer one frame. While the
 * direction flag is set the speed grows by the step and the position moves
 * down by it; otherwise the speed shrinks and the position moves up. The flag
 * flips instead when the speed would pass $FFFF or 0. The offset is the base
 * plus the position's whole part (minus it for the second channel). */
static void swing(Snes *s)
{
    uint16_t ch = m_read16(s, RAM_BATTLE_SCREEN_CHANNEL);
    uint32_t direction = RAM_BATTLE_SCREEN_MOTION_DIRECTION + ch;
    uint32_t speed = RAM_BATTLE_SCREEN_SWING_SPEED + ch, step = RAM_BATTLE_SCREEN_SWING_STEP + ch;
    uint32_t frac = RAM_BATTLE_SCREEN_SWING_POSITION + ch, whole = RAM_BATTLE_SCREEN_SWING_POSITION + 2 + ch;
    if (m_read8(s, direction)) {
        uint32_t v = (uint32_t)m_read16(s, speed) + m_read16(s, step);
        if (v > 0xFFFF)
            m_write16(s, direction, 0);
        else
            m_write16(s, speed, (uint16_t)v);
        uint16_t f = m_read16(s, frac), sp = m_read16(s, speed);
        m_write16(s, frac, (uint16_t)(f - sp));
        m_write16(s, whole, (uint16_t)(m_read16(s, whole) - (f < sp)));
    } else {
        uint16_t sp = m_read16(s, speed), st = m_read16(s, step);
        if (sp < st)
            m_write16(s, direction, (uint16_t)(m_read16(s, direction) + 1));
        else
            m_write16(s, speed, (uint16_t)(sp - st));
        uint32_t f = (uint32_t)m_read16(s, frac) + m_read16(s, speed);
        m_write16(s, frac, (uint16_t)f);
        m_write16(s, whole, (uint16_t)(m_read16(s, whole) + (f > 0xFFFF)));
    }
    uint16_t base = m_read16(s, RAM_BATTLE_SCREEN_MOTION_BASE + ch), pos = m_read16(s, whole);
    m_write16(s, RAM_BATTLE_BG3_SCROLL_OFFSET + 2 + ch, (uint16_t)(ch == 0x10 ? base - pos : base + pos));
}

void battle_screen_swing(Snes *s)
{
    swing(s);
    set_a(s, 0);
    m_rtl(s);
}

// $D1:F5CD
/* Screen command $DD: save the three animation positions in the slot the
 * operand gives (the slots interleave: a position's bytes are two apart). */
void battle_screen_save_positions(Snes *s)
{
    uint16_t slot = (uint16_t)((s->cpu.a & 0xFF00) | operand(s, 0));
    static const uint8_t order[6] = { 0, 2, 4, 1, 3, 5 };
    uint8_t last = 0;
    for (int i = 0; i < 6; i++) {
        last = m_read8(s, RAM_BATTLE_ANIMATION_POSITIONS + order[i]);
        m_write8(s, RAM_BATTLE_SAVED_POSITIONS + 2u * order[i] + slot, last);
    }
    m_set_a8(s, last);
    m_rtl(s);
}

/* ---- Shake and palette preset settings ---------------------------------------------- */

// $D1:F692
/* Stop the screen shake: both axes off, no layers. */
static void stop_shake(Snes *s)
{
    m_write8(s, RAM_BATTLE_SHAKE_ON, 0);
    m_write8(s, RAM_BATTLE_SHAKE_ON + RAM_BATTLE_SHAKE_STRIDE, 0);
    m_write8(s, RAM_BATTLE_SHAKE_LAYERS, 0);
}

/* One axis's shake from its two setting bytes: bits 2-4 the period, 5-7 the
 * rate, the second byte's high nibble the amplitude and low nibble its
 * target. */
static void set_shake_axis(Snes *s, int axis, uint16_t a_high, uint8_t timing, uint8_t levels)
{
    uint32_t stride = (uint32_t)axis * RAM_BATTLE_SHAKE_STRIDE;
    m_write8(s, RAM_BATTLE_SHAKE_PERIOD + stride, shake_period(s, (uint16_t)(a_high | ((timing & 0x1C) >> 2))));
    m_write8(s, RAM_BATTLE_SHAKE_ELAPSED + stride, 0);
    m_write8(s, RAM_BATTLE_SHAKE_RATE + stride, shake_rate(s, (uint16_t)(a_high | ((timing & 0xE0) >> 5))));
    m_write8(s, RAM_BATTLE_SHAKE_RATE_ELAPSED + stride, 0);
    m_write8(s, RAM_BATTLE_SHAKE_TARGET + stride, levels & 0x0F);
    m_write8(s, RAM_BATTLE_SHAKE_AMPLITUDE + stride, levels >> 4);
}

// $D1:F6B4
/* Start the screen shake from the settings in $54-$57: per axis (horizontal
 * from $54/$55, vertical from $56/$57) the timing, the amplitudes and, in the
 * low two bits of the first byte, whether it runs and which layers it
 * moves. Returns what the original leaves in A. */
static uint8_t start_shake(Snes *s, uint16_t a_high)
{
    stop_shake(s);
    uint8_t h = scratch(s, 0x0F), v = scratch(s, 0x11);
    set_shake_axis(s, 0, a_high, h, scratch(s, 0x10));
    set_shake_axis(s, 1, a_high, v, scratch(s, 0x12));
    uint8_t layers_x = shake_layers_x(s, (uint16_t)(a_high | (h & 0x03)));
    set_scratch(s, 0x16, layers_x);
    m_write8(s, RAM_BATTLE_SHAKE_LAYERS, (uint8_t)(shake_layers_y(s, (uint16_t)(a_high | (v & 0x03))) | layers_x));
    m_write8(s, RAM_BATTLE_SHAKE_ON, h & 0x03);
    m_write8(s, RAM_BATTLE_SHAKE_ON + RAM_BATTLE_SHAKE_STRIDE, v & 0x03);
    return v & 0x03;
}

/* The preset: two palette animation slots, which the frame work copies over
 * slots 8 and 9 on request (battle_palette_slot_presets). Their bytes are the
 * slots' fields (core/palette.c). */
enum { PRESET_SLOT_BYTES = RAM_PALETTE_ANIMATIONS_STRIDE, KIND = 0, STEP = 3, DELAY = 4, PERIOD = 5,
       FADE_MODE = 6, FADE_LEVEL = 7, FADE_TARGET = 8, PRESET_COPY_BYTES = 8 };

static uint32_t preset(int slot, uint8_t field) { return RAM_BATTLE_PALETTE_SLOT_PRESETS + (uint32_t)slot * PRESET_SLOT_BYTES + field; }

/* Mark the fade level and target of both slots with bit 7: the nonzero ones,
 * or the zero ones. Returns the last byte written. */
static uint8_t mark_fades(Snes *s, bool mark_zero)
{
    static const uint8_t bytes[4][2] = { { 0, FADE_LEVEL }, { 0, FADE_TARGET }, { 1, FADE_LEVEL }, { 1, FADE_TARGET } };
    uint8_t v = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t at = preset(bytes[i][0], bytes[i][1]);
        v = m_read8(s, at);
        if (mark_zero) {
            if (v == 0)
                v |= 0x80;
        } else if (v != 0) {
            v = 0x80;
        }
        m_write8(s, at, v);
    }
    return v;
}

// $D1:F74D
/* Set up the palette preset from the settings in $53/$54: both slots' fields
 * from the table by $53's high nibble; kind $40 (a fade), or $50 with $54 bit
 * 6; the fade mode from its bits 0-2, the delay, period and step from bits
 * 3-5; with bit 7 their fade level and target marked (the nonzero ones, or
 * with bit 6 the zero ones); with bit 6 those two swapped. Then request the
 * copy. Returns what the original leaves in A. */
static uint16_t set_palette_preset(Snes *s, uint16_t a_high)
{
    uint8_t select = scratch(s, 0x0E), bits = scratch(s, 0x0F);
    uint16_t table = (uint16_t)(a_high | (select & 0xF0));
    for (uint16_t i = 0; i < PRESET_COPY_BYTES; i++) {
        m_write8(s, preset(0, 1) + i, palette_preset_byte(s, (uint16_t)(table + i)));
        m_write8(s, preset(1, 1) + i, palette_preset_byte(s, (uint16_t)(table + 8 + i)));
    }
    /* A is 0 from here (TDC), so the other tables' indexes have no high byte */
    uint8_t kind = (bits & 0x40) ? 0x50 : 0x40;
    m_write8(s, preset(0, KIND), kind);
    m_write8(s, preset(1, KIND), kind);
    m_write8(s, preset(0, FADE_MODE), bits & 0x07);
    m_write8(s, preset(1, FADE_MODE), bits & 0x07);
    uint8_t range = (uint8_t)((bits & 0x38) >> 1);
    m_write8(s, preset(0, DELAY), palette_preset_range(s, range));
    m_write8(s, preset(0, PERIOD), palette_preset_range(s, (uint16_t)(range + 1)));
    m_write8(s, preset(1, DELAY), palette_preset_range(s, (uint16_t)(range + 2)));
    m_write8(s, preset(1, PERIOD), palette_preset_range(s, (uint16_t)(range + 3)));
    uint8_t step = (uint8_t)((bits & 0x38) >> 2);
    m_write8(s, preset(0, STEP), palette_preset_step(s, step));
    m_write8(s, preset(1, STEP), palette_preset_step(s, (uint16_t)(step + 1)));

    bool swap = bits & 0x40;
    uint8_t a = 0;
    if (bits & 0x80)
        a = mark_fades(s, swap);
    if (swap) {
        for (int slot = 0; slot < 2; slot++) {
            uint8_t level = m_read8(s, preset(slot, FADE_LEVEL));
            m_write8(s, preset(slot, FADE_LEVEL), m_read8(s, preset(slot, FADE_TARGET)));
            m_write8(s, preset(slot, FADE_TARGET), level);
            a = level;
        }
    } else if (!(bits & 0x80)) {
        a = 0;   /* what the bit 6 test left */
    }
    m_write8(s, RAM_BATTLE_PALETTE_PRESET_REQUEST, (uint8_t)(m_read8(s, RAM_BATTLE_PALETTE_PRESET_REQUEST) + 1));
    return a;
}

// $D1:F67A
/* Set a screen effect from the settings in $53-$57, by $53's high nibble:
 * 0-3 a palette preset, 4 the screen shake, 5 stop the shake. Operations past
 * the table jump where the original's table points, as a call-out. */
void battle_set_screen_effect(Snes *s)
{
    uint16_t a_high = s->cpu.a & 0xFF00;
    uint8_t index = (uint8_t)((scratch(s, 0x0E) & 0xF0) >> 3);
    uint16_t target = screen_dispatch_target(s, 0xF686, (uint16_t)(a_high | index));
    switch (target) {
    case 0xF74D:
        set_a(s, set_palette_preset(s, a_high));
        s->cpu.y = PRESET_COPY_BYTES;   /* where its copy loop leaves Y */
        break;
    case 0xF6B4: m_set_a8(s, start_shake(s, a_high)); break;
    case 0xF692:
        stop_shake(s);
        m_set_a8(s, index);
        break;
    default:
        /* the registers and flags as LSR : TAX leave them */
        m_set_a8(s, index);
        s->cpu.x = (uint16_t)(a_high | index);
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_nz8(s, index);
        m_jsr(s, 0xD1F682, 0xD10000 | target);
        break;
    }
    m_rtl(s);
}

/* The three operations as entries of their own, as $D1:F67A's JSR reaches
 * them. */
void battle_stop_shake(Snes *s)
{
    stop_shake(s);
    m_rts(s);
}

void battle_start_shake(Snes *s)
{
    m_set_a8(s, start_shake(s, s->cpu.a & 0xFF00));
    m_rts(s);
}

void battle_set_palette_preset(Snes *s)
{
    set_a(s, set_palette_preset(s, s->cpu.a & 0xFF00));
    s->cpu.y = PRESET_COPY_BYTES;
    m_rts(s);
}
