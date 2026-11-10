#include "battle/machine_forms.h"
/* Checked calls between the menus and other C services. Call frames keep
 * held tasks saveable; each callee retains its complete machine contract. */
#include "menu/bridge.h"
#include "menu/machine_forms.h"

#include "charge.h"
#include "core/program.h"
#include "core/machine_forms.h"
#include "event/machine_forms.h"
#include "field/machine_forms.h"
#include "overworld/entries.h"
#include "scene/scene.h"
#include "menu/menu.h"
#include "port/native.h"

/* The machine's state a call's check and the guards look at. */
static uint8_t data_bank(const Snes *s) { return s->cpu.db; }
static uint16_t stack_pointer(const Snes *s) { return s->cpu.s; }

/* Retired services retain the same contracts at their checked C forms. */
static bool call_is_taken(Snes *s, uint32_t routine, void (*entry)(Snes *))
{
    const NativeEntry *e = native_continuation_entry_at(s, routine);
    return (e && e->fn == entry) || menu_machine_form_takes(s, routine, entry) || core_machine_form_takes(s, entry) ||
           event_machine_form_takes(s, entry) || field_machine_form_takes(s, entry) ||
           scene_machine_form_takes(s, entry) || battle_machine_form_takes(s, routine, entry) ||
           (entry && overworld_machine_form(s, routine) == entry);
}

MenuMachine menu_machine(const Snes *s)
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
    program_breaks_a_contract(state_of(s), returns_to, "a menu return outside its contract");
    m_stop_at_own(s, returns_to, 0);
    return false;
}

void menu_refuse_call(Snes *s, uint32_t returns_to, uint32_t routine, bool far)
{
    program_breaks_a_contract(state_of(s), routine, "a menu call without a named C form");
    if (far)
        m_push8(s, (uint8_t)(returns_to >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    m_stop_at(s, routine, 0);
}

bool menu_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    MenuMachine m = menu_machine(s);
    m_jsr_dc(s, site, routine, dont_care);
    return menu_back(s, site + MENU_JSR_SIZE, m);
}

bool menu_yield(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    MenuMachine m = menu_machine(s);
    if (!m_runs_retired_as_c(s)) {
        native_yield_call_dc(s, site, routine, false, dont_care, menu_task_goes_on_from);
        return menu_back(s, site + MENU_JSR_SIZE, m);
    }
    void (*entry)(Snes *) = routine == MENU_TASK_WAITS_A_FRAME ? menu_task_waits_a_frame :
                            routine == MENU_TASK_WAITS_FRAMES ? menu_task_waits_frames : NULL;
    uint16_t stack = stack_pointer(s);
    uint32_t returns_to = site + MENU_JSR_SIZE;
    m_push16(s, (uint16_t)(returns_to - 1));
    m_jump_to(s, routine);
    native_sync_boundary(s);
    if (native_call_stop)
        native_call_stop(site, routine, dont_care);
    if (!entry || !call_is_taken(s, routine, entry)) {
        m_stop_at(s, routine, dont_care);
        return false;
    }
    NATIVE_TIME_AS_SIBLING(s, routine, entry(s));
    native_park_after_task_switch(s, returns_to, stack, dont_care, menu_task_goes_on_from);
    return menu_back(s, returns_to, m);
}

bool menu_wait_a_frame_from(Snes *s, uint32_t site)
{
    return menu_yield(s, site, MENU_TASK_WAITS_A_FRAME, MENU_TASK_WAIT_READS_NOTHING);
}

/* Run the sibling as the original's call reaches it, its return to
 * `returns_to` pushed as the call pushes it: a JSR's or a PER's, or a JSL's,
 * which pushes its bank too. */
typedef enum { BY_JSR, BY_JSL } CallKind;

/* A sibling has returned only after unwinding its call frame to the caller. */
static bool returned_to_call(const Snes *s, uint16_t stack, uint32_t returns_to)
{
    return stack_pointer(s) == stack && s->cpu.pb == (uint8_t)(returns_to >> 16) && s->cpu.pc == (uint16_t)returns_to;
}

static bool run(Snes *s, uint32_t returns_to, uint32_t routine, void (*entry)(Snes *), bool any_widths, CallKind kind)
{
    MenuMachine m = menu_machine(s);
    uint16_t stack = stack_pointer(s);
    if (kind == BY_JSL)
        m_push8(s, (uint8_t)(returns_to >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    m_jump_to(s, routine);
    if (!call_is_taken(s, routine, entry)) {
        m_stop_at(s, routine, 0);
        return false;
    }
    TIMED_AS(state_of(s), routine, entry(s));
    bool returned = returned_to_call(s, stack, returns_to);
    if (any_widths) {
        m.a8 = m_m8(s);
        m.x8 = m_x8(s);
    }
    return returned && menu_back(s, returns_to, m);
}

/* A former call-out keeps the same call frame and time boundaries. Check
 * the full entry contract after pushing the frame; a refusal leaves the
 * original entry ready for the runtime instead of running an unchecked C call. */
static bool run_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care,
                       void (*entry)(Snes *), CallKind kind, bool settles)
{
    uint16_t stack = stack_pointer(s);
    uint32_t returns_to = site + (kind == BY_JSL ? MENU_JSL_SIZE : MENU_JSR_SIZE);
    if (kind == BY_JSL)
        m_push8(s, (uint8_t)(returns_to >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    m_jump_to(s, routine);
    if (settles)
        charge_settle(state_of(s));
    if (native_call_stop)
        native_call_stop(site, routine, dont_care);
    if (!call_is_taken(s, routine, entry)) {
        m_stop_at(s, routine, dont_care);
        return false;
    }
    /* A former call-out's stretches still belong to the callee. In
     * particular, loading a screen keeps the unpacker's error with the
     * loading service instead of charging it to the screen around it. */
    if (settles) {
        NATIVE_TIME_AS_SIBLING(s, routine, entry(s));
    } else {
        TIMED_AS(state_of(s), routine, entry(s));
    }
    bool returned = returned_to_call(s, stack, returns_to);
    if (returned && settles)
        charge_settle(state_of(s));
    return returned;
}

bool menu_run_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_called(s, site, routine, dont_care, entry, BY_JSR, true);
}

bool menu_run_long_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_called(s, site, routine, dont_care, entry, BY_JSL, true);
}

bool menu_run_checked(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_called(s, site, routine, dont_care, entry, BY_JSR, false);
}

bool menu_begin_inline_call(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    uint32_t before = native_uncounted_begin(s);
    m_push16(s, (uint16_t)(site + MENU_JSR_SIZE - 1));
    m_jump_to(s, routine);
    bool takes = call_is_taken(s, routine, entry);
    native_uncounted_end(s, before);
    if (!takes)
        m_stop_at(s, routine, 0);
    return takes;
}

void menu_end_inline_call(Snes *s, uint32_t site)
{
    uint32_t before = native_uncounted_begin(s);
    (void)m_pull16(s);
    m_jump_to(s, site + MENU_JSR_SIZE);
    native_uncounted_end(s, before);
}

bool menu_run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run(s, site + MENU_JSR_SIZE, routine, entry, false, BY_JSR);
}

bool menu_run_long(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run(s, site + MENU_JSL_SIZE, routine, entry, false, BY_JSL);
}

bool menu_run_any_widths(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run(s, site + MENU_JSR_SIZE, routine, entry, true, BY_JSR);
}

bool menu_run_long_any_widths(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run(s, site + MENU_JSL_SIZE, routine, entry, true, BY_JSL);
}

bool menu_run_jumped(Snes *s, uint32_t returns_to, uint32_t routine, void (*entry)(Snes *))
{
    return run(s, returns_to, routine, entry, true, BY_JSR);
}

bool menu_runs_as_port(Snes *s, uint32_t routine, void (*entry)(Snes *))
{
    return call_is_taken(s, routine, entry);
}

bool menu_run_or_call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    MenuMachine m = menu_machine(s);
    if (!menu_run_checked(s, site, routine, dont_care, entry))
        return false;
    m.a8 = m_m8(s);
    m.x8 = m_x8(s);
    return menu_back(s, site + MENU_JSR_SIZE, m);
}

void menu_go_on_in(Snes *s, uint32_t routine, void (*entry)(Snes *), uint16_t dont_care)
{
    m_jump_to(s, routine);
    if (!menu_runs_as_port(s, routine, entry)) {
        m_stop_at(s, routine, dont_care);
        return;
    }
    TIMED_AS(state_of(s), routine, entry(s));
}

/* A former stop keeps its dispatch boundary and callee's timing owner. */
void menu_continue(Snes *s, uint32_t routine, void (*entry)(Snes *), uint16_t dont_care)
{
    if (!m_runs_retired_as_c(s)) {
        m_stop_at(s, routine, dont_care);
        return;
    }
    m_jump_to(s, routine);
    native_sync_boundary(s);
    if (!call_is_taken(s, routine, entry)) {
        m_stop_at(s, routine, dont_care);
        return;
    }
    NATIVE_TIME_AS_SIBLING(s, routine, entry(s));
}

void menu_set_widths(Snes *s, bool a8, bool x8)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_sep(s, (uint8_t)((a8 ? CPU_FLAG_M : 0) | (x8 ? CPU_FLAG_X : 0)));
}

void menu_save_p(Snes *s)
{
    m_push8(s, s->cpu.p);
    native_flags_saved(s);
}

void menu_restore_p(Snes *s)
{
    uint8_t p = m_pull8(s);
    native_flags_restored(s);
    m_rep(s, (uint8_t)~p);   /* the index registers made 8-bit lose their high bytes */
    m_sep(s, p);
}

void menu_leave_load(Snes *s, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_N, (v & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_Z, v == 0);
}

bool menu_on_page(Snes *s) { return m_direct_page(s) == MENU_PAGE; }
bool menu_on_page_work_ram(Snes *s) { return menu_on_page(s) && data_bank(s) == MENU_DATA_BANK; }
bool menu_on_page_low_ram(Snes *s) { return menu_on_page(s) && m_bank_reaches_low_ram(s); }
