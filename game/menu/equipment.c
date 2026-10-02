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
 * the parts here: the slots' and the list's drawing, the party's inventory,
 * the lists' paging and sliding, the frames the screen waits. */
#include "menu/equipment.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/members.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

static void queue_upload(GameState *g, uint16_t record)
{
    TIMED_AS(g, 0xC28385, menu_queue_upload(g, menu_upload_record(g, record)));
}

static void draw_list(GameState *g, Platform *pf, uint16_t list)
{
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, list));
}

/* The frame's buttons and pushes, as the cursor's reading leaves them
 * (menu/cursor.c). */
enum { CHOOSE = 0x80, GO_BACK = 0x40, NEXT_MEMBER = 0x02, PREVIOUS_MEMBER = 0x01 };
enum { PUSHED_RIGHT = 0x01, PUSHED_LEFT = 0x02, PUSHED_DOWN = 0x04, PUSHED_PAST_AN_EDGE = 0x80 };

static bool chose(const GameState *g) { return (menu_cursor_buttons(g) & CHOOSE) != 0; }
static bool went_back(const GameState *g) { return (menu_cursor_buttons(g) & GO_BACK) != 0; }
static bool pushed(const GameState *g, uint8_t way) { return (menu_cursor_moves(g) & way) != 0; }

/* The screen's steps. */
enum { STEP_MEMBER = 1, STEP_SLOT = 2 };

static void next_step(GameState *g) { set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1)); }

/* The list of what fits is drawn for a slot and a member; forgetting which
 * has it drawn again. */
enum { NOTHING_SHOWN = 0xFF };

static void forget_the_list_shown(GameState *g) { set_menu_equip_shown(g, NOTHING_SHOWN); }

/* The member the screen works on, in its argument's low byte. */
static uint8_t member_worked_on(const GameState *g) { return (uint8_t)menu_screen_argument(g); }
static void work_on_member(GameState *g, uint8_t member)
{
    set_menu_screen_argument(g, (uint16_t)((menu_screen_argument(g) & 0xFF00) | member));
}

void equip_queue_list_rows(GameState *g) { queue_upload(g, EQUIP_LIST_ROWS_UPLOAD); }

/* ---- Opening the screen ------------------------------------------------------------------------ */

/* The screen's windows; the member list's hidden rows (the half of its front
 * layer's buffer below the view, where a slide copies rows to) cleared; its
 * four colours twice over into palette row 7. */
enum { SCREEN_WINDOWS = 0xBDCD, HIDDEN_ROWS_AT = 0x3600, HIDDEN_ROWS_BYTES = 0x800 };
enum { SCREEN_COLOURS = 4, FIRST_SCREEN_COLOUR = 0x70, SCREEN_LINE_EFFECTS = 0x65, UPLOAD_BG2_ROWS = 0xFBFF };

// $C2:9C31
/* The screen's first step leads on to choosing a member, once it is set up. */
void equip_open(GameState *g) { next_step(g); }

// $C2:A11A
void equip_set_up_screen(GameState *g, Platform *pf)
{
    wram_fill8(g, 0x7E0000u | HIDDEN_ROWS_AT, HIDDEN_ROWS_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, HIDDEN_ROWS_BYTES));
    for (unsigned copy = 0; copy < 2; copy++)
        for (unsigned i = 0; i < SCREEN_COLOURS; i++)
            set_menu_palette(g, FIRST_SCREEN_COLOUR + copy * SCREEN_COLOURS + i,
                             menu_rom_colour(g, MENU_ROM_EQUIPMENT_COLOURS, i));
    draw_list(g, pf, SCREEN_WINDOWS);
    set_menu_cursor_index(g, menu_member_row(g));
    work_on_member(g, menu_member_on_the_cursor(g));
    TIMED_AS(g, 0xC2E923, menu_clear_sprites_but_cursor(g));
}

void equip_show_screen(GameState *g)
{
    set_menu_line_effects(g, SCREEN_LINE_EFFECTS);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    queue_upload(g, UPLOAD_BG2_ROWS);
}

/* ---- Choosing a member ------------------------------------------------------------------------- */

// $C2:9C39
MemberChoice equip_choose_member(GameState *g)
{
    if (pushed(g, PUSHED_RIGHT) || chose(g)) {
        forget_the_list_shown(g);
        next_step(g);
        return MEMBER_CHOSEN;
    }
    return went_back(g) ? MEMBER_LEFT_FOR_THE_MAIN_MENU : MEMBER_POINTED_AT;
}

/* The list scrolls where there are more members than its rows; then a push
 * that left its rows slides it. */
bool equip_member_list_scrolls(const GameState *g) { return menu_member_scroll_most(g) != 0; }
bool equip_push_slides_members(const GameState *g) { return pushed(g, PUSHED_PAST_AN_EDGE); }

/* With no more members than the rows, a push past the top goes round to the
 * last member and one past the bottom to the first; a cursor that moved onto
 * a row past the last member goes back to the first. */
void equip_take_cursor_round_members(GameState *g)
{
    uint8_t at = menu_cursor_index(g), reachable = (uint8_t)menu_members_reachable(g);
    if (at >= reachable)
        set_menu_cursor_index(g, 0);
    else if (pushed(g, PUSHED_PAST_AN_EDGE))
        set_menu_cursor_index(g, at != 0 ? 0 : (uint8_t)(reachable - 1));
}

/* A list at rest: pointing clears the step's low byte, the step's size and
 * sign as the item lists' slide frame ($C2:EB1F) and its rows' move read
 * them; a member slide's high byte stays. */
static void bring_list_to_rest(GameState *g) { set_menu_scroll_step(g, (uint16_t)(menu_scroll_step(g) & 0xFF00)); }

// $C2:9D17
bool equip_point_at_member(GameState *g)
{
    bring_list_to_rest(g);
    TIMED_AS(g, 0xC29E76, menu_colour_member_rows(g));
    set_menu_member_row(g, menu_cursor_index(g));
    uint8_t member = menu_member_on_the_cursor(g);
    work_on_member(g, member);
    bool another = member != menu_member_pointed(g);
    set_menu_member_pointed(g, member);
    return another;
}

/* The slide: the member coming in is drawn where the original's start of the
 * slide put the drawing origin (beside the list, out of view), and dimmed;
 * the list's rows are copied out of view on both layers, next to it, from
 * the list's place to where the slide starts (menu_slide_copy); then twelve
 * frames move the view of them four lines a frame, the line every band of
 * the rows' per-line table shows. At the end the three rows in view are
 * copied back to the list's place, a member further along, and the view put
 * back. */
enum { SLIDE_FRAMES = 12, LIST_FRONT_LAYER = 0x2E00, LIST_BACK_LAYER = 0x3E00, ROW_TILES_FROM_ORIGIN = 0x84 };
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

// $C2:9DAF
void equip_start_member_slide(GameState *g, Platform *pf)
{
    work_on_member(g, (uint8_t)(menu_cursor_index(g) + menu_member_scroll(g)));
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member_worked_on(g)));
    TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, pf));
    uint16_t new_row = (uint16_t)(menu_draw_origin(g) + ROW_TILES_FROM_ORIGIN);
    TIMED_AS(g, 0xC29EB0, menu_colour_member_row(g, new_row, true));
    ORIGINAL_EFFECT(menu_leave_member_row_coloured(g, new_row, true));
    copy_rows_on(g, LIST_FRONT_LAYER, ROWS_PLACE, slide_place(g));
    copy_rows_on(g, LIST_BACK_LAYER, ROWS_PLACE, slide_place(g));
    queue_upload(g, UPLOAD_BG1_SLID);
    queue_upload(g, UPLOAD_BG2_SLID);
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

bool equip_slide_members_a_frame(GameState *g)
{
    int16_t step = (int16_t)menu_scroll_step(g);
    set_menu_scroll_moved(g, (uint16_t)(menu_scroll_moved(g) + step));
    uint16_t line = (uint16_t)(menu_slide_line(g) + step);
    set_menu_slide_line(g, line);
    move_thumb(g, sliding_up(g));
    show_rows_from(g, line);
    queue_upload(g, UPLOAD_ROW_LINES);
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

/* The slide's end asks the input for a fast repeat on the next frame, so a
 * push held through it carries straight on: the request is any count but 0,
 * and the original counts it down from where it is. */
static void ask_for_a_fast_repeat(GameState *g) { set_menu_fast_repeat(g, (uint8_t)(menu_fast_repeat(g) - 1)); }

void equip_finish_member_slide(GameState *g)
{
    copy_rows_on(g, LIST_BACK_LAYER, slid_place(g), ROWS_PLACE);
    copy_rows_on(g, LIST_FRONT_LAYER, slid_place(g), ROWS_PLACE);
    show_rows_from(g, LINE_AT_REST);
    queue_upload(g, UPLOAD_ROW_LINES);
    ask_for_a_fast_repeat(g);
}

/* ---- Choosing a slot --------------------------------------------------------------------------- */

// $C2:9C70
SlotChoice equip_choose_slot(GameState *g)
{
    if (pushed(g, PUSHED_LEFT) || (!chose(g) && went_back(g))) {
        set_menu_screen_step(g, STEP_MEMBER);
        return SLOT_LEFT_FOR_THE_MEMBERS;
    }
    if (!chose(g))
        return SLOT_POINTED_AT;
    set_menu_cursor_index(g, menu_equip_slot(g));
    return menu_equip_choices(g) != 0 ? SLOT_CHOSEN : SLOT_REFUSED;
}

void equip_start_choosing_an_item(GameState *g) { next_step(g); }

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

/* A member before the first wraps to the last; the original takes "before
 * the first" as a count past 127. */
static uint8_t member_before(uint8_t member, uint8_t reachable)
{
    uint8_t before = (uint8_t)(member - 1);
    if (before < 0x80)
        return before;
    uint8_t last = (uint8_t)(reachable - 1);
    return last < reachable ? last : 0;
}

// $C2:A00B
bool equip_switch_member(GameState *g)
{
    uint8_t asked = menu_cursor_buttons(g) & (NEXT_MEMBER | PREVIOUS_MEMBER);
    if (!asked)
        return false;
    uint8_t was = member_worked_on(g), reachable = (uint8_t)menu_members_reachable(g);
    uint8_t member = (asked & NEXT_MEMBER) ? member_after(was, reachable) : member_before(was, reachable);
    work_on_member(g, member);
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
bool equip_keep_slot(GameState *g)
{
    uint8_t slot = menu_cursor_index(g);
    bool moved = slot != menu_equip_kept_slot(g);
    set_menu_equip_kept_slot(g, slot);
    return moved;
}

/* The slots' line effects, and the card's layer (the slots' marks are on it)
 * queued. */
enum { SLOTS_LINE_EFFECTS = 0x4D, UPLOAD_BG1_CARD = 0xFBE3 };

void equip_show_slots(GameState *g)
{
    set_menu_line_effects(g, SLOTS_LINE_EFFECTS);
    queue_upload(g, UPLOAD_BG1_CARD);
}

/* The list is drawn for a slot and member, recorded as their sum. */
bool equip_list_is_for_another_slot(GameState *g)
{
    uint8_t shown = (uint8_t)(menu_equip_slot(g) + member_worked_on(g));
    bool another = shown != menu_equip_shown(g);
    set_menu_equip_shown(g, shown);
    return another;
}

/* ---- Choosing an item -------------------------------------------------------------------------- */

// $C2:9CA5
ItemChoice equip_choose_item(const GameState *g)
{
    if (chose(g))
        return ITEM_CHOSEN;
    return went_back(g) ? ITEM_LEFT_FOR_THE_SLOTS : ITEM_POINTED_AT;
}

/* The member's record, where the menus noted it as they loaded the member
 * (menu_member_record_at), and the slot's place in it. */
static uint8_t member_character(const GameState *g)
{
    return (uint8_t)((uint16_t)(menu_member_record_at(g) - RAM_ABS(RAM_CHARACTER_RECORDS)) / RAM_CHARACTER_RECORDS_STRIDE);
}

static uint8_t slot_place(const GameState *g) { return (uint8_t)menu_equip_record_place(g); }

// $C2:9CC1
void equip_name_what_comes_off(GameState *g)
{
    set_menu_item_operand_quantity(g, 1);
    set_menu_item_operand(g, character_records_record_byte(g, member_character(g), slot_place(g)));
}

void equip_put_on_the_chosen_item(GameState *g)
{
    uint8_t item = menu_status_record_bytes_byte(g, slot_place(g));
    set_menu_item_operand(g, item);
    set_character_records_record_byte(g, member_character(g), slot_place(g), item);
}

/* A weapon changed on one of the first six characters clears the story's
 * byte for the character (event memory from $B4). The original takes the
 * character from the list's place the cursor's row is, without the list's
 * scroll. */
enum { WEAPON_SLOT = 0, CHARACTERS_WITH_A_WEAPON_BYTE = 6, WEAPON_BYTES = 0xB4 };

// $C2:9CFC
void equip_forget_weapon(GameState *g)
{
    if (menu_equip_slot(g) != WEAPON_SLOT)
        return;
    uint8_t character = menu_members(g, menu_member_row(g));
    if (character < CHARACTERS_WITH_A_WEAPON_BYTE)
        set_event_memory_byte(g, WEAPON_BYTES + character, 0);
}

void equip_return_to_the_kept_slot(GameState *g) { set_menu_cursor_index(g, menu_equip_kept_slot(g)); }

void equip_back_to_the_slots(GameState *g)
{
    forget_the_list_shown(g);
    set_menu_screen_step(g, STEP_SLOT);
}

/* The list's rows start at cursor place 4; a row slides in where the push
 * leaves a row of the list past the last shown or before the first. */
enum { FIRST_LIST_PLACE = 4 };

// $C2:9F05
void equip_note_list_top_for_paging(GameState *g) { set_menu_paging_top(g, menu_equip_list_top(g)); }

/* The scroll bar's thumb for the list's first row: four lines a row,
 * through the multiplier ($C2:8BA6), which it leaves with the product. */
enum { THUMB_LINES_A_ROW = 4 };

static uint8_t thumb_at_row(GameState *g, uint8_t row)
{
    ORIGINAL_EFFECT(hw_set_multiplicand(g, row));
    ORIGINAL_EFFECT((void)hw_multiply_by(g, THUMB_LINES_A_ROW));
    return (uint8_t)(row * THUMB_LINES_A_ROW);
}

/* Whether the paging turned a page: it moved the first row a page on or
 * back. */
bool equip_list_paged(const GameState *g) { return menu_paging_top(g) != menu_equip_list_top(g); }

/* The paging moved the list's first row (and the thumb's place with it, as
 * the row) a page on or back. */
void equip_turn_list_page(GameState *g)
{
    set_menu_scroll_bar_thumb(g, thumb_at_row(g, menu_scroll_bar_thumb(g)));
    set_menu_equip_list_top(g, menu_paging_top(g));
}

/* A push past the list's rows (in a list that scrolls) slides a row in,
 * where there is one: not before the list's first item, nor past its last.
 * The row the cursor is on is noted for the slide either way. */
bool equip_list_slide_brings_a_row(GameState *g)
{
    if (menu_list_top(g) == 0 || !pushed(g, PUSHED_PAST_AN_EDGE))
        return false;
    uint8_t row = (uint8_t)(menu_cursor_index(g) - FIRST_LIST_PLACE);
    set_menu_item_operand(g, row);
    uint8_t item = (uint8_t)(row + menu_equip_list_top(g));
    return item != 0 && (uint8_t)(item + 1) < menu_equip_choices(g);
}

/* The slide moves the list's nine rows, and the per-line table its block
 * move takes the rows' lines from. */
enum { LIST_LINE_TABLE = 0x9303, LIST_LINE_TABLE_BYTES = 0x25, LIST_ROWS_SLID = 9 };

void equip_set_up_list_slide(GameState *g)
{
    set_menu_block_move_dest(g, LIST_LINE_TABLE);
    set_menu_block_move_size(g, LIST_LINE_TABLE_BYTES);
    set_menu_slide_rows(g, LIST_ROWS_SLID);
}

/* The list's first row a row on (or back), and the row coming in named for
 * its drawing: its item and how many of it the party has. */
void equip_bring_in_list_row(GameState *g)
{
    uint8_t top = menu_equip_list_top(g);
    top = pushed(g, PUSHED_DOWN) ? (uint8_t)(top + 1) : (uint8_t)(top - 1);
    set_menu_equip_list_top(g, top);
    uint8_t row = (uint8_t)(menu_item_operand(g) + top);
    set_menu_item_operand_count(g, menu_list_item_counts(g, row));
    set_menu_item_operand(g, menu_list_items(g, row));
}

/* A fast repeat (a push held down) skips the slide's frames. */
enum { REPEATING_FAST = 0x80 };

bool equip_start_list_slide_frames(GameState *g)
{
    if (menu_fast_repeat(g) & REPEATING_FAST)
        return false;
    set_menu_scroll_moved(g, 0);
    return true;
}

/* A frame of the list's slide: the rows moved a step, the thumb a line the
 * way the rows have moved. */
void equip_slide_list_a_frame(GameState *g)
{
    uint16_t moved = (uint16_t)(menu_scroll_moved(g) + menu_scroll_step(g));
    set_menu_scroll_moved(g, moved);
    move_thumb(g, (int16_t)moved < 0);
}

bool equip_list_slide_frames_left(GameState *g)
{
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

/* Once slid: the thumb at the new first row, the upload queue emptied (the
 * frames sent what it held), the block move set to bring the rows' new
 * lines in, the rows moved a row along and the list at rest. */
enum { LIST_LINES_FROM = 0x9710, LINE_EFFECTS_MOVED = 0x80 };

void equip_finish_list_slide(GameState *g)
{
    set_menu_scroll_bar_thumb(g, thumb_at_row(g, menu_equip_list_top(g)));
    set_menu_vram_queue_bytes(g, 0);
    set_menu_block_move_source(g, LIST_LINES_FROM);
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | LINE_EFFECTS_MOVED));
    TIMED_AS(g, 0xC29FDE, equip_move_list_rows(g));
    set_menu_scroll_step(g, 0);
    equip_queue_list_rows(g);
}

/* The list's rows are kept two tile rows apart in a buffer a row longer than
 * the nine it shows; the slide drew the row coming in in that last row. Down
 * the list, each row moves a row up and the new one comes in at the bottom
 * (it is already there); up the list, each moves a row down and the new one
 * goes in at the top. The way is the step's sign, read from its low byte. */
enum { LIST_ROW_BYTES = 0x80, LIST_BYTES = 0x400, STEP_UP = 0x80 };

static void move_list_bytes(GameState *g, unsigned from, unsigned to)
{
    set_menu_equip_list_rows_byte(g, to, menu_equip_list_rows_byte(g, from));
}

// $C2:9FDE
void equip_move_list_rows(GameState *g)
{
    if (!((uint8_t)menu_scroll_step(g) & STEP_UP)) {
        for (unsigned i = 0; i < LIST_BYTES; i++)
            move_list_bytes(g, LIST_ROW_BYTES + i, i);
    } else {
        for (unsigned i = LIST_BYTES - LIST_ROW_BYTES; i-- > 0;)
            move_list_bytes(g, i, LIST_ROW_BYTES + i);
        for (unsigned i = 0; i < LIST_ROW_BYTES; i++)
            move_list_bytes(g, LIST_BYTES + i, i);
    }
}

/* ---- The main menu's panel --------------------------------------------------------------------- */

/* With the main menu's cursor on the equipment, the member list and the card
 * are drawn beside the main menu, dimmed: the panel's window, the rows' per-
 * line tables at rest, the time played, the rows from the list's first, the
 * card for the cursor's member (the bridge runs it), the highlight's colours
 * and the dimmed ones. The list's scroll is brought within the reachable
 * members, and the cursor onto an active member; the menus' fourth task is
 * held a frame longer as the panel starts. */
enum { PANEL_WINDOW = 0xBD74, PANEL_TASK = 3, ROWS_SHOWN = MENU_MEMBER_ROWS, OTHER_LINES_AT_REST = 0xFFFF };
enum { UPLOAD_PANEL_LINES = 0xFC3E };

/* The furthest the list scrolls: the members past the three it shows. */
static uint8_t scroll_most(uint8_t reachable) { return reachable > ROWS_SHOWN ? (uint8_t)(reachable - ROWS_SHOWN) : 0; }

// $C2:A051
void equip_panel_lay_out_list(GameState *g, Platform *pf)
{
    set_menu_tasks_countdown(g, PANEL_TASK, (uint8_t)(menu_tasks_countdown(g, PANEL_TASK) + 1));
    set_menu_member_scroll_most(g, scroll_most((uint8_t)menu_members_reachable(g)));
    if (menu_member_scroll_most(g) < menu_member_scroll(g))
        set_menu_member_scroll(g, menu_member_scroll_most(g));
    uint8_t row = menu_member_row(g);
    if (row >= menu_active_members(g))
        row = (uint8_t)(menu_active_members(g) - 1);
    set_menu_cursor_index(g, row);
    set_menu_member_row(g, row);
    draw_list(g, pf, PANEL_WINDOW);
    show_rows_from(g, LINE_AT_REST);
    for (unsigned band = 0; band < MENU_ROW_OTHER_LINE_VALUES_COUNT; band++)
        set_menu_row_other_line_values(g, band, OTHER_LINES_AT_REST);
    queue_upload(g, UPLOAD_PANEL_LINES);
}

enum { TIME_AT = 0x3390, CLOCK_DIGITS = 0x0402 };

void equip_panel_draw_members(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2F28D, (void)menu_draw_time_played(g, TIME_AT, CLOCK_DIGITS));
    TIMED_AS(g, 0xC2A1B2, menu_draw_member_rows(g, pf));
}

/* The colour math table the panel's window list set up, built for the next
 * frame. */
void equip_panel_set_colour_math(GameState *g, Platform *pf) { TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf)); }

/* The highlight's four colours into palette row 2's second half, and the
 * screen's first 24 dimmed into row 4. */
enum { PANEL_LINE_EFFECTS = 0x64, HIGHLIGHT_COLOURS = 4, FIRST_HIGHLIGHT_COLOUR = 0x24, FIRST_DIMMED_COLOUR = 0x40 };

void equip_panel_dim(GameState *g)
{
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    for (unsigned i = 0; i < HIGHLIGHT_COLOURS; i++)
        set_menu_palette(g, FIRST_HIGHLIGHT_COLOUR + i, menu_rom_colour(g, MENU_ROM_PANEL_HIGHLIGHT_COLOURS, i));
    TIMED_AS(g, 0xC29E76, menu_colour_member_rows(g));
    TIMED_AS(g, 0xC2A0E7, equip_dim_colours(g, FIRST_DIMMED_COLOUR));
}

/* A colour dimmed: each of its five-bit channels halved. */
enum { DIMMED_COLOURS = 24, CHANNELS_HALVED = 0x3DEF };

// $C2:A0E7
void equip_dim_colours(GameState *g, unsigned first)
{
    for (unsigned i = 0; i < DIMMED_COLOURS; i++)
        set_menu_palette(g, first + i, (uint16_t)((menu_palette(g, i) >> 1) & CHANNELS_HALVED));
}

// $C2:A101
/* The main menu leaving the equipment: palette row 7's first half, where the
 * screen put its colours, given back the menus' own (the party's items come
 * back from where the screen set them aside, the original's, between), and
 * the main menu's line effects. */
enum { PANEL_COLOURS = 8 };

void equip_panel_colours_back(GameState *g)
{
    for (unsigned i = 0; i < PANEL_COLOURS; i++)
        set_menu_palette(g, FIRST_SCREEN_COLOUR + i, menu_rom_colour(g, MENU_ROM_ROW_7_COLOURS, i));
}

void equip_panel_line_effects_back(GameState *g) { set_menu_line_effects(g, PANEL_LINE_EFFECTS); }

/* ---- What the steps can run on (the guards') --------------------------------------------------- */

bool equip_screen_can_be_set_up(const GameState *g)
{
    return menu_palette_changes_have_room(g) && menu_member_row(g) < MENU_CURSOR_PLACES_COUNT
           && menu_window_list_drawable(g, SCREEN_WINDOWS);
}

/* The member list's slide as its start makes it: four lines a frame down
 * from where the rows rest, or up; the member coming in loadable, its row
 * drawable where the start put the drawing origin, and the rows' copies in
 * the buffers. */
enum { SLIDE_DOWN_FROM = 0x60EF, SLIDE_UP_FROM = 0x611F, SLIDE_STEP = 4, NEW_ROW_LIST = 0xBE0E };

bool equip_member_slide_can_run(const GameState *g)
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
bool equip_panel_can_be_shown(const GameState *g)
{
    uint8_t reachable = (uint8_t)menu_members_reachable(g), active = menu_active_members(g);
    if (reachable > MENU_MEMBERS_COUNT || active == 0 || active > ROWS_SHOWN || menu_zero(g) != 0)
        return false;
    uint8_t first = menu_member_scroll(g) < scroll_most(reachable) ? menu_member_scroll(g) : scroll_most(reachable);
    uint8_t row = menu_member_row(g) < active ? menu_member_row(g) : (uint8_t)(active - 1);
    return menu_member_rows_can_be_drawn_from(g, first) && menu_member_card_can_be_drawn_for(g, (uint8_t)(row + first))
           && menu_window_list_drawable(g, PANEL_WINDOW);
}
