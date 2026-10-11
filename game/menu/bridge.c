/* The call to the original's code the menus' wrappers and bridges share, and
 * the guards (menu/bridge.h). */
#include "menu/bridge.h"

#include "port/native.h"

/* The machine's state a call's check and the guards look at. */
static uint8_t data_bank(const Snes *s) { return s->cpu.db; }
static uint16_t stack_pointer(const Snes *s) { return s->cpu.s; }

MenuMachine menu_machine_now(const Snes *s)
{
    return (MenuMachine){ m_m8(s), m_x8(s), m_direct_page(s), data_bank(s) };
}

static bool came_back(const Snes *s, MenuMachine m)
{
    return !s->cpu.e && m_m8(s) == m.a8 && m_x8(s) == m.x8 && m_direct_page(s) == m.page && data_bank(s) == m.bank;
}

bool menu_back(Snes *s, uint32_t returns_to, MenuMachine m)
{
    if (came_back(s, m))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

bool menu_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    MenuMachine m = menu_machine_now(s);
    m_jsr_dc(s, site, routine, dont_care);
    return menu_back(s, site + MENU_JSR_SIZE, m);
}

bool menu_call_returning(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, bool a8, bool x8)
{
    MenuMachine m = menu_machine_now(s);
    m.a8 = a8;
    m.x8 = x8;
    m_jsr_dc(s, site, routine, dont_care);
    return menu_back(s, site + MENU_JSR_SIZE, m);
}

bool menu_call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    MenuMachine m = menu_machine_now(s);
    m_jsl_dc(s, site, routine, dont_care);
    return menu_back(s, site + MENU_JSL_SIZE, m);
}

/* Run the sibling as the original's call at `site` reaches it: a JSR, or a
 * JSL that pushes its bank too. */
typedef enum { BY_JSR, BY_JSL } CallKind;

static bool run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *), bool any_widths, CallKind kind)
{
    MenuMachine m = menu_machine_now(s);
    uint16_t stack = stack_pointer(s);
    uint32_t returns_to = site + (kind == BY_JSL ? MENU_JSL_SIZE : MENU_JSR_SIZE);
    if (kind == BY_JSL)
        m_push8(s, (uint8_t)(returns_to >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    s->cpu.pb = (uint8_t)(routine >> 16);
    s->cpu.pc = (uint16_t)routine;
    NATIVE_TIME_AS(s, routine, entry(s));
    bool returned = stack_pointer(s) == stack && s->cpu.pb == (uint8_t)(returns_to >> 16) && s->cpu.pc == (uint16_t)returns_to;
    if (any_widths) {
        m.a8 = m_m8(s);
        m.x8 = m_x8(s);
    }
    return returned && menu_back(s, returns_to, m);
}

bool menu_run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *)) { return run(s, site, routine, entry, false, BY_JSR); }

bool menu_run_long(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run(s, site, routine, entry, false, BY_JSL);
}

bool menu_run_any_widths(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run(s, site, routine, entry, true, BY_JSR);
}

void menu_set_widths(Snes *s, bool a8, bool x8)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_sep(s, (uint8_t)((a8 ? CPU_FLAG_M : 0) | (x8 ? CPU_FLAG_X : 0)));
}

void menu_save_p(Snes *s) { m_push8(s, s->cpu.p); }

void menu_restore_p(Snes *s)
{
    uint8_t p = m_pull8(s);
    m_rep(s, (uint8_t)~p);   /* the index registers made 8-bit lose their high bytes */
    m_sep(s, p);
}

void menu_leave_compare(Snes *s, uint8_t a, uint8_t b)
{
    uint8_t difference = (uint8_t)(a - b);
    m_set_flag(s, CPU_FLAG_N, (difference & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_Z, difference == 0);
    m_set_flag(s, CPU_FLAG_C, a >= b);
}

void menu_leave_bit_test(Snes *s, uint8_t a, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_N, (v & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_V, (v & 0x40) != 0);
    m_set_flag(s, CPU_FLAG_Z, (a & v) == 0);
}

void menu_leave_load(Snes *s, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_N, (v & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_Z, v == 0);
}

bool menu_on_page(Snes *s) { return m_direct_page(s) == MENU_PAGE; }
bool menu_on_page_work_ram(Snes *s) { return menu_on_page(s) && data_bank(s) == MENU_DATA_BANK; }
bool menu_on_page_low_ram(Snes *s) { return menu_on_page(s) && m_bank_reaches_low_ram(s); }

void menu_leave_a_high_byte_clear(Snes *s) { s->cpu.a = (uint16_t)(s->cpu.a & 0x00FF); }

