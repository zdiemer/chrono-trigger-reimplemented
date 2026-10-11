/* A shop on the original's machine: its registered entries, the bridges
 * that run what game/menu/shop.c decides between the calls that are still
 * the original's, and their guards.
 *
 * Each step reads the cursor and asks shop.c what the frame's input does.
 * The sounds are siblings, run as the original's JSRs reach them
 * (menu_run): each saves P as it starts and calls the sound driver, so the
 * flags the original has at a sound are on the stack there, where shadow
 * mode compares them, and A's high byte goes to the driver. The panel, a
 * list's frame and the making of the list for sale are entries of their
 * own, run the same way, since they call out with the P they save under
 * the calls; so are the equipping's steps' own (a member chosen, its slots,
 * the shoulders' switch), which wait for frames, and the equipment screen's
 * list of what fits and its move along. What is still the original's: a
 * list's cursor's move, its frame, and the frame's wait. The shop's way out
 * and its fade in are jumps the bridges stop at.
 *
 * Before each of those calls a bridge leaves the registers and flags the
 * original's own instructions left there, worked out from the state (the
 * leaves at the end of the file): scaffolding, which the native build
 * compiles out. After a call out the machine is the original's again, so a
 * leave covers only what the original did since. */
#include "menu/shop.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/equipment.h"
#include "menu/figures.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/members.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    YIELD = 0xC2821E,              /* the task waits for the next frame */
    MOVE_IN_LIST = 0xC2EA53,       /* the cursor kept within a list's rows */
    FRAME_THE_LIST = 0xC2EA81,     /* a scrolling list's frame laid out: Y places */
    LEAVE_THE_SHOP = 0xC282B2,     /* the menus' way out */
    FADE_IN = 0xC283B2,            /* the screen's fade in started */
    CURSOR_SOUND = 0xC2EAC2,
    REFUSAL_SOUND = 0xC2EACC,
    A_SOUND = 0xC2EABA,
    TAKE_THE_PANEL = 0xC2DD7C,
    FRAME_A_LIST = 0xC2DD40,
    LIST_FOR_SALE = 0xC2DECC,
    SET_UP_THE_SHOP = 0xC2DACA,
};

/* What they read (tools/dontcare_check.py checks each): nothing but the
 * page, the data bank and memory; the description A's low byte; a list's
 * frame Y. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_Y = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool call(Snes *s, uint32_t site, uint32_t routine) { return menu_call(s, site, routine, READS_NOTHING); }
static bool yield(Snes *s, uint32_t site) { return menu_call(s, site, YIELD, READS_NOTHING); }
static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, CURSOR_SOUND, menu_play_cursor_sound); }

/* The sound a deal makes, loaded into A as the original's call does (the
 * load's flags are the P the sound saves). */
enum { DEAL_SOUND = 0x55 };

static bool play_sound(Snes *s, uint32_t site, uint8_t sound)
{
    m_set_a8(s, sound);
    ORIGINAL_EFFECT(menu_leave_load(s, sound));
    return menu_run(s, site, A_SOUND, menu_play_sound);
}


/* The goods' count as the original loads it for the list's frame (the
 * original's), which takes it in Y: a word, with the byte after it. */
static uint16_t goods_offered_as_loaded(const GameState *g)
{
    return (uint16_t)(menu_shop_goods_offered(g) | menu_shop_after_the_counts(g) << 8);
}

/* The description of nothing (item 0, the menus' zero), as the shop opens
 * and comes back to its tabs. */
static void describe_nothing(GameState *g) { TIMED_AS(g, 0xC2F2F3, menu_describe_item(g, (uint8_t)menu_zero(g))); }

/* The cursor read as C, leaving what the original's reading leaves. */
static void read_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
}

static void leave_a_low(Snes *s, uint8_t v);
static void leave_nz(Snes *s, uint8_t loaded);
static void leave_queued(Snes *s, uint16_t record);
static void leave_buttons_tested(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_cursor_compared_with_the_start(Snes *s);
static void leave_tab_chosen(Snes *s);
static void leave_step_set(Snes *s, uint8_t step);
static void leave_room_refused(Snes *s, uint8_t held);
static void leave_gold_refused(Snes *s, uint8_t held);
static void leave_offer_made(Snes *s, uint8_t limit, bool buying, uint8_t held);
static void leave_sale_refused(Snes *s, bool for_want_of_room);
static void leave_place_compared(Snes *s, uint8_t shown_before);
static void leave_back_from_the_list(Snes *s);
static void leave_listed(Snes *s, uint8_t c_before);
static void note_figures_left(GameState *g, uint16_t *left);
static void leave_panel_member(Snes *s, bool usable, uint8_t character, uint16_t figures_left);
static void leave_quantity_compared(Snes *s, uint8_t before);
static void leave_gold_word(Snes *s);
static void leave_frame_set(Snes *s, uint16_t rows);
static void leave_word_loaded(Snes *s, uint16_t word);
static void leave_pushed_up_or_down(Snes *s);
static void leave_goods_taken(Snes *s);
static void leave_place_added(Snes *s, uint8_t row, uint8_t top);
static void leave_slide_tested(Snes *s, bool selling);
static void leave_total_added(Snes *s, uint32_t total_before);

/* ---- Opening ------------------------------------------------------------------------------------- */

// $C2:D52B
/* The shop set up, the held cursor on the tabs, and on to the fade in. */
void menu_open_shop(Snes *s)
{
    if (!menu_run(s, 0xC2D52B, SET_UP_THE_SHOP, menu_set_up_shop))
        return;
    menu_shop_hold_a_cursor_on_the_tabs(state_of(s));
    m_stop_at(s, FADE_IN, READS_NOTHING);
}

// $C2:DACA
/* The shop's windows and goods, the description of nothing (the
 * original's), the colours and the members, the panel (its own), and the
 * prices and layers. */
void menu_set_up_shop(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_shop_open(g, pf);
    menu_set_widths(s, false, false);
    ORIGINAL_EFFECT(leave_goods_taken(s));
    TIMED_AS(g, 0xC2E012, describe_nothing(g));
    menu_shop_open_colours(g);
    menu_shop_open_members(g);
    menu_set_widths(s, true, false);
    ORIGINAL_EFFECT(leave_nz(s, 0x13));
    if (!menu_run(s, 0xC2DB6A, TAKE_THE_PANEL, menu_show_shop_item_panel))
        return;
    menu_shop_open_last(g, pf);
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The panel ----------------------------------------------------------------------------------- */

// $C2:DD7C
/* The panel's counts, then each member: loaded and posed, its figures
 * with the item worked out where it could use it, and drawn. */
void menu_show_shop_item_panel(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    menu_shop_start_panel(g);
    menu_set_widths(s, true, false);
    unsigned m = 0;
    bool usable = false;
    uint8_t character = 0;
    uint16_t figures_left = 0;
    for (; menu_shop_panel_member(g, m, &usable); m++) {
        character = menu_status_record_character(g);
        if (usable) {
            TIMED_AS(g, 0xC2916E, menu_work_out_figures(g));
            ORIGINAL_EFFECT(note_figures_left(g, &figures_left));
        }
        menu_shop_panel_member_shown(g, m);
    }
    ORIGINAL_EFFECT(menu_shop_leave_panel_done(g, m));
    ORIGINAL_EFFECT(leave_panel_member(s, m == 0 || usable, character, figures_left));
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The tabs ------------------------------------------------------------------------------------ */

// $C2:D546
/* A tab chosen opens its list, with a tick; going back leaves the shop; up
 * or down goes to the members (the original's); a tick where the cursor
 * moved. */
void menu_choose_shop_tab(Snes *s)
{
    GameState *g = state_of(s);
    menu_shop_on_the_tabs_frame(g);
    ORIGINAL_EFFECT(leave_nz(s, 1));
    read_cursor(s);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (menu_shop_choose_tab(g)) {
    case TAB_CHOSEN:
        menu_shop_leave_the_tabs(g);
        ORIGINAL_EFFECT(leave_tab_chosen(s));
        if (!tick(s, 0xC2D570))
            return;
        if (menu_shop_buying_chosen(g)) {
            menu_shop_go_to(g, SHOP_BUYING);
            ORIGINAL_EFFECT(leave_step_set(s, SHOP_BUYING));
            NATIVE_TIME_AS(s, 0xC2DC7B, menu_open_goods_list(s));
        } else {
            menu_shop_go_to(g, SHOP_SELLING);
            ORIGINAL_EFFECT(leave_step_set(s, SHOP_SELLING));
            NATIVE_TIME_AS(s, 0xC2DE98, menu_open_for_sale_list(s));
        }
        return;
    case SHOP_LEFT: m_stop_at(s, LEAVE_THE_SHOP, READS_NOTHING); return;
    case TO_THE_MEMBERS:
        menu_shop_stop_holding(g);
        TIMED_AS(g, 0xC2E017, menu_shop_go_down_to_the_members(g, m_platform(s)));
        ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER));
        break;
    case TAB_POINTED_AT: break;
    }
    bool moved = menu_cursor_index(g) != menu_cursor_frame_start(g);
    ORIGINAL_EFFECT(leave_cursor_compared(s));
    if (moved && !tick(s, 0xC2D566))
        return;
    m_rts(s);
}

/* ---- The lists ----------------------------------------------------------------------------------- */

// $C2:DD40
/* A list's frame (the original's): eight rows shown from the first, the
 * scroll bar's track a whole list's, the list's count in Y. */
enum { LIST_BAR_TRACK = 0x5400 };

void menu_frame_shop_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_shop_frame_list(g);
    ORIGINAL_EFFECT(leave_frame_set(s, LIST_BAR_TRACK));
    hw_set_dividend(g, LIST_BAR_TRACK);
    menu_set_widths(s, false, m_x8(s));
    if (!menu_call(s, 0xC2DD51, FRAME_THE_LIST, READS_Y))
        return;
    menu_restore_p(s);
    m_rts(s);
}

static bool frame_the_list(Snes *s, uint32_t site, uint16_t count)
{
    s->cpu.y = count;
    return menu_run(s, site, FRAME_A_LIST, menu_frame_shop_list);
}

// $C2:DC7B
/* The goods' list: its window and rows, its frame (its own), the goods'
 * counts and the bar. */
void menu_open_goods_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_shop_open_buying(g, m_platform(s));
    uint16_t count = goods_offered_as_loaded(g);
    ORIGINAL_EFFECT(leave_word_loaded(s, count));
    if (!frame_the_list(s, 0xC2DCAD, count))
        return;
    menu_shop_open_buying_last(g);
    ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:DECC
/* The list for sale made, framed (its own), and settled. */
void menu_list_for_sale(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    bool c_before = m_flag(s, CPU_FLAG_C);
    menu_shop_list_for_sale(g);
    ORIGINAL_EFFECT(leave_listed(s, c_before));
    if (!frame_the_list(s, 0xC2DF10, menu_shop_for_sale_listed(g)))
        return;
    menu_shop_settle_for_sale(g);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:DE98
/* The list for sale: its window, the list made (its own), its rows and the
 * bar. */
void menu_open_for_sale_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_shop_open_selling(g, m_platform(s));
    ORIGINAL_EFFECT(leave_nz(s, menu_shop_sell_row(g)));
    if (!menu_run(s, 0xC2DEB7, LIST_FOR_SALE, menu_list_for_sale))
        return;
    menu_shop_open_selling_last(g);
    menu_restore_p(s);
    menu_shop_queue_the_list(g);
    ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER));
    m_rts(s);
}

/* The row the cursor is on taken and described (the original's); the
 * panel where the place is another (its own); a tick where the cursor
 * moved; the list queued. */
static void take_the_row(Snes *s, bool selling)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, m_x8(s));
    uint8_t top = selling ? menu_shop_sell_top(g) : menu_shop_buy_top(g);
    menu_shop_take_the_row(g, selling);
    ORIGINAL_EFFECT(leave_place_added(s, menu_cursor_index(g), top));
    TIMED_AS(g, 0xC2F2F3, menu_describe_item(g, menu_item_operand(g)));
    uint8_t shown_before = menu_shop_place_shown(g);
    bool another = menu_shop_another_place(g);
    ORIGINAL_EFFECT(leave_place_compared(s, shown_before));
    if (another && !menu_run(s, selling ? 0xC2D8FC : 0xC2D819, TAKE_THE_PANEL, menu_show_shop_item_panel))
        return;
    bool moved = menu_cursor_index(g) != menu_cursor_frame_start(g);
    ORIGINAL_EFFECT(leave_cursor_compared_with_the_start(s));
    if (moved && !tick(s, selling ? 0xC2D905 : 0xC2D822))
        return;
    menu_shop_queue_the_list(g);
    ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER));
    m_rts(s);
}

/* The list slid a row: started (the original's), the row coming in drawn,
 * a tick, the frames (each the original's, waiting for the next), the rows
 * at rest, and the row taken. */
static void slide_the_list(Snes *s, bool selling)
{
    GameState *g = state_of(s);
    menu_shop_set_up_list_slide(g);
    ORIGINAL_EFFECT(leave_slide_tested(s, selling));
    TIMED_AS(g, 0xC2EB9B, menu_start_list_slide(g));
    menu_shop_bring_in_a_row(g, selling);
    ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER));
    ORIGINAL_EFFECT(leave_nz(s, 0x80));
    if (!tick(s, selling ? 0xC2D95B : 0xC2D878))
        return;
    menu_set_widths(s, false, false);
    if (menu_shop_list_slide_takes_frames(g))
        while (menu_shop_slide_list_a_frame(g)) {
            TIMED_AS(g, 0xC2EB1F, menu_slide_list_a_frame(g));
            if (!yield(s, selling ? 0xC2D977 : 0xC2D894))
                return;
        }
    menu_shop_finish_list_slide(g);
    take_the_row(s, selling);
}

// $C2:D7CF, $C2:D8B2
/* Pointing along a list: a page turned, the rows drawn again and a
 * frame's wait; or the cursor moved within the rows
 * (the original's), a push past them sliding the list. Then the row. */
static void point_along(Snes *s, bool selling)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, m_x8(s));
    uint8_t top = menu_shop_list_top(g, selling), to = top;
    TIMED_AS(g, 0xC293BF, to = menu_turn_page(g, top));
    if (to != top) {
        menu_shop_turn_page(g, selling, to);
        if (!yield(s, selling ? 0xC2D8CA : 0xC2D7E7))
            return;
        take_the_row(s, selling);
        return;
    }
    if (!call(s, selling ? 0xC2D8CF : 0xC2D7EC, MOVE_IN_LIST))
        return;
    if (menu_shop_list_slide_brings_a_row(g, selling)) {
        slide_the_list(s, selling);
        return;
    }
    take_the_row(s, selling);
}

/* Going back from a list to the tabs: the panel for nothing (its own), a
 * tick, the tabs, and on to the description of nothing (the original's
 * jump). */
static void back_to_the_tabs(Snes *s, bool selling)
{
    GameState *g = state_of(s);
    menu_shop_back_from_the_list(g, selling);
    ORIGINAL_EFFECT(leave_back_from_the_list(s));
    if (!menu_run(s, 0xC2D5E0, TAKE_THE_PANEL, menu_show_shop_item_panel) || !tick(s, 0xC2D5E3))
        return;
    menu_shop_back_on_the_tabs(g, m_platform(s));
    TIMED_AS(g, 0xC2E012, describe_nothing(g));
    m_rts(s);
}

// $C2:D58B
/* Choosing a row of the goods opens the quantity window, with a tick, where
 * the party can take one; else the refusal's sound (the original jumps to
 * it). */
void menu_choose_goods(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (menu_shop_choose_on_the_list(g)) {
    case SHOP_ROW_CHOSEN: {
        uint8_t held = menu_shop_held_and_worn(g);
        if (!menu_shop_room_to_buy(g)) {
            ORIGINAL_EFFECT(leave_room_refused(s, held));
            menu_play_refusal_sound(s);
            return;
        }
        uint8_t room = menu_item_operand_count(g);
        if (!menu_shop_gold_buys_some(g)) {
            ORIGINAL_EFFECT(leave_gold_refused(s, held));
            menu_play_refusal_sound(s);
            return;
        }
        ORIGINAL_EFFECT(leave_offer_made(s, room, true, held));
        if (!tick(s, 0xC2D5C6))
            return;
        menu_shop_open_quantity(g, m_platform(s), SHOP_BUYING_HOW_MANY);
        m_rts(s);
        return;
    }
    case SHOP_LIST_LEFT: back_to_the_tabs(s, false); return;
    case SHOP_ROW_POINTED_AT: point_along(s, false); return;
    }
}

// $C2:D645
/* Choosing a row for sale opens the quantity window, with a tick, where the
 * party has some and room in its gold; else the refusal's sound. */
void menu_choose_for_sale(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (menu_shop_choose_on_the_list(g)) {
    case SHOP_ROW_CHOSEN: {
        if (!menu_shop_some_to_sell(g)) {
            ORIGINAL_EFFECT(leave_sale_refused(s, true));
            menu_play_refusal_sound(s);
            return;
        }
        uint8_t held = menu_item_operand_count(g);
        if (!menu_shop_room_for_the_gold(g)) {
            ORIGINAL_EFFECT(leave_sale_refused(s, false));
            menu_play_refusal_sound(s);
            return;
        }
        ORIGINAL_EFFECT(leave_offer_made(s, held, false, 0));
        if (!tick(s, 0xC2D676))
            return;
        menu_shop_open_quantity(g, m_platform(s), SHOP_SELLING_HOW_MANY);
        m_rts(s);
        return;
    }
    case SHOP_LIST_LEFT: back_to_the_tabs(s, true); return;
    case SHOP_ROW_POINTED_AT: point_along(s, true); return;
    }
}

/* ---- The quantity -------------------------------------------------------------------------------- */

/* A push changes the quantity, with a tick where it changed. */
enum { UPLOAD_QUANTITY = 0xFBEA };
static void change_the_quantity(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    uint8_t before = menu_item_operand_quantity(g);
    uint32_t total_before = menu_shop_total(g) & 0xFFFFFF;
    bool changed = menu_shop_change_quantity(g, m_platform(s));
    if (!menu_shop_quantity_pushed(g)) {
        menu_restore_p(s);
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(leave_queued(s, UPLOAD_QUANTITY));
    ORIGINAL_EFFECT(leave_quantity_compared(s, before));
    if (changed)
        ORIGINAL_EFFECT(leave_total_added(s, total_before));
    if (changed && !tick(s, 0xC2DA30))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* The inventory's routines run with 8-bit index registers, which leaves
 * Y's high byte clear. */
static void leave_inventory_changed(Snes *s) { s->cpu.y &= 0x00FF; }

// $C2:D618
/* The quantity bought: the goods added to the party's items, the gold
 * paid, the goods counted again, the deal's sound. Bought or left, a tick,
 * and back on the list. */
void menu_choose_how_many_to_buy(Snes *s)
{
    GameState *g = state_of(s);
    menu_shop_hold_the_quantity(g);
    read_cursor(s);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (menu_shop_choose_quantity(g)) {
    case QUANTITY_TAKEN:
        menu_shop_bought(g);
        ORIGINAL_EFFECT(leave_inventory_changed(s));
        ORIGINAL_EFFECT(leave_gold_word(s));
        if (!play_sound(s, 0xC2D635, DEAL_SOUND))
            return;
        break;
    case QUANTITY_LEFT: break;
    case QUANTITY_CHANGED: change_the_quantity(s); return;
    }
    if (!tick(s, 0xC2D638))
        return;
    menu_shop_back_to_buying(g);
    m_rts(s);
}

// $C2:D690
/* The quantity sold: the items taken from the party's, the gold received,
 * the deal's sound. Sold or left, a tick, and back on the list, made
 * again. */
void menu_choose_how_many_to_sell(Snes *s)
{
    GameState *g = state_of(s);
    menu_shop_hold_the_quantity(g);
    read_cursor(s);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (menu_shop_choose_quantity(g)) {
    case QUANTITY_TAKEN:
        menu_shop_sold(g);
        ORIGINAL_EFFECT(leave_inventory_changed(s));
        ORIGINAL_EFFECT(leave_gold_word(s));
        if (!play_sound(s, 0xC2D6AA, DEAL_SOUND))
            return;
        break;
    case QUANTITY_LEFT: break;
    case QUANTITY_CHANGED: change_the_quantity(s); return;
    }
    menu_shop_restart_the_bars_blink(g);
    if (!tick(s, 0xC2D6B0))
        return;
    menu_shop_back_to_selling(g);
    ORIGINAL_EFFECT(leave_nz(s, SHOP_SELLING));
    if (!menu_run(s, 0xC2D6BD, LIST_FOR_SALE, menu_list_for_sale))
        return;
    menu_shop_draw_for_sale(g);
    m_rts(s);
}

/* ---- The equipping from the shop ---------------------------------------------------------------- */

/* The equipment screen's list of what fits the slot, and its move along,
 * which the shop's equipping shares. */
enum { MAKE_THE_LIST = 0xC2A463, POINT_ALONG_THE_LIST = 0xC29F05 };

/* The equip's sound. */
enum { EQUIP_SOUND = 0xB5 };

static bool cursor_moved(const GameState *g) { return menu_cursor_index(g) != menu_cursor_frame_start(g); }

// $C2:D6C3
/* On the members: choosing one opens its slots, with a tick; going back, or
 * up or down, goes back to the tabs, with a tick; a tick where the cursor
 * moved. */
void menu_choose_shop_member(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    ShopMembersChoice c = menu_shop_choose_a_member(g);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (c) {
    case SHOP_MEMBER_CHOSEN:
        if (!tick(s, 0xC2D6DD) || !menu_run(s, 0xC2D6E0, 0xC2E0A5, menu_open_shop_member))
            return;
        menu_shop_go_to(g, SHOP_ON_A_MEMBERS_SLOTS);
        break;
    case SHOP_MEMBERS_LEFT:
        ORIGINAL_EFFECT(leave_pushed_up_or_down(s));
        if (!tick(s, 0xC2D6E6))
            return;
        menu_shop_back_up_to_the_tabs(g, m_platform(s));
        ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER));
        break;
    case SHOP_MEMBER_POINTED_AT: {
        bool moved = cursor_moved(g);
        ORIGINAL_EFFECT(leave_cursor_compared(s));
        if (moved && !tick(s, 0xC2D6D9))
            return;
        break;
    }
    }
    m_rts(s);
}

// $C2:E0A5
/* A member chosen: its equipment window, a frame, and its slots. */
void menu_open_shop_member(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, false);
    menu_shop_choose_the_member(g, m_platform(s));
    if (!yield(s, 0xC2E0CF))
        return;
    set_menu_cursor_index(g, 0);
    if (!menu_run(s, 0xC2E0D4, 0xC2E0F1, menu_show_shop_member_slots))
        return;
    menu_shop_open_the_members_slots(g);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:E0F1
/* The slot on the cursor marked, the list's bar placed, the list of what
 * fits it made, and the strips' rows queued. */
void menu_show_shop_member_slots(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    TIMED_AS(g, 0xC2E150, menu_shop_mark_the_slot(g));
    menu_shop_place_the_lists_bar(g);
    ORIGINAL_EFFECT(leave_word_loaded(s, menu_scroll_bar_place(g)));
    if (!menu_run(s, 0xC2E0FD, MAKE_THE_LIST, menu_make_equipment_list))
        return;
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_STRIPS);
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER_STRIPS));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:E108
/* The shoulders switch to another member: a tick, the member, and a frame.
 * The tick saves the flags of the original's compare of the two members. */
void menu_switch_shop_member(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    uint8_t member = menu_shop_member_by_the_shoulders(g), was = menu_member_worked_on(g);
    if (member != was) {
        menu_work_on_member(g, member);
        menu_set_widths(s, true, false);
        ORIGINAL_EFFECT(menu_leave_compare(s, member, was));
        if (!tick(s, 0xC2E12E))
            return;
        menu_shop_switch_member(g, m_platform(s));
        if (!yield(s, 0xC2E14B))
            return;
    }
    menu_restore_p(s);
    m_rts(s);
}

// $C2:D715
/* On the member's slots: choosing one with something to fit it goes into
 * its list, with a tick (the refusal's sound where nothing fits, which the
 * original jumps to); going back returns to the members, with a tick;
 * otherwise the shoulders' switch, the slot on the cursor and its list,
 * and a tick where the cursor moved. */
void menu_choose_shop_slot(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    ShopSlotsChoice c = menu_shop_choose_a_slot(g);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (c) {
    case SHOP_SLOT_WITH_NOTHING_TO_FIT:
        set_menu_cursor_index(g, menu_cursor_frame_start(g));
        ORIGINAL_EFFECT(leave_nz(s, 0));
        m_stop_at_own(s, REFUSAL_SOUND, READS_A_HIGH);
        return;
    case SHOP_SLOT_CHOSEN:
        set_menu_cursor_index(g, menu_cursor_frame_start(g));
        ORIGINAL_EFFECT(leave_nz(s, menu_equip_choices(g)));
        if (!tick(s, 0xC2D73B))
            return;
        menu_shop_into_the_list(g);
        break;
    case SHOP_SLOTS_LEFT:
        if (!tick(s, 0xC2D745))
            return;
        menu_shop_back_to_the_members(g);
        break;
    case SHOP_SLOT_POINTED_AT:
        if (!menu_run(s, 0xC2D71F, 0xC2E108, menu_switch_shop_member)
            || !menu_run(s, 0xC2D722, 0xC2E0F1, menu_show_shop_member_slots))
            return;
        bool moved = cursor_moved(g);
        ORIGINAL_EFFECT(leave_cursor_compared(s));
        if (moved && !tick(s, 0xC2D72B))
            return;
        break;
    }
    m_rts(s);
}

// $C2:D778
/* On the list of what fits: choosing an item puts it on, with its sound;
 * then, or going back, a tick and out of the list. Otherwise the cursor's
 * move along the list (the equipment screen's), the item on it tried on,
 * and a tick where the cursor moved. */
void menu_choose_shop_equipment(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    read_cursor(s);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    if (!menu_asked(g, MENU_CHOOSE) && !menu_asked(g, MENU_GO_BACK)) {
        if (!menu_run_any_widths(s, 0xC2D782, POINT_ALONG_THE_LIST, menu_point_along_equipment_list))
            return;
        TIMED_AS(g, 0xC2E162, menu_shop_try_on(g, pf));
        ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER_STRIPS));
        bool moved = cursor_moved(g);
        ORIGINAL_EFFECT(leave_cursor_compared(s));
        if (moved && !tick(s, 0xC2D78E))
            return;
        m_rts(s);
        return;
    }
    if (menu_asked(g, MENU_CHOOSE)) {
        menu_equip_put_on_the_chosen_item(g);
        if (!play_sound(s, 0xC2D7AF, EQUIP_SOUND))
            return;
        menu_shop_after_putting_on(g);
    }
    if (!tick(s, 0xC2D7BB))
        return;
    menu_shop_out_of_the_list(g, pf);
    m_rts(s);
}

// $C2:E001
/* The quantity window's frame drawn, and its rows queued. */
enum { QUANTITY_WINDOW = 0xC2DE };

void menu_frame_shop_quantity(Snes *s)
{
    GameState *g = state_of(s);
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, m_platform(s), QUANTITY_WINDOW));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_LOWER_HALF);
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER_LOWER_HALF));
    m_rts(s);
}

// $C2:E012
void menu_describe_nothing(Snes *s)
{
    describe_nothing(state_of(s));
    m_rts(s);
}

// $C2:E017
void menu_go_down_to_shop_members(Snes *s)
{
    menu_shop_go_down_to_the_members(state_of(s), m_platform(s));
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER));
    m_rts(s);
}

// $C2:E162
void menu_try_on_shop_equipment(Snes *s)
{
    menu_shop_try_on(state_of(s), m_platform(s));
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER_STRIPS));
    m_rts(s);
}

// $C2:E18A
void menu_compare_shop_figures(Snes *s)
{
    menu_shop_compare_figures(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:E150
void menu_mark_shop_slot(Snes *s)
{
    menu_shop_mark_the_slot(state_of(s));
    m_rts(s);
}

/* ---- What the original's instructions leave (scaffolding) ---------------------------------------- */

static void leave_a_low(Snes *s, uint8_t v) { s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | v); }

static void leave_nz(Snes *s, uint8_t loaded) { menu_leave_load(s, loaded); }

static void leave_queued(Snes *s, uint16_t record) { menu_leave_queued_upload(s, record); }

/* The buttons tested for choosing and going back, with the accumulator the
 * pad's test left before it. */
static void leave_buttons_tested(Snes *s) { menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s))); }

static void leave_cursor_compared(Snes *s)
{
    const GameState *g = state_of(s);
    leave_a_low(s, menu_cursor_frame_start(g));
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}

/* The same compared the other way round. */
static void leave_cursor_compared_with_the_start(Snes *s)
{
    const GameState *g = state_of(s);
    leave_a_low(s, menu_cursor_index(g));
    menu_leave_compare(s, menu_cursor_index(g), menu_cursor_frame_start(g));
}

/* A tab chosen: its test's overflow, the place forgotten loaded. */
static void leave_tab_chosen(Snes *s)
{
    menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s)));
    leave_a_low(s, 0xFF);
    leave_nz(s, 0xFF);
}

/* The tab told from the cursor's place less the buying tab's (which can't
 * borrow), then the list's step set. */
static void leave_step_set(Snes *s, uint8_t step)
{
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_V, false);
    leave_a_low(s, step);
    leave_nz(s, step);
}

/* No room under ninety-nine: the subtraction that found it borrowed. */

static void leave_room_subtracted(Snes *s, uint8_t sum)
{
    enum { NINETY_EIGHT = 0x62 };
    uint8_t room = (uint8_t)(NINETY_EIGHT - sum);
    m_set_flag(s, CPU_FLAG_V, ((NINETY_EIGHT ^ sum) & (NINETY_EIGHT ^ room) & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_C, sum <= NINETY_EIGHT);
    leave_a_low(s, room);
    leave_nz(s, room);
}

static void leave_room_refused(Snes *s, uint8_t held) { leave_room_subtracted(s, held); }

/* The division's last step leaves its remainder's word in A where it took
 * nothing away, else the word above it. */
static void leave_division_word(Snes *s)
{
    const GameState *g = state_of(s);
    uint32_t q = menu_shop_quotient(g), r = menu_shop_remainder(g);
    s->cpu.a = (uint16_t)((q & 1) ? r >> 16 : r);
}

/* Gold for none: the quotient's low byte loaded, after its high bytes. */
static void leave_gold_refused(Snes *s, uint8_t held)
{
    leave_room_subtracted(s, held);
    leave_division_word(s);
    leave_a_low(s, 0);
    leave_nz(s, 0);
}

static void leave_sale_refused(Snes *s, bool for_want_of_room)
{
    const GameState *g = state_of(s);
    if (for_want_of_room) {
        uint8_t which = menu_item_operand(g) == 0 ? 0 : menu_item_operand_count(g);
        leave_a_low(s, which);
        leave_nz(s, which);
        return;
    }
    leave_division_word(s);
    leave_a_low(s, 0);
    leave_nz(s, 0);
}

/* The offer made: the quotient's high bytes found some, or its low byte
 * compared with the limit it had. Buying, the room's subtraction left the
 * carry and the overflow before. */
static void leave_offer_made(Snes *s, uint8_t limit, bool buying, uint8_t held)
{
    const GameState *g = state_of(s);
    if (buying)
        leave_room_subtracted(s, held);
    uint32_t q = menu_shop_quotient(g) & 0xFFFFFF;
    leave_division_word(s);
    if (q >> 8) {
        uint8_t high = (uint8_t)((q >> 8) | (q >> 16));
        leave_a_low(s, high);
        leave_nz(s, high);
        return;
    }
    leave_a_low(s, (uint8_t)q);
    menu_leave_compare(s, (uint8_t)q, limit);
}

static void leave_place_compared(Snes *s, uint8_t shown_before)
{
    uint8_t place = (uint8_t)menu_item_list_place(state_of(s));
    leave_a_low(s, place);
    menu_leave_compare(s, place, shown_before);
}

/* Back from a list: its tab's place loaded, the test for going back's
 * overflow set. */
static void leave_back_from_the_list(Snes *s)
{
    leave_a_low(s, menu_cursor_index(state_of(s)));
    leave_nz(s, menu_cursor_index(state_of(s)));
    m_set_flag(s, CPU_FLAG_V, true);
}

/* The list for sale made: its end's zero loaded, the carry of the last
 * item's test against the key items. */
static void leave_listed(Snes *s, uint8_t c_before)
{
    const GameState *g = state_of(s);
    enum { KEY_ITEMS = 0xF2 };
    bool carry = c_before;
    for (unsigned place = 0; place < MENU_INVENTORY_PLACES; place++) {
        uint8_t item = menu_inventory_item_at(g, place);
        if (item != 0)
            carry = item >= KEY_ITEMS;
    }
    m_set_flag(s, CPU_FLAG_C, carry);
    s->cpu.y = menu_shop_for_sale_listed(g);
    leave_a_low(s, 0);
    leave_nz(s, 0);
}

/* The panel's last member: A's high byte where the figures' working out
 * left it (its attack's sum's, before the figures' drawing divides again),
 * or the record's place's, as the character's record was found. */
static void note_figures_left(GameState *g, uint16_t *left) { *left = hw_dividend(g); }

static void leave_panel_member(Snes *s, bool usable, uint8_t character, uint16_t figures_left)
{
    uint16_t high = usable ? figures_left : (uint16_t)(character * 0x40);
    s->cpu.a = (uint16_t)((high & 0xFF00) | (s->cpu.a & 0xFF));
    leave_a_low(s, 0xFF);
}

static void leave_quantity_compared(Snes *s, uint8_t before)
{
    leave_a_low(s, before);
    menu_leave_compare(s, before, menu_item_operand_quantity(state_of(s)));
}

/* The gold's cap leaves its low word's high byte in A. */
static void leave_gold_word(Snes *s)
{
    const GameState *g = state_of(s);
    s->cpu.a = (uint16_t)((party_gold_low(g) & 0xFF00) | (s->cpu.a & 0xFF));
}

static void leave_frame_set(Snes *s, uint16_t track) { s->cpu.a = track; }

/* The goods taken: a block move's count loaded, the shop's number's
 * doubling's carry clear. */
static void leave_goods_taken(Snes *s)
{
    leave_nz(s, 0x1F);
    m_set_flag(s, CPU_FLAG_C, false);
}

/* An 8-bit addition's flags. */
static void leave_added(Snes *s, uint8_t a, uint8_t b, bool carry_in)
{
    unsigned sum = (unsigned)a + b + carry_in;
    uint8_t r = (uint8_t)sum;
    m_set_flag(s, CPU_FLAG_C, sum > 0xFF);
    m_set_flag(s, CPU_FLAG_V, ((a ^ r) & (b ^ r) & 0x80) != 0);
    leave_nz(s, r);
}

/* The row's place: the cursor's row added to the list's top; then its item
 * loaded, from a cleared accumulator. */
static void leave_place_added(Snes *s, uint8_t row, uint8_t top)
{
    leave_added(s, row, top, false);
    uint8_t item = menu_item_operand(state_of(s));
    s->cpu.a = item;
    leave_nz(s, item);
}

/* The slide's test: the place before the cursor's (its row added to the
 * top, less one) compared with the count's last but one, which it is short
 * of; then the slide's rows loaded. */
static void leave_slide_tested(Snes *s, bool selling)
{
    const GameState *g = state_of(s);
    uint8_t top = selling ? menu_shop_sell_top(g) : menu_shop_buy_top(g);
    leave_added(s, menu_cursor_index(g), top, false);
    m_set_flag(s, CPU_FLAG_C, false);
    leave_nz(s, 9);
}

/* The quantity's step priced: the total's high byte added to the price's,
 * with the low word's carry. */
static void leave_total_added(Snes *s, uint32_t total_before)
{
    const GameState *g = state_of(s);
    uint32_t product = menu_shop_product(g);
    bool carry = (total_before & 0xFFFF) + (product & 0xFFFF) > 0xFFFF;
    bool c_after = m_flag(s, CPU_FLAG_C);
    bool n_after = m_flag(s, CPU_FLAG_N), z_after = m_flag(s, CPU_FLAG_Z);
    leave_added(s, (uint8_t)(total_before >> 16), (uint8_t)(product >> 16), carry);
    m_set_flag(s, CPU_FLAG_C, c_after);
    m_set_flag(s, CPU_FLAG_N, n_after);
    m_set_flag(s, CPU_FLAG_Z, z_after);
}

/* Going back up from the members by a push up or down: the push's bits
 * taken from the moves, after the buttons' test. */
static void leave_pushed_up_or_down(Snes *s)
{
    const GameState *g = state_of(s);
    if (menu_asked(g, MENU_GO_BACK))
        return;
    uint8_t up_or_down = (uint8_t)(menu_cursor_moves(g) & (MENU_PUSHED_UP | MENU_PUSHED_DOWN));
    leave_a_low(s, up_or_down);
    leave_nz(s, up_or_down);
}

static void leave_word_loaded(Snes *s, uint16_t word)
{
    m_set_flag(s, CPU_FLAG_N, (word & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_Z, word == 0);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

static bool on_the_menus_page(Snes *s) { return menu_on_page_work_ram(s); }

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

static bool zero_is_zero(const GameState *g) { return menu_zero(g) == 0; }

static bool opens(const GameState *g) { return zero_is_zero(g) && menu_shop_can_open(g); }
bool menu_shop_opens(Snes *s) { return on_the_menus_page(s) && menu_sounds_on_page(s) && peek(s, opens); }
bool menu_shop_sets_up(Snes *s) { return on_the_menus_page(s) && peek(s, opens); }

static bool on_the_tabs(const GameState *g) { return zero_is_zero(g) && menu_shop_members_are_shown(g); }
bool menu_shop_tabs_step_runs(Snes *s) { return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, on_the_tabs); }

static bool on_a_list(const GameState *g, bool selling)
{
    return zero_is_zero(g) && menu_shop_on_a_list(g, selling) && menu_shop_gold_within_the_most(g)
           && menu_cursor_kept_among(g, 0, SHOP_LIST_ROWS, false);
}
static bool buying(const GameState *g) { return on_a_list(g, false); }
static bool selling(const GameState *g) { return on_a_list(g, true); }
bool menu_shop_buying_step_runs(Snes *s) { return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, buying); }
bool menu_shop_selling_step_runs(Snes *s) { return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, selling); }

static bool quantity(const GameState *g) { return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_shop_gold_within_the_most(g); }
bool menu_shop_quantity_step_runs(Snes *s) { return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, quantity); }

bool menu_shop_list_opens(Snes *s) { return on_the_menus_page(s) && peek(s, on_the_tabs); }
bool menu_shop_list_frames(Snes *s) { return on_the_menus_page(s); }
bool menu_shop_panel_shows(Snes *s) { return on_the_menus_page(s) && peek(s, on_the_tabs); }

/* The equipping works on a member the player can choose among, the shop's
 * members shown and its list of what fits made for the member's slot. */
static bool equipping(const GameState *g)
{
    return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_members_reachable(g) >= 1
           && menu_members_reachable(g) <= menu_shop_member_count(g) && menu_member_worked_on(g) < menu_members_reachable(g)
           && menu_member_record_is_a_characters(g);
}
static bool on_the_members(const GameState *g)
{
    return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_members_reachable(g) >= 1
           && menu_members_reachable(g) <= menu_shop_member_count(g)
           && menu_cursor_kept_among(g, SHOP_FIRST_MEMBER, (uint8_t)menu_members_reachable(g), false);
}
bool menu_shop_members_step_runs(Snes *s) { return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, on_the_members); }
bool menu_shop_slots_step_runs(Snes *s)
{
    return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, equipping) && menu_figures_are_workable(s);
}
bool menu_shop_equipping_runs(Snes *s) { return on_the_menus_page(s) && peek(s, equipping) && menu_figures_are_workable(s); }

/* The member chosen is the one under the cursor: the opening takes the member
 * worked on from it, so what was worked on before doesn't matter. */
static bool choosing_a_member(const GameState *g)
{
    return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_members_reachable(g) >= 1
           && menu_members_reachable(g) <= menu_shop_member_count(g) && menu_cursor_index(g) >= SHOP_FIRST_MEMBER
           && menu_cursor_index(g) < SHOP_FIRST_MEMBER + menu_members_reachable(g);
}
bool menu_shop_member_opens(Snes *s) { return on_the_menus_page(s) && peek(s, choosing_a_member) && menu_figures_are_workable(s); }
bool menu_shop_goes_down(Snes *s) { return on_the_menus_page(s) && peek(s, on_the_tabs); }
