/* Battle: setting up the screen effects' tables at the start of a battle
 * (bank D1). */
#include "battle/screen.h"

#include "port/machine.h"
#include "ram.h"

enum {
    DATA_BYTES = 0x120,
    ENTRY_BYTES = 4,
    /* the four tables' places in battle_fade_line_data */
    FIRST_DOWN = 0x01, FIRST_UP = 0x61, SECOND_DOWN = 0xC1, SECOND_UP = 0xF1,
    REPEAT_ONE = 0x81,   /* a line count of 1 with the repeat bit */
    ALL_CHANNELS = 0xE0,
};

/* One pair of tables: `entries` entries of one line each, one falling from
 * `down` and one rising from `up` (the high bytes of 8.8 values stepping by
 * `step`), the second with the repeat bit. The values stay in the scratch
 * cells $53-$56. Returns the overflow of the last step up. */
static void fill_fades(Snes *s, uint16_t down_at, uint16_t up_at, int entries, uint16_t down, uint16_t up,
                       uint16_t step, bool *overflow)
{
    for (int i = 0; i < entries; i++) {
        uint16_t at = (uint16_t)(i * ENTRY_BYTES);
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + down_at + 1 + at, (uint8_t)(down >> 8));
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + up_at + 1 + at, (uint8_t)(up >> 8));
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + up_at + at, REPEAT_ONE);
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + down_at + at, 1);
        down = (uint16_t)(down - step);
        m_write16(s, RAM_BATTLE_SCRATCH + 0x0E, down);
        uint32_t sum = (uint32_t)up + step;
        *overflow = (~(up ^ step) & (up ^ sum) & 0x8000) != 0;
        up = (uint16_t)sum;
        m_write16(s, RAM_BATTLE_SCRATCH + 0x10, up);
    }
}

// $D1:F83D
/* Build the fade tables: two pairs of one-line entries whose values fall from
 * $E8 and rise from $E0 (all color channels, intensity 8 to 0 and 0 up), 24
 * entries a third of a step apart and then 12 two thirds apart, each pair
 * followed by a closing entry of $E0. */
static void build_fade_tables(Snes *s, bool *overflow)
{
    for (uint16_t i = 0; i < DATA_BYTES; i++)
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + i, 0);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x0F, 0xE8);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x11, ALL_CHANNELS);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x0E, 0xAB);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x10, 0x55);
    fill_fades(s, FIRST_DOWN, FIRST_UP, 24, 0xE8AB, 0xE055, 0x0055, overflow);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x0F, 0xE8);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x11, ALL_CHANNELS);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x0E, 0x56);
    m_write8(s, RAM_BATTLE_SCRATCH + 0x10, 0xAA);
    fill_fades(s, SECOND_DOWN, SECOND_UP, 12, 0xE856, 0xE0AA, 0x00AA, overflow);
    static const uint16_t closes[2] = { SECOND_DOWN - ENTRY_BYTES, SECOND_UP + 12 * ENTRY_BYTES - ENTRY_BYTES };
    for (int i = 0; i < 2; i++) {
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + closes[i] + 1, ALL_CHANNELS);
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + closes[i] - 1, 0);
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + closes[i], 0);
        m_write8(s, RAM_BATTLE_FADE_LINE_DATA + closes[i] + 2, 0);
    }
}

void battle_build_fade_tables(Snes *s)
{
    bool overflow = false;
    build_fade_tables(s, &overflow);
    /* the last loop's CPX leaves the carry set; the overflow is its last ADC's */
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_V, overflow);
    s->cpu.a = ALL_CHANNELS;
    s->cpu.p |= CPU_FLAG_M;
    m_rtl(s);
}
