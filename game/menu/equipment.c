/* The equipment screen (the menus' screen 1), and the main menu's panel for
 * it.
 *
 * The screen changes what the party's members have on, in three steps of
 * its main task (menu_screen_step):
 *
 * - a member, from the member list down the left (menu/members.c), the
 *   chosen member's card beside it. With more members than the list shows,
 *   pushing past its top or bottom slides the rows a member along, a few
 *   lines a frame, the scroll bar's thumb following;
 * - one of the member's four slots on the right (the weapon, the helmet, the
 *   armor, the accessory), the list of what the party has that fits the slot
 *   drawn below it as the cursor moves; the two shoulder assignments switch to
 *   the next or the previous member without going back;
 * - an item from that list, the member's figures shown as they would be with
 *   it on. Choosing it puts it on and gives the one taken off back to the
 *   party's items. The list pages with the shoulder assignments and slides a
 *   row at a time past its ends.
 *
 * Going back leaves each step for the one before, and the first for the main
 * menu. The main menu shows the member list and the card dimmed beside it
 * while its cursor is on the equipment (the panel).
 *
 * This file is what the game does; equipment_bridge.c runs it on the
 * original's machine, calling out to what is still the original's between
 * the parts here: the member list's scroll bar, the low-HP marks, the trying
 * on of what is no item of the game's, the frames the screen waits. */
#include "menu/equipment.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/items.h"
#include "menu/display.h"
#include "menu/figures.h"
#include "menu/lists.h"
#include "menu/main_menu.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* The frame's input, as the cursor's reading leaves it (menu/cursor.h); the
 * shoulder assignments switch members here. */
enum { NEXT_MEMBER = MENU_PAGE_ONE_WAY, PREVIOUS_MEMBER = MENU_PAGE_THE_OTHER };

static bool chose(const GameState *g) { return menu_asked(g, MENU_CHOOSE); }
static bool went_back(const GameState *g) { return menu_asked(g, MENU_GO_BACK); }

/* The screen's steps. */
enum { STEP_MEMBER = 1, STEP_SLOT = 2 };

static void next_step(GameState *g) { set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1)); }

/* The list of what fits is drawn for a slot and a member; forgetting which
 * has it drawn again. */
enum { NOTHING_SHOWN = 0xFF };

static void forget_the_list_shown(GameState *g) { set_menu_equip_shown(g, NOTHING_SHOWN); }


/* The inventory's places the list looks through (the last fourteen, past
 * the items the game has, it leaves alone). */
enum { MENU_INVENTORY_LISTED = 0xF2 };

/* ---- The list of what fits -------------------------------------------------------------------- */

/* The items each slot takes are a range of item numbers (menu_data.h). */
static bool fits(const GameState *g, uint8_t slot, uint8_t item)
{
    return item >= menu_slot_first_item(g, slot) && item < menu_slot_past_items(g, slot);
}

enum { NO_ITEM = 0 };

static void put_in_list(GameState *g, unsigned row, uint8_t item, uint8_t count)
{
    set_menu_list_items(g, row, item);
    set_menu_list_item_counts(g, row, count);
}

/* The original asks who can use each item through a cell, and sorts with
 * its first two: what is left there is the last item asked about, the last
 * power it kept and how far down the list it got. */
static void leave_list_made(GameState *g, uint8_t last_asked, uint8_t last_power, uint8_t rows)
{
    set_menu_item_operand(g, last_asked);
    set_menu_scratch_fits_power(g, last_power);
    set_menu_scratch_fits_row(g, rows);
}

// $C2:8953
/* The list of what fits the slot the screen is on and the loaded member can
 * use: each kind the party holds, with how many, the most powerful first (of
 * two as powerful, the later in the inventory first), ended by an empty
 * item; menu_equip_choices counts them. The ordering is the original's
 * pass: each row in turn takes the most powerful of those after it, the scan
 * ending at the first of no power (the end's empty item has none). */
void menu_list_what_fits(GameState *g)
{
    uint8_t slot = menu_equip_slot(g);
    uint8_t rows = 0, last_asked = 0;
    for (unsigned place = 0; place < MENU_INVENTORY_LISTED; place++) {
        uint8_t item = menu_inventory_item_at(g, place);
        if (item == NO_ITEM || !fits(g, slot, item))
            continue;
        last_asked = item;
        bool usable;
        TIMED_AS(g, 0xC293A8, usable = menu_loaded_member_can_use(g, item));
        if (usable)
            put_in_list(g, rows++, item, menu_inventory_count_at(g, place));
    }
    set_menu_equip_choices(g, rows);
    set_menu_list_items(g, rows, NO_ITEM);
    uint8_t power = menu_slot_first_item(g, slot);
    unsigned row = 0;
    for (; menu_list_items(g, row) != NO_ITEM; row++) {
        power = menu_power_of_item(g, menu_list_items(g, row));
        for (unsigned other = row + 1;; other++) {
            uint8_t item = menu_list_items(g, other), p = menu_power_of_item(g, item);
            if (p == 0)
                break;
            if (p < power)
                continue;
            power = p;
            uint8_t count = menu_list_item_counts(g, other);
            put_in_list(g, other, menu_list_items(g, row), menu_list_item_counts(g, row));
            put_in_list(g, row, item, count);
        }
    }
    ORIGINAL_EFFECT(leave_list_made(g, last_asked, power, (uint8_t)row));
}

/* The guard's: a slot of the four, an empty item of no power to end the
 * scan, and room in the list for all that fits and its end. */
bool menu_list_what_fits_can_be_made(GameState *g)
{
    uint8_t slot = menu_equip_slot(g);
    if (slot >= MENU_EQUIP_SLOTS || menu_power_of_item(g, NO_ITEM) != 0)
        return false;
    unsigned fitting = 0;
    for (unsigned place = 0; place < MENU_INVENTORY_LISTED; place++) {
        uint8_t item = menu_inventory_item_at(g, place);
        if (item != NO_ITEM && fits(g, slot, item) && (menu_item_users(g, item) & menu_character_bit(g, menu_status_record_character(g))))
            fitting++;
    }
    return fitting < MENU_LIST_ITEMS_COUNT;
}

void menu_equip_queue_list_rows(GameState *g) { menu_queue_fixed_upload(g, MENU_EQUIP_LIST_ROWS_UPLOAD); }

/* ---- Opening the screen ------------------------------------------------------------------------ */

/* The screen's windows; the member list's hidden rows (the half of its front
 * layer's buffer below the view, where a slide copies rows to) cleared; its
 * four colours twice over into palette row 7. */
enum { SCREEN_WINDOWS = 0xBDCD, HIDDEN_ROWS_AT = 0x3600, HIDDEN_ROWS_BYTES = 0x800 };
enum { SCREEN_COLOURS = 4, FIRST_SCREEN_COLOUR = 0x70, SCREEN_LINE_EFFECTS = 0x65, UPLOAD_BG2_ROWS = 0xFBFF };

// $C2:9C31
/* The screen's first step leads on to choosing a member, once it is set up. */
void menu_equip_open(GameState *g) { next_step(g); }

// $C2:A11A
void menu_equip_set_up_screen(GameState *g, Platform *pf)
{
    wram_fill8(g, 0x7E0000u | HIDDEN_ROWS_AT, HIDDEN_ROWS_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, HIDDEN_ROWS_BYTES));
    for (unsigned copy = 0; copy < 2; copy++)
        for (unsigned i = 0; i < SCREEN_COLOURS; i++)
            set_menu_palette(g, FIRST_SCREEN_COLOUR + copy * SCREEN_COLOURS + i,
                             menu_rom_colour(g, MENU_ROM_EQUIPMENT_COLOURS, i));
    menu_draw_windows(g, pf, SCREEN_WINDOWS);
    set_menu_cursor_index(g, menu_member_row(g));
    menu_work_on_member(g, menu_member_on_the_cursor(g));
    TIMED_AS(g, 0xC2E923, menu_clear_sprites_but_cursor(g));
}

void menu_equip_show_screen(GameState *g)
{
    set_menu_line_effects(g, SCREEN_LINE_EFFECTS);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    menu_queue_fixed_upload(g, UPLOAD_BG2_ROWS);
}

/* ---- Choosing a member ------------------------------------------------------------------------- */

// $C2:9C39
MemberChoice menu_equip_choose_member(GameState *g)
{
    if (menu_pushed(g, MENU_PUSHED_RIGHT) || chose(g)) {
        forget_the_list_shown(g);
        next_step(g);
        return MEMBER_CHOSEN;
    }
    return went_back(g) ? MEMBER_LEFT_FOR_THE_MAIN_MENU : MEMBER_POINTED_AT;
}

/* The list scrolls where there are more members than its rows; then a push
 * that left its rows slides it. */
bool menu_equip_member_list_scrolls(const GameState *g) { return menu_member_scroll_most(g) != 0; }
bool menu_equip_push_slides_members(const GameState *g) { return menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE); }

/* With no more members than the rows, a push past the top goes round to the
 * last member and one past the bottom to the first; a cursor that moved onto
 * a row past the last member goes back to the first. */
void menu_equip_take_cursor_round_members(GameState *g)
{
    uint8_t at = menu_cursor_index(g), reachable = (uint8_t)menu_members_reachable(g);
    if (at >= reachable)
        set_menu_cursor_index(g, 0);
    else if (menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE))
        set_menu_cursor_index(g, at != 0 ? 0 : (uint8_t)(reachable - 1));
}

/* A list at rest: pointing clears the step's low byte, the step's size and
 * sign as the item lists' slide frame ($C2:EB1F) and its rows' move read
 * them; a member slide's high byte stays. */
static void bring_list_to_rest(GameState *g) { set_menu_scroll_step(g, (uint16_t)(menu_scroll_step(g) & 0xFF00)); }

// $C2:9D17
bool menu_equip_point_at_member(GameState *g)
{
    bring_list_to_rest(g);
    TIMED_AS(g, 0xC29E76, menu_colour_member_rows(g));
    set_menu_member_row(g, menu_cursor_index(g));
    uint8_t member = menu_member_on_the_cursor(g);
    menu_work_on_member(g, member);
    bool another = member != menu_member_pointed(g);
    set_menu_member_pointed(g, member);
    return another;
}

/* The slide: the member coming in is drawn where the slide's start put the
 * drawing origin (menu_equip_slide_members: beside the list, out of view),
 * and dimmed; the list's rows are copied out of view on both layers, next to
 * it, from the list's place to where the slide starts (menu_slide_copy);
 * then twelve frames move the view of them four lines a frame, the line
 * every band of the rows' per-line table shows. At the end the three rows in
 * view are copied back to the list's place, a member further along, and the
 * view put back. */
enum { SLIDE_FRAMES = 12, LIST_FRONT_LAYER = MENU_FRONT_LAYER_AT, LIST_BACK_LAYER = 0x3E00, ROW_TILES_FROM_ORIGIN = 0x84 };
enum { LINE_AT_REST = 0x61FF, ROWS_SLID = 0x06 };
static const MenuTilePlace ROWS_PLACE = { 2, 2 };
static const MenuWindowSize ROWS_SIZE = { 12, 18 };

static MenuTilePlace slide_place(const GameState *g)
{
    uint16_t place = menu_slide_copy(g);
    return (MenuTilePlace){ (uint8_t)place, (uint8_t)(place >> 8) };
}

/* Where the three rows in view are once the slide is over: a member's six
 * tile rows on from where they were copied, whichever way it went. */
static MenuTilePlace slid_place(const GameState *g)
{
    MenuTilePlace at = slide_place(g);
    at.row ^= ROWS_SLID;
    return at;
}

static void copy_rows_on(GameState *g, uint16_t layer, MenuTilePlace from, MenuTilePlace to)
{
    set_menu_draw_origin(g, layer);
    menu_copy_tile_area(g, from, to, ROWS_SIZE);
}

enum { UPLOAD_BG1_SLID = 0xFBEA, UPLOAD_BG2_SLID = 0xFC06, UPLOAD_ROW_LINES = 0xFC45 };

/* Where a slide's member coming in is drawn (above the rows for a slide up,
 * below them for one down), where the rows are copied to beside it, and the
 * line the view starts from, which moves four lines a frame the way the list
 * slides. */
typedef struct {
    uint16_t copy_to, row_at, line;
    int16_t step;
} MemberSlide;

enum { SLIDE_DOWN_FROM = 0x60EF, SLIDE_UP_FROM = 0x611F, SLIDE_STEP = 4 };

static const MemberSlide SLIDE_UP_A_MEMBER = { 0x2602, 0x3580, SLIDE_UP_FROM, -SLIDE_STEP };
static const MemberSlide SLIDE_DOWN_A_MEMBER = { 0x2002, 0x3A00, SLIDE_DOWN_FROM, SLIDE_STEP };

static void set_up_member_slide(GameState *g, const MemberSlide *s)
{
    set_menu_slide_copy(g, s->copy_to);
    set_menu_draw_origin(g, s->row_at);
    set_menu_scroll_step(g, (uint16_t)s->step);
    set_menu_slide_line(g, s->line);
}

// $C2:9D5B
/* A push past the bottom slides the list down a member where the party has
 * members past the rows; past the top, up one where the list has slid. */
MemberListSlide menu_equip_slide_members(GameState *g)
{
    uint8_t scroll = menu_member_scroll(g);
    MemberListSlide slide = menu_member_slide_for_push(g, scroll, scroll < menu_member_scroll_most(g));
    if (slide == MEMBERS_SLIDE_DOWN) {
        set_menu_member_scroll(g, (uint8_t)(scroll + 1));
        set_up_member_slide(g, &SLIDE_DOWN_A_MEMBER);
    } else if (slide == MEMBERS_SLIDE_UP) {
        set_menu_member_scroll(g, (uint8_t)(scroll - 1));
        set_up_member_slide(g, &SLIDE_UP_A_MEMBER);
    }
    return slide;
}

// $C2:9DAF
void menu_equip_start_member_slide(GameState *g, Platform *pf)
{
    menu_work_on_member(g, (uint8_t)(menu_cursor_index(g) + menu_member_scroll(g)));
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, menu_member_worked_on(g)));
    TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, pf));
    uint16_t new_row = (uint16_t)(menu_draw_origin(g) + ROW_TILES_FROM_ORIGIN);
    TIMED_AS(g, 0xC29EB0, menu_colour_member_row(g, new_row, true));
    ORIGINAL_EFFECT(menu_leave_member_row_coloured(g, new_row, true));
    copy_rows_on(g, LIST_FRONT_LAYER, ROWS_PLACE, slide_place(g));
    copy_rows_on(g, LIST_BACK_LAYER, ROWS_PLACE, slide_place(g));
    menu_queue_fixed_upload(g, UPLOAD_BG1_SLID);
    menu_queue_fixed_upload(g, UPLOAD_BG2_SLID);
    set_menu_scroll_moved(g, 0);
    set_menu_scroll_frames(g, SLIDE_FRAMES);
}

/* The thumb follows the rows a line a frame, up for a slide up. Its place is
 * kept a word wide, with the byte after it, which the bar's own task counts
 * with between frames. (The original takes the way from whether the rows'
 * line carried as it moved, which for the lines a slide starts from is the
 * step's sign; the slide's guard holds it to those.) */
static void move_thumb(GameState *g, bool up)
{
    uint16_t thumb = menu_scroll_bar_thumb_word(g);
    set_menu_scroll_bar_thumb_word(g, (uint16_t)(up ? thumb - 1 : thumb + 1));
}

static bool sliding_up(const GameState *g) { return (int16_t)menu_scroll_step(g) < 0; }

static void show_rows_from(GameState *g, uint16_t line)
{
    for (unsigned band = 0; band < MENU_ROW_LINE_VALUES_COUNT; band++)
        set_menu_row_line_values(g, band, line);
}

bool menu_equip_slide_members_a_frame(GameState *g)
{
    int16_t step = (int16_t)menu_scroll_step(g);
    set_menu_scroll_moved(g, (uint16_t)(menu_scroll_moved(g) + step));
    uint16_t line = (uint16_t)(menu_slide_line(g) + step);
    set_menu_slide_line(g, line);
    move_thumb(g, sliding_up(g));
    show_rows_from(g, line);
    menu_queue_fixed_upload(g, UPLOAD_ROW_LINES);
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

void menu_equip_finish_member_slide(GameState *g)
{
    copy_rows_on(g, LIST_BACK_LAYER, slid_place(g), ROWS_PLACE);
    copy_rows_on(g, LIST_FRONT_LAYER, slid_place(g), ROWS_PLACE);
    show_rows_from(g, LINE_AT_REST);
    menu_queue_fixed_upload(g, UPLOAD_ROW_LINES);
    menu_ask_for_a_fast_repeat(g);   /* a push held through the slide carries straight on */
}

/* ---- Choosing a slot --------------------------------------------------------------------------- */

// $C2:9C70
SlotChoice menu_equip_choose_slot(GameState *g)
{
    if (menu_pushed(g, MENU_PUSHED_LEFT) || (!chose(g) && went_back(g))) {
        set_menu_screen_step(g, STEP_MEMBER);
        return SLOT_LEFT_FOR_THE_MEMBERS;
    }
    if (!chose(g))
        return SLOT_POINTED_AT;
    set_menu_cursor_index(g, menu_equip_slot(g));
    return menu_equip_choices(g) != 0 ? SLOT_CHOSEN : SLOT_REFUSED;
}

void menu_equip_start_choosing_an_item(GameState *g) { next_step(g); }

/* The shoulder assignments switch to the next member, round from the last
 * to the first, or the previous one, round from the first to the last. The
 * list then shows the member in its first rows, or in its last row once it
 * has to scroll. */
enum { LAST_ROW = MENU_MEMBER_ROWS - 1 };

static uint8_t member_after(uint8_t member, uint8_t reachable)
{
    uint8_t next = (uint8_t)(member + 1);
    return next < reachable ? next : 0;
}

/* A member before the first wraps to the last. */
static uint8_t member_before(uint8_t member, uint8_t reachable)
{
    return member != 0 ? (uint8_t)(member - 1) : (uint8_t)(reachable - 1);
}

// $C2:A00B
bool menu_equip_switch_member(GameState *g)
{
    uint8_t asked = menu_cursor_buttons(g) & (NEXT_MEMBER | PREVIOUS_MEMBER);
    if (!asked)
        return false;
    uint8_t was = menu_member_worked_on(g), reachable = (uint8_t)menu_members_reachable(g);
    uint8_t member = (asked & NEXT_MEMBER) ? member_after(was, reachable) : member_before(was, reachable);
    menu_work_on_member(g, member);
    set_menu_member_row(g, member);
    if (member == was)
        return false;
    set_menu_member_scroll(g, 0);
    if (member >= LAST_ROW) {
        set_menu_member_scroll(g, (uint8_t)(member - LAST_ROW));
        set_menu_member_row(g, LAST_ROW);
    }
    return true;
}

// $C2:9ECD
bool menu_equip_keep_slot(GameState *g)
{
    uint8_t slot = menu_cursor_index(g);
    bool moved = slot != menu_equip_kept_slot(g);
    set_menu_equip_kept_slot(g, slot);
    return moved;
}

/* The slots' line effects, and the card's layer (the slots' marks are on it)
 * queued. */
enum { SLOTS_LINE_EFFECTS = 0x4D, UPLOAD_BG1_CARD = 0xFBE3 };

void menu_equip_show_slots(GameState *g)
{
    set_menu_line_effects(g, SLOTS_LINE_EFFECTS);
    menu_queue_fixed_upload(g, UPLOAD_BG1_CARD);
}

/* The list is drawn for a slot and member, recorded as their sum. */
bool menu_equip_list_is_for_another_slot(GameState *g)
{
    uint8_t shown = (uint8_t)(menu_equip_slot(g) + menu_member_worked_on(g));
    bool another = shown != menu_equip_shown(g);
    set_menu_equip_shown(g, shown);
    return another;
}

/* ---- Choosing an item -------------------------------------------------------------------------- */

// $C2:9CA5
ItemChoice menu_equip_choose_item(const GameState *g)
{
    if (chose(g))
        return ITEM_CHOSEN;
    return went_back(g) ? ITEM_LEFT_FOR_THE_SLOTS : ITEM_POINTED_AT;
}

/* The slot's place in the member's record. */
static uint8_t slot_place(const GameState *g) { return (uint8_t)menu_equip_record_place(g); }

/* After the figures it marks, and a place it leaves unmarked, the window
 * marks the member's greatest HP. */
enum { GREATEST_HP_MARK = MENU_EQUIPMENT_FIGURES_SHOWN + 1 };
enum { FIGURES_WINDOW = 0xBEE2 };

/* What comparing the figures leaves in the menus' first cell: the last
 * figure the window shows, as the record has it. */
static void leave_the_last_figure_compared(GameState *g)
{
    uint8_t last = menu_equipment_figure_shown(g, MENU_EQUIPMENT_FIGURES_SHOWN - 1);
    set_menu_scratch_figure_compared(g, menu_status_record_figures_byte(g, last));
}

// $C2:A5E7
/* The member's figures worked out again with the item tried on, each marked
 * by how the item moves it from what the member has now, its greatest HP
 * too; the figures' window drawn and the front layer queued. */
static void compare_figures(GameState *g, Platform *pf)
{
    menu_mark_the_figures_tried_on(g, FIGURES_ON_THE_EQUIPMENT_SCREEN);
    set_menu_figure_marks(g, GREATEST_HP_MARK, menu_figure_mark(menu_status_record_hp_cap(g), menu_figures_greatest_hp(g)));
    ORIGINAL_EFFECT(leave_the_last_figure_compared(g));
    menu_draw_windows(g, pf, FIGURES_WINDOW);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

static uint8_t list_place_on_the_cursor(const GameState *g)
{
    return (uint8_t)(menu_equip_list_top(g) + menu_equip_list_row(g));
}

static uint8_t item_on_the_cursor(const GameState *g) { return menu_list_items(g, list_place_on_the_cursor(g)); }

/* The original marks the figures from the menus' zero cell's place in its
 * table of them, which the menus keep at 0. */
bool menu_equip_can_try_on(const GameState *g)
{
    return menu_zero(g) == 0 && list_place_on_the_cursor(g) < MENU_LIST_ITEMS_COUNT
           && menu_figures_workable_with(g, slot_place(g), item_on_the_cursor(g));
}

// $C2:A5C3
/* The list's item on the cursor tried on in the slot: the member's figures
 * compared with it, and the item described. */
void menu_equip_try_on(GameState *g, Platform *pf)
{
    uint8_t item = item_on_the_cursor(g);
    menu_try_on(g, item);
    compare_figures(g, pf);
    TIMED_AS(g, 0xC2F2F3, menu_describe_item(g, item));
}

/* For the original's code and shadow mode: the item and how many the
 * inventory's routines are handed in the menus' cells. */
static void leave_operand(GameState *g, uint8_t item)
{
    set_menu_item_operand_quantity(g, 1);
    set_menu_item_operand(g, item);
}

// $C2:9CC1
void menu_equip_put_on_the_chosen_item(GameState *g)
{
    uint8_t character = menu_member_character(g), place = slot_place(g);
    uint8_t was = character_records_record_byte(g, character, place);
    ORIGINAL_EFFECT(leave_operand(g, was));
    TIMED_AS(g, 0xC28791, (void)menu_add_items(g, was, 1));
    uint8_t item = menu_status_record_bytes_byte(g, place);
    ORIGINAL_EFFECT(leave_operand(g, item));
    set_character_records_record_byte(g, character, place, item);
    TIMED_AS(g, 0xC287D5, (void)menu_take_items(g, item, 1));
}

/* A weapon changed on one of the first six characters clears the story's
 * byte for the character (event memory from $B4). The original takes the
 * character from the list's place the cursor's row is, without the list's
 * scroll. */
enum { WEAPON_SLOT = 0, CHARACTERS_WITH_A_WEAPON_BYTE = 6, WEAPON_BYTES = 0xB4 };

// $C2:9CFC
void menu_equip_forget_weapon(GameState *g)
{
    if (menu_equip_slot(g) != WEAPON_SLOT)
        return;
    uint8_t character = menu_members(g, menu_member_row(g));
    if (character < CHARACTERS_WITH_A_WEAPON_BYTE)
        set_event_memory_byte(g, WEAPON_BYTES + character, 0);
}

void menu_equip_return_to_the_kept_slot(GameState *g) { set_menu_cursor_index(g, menu_equip_kept_slot(g)); }

void menu_equip_back_to_the_slots(GameState *g)
{
    forget_the_list_shown(g);
    set_menu_screen_step(g, STEP_SLOT);
}

/* The list's rows start at cursor place 4; a row slides in where the push
 * leaves a row of the list past the last shown or before the first. */
enum { FIRST_LIST_PLACE = 4 };


/* The scroll bar's thumb for the list's first row: four lines a row,
 * through the multiplier ($C2:8BA6), which it leaves with the product. */
enum { THUMB_LINES_A_ROW = 4 };

static uint8_t thumb_at_row(GameState *g, uint8_t row)
{
    ORIGINAL_EFFECT(hw_set_multiplicand(g, row));
    ORIGINAL_EFFECT((void)hw_multiply_by(g, THUMB_LINES_A_ROW));
    return (uint8_t)(row * THUMB_LINES_A_ROW);
}

/* The paging moved the list's first row (and the thumb's place with it, as
 * the row) a page on or back. */
void menu_equip_turn_list_page(GameState *g, uint8_t top)
{
    set_menu_scroll_bar_thumb(g, thumb_at_row(g, menu_scroll_bar_thumb(g)));
    set_menu_equip_list_top(g, top);
}

/* The row of the list the cursor is on. */
uint8_t menu_equip_list_row(const GameState *g) { return (uint8_t)(menu_cursor_index(g) - FIRST_LIST_PLACE); }

/* A push past the list's rows (in a list that scrolls) slides a row in,
 * where there is one: not before the list's first item, nor past its last. */
bool menu_equip_list_slide_brings_a_row(const GameState *g, uint8_t row)
{
    if (!menu_list_scrolls_a_row(g))
        return false;
    uint8_t item = (uint8_t)(row + menu_equip_list_top(g));
    return item != 0 && (uint8_t)(item + 1) < menu_equip_choices(g);
}

/* The slide moves the list's nine rows, and the per-line table its block
 * move takes the rows' lines from. */
enum { LIST_LINE_TABLE = 0x9303, LIST_LINE_TABLE_BYTES = 0x25, LIST_ROWS_SLID = 9 };

void menu_equip_set_up_list_slide(GameState *g)
{
    set_menu_block_move_dest(g, LIST_LINE_TABLE);
    set_menu_block_move_size(g, LIST_LINE_TABLE_BYTES);
    set_menu_slide_rows(g, LIST_ROWS_SLID);
}

/* The list's first row a row on (or back), and the row coming in (the
 * cursor's) named for its drawing: its item and how many of it the party
 * has. */
void menu_equip_bring_in_list_row(GameState *g, uint8_t row_pushed)
{
    uint8_t top = menu_equip_list_top(g);
    top = menu_pushed(g, MENU_PUSHED_DOWN) ? (uint8_t)(top + 1) : (uint8_t)(top - 1);
    set_menu_equip_list_top(g, top);
    uint8_t row = (uint8_t)(row_pushed + top);
    set_menu_item_operand_count(g, menu_list_item_counts(g, row));
    set_menu_item_operand(g, menu_list_items(g, row));
}

/* A fast repeat (a push held down) skips the slide's frames. */
enum { REPEATING_FAST = 0x80 };

bool menu_equip_start_list_slide_frames(GameState *g)
{
    if (menu_fast_repeat(g) & REPEATING_FAST)
        return false;
    set_menu_scroll_moved(g, 0);
    return true;
}

/* A frame of the list's slide: the rows moved a step, the thumb a line the
 * way the rows have moved. */
void menu_equip_slide_list_a_frame(GameState *g)
{
    uint16_t moved = (uint16_t)(menu_scroll_moved(g) + menu_scroll_step(g));
    set_menu_scroll_moved(g, moved);
    move_thumb(g, (int16_t)moved < 0);
}

bool menu_equip_list_slide_frames_left(GameState *g)
{
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

/* Once slid: the thumb at the new first row, the upload queue emptied (the
 * frames sent what it held), the block move set to bring the rows' new
 * lines in, the rows moved a row along and the list at rest. */
void menu_equip_finish_list_slide(GameState *g)
{
    set_menu_scroll_bar_thumb(g, thumb_at_row(g, menu_equip_list_top(g)));
    set_menu_vram_queue_bytes(g, 0);
    menu_put_the_list_bands_back(g);
    TIMED_AS(g, 0xC29FDE, menu_equip_move_list_rows(g));
    set_menu_scroll_step(g, 0);
    menu_equip_queue_list_rows(g);
}

/* The list's rows moved a row along in their buffer once it has slid
 * (menu/lists.h): its nine rows shown, the way it slid. */
enum { LIST_ROWS_AT = 0x4F80, LIST_ROW_BYTES = 0x80, LIST_BYTES = 0x400 };

// $C2:9FDE
void menu_equip_move_list_rows(GameState *g) { menu_move_list_rows(g, LIST_ROWS_AT, LIST_BYTES, LIST_ROW_BYTES, menu_list_slid_up(g)); }

/* ---- The main menu's panel --------------------------------------------------------------------- */

/* With the main menu's cursor on the equipment, the member list and the card
 * are drawn beside the main menu, dimmed: the panel's window, the rows' per-
 * line tables at rest, the time played, the rows from the list's first, the
 * card for the cursor's member (the bridge runs it), the highlight's colours
 * and the dimmed ones. The list's scroll is brought within the reachable
 * members, and the cursor onto an active member; the menus' fourth task is
 * held a frame longer as the panel starts. */
enum { PANEL_WINDOW = 0xBD74, ROWS_SHOWN = MENU_MEMBER_ROWS, OTHER_LINES_AT_REST = 0xFFFF };
enum { UPLOAD_PANEL_LINES = 0xFC3E };

/* The furthest the list scrolls: the members past the three it shows. */
static uint8_t scroll_most(uint8_t reachable) { return reachable > ROWS_SHOWN ? (uint8_t)(reachable - ROWS_SHOWN) : 0; }

// $C2:A051
void menu_equip_panel_lay_out_list(GameState *g, Platform *pf)
{
    set_menu_tasks_countdown(g, MENU_CURSOR_TASK, (uint8_t)(menu_tasks_countdown(g, MENU_CURSOR_TASK) + 1));
    set_menu_member_scroll_most(g, scroll_most((uint8_t)menu_members_reachable(g)));
    if (menu_member_scroll_most(g) < menu_member_scroll(g))
        set_menu_member_scroll(g, menu_member_scroll_most(g));
    uint8_t row = menu_member_row(g);
    if (row >= menu_active_members(g))
        row = (uint8_t)(menu_active_members(g) - 1);
    set_menu_cursor_index(g, row);
    set_menu_member_row(g, row);
    menu_draw_windows(g, pf, PANEL_WINDOW);
    show_rows_from(g, LINE_AT_REST);
    for (unsigned band = 0; band < MENU_ROW_OTHER_LINE_VALUES_COUNT; band++)
        set_menu_row_other_line_values(g, band, OTHER_LINES_AT_REST);
    menu_queue_fixed_upload(g, UPLOAD_PANEL_LINES);
}

void menu_equip_panel_draw_members(GameState *g, Platform *pf)
{
    menu_draw_the_clock(g);
    TIMED_AS(g, 0xC2A1B2, menu_draw_member_rows(g, pf));
}

/* The colour math table the panel's window list set up, built for the next
 * frame. */
void menu_equip_panel_set_colour_math(GameState *g, Platform *pf) { TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf)); }

/* The highlight's four colours into palette row 2's second half, and the
 * screen's first 24 dimmed into row 4. */
enum { PANEL_LINE_EFFECTS = 0x64, HIGHLIGHT_COLOURS = 4, FIRST_HIGHLIGHT_COLOUR = 0x24, FIRST_DIMMED_COLOUR = 0x40 };

void menu_equip_panel_dim(GameState *g)
{
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    for (unsigned i = 0; i < HIGHLIGHT_COLOURS; i++)
        set_menu_palette(g, FIRST_HIGHLIGHT_COLOUR + i, menu_rom_colour(g, MENU_ROM_PANEL_HIGHLIGHT_COLOURS, i));
    TIMED_AS(g, 0xC29E76, menu_colour_member_rows(g));
    TIMED_AS(g, 0xC2A0E7, menu_equip_dim_colours(g, FIRST_DIMMED_COLOUR));
}

/* A colour dimmed: each of its five-bit channels halved. */
enum { DIMMED_COLOURS = 24, CHANNELS_HALVED = 0x3DEF };

// $C2:A0E7
void menu_equip_dim_colours(GameState *g, unsigned first)
{
    for (unsigned i = 0; i < DIMMED_COLOURS; i++)
        set_menu_palette(g, first + i, (uint16_t)((menu_palette(g, i) >> 1) & CHANNELS_HALVED));
}

// $C2:A101
/* The main menu leaving the equipment: palette row 7's first half, where the
 * screen put its colours, given back the menus' own (the party's items come
 * back from where the screen set them aside between), and the main menu's
 * line effects. */
enum { PANEL_COLOURS = 8 };

void menu_equip_panel_put_away(GameState *g)
{
    for (unsigned i = 0; i < PANEL_COLOURS; i++)
        set_menu_palette(g, FIRST_SCREEN_COLOUR + i, menu_rom_colour(g, MENU_ROM_ROW_7_COLOURS, i));
    TIMED_AS(g, 0xC29121, menu_put_items_back(g));
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
}

// $C2:A051
/* The panel: the list laid out, a frame, the time and the rows, a frame,
 * the colour math and the member's card, then the screen behind dimmed. */
bool menu_show_equipment_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx)
{
    uint8_t row_before = menu_member_row(g);
    menu_equip_panel_lay_out_list(g, pf);
    if (!n->wait(ctx, MAIN_MENU_AT_THE_EQUIPMENT_LAID_OUT))
        return false;
    menu_equip_panel_draw_members(g, pf);
    if (!n->wait(ctx, MAIN_MENU_AT_THE_EQUIPMENT_DRAWN))
        return false;
    menu_equip_panel_set_colour_math(g, pf);
    n->note(ctx, MAIN_MENU_NOTE_EQUIPMENT_TESTED, row_before);
    if (!n->member_card(ctx))
        return false;
    menu_equip_panel_dim(g);
    return true;
}

/* ---- What the steps can run on (the guards') --------------------------------------------------- */

/* The panel's: room for its four uploads and its colours, and the panel
 * drawable. */
bool menu_equipment_panel_can_show(const GameState *g)
{
    return menu_upload_queue_takes(g, 4) && menu_palette_changes_have_room(g) && menu_equip_panel_can_be_shown(g);
}

bool menu_equip_screen_can_be_set_up(const GameState *g)
{
    return menu_palette_changes_have_room(g) && menu_member_row(g) < MENU_CURSOR_PLACES_COUNT
           && menu_window_list_drawable(g, SCREEN_WINDOWS);
}

/* The member list's slide as its start makes it: four lines a frame down
 * from where the rows rest, or up; the member coming in loadable, its row
 * drawable where the start put the drawing origin, and the rows' copies in
 * the buffers. */
enum { NEW_ROW_LIST = 0xBE0E };

bool menu_equip_member_slide_can_run(const GameState *g)
{
    int16_t step = (int16_t)menu_scroll_step(g);
    uint16_t line = menu_slide_line(g);
    if (!((step == SLIDE_STEP && line == SLIDE_DOWN_FROM) || (step == -SLIDE_STEP && line == SLIDE_UP_FROM)))
        return false;
    uint8_t member = (uint8_t)(menu_cursor_index(g) + menu_member_scroll(g));
    uint16_t new_row = (uint16_t)(menu_draw_origin(g) + ROW_TILES_FROM_ORIGIN);
    return menu_member_card_can_be_drawn_for(g, member) && menu_window_list_drawable(g, NEW_ROW_LIST)
           && new_row >= LIST_FRONT_LAYER && new_row < LIST_BACK_LAYER
           && menu_tile_area_can_be_copied(g, LIST_FRONT_LAYER, ROWS_PLACE, slide_place(g), ROWS_SIZE)
           && menu_tile_area_can_be_copied(g, LIST_BACK_LAYER, ROWS_PLACE, slide_place(g), ROWS_SIZE)
           && menu_tile_area_can_be_copied(g, LIST_FRONT_LAYER, slid_place(g), ROWS_PLACE, ROWS_SIZE)
           && menu_tile_area_can_be_copied(g, LIST_BACK_LAYER, slid_place(g), ROWS_PLACE, ROWS_SIZE);
}

/* The panel: an active member to bring the cursor onto, the rows from the
 * list's first as it will scroll, the card, its window, and the dimming's
 * colours from the palette's first. */
bool menu_equip_panel_can_be_shown(const GameState *g)
{
    uint8_t reachable = (uint8_t)menu_members_reachable(g), active = menu_active_members(g);
    if (reachable > MENU_MEMBERS_COUNT || active == 0 || active > ROWS_SHOWN || menu_zero(g) != 0)
        return false;
    uint8_t first = menu_member_scroll(g) < scroll_most(reachable) ? menu_member_scroll(g) : scroll_most(reachable);
    uint8_t row = menu_member_row(g) < active ? menu_member_row(g) : (uint8_t)(active - 1);
    return menu_member_rows_can_be_drawn_from(g, first) && menu_member_card_can_be_drawn_for(g, (uint8_t)(row + first))
           && menu_window_list_drawable(g, PANEL_WINDOW);
}

/* The set-up: room for its upload, and the screen drawable. */
bool menu_equip_can_set_up(const GameState *g) { return menu_upload_queue_takes(g, 1) && menu_equip_screen_can_be_set_up(g); }

/* Choosing a member: room for an upload, and the cursor movable. */
bool menu_equip_can_choose_a_member(const GameState *g) { return menu_upload_queue_takes(g, 1) && menu_cursor_can_move(g); }

/* Pointing at a member: the cursor on one of the list's rows, the rows'
 * upload. */
bool menu_equip_can_point_at_a_member(const GameState *g)
{
    return menu_cursor_index(g) < MENU_MEMBER_ROWS && menu_members_reachable(g) <= MENU_MEMBERS_COUNT
           && menu_member_scroll(g) < MENU_MEMBERS_COUNT && menu_upload_queue_takes(g, 1);
}

/* ---- The opening and the members ---------------------------------------------------------------- */

// $C2:A11A
void menu_equip_set_up(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC290FC, menu_set_items_aside(g));
    menu_equip_set_up_screen(g, pf);
    menu_equip_show_the_member_bar(g);
    menu_equip_show_screen(g);
}

// $C2:9C31
/* The screen's first step: a tick, on to choosing a member, and the screen
 * set up. */
bool menu_open_equipment_with(GameState *g, Platform *pf, const EquipmentSteps *n, void *ctx)
{
    (void)pf;
    if (!n->tick(ctx, EQUIPMENT_AT_THE_OPENING))
        return false;
    menu_equip_open(g);
    return n->ends(ctx, EQUIPMENT_SETS_UP_THE_SCREEN);
}

// $C2:9C39
/* A member chosen: a tick, the slots' window and the member's slots drawn, a
 * frame, a tick, and the list of what fits the slot made, drawn and queued;
 * going back leaves for the main menu, and a push points at another member. */
bool menu_choose_equipment_member_with(GameState *g, Platform *pf, const EquipmentSteps *n, void *ctx)
{
    n->read_cursor(ctx);
    MemberChoice c = menu_equip_choose_member(g);
    n->note(ctx, EQUIPMENT_NOTE_MEMBER_STEP_TESTED, 0);
    switch (c) {
    case MEMBER_LEFT_FOR_THE_MAIN_MENU: return n->ends(ctx, EQUIPMENT_GOES_BACK_TO_THE_MAIN_MENU);
    case MEMBER_POINTED_AT: return n->ends(ctx, EQUIPMENT_POINTS_AT_A_MEMBER);
    case MEMBER_CHOSEN: break;
    }
    if (!n->tick(ctx, EQUIPMENT_AT_THE_MEMBER_CHOSEN) || !n->open_the_slots_window(ctx))
        return false;
    TIMED_AS(g, 0xC2A321, menu_equip_show_slots_of_member(g, pf));
    if (!n->wait(ctx, EQUIPMENT_AT_THE_SLOTS_DRAWN) || !n->tick(ctx, EQUIPMENT_AT_THE_SLOTS_DRAWN))
        return false;
    TIMED_AS(g, 0xC28953, menu_list_what_fits(g));
    TIMED_AS(g, 0xC2A569, menu_equip_draw_list(g));
    menu_equip_queue_list_rows(g);
    return true;
}

// $C2:9D17
/* The cursor among the members: the list goes round or slides, the rows are
 * coloured, a tick where it points at another member, and on to the card. */
bool menu_point_at_equipment_member_with(GameState *g, Platform *pf, const EquipmentSteps *n, void *ctx)
{
    (void)pf;
    if (!menu_equip_member_list_scrolls(g))
        menu_equip_take_cursor_round_members(g);
    else if (menu_equip_push_slides_members(g) && !n->slide_members(ctx))
        return false;
    uint8_t pointed_before = menu_member_pointed(g);
    bool another = menu_equip_point_at_member(g);
    n->note(ctx, EQUIPMENT_NOTE_MEMBER_POINTED, pointed_before);
    if (another && !n->tick(ctx, EQUIPMENT_AT_THE_MEMBER_POINTED))
        return false;
    return n->ends(ctx, EQUIPMENT_SHOWS_THE_CARD);
}

/* ---- The opening and the members as C ----------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} EquipmentAsC;

static void read_cursor(void *ctx) { menu_read_the_cursor_for_a_step(((EquipmentAsC *)ctx)->game); }

static bool tick(void *ctx, EquipmentPlace at)
{
    (void)at;
    EquipmentAsC *c = ctx;
    menu_tick(c->game, c->pf);
    return true;
}

static bool only_on_the_machine(EquipmentAsC *c, uint32_t routine, const char *what)
{
    program_needs_the_machine(c->game, routine, what);
    return false;
}

static bool slide_members(void *ctx) { return only_on_the_machine(ctx, 0xC29D5B, "the equipment's member list slid"); }

static bool open_the_slots_window(void *ctx) { return only_on_the_machine(ctx, 0xC2A71E, "the equipment's slots window opened"); }

static bool wait(void *ctx, EquipmentPlace at)
{
    (void)at;
    EquipmentAsC *c = ctx;
    menu_task_waits(c->game, 1);
    return true;
}

static void note(void *ctx, EquipmentNote note, uint8_t value) { (void)ctx, (void)note, (void)value; }

static const EquipmentSteps as_c;

static bool not_taken(EquipmentAsC *c, uint32_t routine)
{
    return only_on_the_machine(c, routine, "an equipment screen the port doesn't take");
}

/* The steps' ends: the routine each goes on in, as C where its entry's
 * guard would take the state, timed as its own. */
static bool ends(void *ctx, EquipmentEnd end)
{
    EquipmentAsC *c = ctx;
    GameState *g = c->game;
    bool back = true;
    switch (end) {
    case EQUIPMENT_SETS_UP_THE_SCREEN:
        if (!menu_equip_can_set_up(g))
            return not_taken(c, 0xC2A11A);
        TIMED_AS(g, 0xC2A11A, menu_equip_set_up(g, c->pf));
        return true;
    case EQUIPMENT_POINTS_AT_A_MEMBER:
        if (!menu_equip_can_point_at_a_member(g))
            return not_taken(c, 0xC29D17);
        TIMED_AS(g, 0xC29D17, back = menu_point_at_equipment_member_with(g, c->pf, &as_c, c));
        return back;
    case EQUIPMENT_SHOWS_THE_CARD:
        if (!menu_member_card_can_be_drawn(g))
            return not_taken(c, 0xC2A22F);
        TIMED_AS(g, 0xC2A22F, menu_draw_member_card(g, c->pf));
        return true;
    case EQUIPMENT_GOES_BACK_TO_THE_MAIN_MENU: return menu_back_to_the_main_menu(g, c->pf);
    }
    return false;
}

static const EquipmentSteps as_c = { read_cursor, tick, slide_members, open_the_slots_window, wait, note, ends };

bool menu_open_equipment(GameState *g, Platform *pf)
{
    EquipmentAsC c = { g, pf };
    return menu_open_equipment_with(g, pf, &as_c, &c);
}

bool menu_choose_equipment_member(GameState *g, Platform *pf)
{
    EquipmentAsC c = { g, pf };
    if (!menu_equip_can_choose_a_member(g))
        return only_on_the_machine(&c, 0xC29C39, "an equipment screen the port doesn't take");
    return menu_choose_equipment_member_with(g, pf, &as_c, &c);
}
