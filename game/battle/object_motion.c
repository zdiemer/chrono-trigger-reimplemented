/* Battle: per-frame object motion and effect bookkeeping (bank CF), run by
 * battle_frame_update while effects run: the objects' scripted paths, the
 * anchor points following their objects, the position trails the afterimages
 * are drawn from, the rising number sprites, and the party members' cycling
 * effect colors.
 *
 * The routines run with the battle page and data bank $7E, guarded on A's
 * high byte being 0 as their caller leaves it (their indexes take it along).
 * Each returns what the original leaves in A (and Y where it is live), since
 * A's high byte is live after them. */
#include "battle/entries.h"

#include "battle/sprite_data.h"
#include "port/machine.h"
#include "ram.h"

enum {
    COMBATANTS = 11,
    FIRST_OBJECT = 0x0C,
    ANCHORS = 8,
    ANCHOR_FOLLOWS_ANCHOR = 0x40,
    ANCHOR_OFF = 0x80,
    PATH_STEPS = 7,
    TRAIL_RECORDS = 4,
    TRAIL_RECORD_STRIDE = 0x16,
    NUMBER_SPRITES = 4,
    ROW_END = 0x80,
    PALETTE_CYCLE_END = 0x7C,
    NO_EFFECT_PALETTE = 0x08,
};

static uint8_t cell(Snes *s, uint8_t c) { return m_read8(s, RAM_BATTLE_EFFECT_CELLS + c); }
static void set_cell(Snes *s, uint8_t c, uint8_t v) { m_write8(s, RAM_BATTLE_EFFECT_CELLS + c, v); }
static void set_cell16(Snes *s, uint8_t c, uint16_t v) { m_write16(s, RAM_BATTLE_EFFECT_CELLS + c, v); }

static uint32_t object_x(uint16_t slot) { return RAM_BATTLE_OBJECT_X + FIRST_OBJECT + slot; }
static uint32_t object_y(uint16_t slot) { return RAM_BATTLE_OBJECT_Y + FIRST_OBJECT + slot; }

static void finish(Snes *s, uint16_t a)
{
    s->cpu.a = a;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

// $CF:EC78
/* Step each combatant along its scripted path: eight steps (the count runs
 * from 7 down, negative when done), each moving the object by the path's
 * deltas for the step. */
static uint8_t step_object_paths(Snes *s)
{
    uint8_t a = 0;
    for (uint16_t slot = 0; slot < COMBATANTS; slot++) {
        a = m_read8(s, RAM_BATTLE_PATH_ACTIVE + slot);
        if (!a)
            continue;
        uint32_t steps = RAM_BATTLE_PATH_STEPS + slot;
        a = m_read8(s, steps);
        if (a & 0x80)
            continue;
        uint16_t step = (uint8_t)(PATH_STEPS - a);
        set_cell16(s, 0, step);
        uint16_t at = (uint16_t)((uint16_t)(m_read8(s, RAM_BATTLE_PATH + slot) << 3) + step);
        m_write8(s, object_x(slot), (uint8_t)(battle_path_delta_x(s, at) + m_read8(s, object_x(slot))));
        m_write8(s, object_y(slot), (uint8_t)(battle_path_delta_y(s, at) + m_read8(s, object_y(slot))));
        a = (uint8_t)(m_read8(s, steps) - 1);
        m_write8(s, steps, a);
    }
    return a;
}

void battle_step_object_paths(Snes *s) { finish(s, step_object_paths(s)); }

// $CF:ECC2
/* Place the eight anchors, from 7 down: an anchor following an object (a
 * battle slot) takes its position, one following another anchor (bit 6, the
 * anchor in the low three bits) copies it, and one with bit 7 set stays. */
static uint8_t update_anchor_points(Snes *s)
{
    uint8_t a = 0;
    set_cell16(s, 0, ANCHORS - 1);
    do {
        uint8_t anchor = cell(s, 0);
        uint8_t source = m_read8(s, RAM_BATTLE_ANCHOR_SOURCES + anchor);
        a = source;
        uint16_t w = (uint8_t)(anchor << 1);
        if (!(source & ANCHOR_OFF)) {
            if (!(source & ANCHOR_FOLLOWS_ANCHOR)) {
                m_write8(s, RAM_BATTLE_ANCHOR_X + w, m_read8(s, object_x(source)));
                m_write8(s, RAM_BATTLE_ANCHOR_X + 1 + w, 0);
                a = m_read8(s, object_y(source));
                m_write8(s, RAM_BATTLE_ANCHOR_Y + w, a);
                m_write8(s, RAM_BATTLE_ANCHOR_Y + 1 + w, 0);
            } else {
                uint16_t from = (uint8_t)((source & 0x07) << 1);
                static const uint32_t bytes[4] = { RAM_BATTLE_ANCHOR_X, RAM_BATTLE_ANCHOR_X + 1, RAM_BATTLE_ANCHOR_Y,
                                                   RAM_BATTLE_ANCHOR_Y + 1 };
                for (int i = 0; i < 4; i++) {
                    a = m_read8(s, bytes[i] + from);
                    m_write8(s, bytes[i] + w, a);
                }
            }
        }
        set_cell(s, 0, (uint8_t)(cell(s, 0) - 1));
    } while (!(cell(s, 0) & 0x80));
    return a;
}

void battle_update_anchor_points(Snes *s) { finish(s, update_anchor_points(s)); }

/* One axis of a trail record: shift the four recorded positions on and
 * record the object's, plus its sprite offset, as a 16-bit value (low and
 * high bytes in separate arrays, one record every $16 bytes). */
static uint8_t record_axis(Snes *s, uint16_t slot, uint32_t low, uint32_t high, uint32_t offset, uint32_t position)
{
    for (int r = TRAIL_RECORDS - 1; r > 0; r--)
        m_write8(s, low + (uint32_t)r * TRAIL_RECORD_STRIDE + slot, m_read8(s, low + (uint32_t)(r - 1) * TRAIL_RECORD_STRIDE + slot));
    for (int r = TRAIL_RECORDS - 1; r > 0; r--)
        m_write8(s, high + (uint32_t)r * TRAIL_RECORD_STRIDE + slot, m_read8(s, high + (uint32_t)(r - 1) * TRAIL_RECORD_STRIDE + slot));
    uint8_t off = m_read8(s, offset + slot);
    uint8_t h = (off & 0x80) ? 0xFF : 0x00;
    uint16_t sum = (uint16_t)(off + m_read8(s, position + slot));
    m_write8(s, low + slot, (uint8_t)sum);
    h = (uint8_t)(h + (sum > 0xFF));
    m_write8(s, high + slot, h);
    return h;
}

// $CF:ED18
/* Every period frames, record the trailing combatants' positions. */
static uint8_t record_object_trails(Snes *s)
{
    uint8_t a = 0;
    for (int slot = COMBATANTS - 1; slot >= 0; slot--) {
        uint16_t i = (uint16_t)slot;
        a = m_read8(s, RAM_BATTLE_TRAIL_ON + i);
        if (!a)
            continue;
        uint32_t countdown = RAM_BATTLE_TRAIL_COUNTDOWN + i;
        uint8_t left = (uint8_t)(m_read8(s, countdown) - 1);
        m_write8(s, countdown, left);
        if (left)
            continue;
        m_write8(s, countdown, m_read8(s, RAM_BATTLE_TRAIL_PERIOD + i));
        record_axis(s, i, RAM_BATTLE_TRAIL_LOW_X, RAM_BATTLE_TRAIL_HIGH_X, RAM_BATTLE_SPRITE_OFFSET_X, RAM_BATTLE_OBJECT_X + FIRST_OBJECT);
        a = record_axis(s, i, RAM_BATTLE_TRAIL_LOW_Y, RAM_BATTLE_TRAIL_HIGH_Y, RAM_BATTLE_SPRITE_OFFSET_Y, RAM_BATTLE_OBJECT_Y + FIRST_OBJECT);
    }
    return a;
}

void battle_record_object_trails(Snes *s) { finish(s, record_object_trails(s)); }

// $CF:E781
/* While the numbers rise: each combatant's four number sprites move by the
 * next row's amount (its own table's for those with the second), until a
 * row starts with $80. */
static uint8_t step_prepared_sprites(Snes *s)
{
    uint8_t rising = m_read8(s, RAM_BATTLE_NUMBERS_RISING);
    if (!rising)
        return rising;
    uint16_t row = m_read8(s, RAM_BATTLE_NUMBERS_ROW);
    if (battle_number_rise_row(s, false, row) == ROW_END) {
        m_write8(s, RAM_BATTLE_NUMBERS_RISING, 0);
        return ROW_END;
    }
    for (uint16_t slot = 0; slot < COMBATANTS; slot++)
        m_write8(s, RAM_BATTLE_NUMBER_RISE + slot,
                 battle_number_rise_row(s, m_read8(s, RAM_BATTLE_NUMBER_RISE_ALT + slot) != 0, row));
    m_write8(s, RAM_BATTLE_NUMBERS_ROW, (uint8_t)(m_read8(s, RAM_BATTLE_NUMBERS_ROW) + 1));
    uint16_t at = 0;
    uint8_t a = 0;
    for (uint16_t slot = 0; slot < COMBATANTS; slot++) {
        set_cell(s, 0, NUMBER_SPRITES);
        do {
            uint32_t y = RAM_BATTLE_NUMBER_SPRITES_Y + at;
            a = (uint8_t)(m_read8(s, y) + m_read8(s, RAM_BATTLE_NUMBER_RISE + slot));
            m_write8(s, y, a);
            at = (uint16_t)(at + 4);
            set_cell(s, 0, (uint8_t)(cell(s, 0) - 1));
        } while (cell(s, 0) != 0);
    }
    return a;
}

void battle_step_prepared_sprites(Snes *s) { finish(s, step_prepared_sprites(s)); }

// $CF:E7D7
/* The party members' effect colors: for each member (2 down to 0) whose
 * cycle runs, unless the sprite palettes are flipped, put the cycle's next
 * color (by the member's pose, $CE:0100) in both of its sprite palette's
 * effect colors (1 and 12), unless it has no palette of its own (8); the
 * cycle steps two bytes and wraps at $7C. Returns A and leaves Y as the
 * original does. */
static uint8_t cycle_party_palettes(Snes *s, uint16_t *y)
{
    uint8_t a = 0;
    set_cell16(s, 0, 2);
    do {
        uint8_t member = cell(s, 0);
        *y = member;
        a = m_read8(s, RAM_BATTLE_PARTY_PALETTE_CYCLE_ON + member);
        if (a) {
            a = m_read8(s, RAM_BATTLE_SPRITE_PALETTES_FLIPPED);
            if (!a) {
                uint16_t at = (uint16_t)(m_read8(s, RAM_BATTLE_PARTY_PALETTE_CYCLE + member)
                                         + battle_party_cycle_start(s, (uint8_t)(m_read8(s, RAM_BATTLE_OBJECT_POSE + member) << 1)));
                a = m_read8(s, RAM_BATTLE_OBJECT_PALETTE + member);
                if (a != NO_EFFECT_PALETTE) {
                    uint16_t color = (uint8_t)(a << 4);
                    *y = color;
                    for (int i = 0; i < 2; i++) {
                        a = battle_party_cycle_byte(s, at, (uint8_t)i);
                        m_write8(s, RAM_PALETTE_WORKING + 0x102 + color + (uint32_t)i, a);
                        m_write8(s, RAM_PALETTE_WORKING + 0x118 + color + (uint32_t)i, a);
                    }
                    uint32_t pos = RAM_BATTLE_PARTY_PALETTE_CYCLE + m_read16(s, RAM_BATTLE_EFFECT_CELLS);
                    a = (uint8_t)(m_read8(s, pos) + 2);
                    m_write8(s, pos, a);
                    if (a == PALETTE_CYCLE_END)
                        m_write8(s, pos, 0);
                    *y = (uint8_t)*y;
                }
            }
        }
        set_cell(s, 0, (uint8_t)(cell(s, 0) - 1));
    } while (!(cell(s, 0) & 0x80));
    return a;
}

void battle_cycle_party_palettes(Snes *s)
{
    uint16_t y = 0;
    uint8_t a = cycle_party_palettes(s, &y);
    s->cpu.y = y;
    finish(s, a);
}
