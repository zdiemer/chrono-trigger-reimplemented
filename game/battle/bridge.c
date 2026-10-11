/* The guards the battle's entries run under, and the call to the original's
 * code the wrappers and bridges share (battle/bridge.h). */
#include "battle/bridge.h"

#include "battle/battle.h"
#include "platform/timing.h"
#include "port/native.h"

/* The machine's state a guard or a call's check looks at. */
static uint8_t data_bank(const Snes *s) { return s->cpu.db; }
static bool a_high_clear(const Snes *s) { return (s->cpu.a & 0xFF00) == 0; }
static uint16_t index_x(const Snes *s) { return s->cpu.x; }
static uint16_t index_y(const Snes *s) { return s->cpu.y; }
static bool binary_m8_x16(const Snes *s) { return !s->cpu.e && m_m8(s) && !m_x8(s) && !m_flag(s, CPU_FLAG_D); }

static bool came_back(const Snes *s, uint16_t page, uint8_t bank)
{
    return binary_m8_x16(s) && m_direct_page(s) == page && data_bank(s) == bank;
}

bool battle_back(Snes *s, uint32_t returns_to, uint16_t page, uint8_t bank)
{
    if (came_back(s, page, bank))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

bool battle_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    m_jsr_dc(s, site, routine, dont_care);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

bool battle_call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    m_jsl_dc(s, site, routine, dont_care);
    return battle_back(s, site + BATTLE_JSL_SIZE, page, bank);
}

bool battle_wait_frame(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    tm_wait_frame(m_platform(s), PF_CALL_DC(site, routine, dont_care));
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

bool battle_wait_line(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint8_t line)
{
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    tm_wait_line(m_platform(s), PF_CALL_DC(site, routine, dont_care), line);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

void battle_stop_after_call(Snes *s, uint32_t site) { m_stop_at_own(s, site + BATTLE_JSR_SIZE, 0); }

static bool run_entry(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *), bool long_call)
{
    uint16_t page = m_direct_page(s), stack = s->cpu.s;
    uint32_t returns_to = site + (long_call ? BATTLE_JSL_SIZE : BATTLE_JSR_SIZE);
    if (long_call)
        m_push8(s, (uint8_t)(site >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    s->cpu.pb = (uint8_t)(routine >> 16);
    s->cpu.pc = (uint16_t)routine;
    /* the sibling runs as C only where the runtime would run it there, its
     * guard and preconditions holding; otherwise the call is a real one, and
     * the runtime runs whatever the registry has for the state it meets */
    const NativeEntry *e = native_hand_entry(s);
    if (!e || e->fn != entry) {
        s->cpu.s = stack;
        return long_call ? battle_call_long(s, site, routine, 0) : battle_call(s, site, routine, 0);
    }
    NATIVE_TIME_AS(s, routine, entry(s));
    bool returned = s->cpu.s == stack && s->cpu.pb == (uint8_t)(returns_to >> 16) && s->cpu.pc == (uint16_t)returns_to;
    return returned && battle_back(s, returns_to, page, data_bank(s));
}

bool battle_run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, entry, false);
}

bool battle_run_long(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, entry, true);
}

bool battle_widths_m8_x16(const Snes *s) { return !s->cpu.e && m_m8(s) && !m_x8(s); }

bool battle_names_combatant(uint16_t slot) { return slot < BATTLE_COMBATANTS; }
bool battle_names_stat_block(uint16_t offset)
{
    return offset % BATTLE_STAT_BLOCK_BYTES == 0 && battle_names_combatant(battle_slot_of_stat_block(offset));
}

bool battle_page(Snes *s) { return m_direct_page(s) == BATTLE_PAGE; }
bool battle_on_page(Snes *s) { return battle_page(s) && data_bank(s) == BATTLE_DATA_BANK; }
bool battle_on_page_a_high_clear(Snes *s) { return battle_on_page(s) && a_high_clear(s); }
bool battle_on_page_io_bank(Snes *s) { return battle_page(s) && m_bank_reaches_io(s); }
bool battle_data_bank_is_io(Snes *s) { return m_bank_reaches_io(s); }
bool battle_data_bank_is_io_or_work_ram(Snes *s) { return m_bank_reaches_io(s) || data_bank(s) == BATTLE_DATA_BANK; }

bool battle_sprite_page(Snes *s) { return m_direct_page(s) == BATTLE_SPRITE_PAGE && data_bank(s) == BATTLE_DATA_BANK; }
bool battle_sprite_page_ready(Snes *s) { return battle_sprite_page(s) && a_high_clear(s); }
bool battle_sprite_bank_ready(Snes *s) { return data_bank(s) == BATTLE_DATA_BANK && a_high_clear(s); }

bool battle_on_page_stat_block_x(Snes *s) { return battle_on_page(s) && battle_names_stat_block(index_x(s)); }
bool battle_on_page_combatant_xy(Snes *s)
{
    return battle_on_page_stat_block_x(s) && index_y(s) == battle_slot_of_stat_block(index_x(s));
}
