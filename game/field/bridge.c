/* The guards the field's entries run under, and what the entry wrappers and
 * bridges share (field/bridge.h). */
#include "field/bridge.h"

#include "vars.h"

bool field_machine_back(const Snes *s, bool x8, uint16_t d, uint8_t db)
{
    return !s->cpu.e && m_m8(s) && m_x8(s) == x8 && m_direct_page(s) == d && s->cpu.db == db;
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
