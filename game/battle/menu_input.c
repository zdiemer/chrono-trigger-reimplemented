/* Battle: the menu taking the player's buttons (battle/choosing.h).
 *
 * Each frame of the fight, while a party member's menu is open, the menu
 * takes the buttons gathered since it last looked (battle/buttons.c) and does
 * what they ask on the page that is open:
 *
 *  - the command page: up and down move the cursor among Attack, Tech and
 *    Item, left and right go to another member waiting to choose, A chooses;
 *    with the remembered commands set, X attacks at once;
 *  - the tech page and the item page: the directions move the cursor in the
 *    list, scrolling it, L and R turn the item list a page, A chooses, B goes
 *    back to the commands;
 *  - choosing a target (target_choice.c lists whom it can be): the directions
 *    move among them, A or X confirms, B calls the choosing off.
 *
 * Choosing Attack, or a tech or an item that can be used, starts choosing its
 * target; confirming the target queues the member's action and the menu goes
 * to the next member waiting. Each move of a cursor clicks, which the menu's
 * host plays (battle/menu_bridge.c on the machine).
 *
 * Before the buttons, an item a member has stolen joins the item list, and
 * while no target is being chosen a frame of four sprites surrounds the
 * member choosing (the menu's cursors, which battle_place_menu_cursors puts
 * elsewhere when there is something to point at). */
#include "battle/choosing.h"

#include "battle/arith.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/menu_bridge.h"
#include "battle/window_data.h"
#include "charge.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"

/* ---- Around the menu --------------------------------------------------------------------------- */

/* The frame's four corners are the first four sprites, all large. */
enum { FRAME_CORNERS = 4, CORNERS_HIGH_BYTE = 0, ALL_CORNERS_LARGE = 0xAA, CORNER_PALETTE_BITS = 0x0E };

// $C1:1115
void battle_frame_member_choosing(GameState *g)
{
    uint8_t member = battle_menu_member(g);
    for (unsigned c = 0; c < FRAME_CORNERS; c++) {
        ChoosingFrameCorner corner = battle_choosing_frame_corner(g, c);
        set_oam_buffer_x(g, c, (uint8_t)(combatant_x(g, member) + corner.dx));
        set_oam_buffer_y(g, c, (uint8_t)(combatant_y(g, member) + corner.dy));
        set_oam_buffer_tile(g, c, corner.tile);
        set_oam_buffer_attributes(g, c,
                                  (uint8_t)((corner.attributes & ~CORNER_PALETTE_BITS) | battle_cursor_priority(g)));
    }
    set_oam_high_table_byte(g, CORNERS_HIGH_BYTE, ALL_CORNERS_LARGE);
}

// $CF:FD9E
void battle_note_menu_stat_block(GameState *g, uint8_t member)
{
    set_battle_current_stat_block(g, battle_stat_block_place(g, member));
}

/* A stolen item goes on the list where the list has it already (one more of
 * it, up to 99), else in its first empty place. The member's last waiting
 * item goes first. */
enum { ITEM_RECORDS = BATTLE_ITEM_LIST_COUNT, MOST_HELD = 99, NO_ITEM = 0, KEPT_ITEM_STRIDE = 8 };

static int item_place(const GameState *g, uint8_t item)
{
    for (unsigned record = 0; record < ITEM_RECORDS; record++)
        if (battle_item_list_item(g, record) == item)
            return (int)record;
    return -1;
}

static int empty_place(const GameState *g) { return item_place(g, NO_ITEM); }

/* The cells the original takes the item in with: the member's slot, its
 * kept record's place (eight bytes a member), the item, and the bytes left
 * to copy where it copied the record. */
enum { STOLEN_SLOT_CELL = 0x80, BYTES_LEFT_CELL = 0x82, STOLEN_ITEM_CELL = 0x84, KEPT_AT_CELL = 0x86 };

static void leave_stolen_cells(GameState *g, unsigned member, uint8_t item, bool copied)
{
    battle_leave_work16(g, STOLEN_SLOT_CELL, (uint16_t)member);
    battle_leave_work16(g, KEPT_AT_CELL, (uint16_t)(member * KEPT_ITEM_STRIDE));
    battle_leave_work8(g, STOLEN_ITEM_CELL, item);
    if (copied)
        battle_leave_work8(g, BYTES_LEFT_CELL, 0);
}

static void forget_kept_item(GameState *g, unsigned member)
{
    set_battle_member_kept_item_waiting(g, member, 0);
    set_battle_member_kept_item_id(g, member, 0);
    set_battle_member_kept_item_detail(g, member, 0);
    set_battle_member_kept_item_kind(g, member, 0);
    set_battle_member_kept_item_held(g, member, 0);
    set_battle_member_kept_item_spare(g, member, 0);
}

// $CF:FAE2
void battle_list_stolen_item(GameState *g)
{
    int member = BATTLE_PARTY_SIZE - 1;
    while (member >= 0 && !battle_member_kept_item_waiting(g, (unsigned)member))
        member--;
    if (member < 0)
        return;
    uint8_t item = battle_member_kept_item_id(g, (unsigned)member);
    int place = item_place(g, item);
    bool copied = place < 0;
    if (!copied) {
        /* one more, counted in a byte as the original counts it */
        uint8_t more = (uint8_t)(battle_item_list_held(g, (unsigned)place) + 1);
        if (more <= MOST_HELD)
            set_battle_item_list_held(g, (unsigned)place, more);
    } else {
        place = empty_place(g);
        set_battle_item_list_item(g, (unsigned)place, item);
        set_battle_item_list_menu_detail(g, (unsigned)place, battle_member_kept_item_detail(g, (unsigned)member));
        set_battle_item_list_flags(g, (unsigned)place, battle_member_kept_item_kind(g, (unsigned)member));
        set_battle_item_list_held(g, (unsigned)place, battle_member_kept_item_held(g, (unsigned)member));
        set_battle_item_list_spare(g, (unsigned)place, battle_member_kept_item_spare(g, (unsigned)member));
    }
    ORIGINAL_EFFECT(leave_stolen_cells(g, (unsigned)member, item, copied));
    forget_kept_item(g, (unsigned)member);
    if (!menu_nobody(battle_menu_member(g)) && battle_menu_page(g) == PAGE_ITEMS)
        set_battle_item_list_changed(g, (uint8_t)(battle_item_list_changed(g) + 1));
}

/* The message line forgets the enemy it last named, so that choosing one
 * names it again; not while a message opened by its number is up. The
 * original's message call notes which kind of message it was asked for in a
 * cell. */
enum { NO_MESSAGE = 0xFF, NAME_WITH_HP = 0x0B };

static void forget_target_named(GameState *g)
{
    if (battle_message_by_id(g) || battle_message_by_id_high(g))
        return;
    ORIGINAL_EFFECT(set_battle_message_window_kind(g, NAME_WITH_HP));
    set_battle_message_asked(g, NO_MESSAGE);
}

/* ---- The menu's state ------------------------------------------------------------------------ */

/* The frame's buttons, and the host that clicks. */
typedef struct {
    Buttons pressed;
    MenuHost *host;
} Frame;

static bool pressed(const Frame *f, Buttons b) { return (f->pressed & b) != 0; }
static void click(const Frame *f, MenuClick c) { f->host->click(f->host, c); }

static bool choose_targets(GameState *g, const Frame *f)
{
    bool nobody;
    TIMED_AS(g, 0xC11F79, nobody = battle_choose_targets(g, f->pressed, f->host));
    return nobody;
}

static uint8_t member_choosing(const GameState *g) { return battle_menu_member(g); }

static void redraw_member_cells(GameState *g, uint8_t times)
{
    set_battle_member_cells_redraw(g, (uint8_t)(battle_member_cells_redraw(g) + times));
}

/* The battle's timers wait while a list is open, as the settings say. */
static void hold_timers_for_lists(GameState *g) { set_battle_timers_held(g, battle_lists_hold_timers(g)); }

/* The commands: Attack, Tech, Item. */
typedef enum { COMMAND_ATTACK, COMMAND_TECH, COMMAND_ITEM } Command;
enum { COMMANDS = 3 };

/* The fight's flags: no techs at all where the first is set, and no items
 * where any is. */
enum { NO_TECHS_IN_THIS_FIGHT = 0x01 };

/* ---- Choosing targets --------------------------------------------------------------------------- */

static void start_choosing_target(GameState *g, const Frame *f)
{
    if (choose_targets(g, f)) {
        set_battle_target_choice(g, 0);
        return;
    }
    set_battle_targets_changed(g, (uint8_t)(battle_targets_changed(g) + 1));
    set_battle_targeting(g, (uint8_t)(battle_targeting(g) + 1));
}

/* The player's cursor to another target, the first of them the target. */
static void move_to_target(GameState *g, const Frame *f, bool forward)
{
    set_battle_targets_changed(g, (uint8_t)(battle_targets_changed(g) + 1));
    click(f, forward ? CLICK_TARGET_NEXT : CLICK_TARGET_PREVIOUS);
    if (forward)
        battle_next_target(g);
    else
        battle_previous_target(g);
    set_battle_targets(g, 0, battle_area_found(g, battle_target_choice(g)));
}

/* The window the list's page had, back after an action aimed at the party
 * turned it over: the item page's frames (or the command page's, which the
 * original puts the same way), or the tech page's, reloaded. */
static void restore_list_frames(GameState *g, MenuPage page)
{
    if (page == PAGE_TECHS) {
        ORIGINAL_EFFECT(set_battle_tech_page_open_mark(g, 0));
        set_battle_frames_reload_request(g, (uint8_t)(battle_frames_reload_request(g) + 1));
        return;
    }
    for (unsigned cell = 0; cell < BATTLE_WINDOW_FRAMES_TILEMAP_COUNT; cell++) {
        set_battle_window_frames_tilemap_tile(g, cell, battle_item_page_frames_tile(g, cell));
        set_battle_window_frames_tilemap_attributes(g, cell, battle_item_page_frames_attributes(g, cell));
    }
    charge_block_move(g, 2 * BATTLE_WINDOW_FRAMES_TILEMAP_COUNT);
    set_battle_item_arrows_shown(g, MENU_NONE);
    set_battle_frames_upload_request(g, (uint8_t)(battle_frames_upload_request(g) + 1));
}

/* Back from an action aimed at the party to the page it was chosen on. */
static MenuPage back_from_the_party(GameState *g)
{
    MenuPage page = page_kept_for_targets(g);
    set_battle_menu_page(g, (uint8_t)page);
    forget_page_kept_for_targets(g);
    return page;
}

static void stop_targeting(GameState *g)
{
    set_battle_target_choice(g, 0);
    ORIGINAL_EFFECT(set_battle_target_choice_spare(g, 0));
    set_battle_targeting(g, (uint8_t)(battle_targeting(g) - 1));
}

// $C1:1577
/* Called off: back to the page the action was chosen on, the window it had
 * put back, the cursors taken away. */
static void call_off_targeting(GameState *g)
{
    stop_targeting(g);
    if (party_shown_for_targets(g)) {
        MenuPage page = back_from_the_party(g);
        set_battle_ally_target_window(g, 0);
        restore_list_frames(g, page);
    }
    if (battle_menu_page(g) == PAGE_COMMANDS)
        redraw_member_cells(g, 1);
    battle_move_cursors_away(g);
}

/* The cells the original queues the action from: the choice (where a
 * partner taken was noted first), the partners, and what it is. */
enum { CHOICE_CELL = 0x80, PARTNERS_CELL = 0x82, WHAT_CELL = 0x84 };

/* A partner in the member's combination tech is spoken for: its menu slot
 * empties and its own choosing is dropped. */
static void take_partner(GameState *g, uint8_t partner)
{
    if (menu_slot_has_member(g, partner))
        set_battle_menu_member_count(g, (uint8_t)(battle_menu_member_count(g) - 1));
    set_battle_menu_slot_members(g, partner, MENU_NONE);
    set_battle_member_actions_state(g, partner, battle_member_actions_state(g, partner) & (uint8_t)~MEMBER_ACTION_CHOSEN);
    ORIGINAL_EFFECT(battle_leave_work8(g, CHOICE_CELL, partner));
}

/* What the member chose, as its action record keeps it. */
typedef struct {
    uint8_t choice, partners, what;
    bool what_kept;   /* an attack names nothing: the record keeps what the cell held */
} Chosen;

static Chosen chose_attack(void) { return (Chosen){ CHOSE_AN_ATTACK, NO_PARTNERS, 0, false }; }

static Chosen chose_tech(GameState *g, uint8_t member)
{
    if (battle_remembers_lists(g)) {
        set_battle_remembered_tech_scroll(g, member, battle_tech_scroll(g, member));
        set_battle_remembered_tech_row(g, member, battle_tech_cursor(g, member));
        set_battle_remembered_tech(g, member, battle_selected_tech_id(g));
    }
    uint8_t partners = battle_selected_tech_partners(g);
    if (partners != NO_PARTNERS) {
        take_partner(g, first_partner(partners));
        if (second_partner(partners) != NO_PARTNER)
            take_partner(g, second_partner(partners));
    }
    return (Chosen){ CHOSE_A_TECH, partners, battle_selected_tech_id(g), true };
}

static Chosen chose_item(GameState *g)
{
    unsigned record = battle_item_chosen_place(g) / ITEM_LIST_RECORD_BYTES;
    set_battle_item_list_held(g, record, (uint8_t)(battle_item_list_held(g, record) - 1));
    set_battle_item_arrows_shown(g, MENU_NONE);
    if (!battle_remembers_lists(g)) {
        set_battle_item_row(g, 0);
        set_battle_item_top(g, 0);
    }
    return (Chosen){ CHOSE_AN_ITEM, NO_PARTNERS, battle_item_chosen(g), true };
}

/* The command cursors are remembered, unless the attack came from the
 * shortcut; without the setting to remember them, they all go back to
 * Attack. */
static void remember_command(GameState *g, uint8_t member)
{
    if (battle_attack_by_shortcut(g))
        return;
    set_battle_remembered_command(g, member, battle_command_cursor(g, member));
    if (battle_remembers_commands(g))
        return;
    for (unsigned m = 0; m < BATTLE_PARTY_SIZE; m++) {
        set_battle_command_cursor(g, m, COMMAND_ATTACK);
        set_battle_remembered_command(g, m, COMMAND_ATTACK);
    }
}

/* The action's choosing cells the original queues it from. */
static void leave_chosen_cells(GameState *g, const Chosen *c)
{
    battle_leave_work8(g, CHOICE_CELL, c->choice);
    battle_leave_work8(g, PARTNERS_CELL, c->partners);
    if (c->what_kept)
        battle_leave_work8(g, WHAT_CELL, c->what);
}

/* The member's action queued; its menu closes and the menu goes to the first
 * slot with a member still to choose. */
static void queue_action(GameState *g, uint8_t member, const Chosen *c)
{
    remember_command(g, member);
    set_battle_attack_by_shortcut(g, 0);
    ORIGINAL_EFFECT(set_battle_target_choice_spare(g, 0));
    set_battle_target_choice(g, 0);
    set_battle_ally_target_window(g, 0);
    uint8_t queued = battle_action_queue_length(g);
    set_battle_action_queue(g, queued, member);
    set_battle_action_queue_length(g, (uint8_t)(queued + 1));
    set_battle_member_actions_state(g, member,
                                    (uint8_t)((battle_member_actions_state(g, member) & ~MEMBER_ACTION_CHOSEN)
                                              | MEMBER_ACTION_WAITING));
    set_battle_member_actions_choice_flags(g, member, (uint8_t)(c->choice | battle_member_choice_extra(g, member)));
    set_battle_member_actions_command(g, member, battle_menu_command(g));
    set_battle_member_actions_target(g, member, battle_targets(g, 0));
    set_battle_member_actions_partners(g, member, c->partners);
    if (c->what_kept)
        set_battle_member_actions_what(g, member, c->what);
    else
        ORIGINAL_EFFECT(set_battle_member_actions_what(g, member, (uint8_t)battle_work16(g, WHAT_CELL)));
    ORIGINAL_EFFECT(leave_chosen_cells(g, c));

    set_battle_menu_page(g, PAGE_COMMANDS);
    set_battle_menu_slot_shown(g, NO_SLOT_SHOWN);
    set_battle_menu_member_count(g, (uint8_t)(battle_menu_member_count(g) - 1));
    set_battle_menu_slot_members(g, member, MENU_NONE);
    uint8_t next = MENU_NONE;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (menu_slot_has_member(g, slot)) {
            next = battle_menu_slot_members(g, slot);
            break;
        }
    set_battle_menu_slot(g, next);
    set_battle_menu_member(g, next);
    set_battle_timers_held(g, 0);
}

// $C1:161A
/* Confirmed: the target and what was chosen go into the member's action. */
static void confirm_targets(GameState *g, uint8_t member)
{
    if (party_shown_for_targets(g))
        back_from_the_party(g);
    set_battle_targeting(g, (uint8_t)(battle_targeting(g) - 1));
    Chosen c;
    switch (battle_menu_page(g)) {
    case PAGE_COMMANDS: c = chose_attack(); break;
    case PAGE_TECHS: c = chose_tech(g, member); break;
    default: c = chose_item(g); break;
    }
    queue_action(g, member, &c);
}

// $C1:1561
static void choose_target(GameState *g, const Frame *f, uint8_t member)
{
    if (choose_targets(g, f)) {
        set_battle_target_choice(g, 0);
        call_off_targeting(g);
    } else if (pressed(f, BUTTON_B)) {
        click(f, CLICK_TARGETS_CALLED_OFF);
        call_off_targeting(g);
    } else if (pressed(f, BUTTON_A | BUTTON_X)) {
        click(f, CLICK_TARGET_CONFIRMED);
        confirm_targets(g, member);
    } else if (!(battle_targets_together(g) & TARGETS_TOGETHER)) {
        if (pressed(f, BUTTON_DOWN | BUTTON_RIGHT))
            move_to_target(g, f, true);
        else if (pressed(f, BUTTON_UP | BUTTON_LEFT))
            move_to_target(g, f, false);
    }
}

/* ---- The command page ----------------------------------------------------------------------------- */

/* Another member waiting to choose, the menu's slots taken round one way or
 * the other. The command cursor goes with the player: the member gets the
 * one it is on, or, where the commands are remembered, the one left gets its
 * remembered one back. (The original goes round for ever where no slot has
 * a member; it stops after a round here.) */
static void go_to_other_member(GameState *g, const Frame *f, uint8_t member, bool forward)
{
    if (battle_menu_member_count(g) != 1)
        click(f, forward ? CLICK_MEMBER_ON : CLICK_MEMBER_BACK);
    uint8_t slot = battle_menu_slot(g);
    for (unsigned tries = 0; tries < BATTLE_PARTY_SIZE; tries++) {
        if (forward)
            slot = slot + 1 == BATTLE_PARTY_SIZE ? 0 : (uint8_t)(slot + 1);
        else
            slot = menu_nobody((uint8_t)(slot - 1)) ? BATTLE_PARTY_SIZE - 1 : (uint8_t)(slot - 1);
        if (menu_slot_has_member(g, slot))
            break;
    }
    set_battle_menu_slot(g, slot);
    uint8_t other = battle_menu_slot_members(g, slot);
    if (!battle_remembers_commands(g))
        set_battle_command_cursor(g, other, battle_command_cursor(g, member));
    else
        set_battle_command_cursor(g, member, battle_remembered_command(g, member));
}

static void move_command_cursor(GameState *g, const Frame *f, uint8_t member, bool down)
{
    click(f, down ? CLICK_COMMAND_DOWN : CLICK_COMMAND_UP);
    uint8_t cursor = battle_command_cursor(g, member);
    if (down)
        cursor = cursor + 1 == COMMANDS ? COMMAND_ATTACK : (uint8_t)(cursor + 1);
    else
        cursor = menu_nobody((uint8_t)(cursor - 1)) ? COMMANDS - 1 : (uint8_t)(cursor - 1);
    set_battle_command_cursor(g, member, cursor);
    redraw_member_cells(g, 1);
}

// $C1:129C
static void choose_attack(GameState *g, const Frame *f)
{
    forget_target_named(g);
    redraw_member_cells(g, 2);
    set_battle_target_kind(g, TARGET_KIND_ATTACK);
    set_battle_menu_command(g, COMMAND_ATTACK);
    (void)choose_targets(g, f);
    set_battle_targets_changed(g, (uint8_t)(battle_targets_changed(g) + 1));
    set_battle_targeting(g, (uint8_t)(battle_targeting(g) + 1));
}

// $C1:12BC
static void open_techs(GameState *g)
{
    if (battle_fight_flags(g) & NO_TECHS_IN_THIS_FIGHT)
        return;
    set_battle_selected_tech_partners(g, NO_PARTNERS);
    set_battle_ally_target_window(g, 0);
    set_battle_tech_list_dimmed(g, 0);
    TIMED_AS(g, 0xC10A88, battle_lay_out_tech_list(g));
    battle_mark_tech_rows(g);
    set_battle_menu_command(g, COMMAND_TECH);
    set_battle_menu_page(g, PAGE_TECHS);
    ORIGINAL_EFFECT(set_battle_tech_page_open_mark(g, 0));
    set_battle_frames_reload_request(g, (uint8_t)(battle_frames_reload_request(g) + 1));
}

// $C1:12ED
static void open_items(GameState *g)
{
    if (battle_fight_flags(g))
        return;
    set_battle_ally_target_window(g, 0);
    set_battle_menu_command(g, COMMAND_ITEM);
    TIMED_AS(g, 0xC1095D, battle_lay_out_item_list(g, battle_item_top(g)));
    TIMED_AS(g, 0xCFFD6A, battle_send_lower_window_rows(g));
    for (unsigned cell = 0; cell < BATTLE_WINDOW_FRAMES_TILEMAP_COUNT; cell++) {
        set_battle_window_frames_tilemap_tile(g, cell, battle_item_page_frames_tile(g, cell));
        set_battle_window_frames_tilemap_attributes(g, cell, battle_item_page_frames_attributes(g, cell));
    }
    charge_block_move(g, 2 * BATTLE_WINDOW_FRAMES_TILEMAP_COUNT);
    set_battle_frames_upload_request(g, (uint8_t)(battle_frames_upload_request(g) + 1));
    set_battle_menu_page(g, PAGE_ITEMS);
}

// $C1:127B
static void choose_command(GameState *g, const Frame *f, uint8_t member)
{
    set_battle_ally_target_marked(g, MENU_NONE);
    TIMED_AS(g, 0xCFFD9E, battle_note_menu_stat_block(g, member));
    switch (battle_command_cursor(g, member)) {
    case COMMAND_ATTACK: choose_attack(g, f); break;
    case COMMAND_TECH: open_techs(g); break;
    default: open_items(g); break;
    }
}

// $C1:1184
static void take_command_page(GameState *g, const Frame *f, uint8_t member)
{
    set_battle_ally_target_window(g, 0);
    set_battle_timers_held(g, 0);
    if (battle_remembers_commands(g) && pressed(f, BUTTON_X)) {
        set_battle_attack_by_shortcut(g, (uint8_t)(battle_attack_by_shortcut(g) + 1));
        set_battle_command_cursor(g, member, COMMAND_ATTACK);
        choose_command(g, f, member);
    } else if (pressed(f, BUTTON_LEFT)) {
        go_to_other_member(g, f, member, false);
    } else if (pressed(f, BUTTON_UP)) {
        move_command_cursor(g, f, member, false);
    } else if (pressed(f, BUTTON_DOWN)) {
        move_command_cursor(g, f, member, true);
    } else if (pressed(f, BUTTON_RIGHT)) {
        go_to_other_member(g, f, member, true);
    } else if (pressed(f, BUTTON_A)) {
        click(f, CLICK_COMMAND_CHOSEN);
        choose_command(g, f, member);
    }
}

/* ---- The lists ---------------------------------------------------------------------------------- */

/* Back to the commands from a list: the frames put back, the window rebuilt. */
static void back_to_commands(GameState *g)
{
    TIMED_AS(g, 0xC11C3A, battle_reset_frames(g));
    set_battle_menu_page(g, PAGE_COMMANDS);
    set_battle_menu_slot_shown(g, NO_SLOT_SHOWN_FROM_A_LIST);
}

enum { TECH_ROWS_SHOWN = 3, TECH_DIMMED = 0x80 };

/* The cells the original moves a list's cursor in: the entry it is at, the
 * rows it went, and the cursor's row worked out. */
enum { AT_CELL = 0x80, ROWS_CELL = 0x82, ROW_CELL = 0x83 };

static void leave_tech_move(GameState *g, uint8_t at, uint8_t rows_left, bool worked_out, uint8_t row)
{
    battle_leave_work16(g, AT_CELL, at);
    battle_leave_work8(g, ROWS_CELL, rows_left);
    if (worked_out)
        battle_leave_work8(g, ROW_CELL, row);
}

// $C1:13AD
/* Up to the row before with a tech on it, scrolling the list where the
 * cursor would leave its top; nothing where there is none before. */
static void tech_cursor_up(GameState *g, uint8_t member, uint8_t at)
{
    uint8_t rows = 0;
    for (;;) {
        rows++;
        if (at == 0) {
            ORIGINAL_EFFECT(leave_tech_move(g, at, rows, false, 0));
            return;
        }
        at--;
        if (battle_tech_rows_selectable(g, at))
            break;
    }
    int row = battle_tech_cursor(g, member) - rows;
    uint8_t scroll = battle_tech_scroll(g, member);
    ORIGINAL_EFFECT(leave_tech_move(g, at, 0, true, (uint8_t)row));
    if (row < 0 && scroll == 0)
        return;
    if (row < 0) {
        scroll = (uint8_t)(scroll + row);
        row = 0;
    }
    set_battle_tech_scroll(g, member, scroll);
    set_battle_tech_cursor(g, member, (uint8_t)row);
}

// $C1:13F4
/* Down to the next row with a tech on it, scrolling the list where the
 * cursor would leave its foot; nothing past the list's end. */
static void tech_cursor_down(GameState *g, uint8_t member, uint8_t at)
{
    uint8_t rows = 0;
    for (;;) {
        rows++;
        if ((uint8_t)(at + 1) >= battle_tech_list_length(g, member)) {
            ORIGINAL_EFFECT(leave_tech_move(g, at, rows, false, 0));
            return;
        }
        at++;
        if (battle_tech_rows_selectable(g, at))
            break;
    }
    unsigned row = battle_tech_cursor(g, member) + rows;
    ORIGINAL_EFFECT(leave_tech_move(g, at, 0, true, (uint8_t)row));
    uint8_t scroll = battle_tech_scroll(g, member);
    if (row >= TECH_ROWS_SHOWN) {
        scroll = (uint8_t)(scroll + row - (TECH_ROWS_SHOWN - 1));
        row = TECH_ROWS_SHOWN - 1;
    }
    set_battle_tech_scroll(g, member, scroll);
    set_battle_tech_cursor(g, member, (uint8_t)row);
}

// $C1:1369
/* The selected tech, where the page has worked out it can be used. */
static void use_tech(GameState *g, const Frame *f)
{
    if (!battle_tech_list_dimmed(g) || (battle_selected_tech_flags(g) & TECH_DIMMED))
        return;
    set_battle_target_kind(g, battle_selected_tech_menu_detail(g));
    start_choosing_target(g, f);
}

// $C1:1320
static void take_tech_page(GameState *g, const Frame *f, uint8_t member)
{
    hold_timers_for_lists(g);
    uint8_t at = (uint8_t)(battle_tech_scroll(g, member) + battle_tech_cursor(g, member));
    ORIGINAL_EFFECT(battle_leave_work16(g, AT_CELL, at));
    if (pressed(f, BUTTON_A)) {
        click(f, CLICK_TECH_CHOSEN);
        use_tech(g, f);
    } else if (pressed(f, BUTTON_B)) {
        click(f, CLICK_TECH_BACK);
        back_to_commands(g);
        set_battle_tech_cursor(g, member, battle_remembered_tech_row(g, member));
        set_battle_tech_scroll(g, member, battle_remembered_tech_scroll(g, member));
    } else if (pressed(f, BUTTON_UP | BUTTON_LEFT)) {
        click(f, CLICK_TECH_UP);
        tech_cursor_up(g, member, at);
    } else if (pressed(f, BUTTON_DOWN | BUTTON_RIGHT)) {
        click(f, CLICK_TECH_DOWN);
        tech_cursor_down(g, member, at);
    }
}

/* The item list's top, and its rows: three on the screen, the list's top
 * going no further than leaves the last records' rows on it. */
enum { ITEM_ROWS_SHOWN = 3, ITEM_LIST_LAST_TOP = 0xFA, ITEM_PAGE = 3 };

/* The item list's cursor and its arrows drawn again (menu_window.c draws
 * them where they changed). */
static void redraw_item_cursor(GameState *g)
{
    set_battle_item_cursor_shown(g, MENU_NONE);
    set_battle_item_arrows_shown(g, MENU_NONE);
}

/* The list laid out from a new top, the cursor and arrows drawn again. */
static void show_items_from(GameState *g, uint8_t top)
{
    set_battle_item_top(g, top);
    TIMED_AS(g, 0xC1095D, battle_lay_out_item_list(g, top));
    redraw_item_cursor(g);
}

// $C1:1498
/* The item under the cursor, where the party holds one and it can be used in
 * battle. */
enum { NOT_FOR_BATTLE = 0x80 };

static void use_item(GameState *g, const Frame *f)
{
    uint8_t at = (uint8_t)(battle_item_top(g) + battle_item_row(g));
    ORIGINAL_EFFECT(battle_leave_work8(g, AT_CELL, at));
    uint16_t place = battle_multiply(g, at, ITEM_LIST_RECORD_BYTES);
    if ((battle_item_list_flags(g, at) & NOT_FOR_BATTLE) || battle_item_list_held(g, at) == 0)
        return;
    set_battle_item_chosen(g, battle_item_list_item(g, at));
    set_battle_target_kind(g, battle_item_list_menu_detail(g, at));
    set_battle_item_chosen_place(g, place);
    start_choosing_target(g, f);
}

// $C1:143D
static void take_item_page(GameState *g, const Frame *f)
{
    hold_timers_for_lists(g);
    uint8_t top = battle_item_top(g), row = battle_item_row(g);
    if (pressed(f, BUTTON_A)) {
        click(f, CLICK_ITEM_CHOSEN);
        use_item(g, f);
    } else if (pressed(f, BUTTON_B)) {
        click(f, CLICK_ITEM_BACK);
        back_to_commands(g);
        set_battle_item_arrows_shown(g, MENU_NONE);
    } else if (pressed(f, BUTTON_UP | BUTTON_LEFT)) {
        click(f, CLICK_ITEM_UP);
        /* at the top row the list scrolls back a row (laid out again even at
           its top) */
        if (row == 0)
            show_items_from(g, top == 0 ? 0 : (uint8_t)(top - 1));
        if (row > 0)
            set_battle_item_row(g, (uint8_t)(row - 1));
    } else if (pressed(f, BUTTON_DOWN | BUTTON_RIGHT)) {
        click(f, CLICK_ITEM_DOWN);
        if (row == ITEM_ROWS_SHOWN - 1) {
            if (top >= ITEM_LIST_LAST_TOP)
                redraw_item_cursor(g);
            else
                show_items_from(g, (uint8_t)(top + 1));
        }
        if (row + 1 < ITEM_ROWS_SHOWN)
            set_battle_item_row(g, (uint8_t)(row + 1));
    } else if (pressed(f, BUTTON_L)) {
        click(f, CLICK_ITEM_PAGE_ON);
        uint8_t further = (uint8_t)(top + ITEM_PAGE);
        show_items_from(g, further >= ITEM_LIST_LAST_TOP || further == 0 ? ITEM_LIST_LAST_TOP : further);
    } else if (pressed(f, BUTTON_R)) {
        click(f, CLICK_ITEM_PAGE_BACK);
        show_items_from(g, top >= ITEM_PAGE ? (uint8_t)(top - ITEM_PAGE) : 0);
    } else if (battle_item_list_changed(g)) {
        show_items_from(g, top);
        set_battle_item_list_changed(g, 0);
    }
}

/* ---- The frame ---------------------------------------------------------------------------------- */

// $C1:1153
void battle_menu_frame(GameState *g, Buttons buttons, MenuHost *host)
{
    Frame f = { buttons, host };
    battle_list_stolen_item(g);
    uint8_t member = member_choosing(g);
    if (!menu_nobody(member)) {
        if (battle_targeting(g)) {
            choose_target(g, &f, member);
        } else {
            TIMED_AS(g, 0xC11115, battle_frame_member_choosing(g));
            switch (battle_menu_page(g)) {
            case PAGE_COMMANDS: take_command_page(g, &f, member); break;
            case PAGE_TECHS: take_tech_page(g, &f, member); break;
            default: take_item_page(g, &f); break;
            }
        }
    }
    set_battle_menu_buttons(g, 0);
}

/* ---- The entries -------------------------------------------------------------------------------- */

/* The frame around the member leaves A the high table's byte, its high byte
 * clear, X past the four sprites, the carry of its loop's last compare and
 * the other flags of loading the byte. */
void battle_frame_member_choosing_entry(Snes *s)
{
    battle_frame_member_choosing(state_of(s));
    s->cpu.a = ALL_CORNERS_LARGE;
    s->cpu.x = FRAME_CORNERS * 4;
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_N, true);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_rts(s);
}

/* The stat block noted from the member in A; the original leaves A clear (its
 * direct page's) and X the stat block. */
void battle_note_menu_stat_block_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t slot = (uint8_t)s->cpu.a;
    battle_note_menu_stat_block(g, slot);
    s->cpu.x = battle_stat_block_place(g, slot);
    s->cpu.a = BATTLE_PAGE;
    m_set_flag(s, CPU_FLAG_Z, true);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rtl(s);
}

/* ---- The guards --------------------------------------------------------------------------------- */

/* The stat block of a combatant or the menu's nobody, by the byte in A, its
 * high byte clear (the original reads the table with the whole of A). */
bool battle_note_menu_stat_block_ready(Snes *s) { return battle_on_page_a_high_clear(s); }

/* What the menu's frame takes on trust beside the window's state
 * (battle_menu_window_ready): the menu's slot one of the three or none; a
 * member's tech list no longer than its twenty, its cursor's entry inside
 * them; the queue room for another action; the selected tech's partners
 * party slots; the chosen item's place a record's, and the item list's top no
 * further than the list goes; a stolen item a place to
 * go; and the tech list not being laid out, which no frame sees. The
 * original carries A's high byte into the indexes it finds the member's
 * records with, as the game's is clear. */
static bool partners_fit(uint8_t partners)
{
    return partners == NO_PARTNERS
           || (first_partner(partners) < BATTLE_PARTY_SIZE
               && (second_partner(partners) < BATTLE_PARTY_SIZE || second_partner(partners) == NO_PARTNER));
}

static bool stolen_item_fits(const GameState *g)
{
    for (int member = BATTLE_PARTY_SIZE - 1; member >= 0; member--)
        if (battle_member_kept_item_waiting(g, (unsigned)member)) {
            uint8_t item = battle_member_kept_item_id(g, (unsigned)member);
            return item_place(g, item) >= 0 || empty_place(g) >= 0;
        }
    return true;
}

static bool someone_in_a_slot(const GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (menu_slot_has_member(g, slot))
            return true;
    return false;
}

static bool menu_frame_fits(const GameState *g)
{
    uint8_t slot = battle_menu_slot(g), member = battle_menu_member(g);
    if (battle_tech_list_being_laid_out(g) || !stolen_item_fits(g))
        return false;
    if (menu_nobody(member))
        return true;
    if (!(slot < BATTLE_PARTY_SIZE || slot == MENU_NONE) || !someone_in_a_slot(g))
        return false;
    if (battle_target_choice(g) >= AREA_FOUND_PLACES || battle_item_top(g) > ITEM_LIST_LAST_TOP)
        return false;
    if (battle_tech_list_length(g, member) > TECHS_A_MEMBER
        || (uint8_t)(battle_tech_scroll(g, member) + battle_tech_cursor(g, member)) > TECHS_A_MEMBER)
        return false;
    if (!battle_targeting(g))
        return true;
    uint16_t place = battle_item_chosen_place(g);
    switch (battle_menu_page(g)) {
    case PAGE_COMMANDS: break;
    case PAGE_TECHS:
        if (!partners_fit(battle_selected_tech_partners(g)))
            return false;
        break;
    default:
        if (place % ITEM_LIST_RECORD_BYTES || place / ITEM_LIST_RECORD_BYTES >= BATTLE_ITEM_LIST_COUNT)
            return false;
        break;
    }
    return battle_action_queue_length(g) < BATTLE_ACTION_QUEUE_COUNT;
}

bool battle_take_menu_input_ready(Snes *s)
{
    if (!battle_menu_window_ready(s) || (s->cpu.a & 0xFF00))
        return false;
    vars_peek(true);
    bool fits = menu_frame_fits(state_of(s));
    vars_peek(false);
    return fits;
}
