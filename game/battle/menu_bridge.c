/* Battle: the menu's clicks on the machine (battle/choosing.h, MenuHost).
 *
 * The menu's logic says when the cursor clicks and why; the click is a
 * command to the sound driver, which on the emulated machine is a call to the
 * original's driver ($C7:0004) from the original's click routine ($C1:1B55).
 * Shadow mode compares that call where the original makes it, so it is made
 * from the frames the original has there: the click routine's, called from
 * the menu's frame ($C1:1153) at the site of the move the click is for, and
 * for the cursor moved over the enemy an area is placed on, from inside the
 * choosing of targets ($C1:1F79) and its kind's routine as well. This file
 * keeps where those calls are in the ROM, and builds the frames.
 *
 * Where the driver doesn't come back as it was called (a state no game makes:
 * the call ran past its budget), the port has stopped at the click's return
 * and the original carries on from there; the logic's work after the click is
 * done all the same, and later clicks are skipped. */
#include "battle/menu_bridge.h"

#include "battle/program.h"
#include "battle/window.h"
#include "battle/setup_data.h"
#include "vars.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "platform/audio.h"
#include "port/machine.h"

/* ---- The click routine ($C1:1B55) --------------------------------------------------------------- */

/* The click: the driver's effect 0 on its second pair of effect voices, from
 * the middle. What the driver's command routine reads of the registers it is
 * called with, and what is read after it returns: nothing but A's high byte,
 * which it hands back (checked against liveness by tools/dontcare_check.py). */
enum { CURSOR_CLICK_EFFECT = 0x00, CLICK_CALL = 0xC11B62, CLICK_RETURN = 0xC11B66, CLICK = 0xC11B55 };
#define DRIVER_IGNORES (DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C)

/* The driver is called with A holding the pan the original loads last, and
 * hands back the registers as it found them. */
void battle_click_cursor(Snes *s)
{
    m_set_a8(s, AU_CENTRE);
    au_command(m_platform(s), PF_CALL_DC(CLICK_CALL, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = AU_START_OTHER_EFFECT, .p1 = CURSOR_CLICK_EFFECT, .p2 = AU_CENTRE, .count = 2 });
    if (battle_back(s, CLICK_RETURN, BATTLE_PAGE, BATTLE_DATA_BANK))
        m_rts(s);
}

/* ---- Where the menu clicks ------------------------------------------------------------------------ */

/* The menu frame's call to the click, by the move it is for. */
static const uint32_t CLICK_SITE[] = {
    [CLICK_COMMAND_UP] = 0xC111B7,     [CLICK_COMMAND_DOWN] = 0xC111C3,   [CLICK_COMMAND_CHOSEN] = 0xC111D8,
    [CLICK_MEMBER_BACK] = 0xC111E7,    [CLICK_MEMBER_ON] = 0xC1121E,      [CLICK_TECH_CHOSEN] = 0xC11334,
    [CLICK_TECH_BACK] = 0xC11340,      [CLICK_TECH_UP] = 0xC11350,        [CLICK_TECH_DOWN] = 0xC11360,
    [CLICK_ITEM_CHOSEN] = 0xC11443,    [CLICK_ITEM_BACK] = 0xC1144F,      [CLICK_ITEM_UP] = 0xC1145F,
    [CLICK_ITEM_DOWN] = 0xC1146F,      [CLICK_ITEM_PAGE_ON] = 0xC1147B,   [CLICK_ITEM_PAGE_BACK] = 0xC11487,
    [CLICK_TARGETS_CALLED_OFF] = 0xC11574, [CLICK_TARGET_CONFIRMED] = 0xC115EE, [CLICK_TARGET_NEXT] = 0xC11602,
    [CLICK_TARGET_PREVIOUS] = 0xC11611,
};

/* The menu frame's calls to the choosing of targets: for an attack, a tech
 * or an item just chosen, and for the choosing going on from an earlier
 * frame. Which one a frame makes is decided by where the menu stood as the
 * frame began (choosing_call_of). The choosing calls its kind's routine from
 * one place, and each kind that places an area on an enemy clicks from its
 * own two, as the enemy moves on or back. */
enum { CHOOSE_FOR_ATTACK = 0xC112B0, CHOOSE_FOR_TECH = 0xC11379, CHOOSE_FOR_ITEM = 0xC114C5,
       CHOOSE_GOING_ON = 0xC11561, NO_CHOOSING_CALL = 0, KIND_CALL = 0xC11FC9,
       BATTLE_JSR_RETURN = BATTLE_JSR_SIZE - 1 };

typedef struct {
    uint8_t kind;
    uint32_t on, back;
} KindClickSites;

static const KindClickSites KIND_CLICK_SITES[] = {
    { 0x0B, 0xC1217A, 0xC12188 }, { 0x0C, 0xC12214, 0xC12222 }, { 0x0D, 0xC121C0, 0xC121CE },
    { 0x0F, 0xC12301, 0xC1230F }, { 0x12, 0xC1226B, 0xC12279 }, { 0x1A, 0xC1226B, 0xC12279 },
};

static uint32_t kind_click_site(uint8_t kind, bool on)
{
    for (unsigned i = 0; i < sizeof KIND_CLICK_SITES / sizeof KIND_CLICK_SITES[0]; i++)
        if (KIND_CLICK_SITES[i].kind == kind)
            return on ? KIND_CLICK_SITES[i].on : KIND_CLICK_SITES[i].back;
    return 0;
}

static uint32_t choosing_call_of(const GameState *g)
{
    if (battle_targeting(g))
        return CHOOSE_GOING_ON;
    switch (battle_menu_page(g)) {
    case PAGE_COMMANDS: return CHOOSE_FOR_ATTACK;
    case PAGE_TECHS: return CHOOSE_FOR_TECH;
    default: return CHOOSE_FOR_ITEM;
    }
}

static void push_return_of(Snes *s, uint32_t site) { m_push16(s, (uint16_t)(site + BATTLE_JSR_RETURN)); }

static void click_on_machine(MenuHost *host, MenuClick click)
{
    BattleMenuMachine *m = (BattleMenuMachine *)host;
    if (m->stopped)
        return;
    Snes *s = m->s;
    uint32_t site = CLICK_SITE[click];
    unsigned frames = 0;
    if (click == CLICK_AREA_ENEMY_ON || click == CLICK_AREA_ENEMY_BACK) {
        if (m->choosing_call != NO_CHOOSING_CALL) {
            push_return_of(s, m->choosing_call);
            frames++;
        }
        push_return_of(s, KIND_CALL);
        frames++;
        site = kind_click_site(battle_target_kind_number(battle_target_kind(state_of(s))), click == CLICK_AREA_ENEMY_ON);
    }
    if (!battle_run(s, site, CLICK, battle_click_cursor)) {
        m->stopped = true;
        return;
    }
    while (frames--)
        (void)m_pull16(s);
}

/* The menu's frame calls the choosing from the place the menu stood at as it
 * began; the choosing's own entry is called by the original's code, whose
 * frame is there already. */
BattleMenuMachine battle_menu_machine(Snes *s, bool inside_choosing)
{
    uint32_t call = inside_choosing ? NO_CHOOSING_CALL : choosing_call_of(state_of(s));
    return (BattleMenuMachine){ { click_on_machine }, s, call, false };
}

/* ---- What the menu's parts leave in the carry ----------------------------------------------------- */

/* The choosing of targets, the rows' marking and the item list are reached
 * by the original's code only where the menu's frame is the original's (its
 * guard refused), which goes on with the carry each leaves: the choosing's
 * last compare, set but where the member itself or a fallen member is aimed
 * at, which ask none and leave it clear where they found someone; the
 * marking's, of the last entry against the marks; the list's, of its last
 * row's count. The menu's frame itself leaves none worth keeping (the
 * registry's dc-evidence). */
static bool choosing_carry(const GameState *g)
{
    TargetPick pick = battle_target_pick(battle_target_kind(g));
    return (pick == PICK_SELF || pick == PICK_FALLEN) ? !battle_anyone_aimed_at(g) : true;
}

static bool marking_carry(const GameState *g, uint8_t member)
{
    uint8_t last = (uint8_t)(setup_member_tech_list_start(g, member) + TECHS_A_MEMBER - 1);
    return !battle_tech_entry_is_a_tech(battle_tech_list_ids(g, last));
}

/* ---- The entries ---------------------------------------------------------------------------------- */

/* The menu's frame, from the buttons the menu gathered. A's high byte goes
 * back clear, as it came. */
void battle_take_menu_input(Snes *s)
{
    GameState *g = state_of(s);
    BattleMenuMachine m = battle_menu_machine(s, false);
    battle_menu_frame(g, battle_menu_buttons(g), &m.host);
    if (!m.stopped)
        m_rts(s);
}

/* The choosing of targets for the original's callers. It leaves A's high
 * byte clear and the carry of its last compare. */
void battle_choose_targets_entry(Snes *s)
{
    GameState *g = state_of(s);
    BattleMenuMachine m = battle_menu_machine(s, true);
    battle_choose_targets(g, battle_menu_buttons(g), &m.host);
    if (m.stopped)
        return;
    s->cpu.a &= 0x00FF;
    vars_peek(true);
    m_set_flag(s, CPU_FLAG_C, choosing_carry(g));
    vars_peek(false);
    m_rts(s);
}

/* The tech rows' marking for the original's callers: A's high byte clear,
 * the carry of the last entry's compare. */
void battle_mark_tech_rows_entry(Snes *s)
{
    GameState *g = state_of(s);
    battle_mark_tech_rows(g);
    s->cpu.a &= 0x00FF;
    vars_peek(true);
    m_set_flag(s, CPU_FLAG_C, marking_carry(g, battle_menu_member(g)));
    vars_peek(false);
    m_rts(s);
}

/* ---- As C ------------------------------------------------------------------------------------ */

/* The click as C: the driver's command, from the click routine's call. */
typedef struct {
    MenuHost host;
    Platform *pf;
} BattleMenuAsC;

static void click_the_menu(MenuHost *host, MenuClick click)
{
    (void)click;
    BattleMenuAsC *c = (BattleMenuAsC *)host;
    au_command(c->pf, PF_CALL_DC(CLICK_CALL, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = AU_START_OTHER_EFFECT, .p1 = CURSOR_CLICK_EFFECT, .p2 = AU_CENTRE, .count = 2 });
}

/* The guard's state half: the menu window's state and the menu's frame as
 * the game keeps them. */
bool battle_menu_input_taken(GameState *g, Platform *pf)
{
    if (!battle_menu_window_fits(g) || !battle_menu_frame_fits(g))
        return battle_needs_the_machine(g, 0xC11153, "a menu state the menu's frame doesn't take");
    BattleMenuAsC c = { { click_the_menu }, pf };
    battle_menu_frame(g, battle_menu_buttons(g), &c.host);
    return true;
}
