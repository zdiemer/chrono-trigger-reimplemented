#include "field/machine_forms.h"
#include "field/sounds.h"
#include "field/bridge.h"
#include "core/program.h"
#include "battle/machine_forms.h"
/* Checked battle handoffs and the legacy driver's calling conventions. */
#include "battle/frame.h"
#include "charge.h"
#include "battle/bridge.h"
#include "menu/machine_forms.h"

#include "battle/battle.h"
#include "battle/wait.h"
#include "battle/entries.h"
#include "core/machine_forms.h"
#include "platform/timing.h"
#include "platform/video.h"
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

bool battle_field_music_on_the_machine(Snes *s, uint32_t site, bool restore)
{
    uint32_t routine = restore ? FIELD_MUSIC_BACK_ENTRY : FIELD_BATTLE_SONG_ENTRY;
    m_push8(s, (uint8_t)(site >> 16));
    m_push16(s, (uint16_t)(site + BATTLE_JSL_SIZE - 1));
    m_jump_to(s, routine);
    /* Both former rows required native mode, M=8 and X=16, with no
     * decimal, page, bank or instruction-image restriction. */
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M) {
        program_breaks_a_contract(state_of(s), routine, "field music outside its contract");
        m_stop_at(s, routine, 0);
        return false;
    }
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    s->cpu.a = FIELD_PAGE;
    s->cpu.d = FIELD_PAGE;
    s->cpu.db = 0;
    if (restore) {
        TIMED_AS(state_of(s), routine, field_bring_the_music_back(state_of(s), m_platform(s)));
        /* The caller restores its own context after the original's crossed
         * pulls; preserve that continuation for the legacy driver. */
        s->cpu.db = m_pull8(s);
        s->cpu.d = m_pull16(s);
    } else {
        TIMED_AS(state_of(s), routine, field_start_the_battle_song(state_of(s), m_platform(s)));
        s->cpu.d = m_pull16(s);
        s->cpu.db = m_pull8(s);
    }
    m_rtl(s);
    return true;
}

void battle_refuse_long_call(Snes *s, uint32_t site, uint32_t routine)
{
    m_push8(s, (uint8_t)(site >> 16));
    m_push16(s, (uint16_t)(site + BATTLE_JSL_SIZE - 1));
    m_stop_at(s, routine, 0);
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
    if (m_runs_retired_as_c(s)) {
        m_push16(s, (uint16_t)(site + BATTLE_JSR_SIZE - 1));
        program_breaks_a_contract(state_of(s), routine, "a battle call without a C continuation");
        m_stop_at(s, routine, dont_care);
        return false;
    }
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    m_jsr_dc(s, site, routine, dont_care);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

bool battle_call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    if (m_runs_retired_as_c(s)) {
        m_push8(s, (uint8_t)(site >> 16));
        m_push16(s, (uint16_t)(site + BATTLE_JSL_SIZE - 1));
        program_breaks_a_contract(state_of(s), routine, "a battle call without a C continuation");
        m_stop_at(s, routine, dont_care);
        return false;
    }
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    m_jsl_dc(s, site, routine, dont_care);
    return battle_back(s, site + BATTLE_JSL_SIZE, page, bank);
}

/* The timing service's named battle waits, called with their original
 * frames and complete contracts in the legacy driver. */
static void (*frame_wait_entry(uint32_t routine))(Snes *)
{
    switch (routine) {
    case 0xC1007E: return battle_wait_for_frame_entry;
    case 0xCFFCFB: return battle_wait_for_a_last_frame_entry;
    case 0xCD3E7D: return battle_wait_for_the_handler_entry;
    case 0xCD3E75: return battle_wait_for_the_next_frame_entry;
    case 0xCD044A: return battle_wait_for_the_small_handler_entry;
    default: return NULL;
    }
}

bool battle_wait_frame(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    void (*entry)(Snes *) = frame_wait_entry(routine);
    if (m_runs_retired_as_c(s) && entry)
        return battle_run_called(s, site, routine, dont_care, entry);
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    tm_wait_frame(m_platform(s), PF_CALL_DC(site, routine, dont_care));
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

bool battle_wait_frames(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint8_t frames)
{
    if (m_runs_retired_as_c(s) && routine == 0xCD3E3B) {
        m_set_a8(s, frames);
        return battle_run_called(s, site, routine, dont_care, battle_wait_some_frames_entry);
    }
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    tm_wait_frames(m_platform(s), PF_CALL_DC(site, routine, dont_care), frames);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

bool battle_send_and_wait(Snes *s, uint32_t site, uint32_t from, uint16_t vram, uint16_t bytes)
{
    if (m_runs_retired_as_c(s)) {
        /* The video service's arguments, before the sender's full contract. */
        m_write16(s, (uint16_t)(m_direct_page(s) + 0x53), bytes);
        s->cpu.a = (uint8_t)(from >> 16);
        s->cpu.x = (uint16_t)from;
        s->cpu.y = vram;
        return battle_run_called(s, site, BATTLE_SEND, BATTLE_SEND_READS, battle_send_vram_and_wait);
    }
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    vid_send_vram_and_wait(m_platform(s), PF_CALL_DC(site, BATTLE_SEND, BATTLE_SEND_READS), vram, PF_GAME(from), bytes);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

/* For the original's code: its wait for a line keeps the line in the first
 * of the battle's scratch cells. */

bool battle_wait_line(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint8_t line)
{
    uint16_t page = m_direct_page(s);
    uint8_t bank = data_bank(s);
    tm_wait_line(m_platform(s), PF_CALL_DC(site, routine, dont_care), line);
    charge_machine_accesses(state_of(s), 1);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

void battle_stop_after_call(Snes *s, uint32_t site) { m_stop_at_own(s, site + BATTLE_JSR_SIZE, 0); }

/* What a sibling's call must come back as: on the page it was made on, or
 * (for one whose caller puts its page and bank right after) only the battle's
 * widths, at the call's return. */
typedef enum { BACK_ON_THE_PAGE, BACK_ON_ANY_PAGE } ComesBack;

static bool came_back_from(Snes *s, uint32_t returns_to, uint16_t page, ComesBack how)
{
    return battle_back(s, returns_to, how == BACK_ON_THE_PAGE ? page : m_direct_page(s), data_bank(s));
}

static bool run_entry(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                      bool long_call, ComesBack how, bool replaces_call_out)
{
    uint16_t page = m_direct_page(s), stack = s->cpu.s;
    uint32_t returns_to = site + (long_call ? BATTLE_JSL_SIZE : BATTLE_JSR_SIZE);
    if (long_call)
        m_push8(s, (uint8_t)(site >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    s->cpu.pb = (uint8_t)(routine >> 16);
    s->cpu.pc = (uint16_t)routine;
    if (replaces_call_out)
        charge_settle(state_of(s));
    /* Check the sibling at its constructed call frame, preserving the
     * original entry and frame if its contract is broken. */
    if (native_call_stop)
        native_call_stop(site, routine, dont_care);
    const NativeEntry *e = native_continuation_entry_at(s, routine);
    bool retired = (!e || e->fn != entry) && (core_machine_form_takes(s, entry) || menu_machine_form_takes(s, routine, entry) || battle_machine_form_takes(s, routine, entry) || field_machine_form_takes(s, entry));
    if ((!e || e->fn != entry) && !retired) {
        /* Keep the constructed call at its original entry on refusal. */
        m_stop_at(s, routine, dont_care);
        return false;
    }
    TIMED_AS(state_of(s), routine, entry(s));
    bool returned = s->cpu.s == stack && s->cpu.pb == (uint8_t)(returns_to >> 16) && s->cpu.pc == (uint16_t)returns_to;
    if (returned && !retired)
        native_poison_sibling_return(s, e);
    if (replaces_call_out && returned)
        charge_settle(state_of(s));
    return returned && came_back_from(s, returns_to, page, how);
}

/* A former stop keeps the dispatch boundary and the callee's full contract. */
void battle_continue(Snes *s, uint32_t routine, void (*entry)(Snes *), uint16_t dont_care)
{
    if (!m_runs_retired_as_c(s)) {
        m_stop_at(s, routine, dont_care);
        return;
    }
    m_jump_to(s, routine);
    native_sync_boundary(s);
    const NativeEntry *e = native_continuation_entry_at(s, routine);
    if ((!e || e->fn != entry) && !battle_machine_form_takes(s, routine, entry)) {
        m_stop_at(s, routine, dont_care);
        return;
    }
    NATIVE_TIME_AS_SIBLING(s, routine, entry(s));
}

void battle_push_the_return(Snes *s, uint32_t site) { m_push16(s, (uint16_t)(site + BATTLE_JSR_SIZE - 1)); }
void battle_pull_the_return(Snes *s) { m_pull16(s); }

bool battle_run(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, 0, entry, false, BACK_ON_THE_PAGE, false);
}

bool battle_run_long(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, 0, entry, true, BACK_ON_THE_PAGE, false);
}

bool battle_run_dc(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, false, BACK_ON_THE_PAGE, false);
}

bool battle_run_long_dc(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, true, BACK_ON_THE_PAGE, false);
}

bool battle_run_long_to_any_page(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, 0, entry, true, BACK_ON_ANY_PAGE, false);
}

/* The frame dispatcher used to yield to the machine at these calls. Its
 * work and its callee's keep their separate time boundaries, and refusal
 * leaves the pushed call at the original entry. */
bool battle_run_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, false, BACK_ON_THE_PAGE, true);
}

bool battle_run_long_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, true, BACK_ON_THE_PAGE, true);
}

bool battle_widths_m8_x16(const Snes *s) { return !s->cpu.e && m_m8(s) && !m_x8(s); }
bool battle_binary_m8_x16(const Snes *s) { return binary_m8_x16(s); }

/* ---- What the original keeps across a call or a wait ------------------------------------- */

void battle_keep(Snes *s, BattleFrame f)
{
    for (unsigned i = 0; i < f.kept_bytes; i++)
        m_push8(s, f.kept[i]);
    if (f.carry != BATTLE_CARRY_AS_IT_IS)
        m_set_flag(s, CPU_FLAG_C, f.carry == BATTLE_CARRY_SET);
}

/* What was kept is the original's to pull again: the port only moves past
 * it. */
void battle_stop_keeping(Snes *s, BattleFrame f) { s->cpu.s = (uint16_t)(s->cpu.s + f.kept_bytes); }

BattleFrame battle_keeping_the_registers(const Snes *s, BattleCarry carry)
{
    uint16_t x = s->cpu.x, y = s->cpu.y;
    return (BattleFrame){ .kept = { (uint8_t)s->cpu.a, (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(y >> 8), (uint8_t)y },
                          .kept_bytes = 5,
                          .carry = carry };
}

void battle_give_the_registers_back(Snes *s, BattleFrame f)
{
    m_set_a8(s, f.kept[0]);
    s->cpu.x = (uint16_t)(f.kept[1] << 8 | f.kept[2]);
    s->cpu.y = (uint16_t)(f.kept[3] << 8 | f.kept[4]);
}

bool battle_run_under(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *), BattleFrame f)
{
    battle_keep(s, f);
    if (!battle_run(s, site, routine, entry))
        return false;
    battle_stop_keeping(s, f);
    return true;
}

bool battle_run_dc_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                         BattleFrame f)
{
    battle_keep(s, f);
    if (!battle_run_dc(s, site, routine, dont_care, entry))
        return false;
    battle_stop_keeping(s, f);
    return true;
}

bool battle_run_long_dc_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                              BattleFrame f)
{
    battle_keep(s, f);
    if (!battle_run_long_dc(s, site, routine, dont_care, entry))
        return false;
    battle_stop_keeping(s, f);
    return true;
}

bool battle_wait_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, BattleFrame f)
{
    battle_keep(s, f);
    if (!battle_wait_frame(s, site, routine, dont_care))
        return false;
    battle_stop_keeping(s, f);
    return true;
}

bool battle_wait_frames_under(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, uint8_t frames,
                              BattleFrame f)
{
    battle_keep(s, f);
    if (!battle_wait_frames(s, site, routine, dont_care, frames))
        return false;
    battle_stop_keeping(s, f);
    return true;
}

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
