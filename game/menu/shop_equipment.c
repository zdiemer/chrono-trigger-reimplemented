/* Changing the party's equipment from a shop: the shop's steps 6 to 8.
 *
 * From the shop's tabs, up or down takes the cursor down to the members
 * along the shop's bottom window, the ones the player can't choose among
 * dimmed. Choosing a member opens its equipment over the panel, its four
 * slots, the others dimmed; the shoulders switch to the member before or
 * after. Choosing a slot takes the cursor into the list of what fits it
 * (the equipment screen's own, menu/equipment.h), and each item the cursor
 * meets is tried on: the member's figures are worked out with it, each
 * coloured brighter where the item raises it and darker where it lowers
 * it, and the item described. Choosing an item puts it on, what comes off
 * going back to the party's items. Going back leaves the list for the
 * slots, the slots for the members, and the members for the tabs.
 *
 * What is tried on goes into the menus' status record of the member
 * (menu_status_record), the figures with it; the member is loaded from its
 * own record again when the trying is over. Each member's strip of the
 * bottom window (its icon and figures, two rows of four tiles) takes the
 * dimmed colour with its sprite's colours, or its own. */
#include "menu/shop.h"

#include "charge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/equipment.h"
#include "menu/figures.h"
#include "menu/items.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "menu/tiles.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "vars.h"

/* The shop's window lists: the members' window as the cursor comes down
 * to it, the tabs' as it goes back up; a member's equipment, and its
 * figures. */
enum { MEMBERS_WINDOW = 0xC2D5, TABS_WINDOW = 0xC2B0, EQUIPMENT_WINDOW = 0xC309, FIGURES_WINDOW = 0xC318 };

/* What the member has on is drawn in the equipment window; its slots are
 * marked there too. */
enum { EQUIPPED_AT = 0x3664, SLOTS_AT = 0x3624 };

/* ---- The members' strips ------------------------------------------------------------------------- */

/* A member's strip (shop.h): two rows of four tiles; the whole row's 28
 * tiles from the first's. A dimmed strip takes the dimmed palette, its
 * member's sprite the dimmed colours. */
enum { STRIPS_AT = SHOP_MEMBER_STRIPS_AT, STRIP_BYTES = SHOP_MEMBER_STRIP_BYTES, STRIP_ROWS = 2, STRIP_TILES = 4, ALL_STRIPS_TILES = 0x1C };
enum { BRIGHT = 0x00, DIMMED = 0x10 };

/* For the original's code and shadow mode: the strip's colouring takes its
 * member, tiles and colour from the menus' first cells. */
static void leave_strip_coloured(GameState *g, unsigned member, uint8_t tiles, uint8_t colour)
{
    set_menu_scratch_strip_member(g, (uint16_t)member);
    set_menu_scratch_strip_tiles(g, tiles);
    set_menu_scratch_strip_colour(g, colour);
}

// $C2:E058
static void colour_strip(GameState *g, unsigned member, uint8_t tiles, uint8_t colour)
{
    uint16_t at = (uint16_t)(STRIPS_AT + member * STRIP_BYTES);
    TIMED_AS(g, 0xC2EC93, menu_recolour_block(g, at, STRIP_ROWS, tiles, colour));
}

static void put_sprite_colours(GameState *g, uint8_t character, uint8_t colour)
{
    TIMED_AS(g, 0xFFF628, menu_put_character_colours(g, character, colour != BRIGHT));
}

/* For the original's code and shadow mode: where its loop over the members
 * left its cells. */
static void leave_members_coloured(GameState *g, unsigned members, uint8_t tiles, uint8_t colour)
{
    set_menu_shop_work(g, (uint16_t)members);
    set_menu_scratch_strip_tiles(g, tiles);
    set_menu_scratch_strip_colour(g, colour);
    set_menu_scratch_strips_colour_kept(g, colour);
    set_menu_scratch_colours_character(g, menu_shop_members(g, members - 1));
}

// $C2:E072
/* Every member the player can choose among in `colour`, but the one the
 * shop works on, which is bright; `tiles` of each strip. */
static void colour_the_members(GameState *g, uint8_t tiles, uint8_t colour)
{
    uint8_t working_on = (uint8_t)menu_screen_argument(g);
    uint8_t reachable = (uint8_t)menu_members_reachable(g);
    unsigned member = 0;
    do {
        uint8_t c = member == working_on ? BRIGHT : colour;
        colour_strip(g, member, tiles, c);
        put_sprite_colours(g, menu_shop_members(g, member), c);
        member++;
    } while (member < reachable);
    ORIGINAL_EFFECT(leave_members_coloured(g, member, tiles, colour));
}

/* ---- Down to the members ------------------------------------------------------------------------- */

enum { END_OF_THE_MEMBERS = 0x80 };

// $C2:E017
/* The cursor to the member it was last on, the tabs' cursor kept; the
 * members past those the player can choose among dimmed. */
void menu_shop_go_down_to_the_members(GameState *g, Platform *pf)
{
    set_menu_shop_tab_cursor(g, menu_cursor_index(g));
    menu_shop_go_to(g, SHOP_ON_THE_MEMBERS);
    set_menu_cursor_index(g, menu_shop_member_cursor(g));
    unsigned member = menu_members_reachable(g);
    bool any = false;
    while (!(menu_shop_members(g, member) & END_OF_THE_MEMBERS)) {
        put_sprite_colours(g, menu_shop_members(g, member), DIMMED);
        colour_strip(g, member, STRIP_TILES, DIMMED);
        ORIGINAL_EFFECT(leave_strip_coloured(g, member, STRIP_TILES, DIMMED));
        any = true;
        member = (uint8_t)(member + 1);
        if (member >= menu_shop_member_count(g))
            break;
    }
    (void)any;
    ORIGINAL_EFFECT(set_menu_scratch_strips_member(g, (uint16_t)member));
    ORIGINAL_EFFECT(set_menu_scratch_strip_tiles(g, STRIP_TILES));
    ORIGINAL_EFFECT(set_menu_scratch_strip_colour(g, DIMMED));
    menu_draw_windows(g, pf, MEMBERS_WINDOW);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

ShopMembersChoice menu_shop_choose_a_member(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE))
        return SHOP_MEMBER_CHOSEN;
    if (menu_asked(g, MENU_GO_BACK) || menu_pushed(g, MENU_PUSHED_UP) || menu_pushed(g, MENU_PUSHED_DOWN))
        return SHOP_MEMBERS_LEFT;
    return SHOP_MEMBER_POINTED_AT;
}

/* Back up to the tabs: every character's colours and every strip its own,
 * the member's cursor kept, the cursor on its tab, the tabs' held cursor
 * going on from where it waited. */
enum { TASK_WAITING = 2 };

// $C2:D6E6
void menu_shop_back_up_to_the_tabs(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2F5A7, menu_load_character_colours(g));
    colour_strip(g, 0, ALL_STRIPS_TILES, BRIGHT);
    ORIGINAL_EFFECT(leave_strip_coloured(g, 0, ALL_STRIPS_TILES, BRIGHT));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    set_menu_shop_member_cursor(g, menu_cursor_index(g));
    set_menu_cursor_index(g, menu_shop_tab_cursor(g));
    set_menu_tasks_state(g, MENU_HELD_CURSORS_TASK, TASK_WAITING);
    menu_shop_go_to(g, SHOP_ON_THE_TABS);
    menu_draw_windows(g, pf, TABS_WINDOW);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- A member's slots ---------------------------------------------------------------------------- */

/* The seven stats the shop's member window shows, their colours cleared. */
enum { FIGURES = MENU_SHOP_FIGURES_SHOWN };

// $C2:E0A5
/* The member on the cursor chosen: its figures uncoloured, the member
 * loaded and its equipment window drawn with what it has on. */
void menu_shop_choose_the_member(GameState *g, Platform *pf)
{
    for (unsigned figure = 0; figure < FIGURES; figure++)
        set_menu_figure_marks(g, figure, FIGURE_AS_IT_IS);
    ORIGINAL_EFFECT(charge_block_move(g, FIGURES - 2));
    menu_work_on_member(g, (uint8_t)(menu_cursor_index(g) - SHOP_FIRST_MEMBER));
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, menu_member_worked_on(g)));
    menu_draw_windows(g, pf, EQUIPMENT_WINDOW);
    TIMED_AS(g, 0xC2A6F0, menu_equip_draw_equipped(g, EQUIPPED_AT));
}

/* The equipment window is drawn on the back layer's first screen of lines,
 * which the layer's vertical scroll turns to (its high byte, a screen of
 * 256 lines a step: the shop's own windows are on the next) and back from
 * as the members come back. The window's line effects; its description
 * starts afresh on nothing. */
enum { SLOTS_LINE_EFFECTS = 0x55, BACK_LAYER = 1, LINES_A_SCREEN = 0x100, NOTHING = 0 };

static uint8_t back_layer_screen(const GameState *g) { return (uint8_t)(menu_scroll_y(g, BACK_LAYER) / LINES_A_SCREEN); }

static void turn_the_back_layer_to(GameState *g, uint8_t screen)
{
    set_menu_scroll_y(g, BACK_LAYER, (uint16_t)(menu_scroll_y(g, BACK_LAYER) % LINES_A_SCREEN + screen * LINES_A_SCREEN));
}

/* A frame later: the cursor on the first slot (the step marks the slots
 * and makes the slot's list), the effects, nothing described, and every
 * member dimmed but the one chosen. */
void menu_shop_open_the_members_slots(GameState *g)
{
    turn_the_back_layer_to(g, 0);
    set_menu_line_effects(g, SLOTS_LINE_EFFECTS);
    set_menu_description_stale(g, (uint8_t)(menu_description_stale(g) - 1));
    TIMED_AS(g, 0xC2F2F3, menu_describe_item(g, NOTHING));
    colour_the_members(g, STRIP_TILES, DIMMED);
}

// $C2:E150
/* The slot on the cursor marked. */
void menu_shop_mark_the_slot(GameState *g)
{
    set_menu_equip_slot(g, menu_cursor_index(g));
    TIMED_AS(g, 0xC2A3E2, menu_equip_mark_slots_at(g, SLOTS_AT));
}

/* The list's scroll bar's place, beside the equipment window. */
enum { LIST_BAR_AT = 0x2A68 };

void menu_shop_place_the_lists_bar(GameState *g) { set_menu_scroll_bar_place(g, LIST_BAR_AT); }

/* The shoulders switch to the member before or after, round the members
 * the player can choose among; the member the shop works on otherwise. */
uint8_t menu_shop_member_by_the_shoulders(const GameState *g)
{
    uint8_t member = (uint8_t)menu_screen_argument(g), reachable = (uint8_t)menu_members_reachable(g);
    if (menu_asked(g, MENU_PAGE_ONE_WAY))
        return (uint8_t)(member + 1) < reachable ? (uint8_t)(member + 1) : 0;
    if (menu_asked(g, MENU_PAGE_THE_OTHER))
        return member != 0 ? (uint8_t)(member - 1) : (uint8_t)(reachable - 1);
    return member;
}

// $C2:E108
/* Another member (the step has the shop work on it): loaded, its
 * equipment window, every member dimmed but it, and the slot on the cursor
 * marked. */
void menu_shop_switch_member(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, menu_member_worked_on(g)));
    menu_draw_windows(g, pf, EQUIPMENT_WINDOW);
    TIMED_AS(g, 0xC2A6F0, menu_equip_draw_equipped(g, EQUIPPED_AT));
    colour_the_members(g, STRIP_TILES, DIMMED);
    TIMED_AS(g, 0xC2E150, menu_shop_mark_the_slot(g));
}

/* Choosing a slot takes the cursor back where it was as the frame began
 * (the step does), whatever pushed it. */
ShopSlotsChoice menu_shop_choose_a_slot(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE))
        return menu_equip_choices(g) != 0 ? SHOP_SLOT_CHOSEN : SHOP_SLOT_WITH_NOTHING_TO_FIT;
    if (menu_asked(g, MENU_GO_BACK))
        return SHOP_SLOTS_LEFT;
    return SHOP_SLOT_POINTED_AT;
}

/* A slot chosen: the cursor to the list's first row. */
enum { FIRST_LIST_PLACE = 4 };

void menu_shop_into_the_list(GameState *g)
{
    set_menu_cursor_index(g, FIRST_LIST_PLACE);
    menu_shop_go_to(g, SHOP_ON_WHAT_FITS);
}

/* Back to the members: the shop's own cursor places again, every member's
 * strip and colours its own, the cursor on the member, the members' line
 * effects, the back layer turned to the shop's windows, and the list's bar
 * and held cursor stopped. */
enum { MEMBERS_LINE_EFFECTS = 0x2F, LIST_TASKS = 1u << MENU_SCROLL_BAR_TASK | 1u << MENU_HELD_CURSORS_TASK };

// $C2:D745
void menu_shop_back_to_the_members(GameState *g)
{
    menu_put_cursor_places_back(g);
    colour_the_members(g, STRIP_TILES, BRIGHT);
    set_menu_cursor_index(g, (uint8_t)(menu_screen_argument(g) + SHOP_FIRST_MEMBER));
    menu_shop_go_to(g, SHOP_ON_THE_MEMBERS);
    set_menu_line_effects(g, MEMBERS_LINE_EFFECTS);
    turn_the_back_layer_to(g, (uint8_t)(back_layer_screen(g) + 1));
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, LIST_TASKS));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- Trying on ----------------------------------------------------------------------------------- */

// $C2:E18A
/* The member's figures as the status record has them, worked out again with
 * what it has on there: each one's colour by how it moves, the record
 * taking the new ones; its strip's attack and defence, its figures'
 * window, and the item's description. */
void menu_shop_compare_figures(GameState *g, Platform *pf)
{
    menu_mark_the_figures_tried_on(g, FIGURES_IN_THE_SHOP);
    menu_shop_panel_member_shown(g, menu_member_worked_on(g));
    menu_draw_windows(g, pf, FIGURES_WINDOW);
    TIMED_AS(g, 0xC2F2F3, menu_describe_item(g, menu_item_operand(g)));
}


// $C2:E162
/* The item on the cursor tried on in the slot. */
void menu_shop_try_on(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, menu_member_worked_on(g)));
    menu_try_on(g, menu_list_items(g, (uint8_t)(menu_equip_list_top(g) + menu_cursor_index(g) - FIRST_LIST_PLACE)));
    TIMED_AS(g, 0xC2E18A, menu_shop_compare_figures(g, pf));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_STRIPS);
}

/* The item tried on put on (menu_equip_put_on_the_chosen_item, which the
 * step runs before the sound of it): the member's figures written back,
 * its equipment drawn again. */
void menu_shop_after_putting_on(GameState *g)
{
    TIMED_AS(g, 0xC29137, menu_note_member_figures(g));
    TIMED_AS(g, 0xC2A6F0, menu_equip_draw_equipped(g, EQUIPPED_AT));
}

/* Out of the list: the member loaded again, nothing tried on, the figures
 * compared with nothing new, and the cursor back on the slot. */
void menu_shop_out_of_the_list(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, menu_member_worked_on(g)));
    set_menu_item_operand(g, NOTHING);
    TIMED_AS(g, 0xC2E18A, menu_shop_compare_figures(g, pf));
    menu_shop_go_to(g, SHOP_ON_A_MEMBERS_SLOTS);
    set_menu_cursor_index(g, menu_equip_slot(g));
}
