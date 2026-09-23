/* The guards the field's entries run under, and what the entry wrappers and
 * bridges share (field/bridge.h). */
#include "field/bridge.h"

#include "vars.h"

static bool came_back(const Snes *s, BridgeMachine m)
{
    return !s->cpu.e && m_m8(s) && m_x8(s) == m.x8 && m_direct_page(s) == m.page && s->cpu.db == m.bank;
}

bool bridge_back(Snes *s, uint32_t returns_to, BridgeMachine m)
{
    if (came_back(s, m))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

bool bridge_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, BridgeMachine m)
{
    m_jsr_dc(s, site, routine, dont_care);
    return bridge_back(s, site + JSR_SIZE, m);
}

bool bridge_call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, BridgeMachine m)
{
    m_jsl_dc(s, site, routine, dont_care);
    return bridge_back(s, site + JSL_SIZE, m);
}

unsigned field_current_object_slot(Snes *s)
{
    uint16_t object = field_current_object(state_of(s));
    return (unsigned)((m_x8(s) ? (uint8_t)object : object) >> 1);
}

bool field_bank_low_ram(Snes *s) { return m_bank_reaches_low_ram(s); }

bool field_on_page(Snes *s) { return m_direct_page(s) == FIELD_PAGE; }
bool field_on_page_io_bank(Snes *s) { return field_on_page(s) && m_bank_reaches_io(s); }
bool field_on_page_low_bank(Snes *s) { return field_on_page(s) && m_bank_reaches_low_ram(s); }
bool field_on_page_bank_zero(Snes *s) { return field_on_page(s) && m_bank_is_zero(s); }

bool field_object_x_low_bank(Snes *s) { return field_on_page_low_bank(s) && field_names_object(s->cpu.x); }

/* The current object, as the entry's index registers read it: its low byte
 * with 8-bit X, the word with 16-bit X. Looked at without the machine's time
 * (vars_peek). */
static bool current_object_is_one(Snes *s)
{
    vars_peek(true);
    uint16_t object = field_current_object(state_of(s));
    vars_peek(false);
    return field_names_object(m_x8(s) ? (uint8_t)object : object);
}

bool field_current_object_low_bank(Snes *s) { return field_on_page_low_bank(s) && current_object_is_one(s); }
bool field_current_object_bank_zero(Snes *s) { return field_on_page_bank_zero(s) && current_object_is_one(s); }
bool field_current_object_io_bank(Snes *s) { return field_on_page_io_bank(s) && current_object_is_one(s); }

bool field_on_layers_page(Snes *s) { return m_direct_page(s) == FIELD_LAYERS_PAGE; }
bool field_on_layers_page_low_bank(Snes *s) { return field_on_layers_page(s) && m_bank_reaches_low_ram(s); }
bool field_on_layers_page_io_bank(Snes *s) { return field_on_layers_page(s) && m_bank_reaches_io(s); }

