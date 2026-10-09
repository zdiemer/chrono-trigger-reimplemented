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

/* The menu frame's call to the choosing of targets, by why the targets are
 * chosen; the choosing's call to its kind's routine; and each kind's calls to
 * the click as the enemy the area goes on moves on, or back. */
static const uint32_t CHOOSE_SITE[] = {
    [TARGETS_FOR_ATTACK] = 0xC112B0, [TARGETS_FOR_TECH] = 0xC11379, [TARGETS_FOR_ITEM] = 0xC114C5,
    [TARGETS_GOING_ON] = 0xC11561,
};
enum { KIND_CALL = 0xC11FC9, BATTLE_JSR_RETURN = BATTLE_JSR_SIZE - 1 };

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

static void push_return_of(Snes *s, uint32_t site) { m_push16(s, (uint16_t)(site + BATTLE_JSR_RETURN)); }

static void click_on_machine(MenuHost *host, MenuClick click, TargetsWhy why, uint8_t kind)
{
    BattleMenuMachine *m = (BattleMenuMachine *)host;
    if (m->stopped)
        return;
    Snes *s = m->s;
    uint32_t site = CLICK_SITE[click];
    unsigned frames = 0;
    if (click == CLICK_AREA_ENEMY_ON || click == CLICK_AREA_ENEMY_BACK) {
        if (!m->inside_choosing) {
            push_return_of(s, CHOOSE_SITE[why]);
            frames++;
        }
        push_return_of(s, KIND_CALL);
        frames++;
        site = kind_click_site(kind, click == CLICK_AREA_ENEMY_ON);
    }
    if (!battle_run(s, site, CLICK, battle_click_cursor)) {
        m->stopped = true;
        return;
    }
    while (frames--)
        (void)m_pull16(s);
}

BattleMenuMachine battle_menu_machine(Snes *s, bool inside_choosing)
{
    return (BattleMenuMachine){ { click_on_machine }, s, inside_choosing, false };
}

/* ---- What the menu's routines leave in the carry -------------------------------------------------- */

/* The original's callers go on with the carry its routines leave, and the
 * code after the menu's frame reads it: worked out here from what the frame
 * did, the state before and after it. */

/* The kinds whose last test isn't a compare: the member itself, and the
 * fallen members to revive (target_choice.c's table). */
enum { KIND_SELF = 2, KIND_FALLEN = 3, LAST_KIND = TARGET_KINDS - 1 };

static uint8_t kind_number(uint8_t kind)
{
    uint8_t low = (uint8_t)(kind & ~TARGET_KIND_ALLY);
    return low < TARGET_KINDS ? low : LAST_KIND;
}

/* $C1:1F79 leaves the carry of its last compare: clear for the member itself,
 * and for a fallen member found, which ask none. */
static bool anyone_aimed_at(const GameState *g)
{
    for (unsigned place = 0; place < AREA_FOUND_PLACES; place++)
        if (!menu_nobody(battle_targets(g, place)))
            return true;
    return false;
}

static bool choosing_carry(const GameState *g)
{
    uint8_t kind = kind_number(battle_target_kind(g));
    return (kind == KIND_SELF || kind == KIND_FALLEN) ? !anyone_aimed_at(g) : true;
}

/* The tech rows' marking leaves the carry of comparing the twentieth entry
 * with the marks: set where it is one. */
static bool marking_carry(const GameState *g, uint8_t member)
{
    return battle_tech_list_ids(g, (uint8_t)(setup_member_tech_list_start(g, member) + TECHS_A_MEMBER - 1)) >= 0xFB;
}

/* What the frame started from. */
typedef struct {
    bool carry;
    uint8_t member, page, command, slot, tech_at, item_top, item_row, ally_page, selected_partners;
    bool targeting, stolen, stolen_held_before, stolen_into_first_place, message_free, remembers_commands;
    uint8_t stolen_held;
    uint8_t usable_item_flags, usable_item_held;
    bool tech_usable;
} MenuBefore;

enum { NO_TECHS_IN_THIS_FIGHT = 0x01, ITEM_NOT_FOR_BATTLE = 0x80, MOST_HELD = 99, TECH_DIMMED = 0x80, LAST_SLOT_ITEM_ROW = 2 };

static MenuBefore note_before(const Snes *s, const GameState *g)
{
    MenuBefore b = { 0 };
    b.carry = m_flag(s, CPU_FLAG_C);
    b.member = battle_menu_member(g);
    b.page = battle_menu_page(g);
    b.targeting = battle_targeting(g) != 0;
    b.slot = battle_menu_slot(g);
    b.ally_page = battle_ally_target_page(g);
    b.selected_partners = battle_selected_tech_partners(g);
    b.item_top = battle_item_top(g);
    b.item_row = battle_item_row(g);
    b.message_free = !battle_message_by_id(g) && !battle_message_by_id_high(g);
    b.remembers_commands = battle_remembers_commands(g) != 0;
    if (!menu_nobody(b.member)) {
        b.command = battle_command_cursor(g, b.member);
        b.tech_at = (uint8_t)(battle_tech_scroll(g, b.member) + battle_tech_cursor(g, b.member));
        b.tech_usable = battle_tech_list_dimmed(g) && !(battle_selected_tech_flags(g) & TECH_DIMMED);
    }
    uint8_t at = (uint8_t)(b.item_top + b.item_row);
    b.usable_item_flags = battle_item_list_flags(g, at);
    b.usable_item_held = battle_item_list_held(g, at);
    for (int m = BATTLE_PARTY_SIZE - 1; m >= 0 && !b.stolen; m--) {
        if (!battle_member_kept_item_waiting(g, (unsigned)m))
            continue;
        b.stolen = true;
        uint8_t item = battle_member_kept_item_id(g, (unsigned)m);
        for (unsigned r = 0; r < BATTLE_ITEM_LIST_COUNT && !b.stolen_held_before; r++)
            if (battle_item_list_item(g, r) == item) {
                b.stolen_held_before = true;
                b.stolen_held = battle_item_list_held(g, r);
            }
        b.stolen_into_first_place = !b.stolen_held_before && battle_item_list_item(g, 0) == 0;
    }
    return b;
}

/* The page an action aimed at the party goes back to, once the choosing has
 * turned the window over: kept from before, or the one it was chosen on. */
static uint8_t page_turned_over_from(const GameState *g, const MenuBefore *b)
{
    if (b->ally_page)
        return b->ally_page;
    return (battle_target_kind(g) & TARGET_KIND_ALLY) ? (uint8_t)(b->page + 1) : 0;
}

/* A stolen item taken in leaves the carry of its count against the most, or
 * of its search's last step: set where it went into the list's first place. */
static bool stolen_carry(const MenuBefore *b)
{
    if (b->stolen_held_before)
        return (uint8_t)(b->stolen_held + 1) >= MOST_HELD + 1;
    return b->stolen_into_first_place;
}

/* Called off: an action aimed at the party going back to a list's page puts
 * the item page's frames back with a copy that ends on a compare. */
static bool called_off_carry(uint8_t ally_page, bool carry)
{
    return ally_page && (uint8_t)(ally_page - 1) != PAGE_TECHS ? true : carry;
}

/* Confirmed: a tech's partners are tested, and the next member is looked
 * for among the slots, which leaves the carry clear past the first and set
 * where nobody is left. */
static bool confirmed_carry(const GameState *g, const MenuBefore *b, uint8_t ally_page, bool carry)
{
    uint8_t page = ally_page ? (uint8_t)(ally_page - 1) : b->page;
    if (page == PAGE_TECHS)
        carry = b->selected_partners == NO_PARTNERS || second_partner(b->selected_partners) == NO_PARTNER;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (menu_slot_has_member(g, slot))
            return slot == 0 ? carry : false;
    return true;
}

static bool targeting_carry(const GameState *g, const MenuBefore *b, Buttons pressed)
{
    bool carry = choosing_carry(g);
    uint8_t ally_page = page_turned_over_from(g, b);
    if (menu_nobody(battle_no_targets(g)) || (pressed & BUTTON_B))
        return called_off_carry(ally_page, carry);
    if (pressed & (BUTTON_A | BUTTON_X))
        return confirmed_carry(g, b, ally_page, carry);
    if (!(battle_targets_together(g) & TARGETS_TOGETHER) && (pressed & (BUTTON_DOWN | BUTTON_RIGHT)))
        return battle_target_choice(g) == 0;
    return carry;
}

static bool command_page_carry(const GameState *g, const MenuBefore *b, Buttons pressed)
{
    if (b->remembers_commands && (pressed & BUTTON_X))
        return choosing_carry(g);
    if (pressed & (BUTTON_LEFT | BUTTON_UP))
        return true;
    if (pressed & BUTTON_DOWN)
        return b->command + 1 >= 3;
    if (pressed & BUTTON_RIGHT)
        return battle_menu_slot(g) == 0;
    if (!(pressed & BUTTON_A))
        return true;
    switch (b->command) {
    case 0: return choosing_carry(g);
    case 1: return (battle_fight_flags(g) & NO_TECHS_IN_THIS_FIGHT) ? false : marking_carry(g, b->member);
    default: return !battle_fight_flags(g);
    }
}

static bool tech_page_carry(const GameState *g, const MenuBefore *b, Buttons pressed)
{
    uint8_t member = b->member;
    bool sum_carry = battle_tech_scroll(g, member) + battle_tech_cursor(g, member) > 0xFF;
    if (pressed & BUTTON_A)
        return b->tech_usable ? choosing_carry(g) : sum_carry;
    if (pressed & BUTTON_B)
        return true;
    bool moved = (uint8_t)(battle_tech_scroll(g, member) + battle_tech_cursor(g, member)) != b->tech_at;
    if (pressed & (BUTTON_UP | BUTTON_LEFT))
        return moved;
    if (pressed & (BUTTON_DOWN | BUTTON_RIGHT))
        return !moved;
    return sum_carry;
}

static bool item_page_carry(const GameState *g, const MenuBefore *b, Buttons pressed)
{
    if (pressed & BUTTON_A) {
        bool usable = !(b->usable_item_flags & ITEM_NOT_FOR_BATTLE) && b->usable_item_held;
        return usable ? choosing_carry(g) : b->item_top + b->item_row > 0xFF;
    }
    if (pressed & BUTTON_B)
        return true;
    if (pressed & (BUTTON_UP | BUTTON_LEFT))
        return b->item_row != 0;
    if (pressed & (BUTTON_DOWN | BUTTON_RIGHT))
        return b->item_row >= LAST_SLOT_ITEM_ROW;
    return true;
}

static bool menu_frame_carry(const GameState *g, const MenuBefore *b, Buttons pressed)
{
    if (menu_nobody(b->member))
        return b->stolen ? stolen_carry(b) : b->carry;
    if (b->targeting)
        return targeting_carry(g, b, pressed);
    switch (b->page) {
    case PAGE_COMMANDS: return command_page_carry(g, b, pressed);
    case PAGE_TECHS: return tech_page_carry(g, b, pressed);
    default: return item_page_carry(g, b, pressed);
    }
}

/* ---- The entries ---------------------------------------------------------------------------------- */

/* The menu's frame, from the buttons the menu gathered. A's high byte goes
 * back clear, as it came. */
void battle_take_menu_input(Snes *s)
{
    GameState *g = state_of(s);
    Buttons pressed = battle_menu_buttons(g);
    vars_peek(true);
    MenuBefore before = note_before(s, g);
    vars_peek(false);
    BattleMenuMachine m = battle_menu_machine(s, false);
    battle_menu_frame(g, pressed, &m.host);
    if (m.stopped)
        return;
    vars_peek(true);
    m_set_flag(s, CPU_FLAG_C, menu_frame_carry(g, &before, pressed));
    vars_peek(false);
    m_rts(s);
}

/* The choosing of targets for the original's callers. It leaves A's high
 * byte clear and the carry of its last compare. */
void battle_choose_targets_entry(Snes *s)
{
    GameState *g = state_of(s);
    BattleMenuMachine m = battle_menu_machine(s, true);
    battle_choose_targets(g, battle_menu_buttons(g), TARGETS_GOING_ON, &m.host);
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
