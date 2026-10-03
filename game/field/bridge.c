/* The guards the field's entries run under, and what the entry wrappers and
 * bridges share (field/bridge.h). */
#include "field/bridge.h"

#include "port/native.h"
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

static bool run_entry(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                      BridgeMachine m, bool long_call, bool hand_over)
{
    uint16_t stack = s->cpu.s;
    uint32_t returns_to = site + (long_call ? JSL_SIZE : JSR_SIZE);
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
        if (hand_over) {
            /* the call made, its return address pushed: the original goes on
             * from the sibling's entry */
            m_stop_at(s, routine, dont_care);
            return false;
        }
        s->cpu.s = stack;
        return long_call ? bridge_call_long(s, site, routine, dont_care, m) : bridge_call(s, site, routine, dont_care, m);
    }
    NATIVE_TIME_AS(s, routine, entry(s));
    bool returned = s->cpu.s == stack && s->cpu.pb == (uint8_t)(returns_to >> 16) && s->cpu.pc == (uint16_t)returns_to;
    return returned && bridge_back(s, returns_to, m);
}

bool field_go_on_with(Snes *s, uint32_t routine, void (*entry)(Snes *))
{
    m_jump_to(s, routine);
    const NativeEntry *e = native_hand_entry(s);
    if (!e || e->fn != entry)
        return false;
    NATIVE_TIME_AS(s, routine, entry(s));
    return true;
}

bool field_run(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *), BridgeMachine m)
{
    return run_entry(s, site, routine, dont_care, entry, m, false, false);
}

bool field_run_or_hand_over(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                            BridgeMachine m)
{
    return run_entry(s, site, routine, dont_care, entry, m, false, true);
}

bool field_run_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                    BridgeMachine m)
{
    return run_entry(s, site, routine, dont_care, entry, m, true, false);
}

bool field_run_holding(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                       BridgeMachine m, uint8_t held)
{
    m_push8(s, held);
    if (!field_run(s, site, routine, dont_care, entry, m))
        return false;
    (void)m_pull8(s);
    return true;
}

bool field_run_on_page(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                       uint16_t page)
{
    uint16_t own = s->cpu.d;
    m_push16(s, own);
    s->cpu.d = page;
    if (!field_run(s, site, routine, dont_care, entry, (BridgeMachine){ m_x8(s), page, s->cpu.db }))
        return false;
    s->cpu.d = m_pull16(s);
    return true;
}

/* No sibling at the routine: field_run_on_page's call made on the machine. */
bool bridge_call_on_page(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint16_t page)
{
    return field_run_on_page(s, site, routine, dont_care, NULL, page);
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

