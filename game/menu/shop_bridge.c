/* A shop on the original's machine: its registered entries, the steps that
 * run the shop's sequences (game/menu/shop_steps.c) between the calls that
 * are still the original's, and their guards.
 *
 * The sequences' ticks and sounds are siblings, run as the original's JSRs
 * reach them (menu_run): each saves P as it starts and calls the sound
 * driver, so the flags the original has at a sound are on the stack there,
 * where shadow mode compares them, and A's high byte goes to the driver. The
 * panel, the set-up and the equipping's own routines (a member chosen, its
 * slots, the shoulders' switch) are entries of their own, run the same way,
 * and so are the equipment screen's list of what fits and its move along. A
 * frame's wait waits in the menus' task switch, parked until its task goes
 * on (menu_wait_a_frame_from). The shop's way out, its fade in and the
 * refusal's sound are jumps the steps stop at, and the lists' openings are
 * the steps' last calls, run as C.
 *
 * Before each of those calls the sequence's note leaves the registers and
 * flags the original's own instructions left there, worked out from the
 * state (the leaves at the end of the file): scaffolding, which the native
 * build compiles out. After a call out the machine is the original's again,
 * so a leave covers only what the original did since. */
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
    LEAVE_THE_SHOP = 0xC282B2,     /* the menus' way out */
    FADE_IN = 0xC283B2,            /* the screen's fade in started */
    CURSOR_SOUND = 0xC2EAC2,
    REFUSAL_SOUND = 0xC2EACC,
    A_SOUND = 0xC2EABA,
};

/* What they read (tools/dontcare_check.py checks each): nothing but the
 * page, the data bank and memory; the description A's low byte. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static void leave_a_low(Snes *s, uint8_t v);
static void leave_nz(Snes *s, uint8_t loaded);
static void leave_queued(Snes *s, uint16_t record);
static void leave_buttons_tested(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_cursor_compared_as_words(Snes *s);
static void leave_cursor_compared_with_the_start(Snes *s);
static void leave_tab_chosen(Snes *s);
static void leave_step_set(Snes *s, uint8_t step);
static void leave_room_refused(Snes *s, uint8_t held);
static void leave_gold_refused(Snes *s, uint8_t held);
static void leave_offer_made(Snes *s, uint8_t limit, bool buying, uint8_t held);
static void leave_sale_refused(Snes *s, bool for_want_of_room);
static void leave_place_compared(Snes *s, uint8_t shown_before);
static void leave_back_from_the_list(Snes *s);
static void leave_panel_member(Snes *s, uint16_t left_by_the_last);
static void leave_quantity_compared(Snes *s, uint8_t before);
static void leave_gold_word(Snes *s);
static void leave_word_loaded(Snes *s, uint16_t word);
static void leave_pushed_up_or_down(Snes *s);
static void leave_goods_taken(Snes *s);
static void leave_place_added(Snes *s, bool selling);
static void leave_slide_tested(Snes *s, bool selling);
static void leave_total_added(Snes *s, uint32_t total_before);
static void leave_inventory_changed(Snes *s);

/* ---- What the sequences ask, on the machine ------------------------------------------------------ */

/* The original's call at each place the sequences tick or wait from. */
static const uint32_t TICKS_AT[] = {
    [SHOP_AT_THE_TAB_CHOSEN] = 0xC2D570,         [SHOP_AT_THE_TABS_POINTED] = 0xC2D566,
    [SHOP_AT_THE_ROW_TAKEN] = 0xC2D822,          [SHOP_AT_THE_ROW_TAKEN_FOR_SALE] = 0xC2D905,
    [SHOP_AT_THE_ROW_BROUGHT_IN] = 0xC2D878,     [SHOP_AT_THE_ROW_BROUGHT_IN_FOR_SALE] = 0xC2D95B,
    [SHOP_AT_THE_TABS_AGAIN] = 0xC2D5E3,         [SHOP_AT_THE_OFFER] = 0xC2D5C6,
    [SHOP_AT_THE_OFFER_FOR_SALE] = 0xC2D676,     [SHOP_AT_THE_QUANTITY_CLOSED] = 0xC2D638,
    [SHOP_AT_THE_QUANTITY_CLOSED_FOR_SALE] = 0xC2D6B0, [SHOP_AT_THE_QUANTITY_CHANGING] = 0xC2DA30,
    [SHOP_AT_THE_MEMBER_CHOSEN] = 0xC2D6DD,      [SHOP_AT_THE_MEMBERS_LEFT] = 0xC2D6E6,
    [SHOP_AT_THE_MEMBER_POINTED] = 0xC2D6D9,     [SHOP_AT_THE_MEMBER_SWITCHING] = 0xC2E12E,
    [SHOP_AT_THE_SLOT_CHOSEN] = 0xC2D73B,        [SHOP_AT_THE_SLOTS_LEFT] = 0xC2D745,
    [SHOP_AT_THE_SLOT_POINTED] = 0xC2D72B,       [SHOP_AT_THE_ITEM_POINTED] = 0xC2D78E,
    [SHOP_AT_THE_ITEM_LEFT] = 0xC2D7BB,
};
static const struct {
    uint32_t site;
} WAITS_AT[] = {
    [SHOP_AT_THE_LIST_SLIDING] = { 0xC2D894 },       [SHOP_AT_THE_LIST_SLIDING_FOR_SALE] = { 0xC2D977 },
    [SHOP_AT_THE_PAGE_TURNED] = { 0xC2D7E7 },        [SHOP_AT_THE_PAGE_TURNED_FOR_SALE] = { 0xC2D8CA },
    [SHOP_AT_THE_MEMBERS_WINDOW_DRAWN] = { 0xC2E0CF }, [SHOP_AT_THE_MEMBER_SWITCHED] = { 0xC2E14B },
};

static void read_cursor_on_the_machine(void *ctx) { menu_step_reads_the_cursor(ctx); }
static bool tick_on_the_machine(void *ctx, ShopPlace at) { return menu_run(ctx, TICKS_AT[at], CURSOR_SOUND, menu_play_cursor_sound); }
static bool wait_on_the_machine(void *ctx, ShopPlace at) { return menu_wait_a_frame_from(ctx, WAITS_AT[at].site); }

/* The widths the original's code sets at those places; the quantity's
 * change saves P as it starts and puts it back. */
static void registers_on_the_machine(void *ctx, ShopPlace at)
{
    Snes *s = ctx;
    switch (at) {
    case SHOP_AT_THE_SET_UP_DRAWN:
    case SHOP_AT_THE_LIST_SLIDING:
    case SHOP_AT_THE_LIST_SLIDING_FOR_SALE: menu_set_widths(s, false, false); break;
    case SHOP_AT_THE_POINTING: menu_set_widths(s, true, m_x8(s)); break;
    case SHOP_AT_THE_QUANTITY_CHANGING:
        menu_save_p(s);
        menu_set_widths(s, true, true);
        break;
    case SHOP_AT_THE_QUANTITY_CHANGED: menu_restore_p(s); break;
    default: menu_set_widths(s, true, false); break;
    }
}

/* The sibling routines the sequences reach, each run where the original
 * calls it from. */
static const struct {
    uint32_t site, routine;
    void (*entry)(Snes *s);
} RUNS[] = {
    [SHOP_SETS_UP] = { 0xC2D52B, 0xC2DACA, menu_set_up_shop },
    [SHOP_SHOWS_THE_PANEL_AS_IT_OPENS] = { 0xC2DB6A, 0xC2DD7C, menu_show_shop_item_panel },
    [SHOP_SHOWS_THE_PANEL_FOR_A_GOOD] = { 0xC2D819, 0xC2DD7C, menu_show_shop_item_panel },
    [SHOP_SHOWS_THE_PANEL_FOR_A_SALE] = { 0xC2D8FC, 0xC2DD7C, menu_show_shop_item_panel },
    [SHOP_SHOWS_THE_PANEL_FOR_NOTHING] = { 0xC2D5E0, 0xC2DD7C, menu_show_shop_item_panel },
    [SHOP_OPENS_THE_MEMBER] = { 0xC2D6E0, 0xC2E0A5, menu_open_shop_member },
    [SHOP_SHOWS_THE_SLOTS_AS_THEY_OPEN] = { 0xC2E0D4, 0xC2E0F1, menu_show_shop_member_slots },
    [SHOP_SHOWS_THE_SLOTS] = { 0xC2D722, 0xC2E0F1, menu_show_shop_member_slots },
    [SHOP_SWITCHES_THE_MEMBER] = { 0xC2D71F, 0xC2E108, menu_switch_shop_member },
    [SHOP_MAKES_THE_LIST] = { 0xC2E0FD, 0xC2A463, menu_make_equipment_list_entry },
    [SHOP_POINTS_ALONG_THE_LIST] = { 0xC2D782, 0xC29F05, menu_point_along_equipment_list_entry },
};

/* The sounds by their number, loaded into A as the original's call does
 * (the load's flags are the P the sound saves). */
enum { DEAL_SOUND = 0x55 };

static const struct {
    uint32_t site;
    uint8_t sound;
} SOUNDS[] = {
    [SHOP_PLAYS_THE_DEAL_SOUND] = { 0xC2D635, DEAL_SOUND },
    [SHOP_PLAYS_THE_DEAL_SOUND_FOR_SALE] = { 0xC2D6AA, DEAL_SOUND },
    [SHOP_PLAYS_THE_PUT_ON_SOUND] = { 0xC2D7AF, MENU_EQUIP_PUT_ON_SOUND },
};

static bool run_on_the_machine(void *ctx, ShopRoutine r)
{
    Snes *s = ctx;
    switch (r) {
    case SHOP_PLAYS_THE_DEAL_SOUND:
    case SHOP_PLAYS_THE_DEAL_SOUND_FOR_SALE:
    case SHOP_PLAYS_THE_PUT_ON_SOUND:
        m_set_a8(s, SOUNDS[r].sound);
        ORIGINAL_EFFECT(menu_leave_load(s, SOUNDS[r].sound));
        return menu_run(s, SOUNDS[r].site, A_SOUND, menu_play_sound);
    case SHOP_POINTS_ALONG_THE_LIST: return menu_run_any_widths(s, RUNS[r].site, RUNS[r].routine, RUNS[r].entry);
    default: return menu_run(s, RUNS[r].site, RUNS[r].routine, RUNS[r].entry);
    }
}

/* The quantity window's rows' upload. */
enum { UPLOAD_QUANTITY = 0xFBEA };

static void note_on_the_machine(void *ctx, ShopNote note, uint32_t value)
{
    Snes *s = ctx;
    (void)value;
    switch (note) {
    case SHOP_NOTE_GOODS_TAKEN: ORIGINAL_EFFECT(leave_goods_taken(s)); break;
    case SHOP_NOTE_LOADED: ORIGINAL_EFFECT(leave_nz(s, (uint8_t)value)); break;
    case SHOP_NOTE_PANEL_DONE: ORIGINAL_EFFECT(leave_panel_member(s, (uint16_t)value)); break;
    case SHOP_NOTE_BUTTONS_TESTED: ORIGINAL_EFFECT(leave_buttons_tested(s)); break;
    case SHOP_NOTE_TAB_CHOSEN: ORIGINAL_EFFECT(leave_tab_chosen(s)); break;
    case SHOP_NOTE_STEP_SET: ORIGINAL_EFFECT(leave_step_set(s, (uint8_t)value)); break;
    case SHOP_NOTE_FRONT_LAYER_QUEUED: ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER)); break;
    case SHOP_NOTE_STRIPS_QUEUED: ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER_STRIPS)); break;
    case SHOP_NOTE_CURSOR_COMPARED: ORIGINAL_EFFECT(leave_cursor_compared(s)); break;
    case SHOP_NOTE_CURSOR_COMPARED_AS_WORDS: ORIGINAL_EFFECT(leave_cursor_compared_as_words(s)); break;
    case SHOP_NOTE_CURSOR_COMPARED_WITH_THE_START: ORIGINAL_EFFECT(leave_cursor_compared_with_the_start(s)); break;
    case SHOP_NOTE_PLACE_ADDED: ORIGINAL_EFFECT(leave_place_added(s, value != 0)); break;
    case SHOP_NOTE_PLACE_COMPARED: ORIGINAL_EFFECT(leave_place_compared(s, (uint8_t)value)); break;
    case SHOP_NOTE_SLIDE_TESTED: ORIGINAL_EFFECT(leave_slide_tested(s, value != 0)); break;
    case SHOP_NOTE_CURSOR_KEPT: ORIGINAL_EFFECT(menu_leave_cursor_kept_on_the_list(s)); break;
    case SHOP_NOTE_BACK_FROM_THE_LIST: ORIGINAL_EFFECT(leave_back_from_the_list(s)); break;
    case SHOP_NOTE_ROOM_REFUSED: ORIGINAL_EFFECT(leave_room_refused(s, (uint8_t)value)); break;
    case SHOP_NOTE_GOLD_REFUSED: ORIGINAL_EFFECT(leave_gold_refused(s, (uint8_t)value)); break;
    case SHOP_NOTE_OFFER_TO_BUY: ORIGINAL_EFFECT(leave_offer_made(s, (uint8_t)value, true, (uint8_t)(value >> 8))); break;
    case SHOP_NOTE_SALE_REFUSED: ORIGINAL_EFFECT(leave_sale_refused(s, value != 0)); break;
    case SHOP_NOTE_OFFER_TO_SELL: ORIGINAL_EFFECT(leave_offer_made(s, (uint8_t)value, false, 0)); break;
    case SHOP_NOTE_INVENTORY_CHANGED: ORIGINAL_EFFECT(leave_inventory_changed(s)); break;
    case SHOP_NOTE_GOLD_WORD: ORIGINAL_EFFECT(leave_gold_word(s)); break;
    case SHOP_NOTE_QUANTITY_QUEUED: ORIGINAL_EFFECT(leave_queued(s, UPLOAD_QUANTITY)); break;
    case SHOP_NOTE_QUANTITY_COMPARED: ORIGINAL_EFFECT(leave_quantity_compared(s, (uint8_t)value)); break;
    case SHOP_NOTE_TOTAL_ADDED: ORIGINAL_EFFECT(leave_total_added(s, value)); break;
    case SHOP_NOTE_PUSHED_UP_OR_DOWN: ORIGINAL_EFFECT(leave_pushed_up_or_down(s)); break;
    case SHOP_NOTE_MEMBERS_COMPARED: ORIGINAL_EFFECT(menu_leave_compare(s, (uint8_t)value, (uint8_t)(value >> 8))); break;
    case SHOP_NOTE_BAR_PLACE_LOADED: ORIGINAL_EFFECT(leave_word_loaded(s, menu_scroll_bar_place(state_of(s)))); break;
    }
}

/* The steps' ends are the original's jumps, which the port stops at, or
 * runs where it is a port: the lists' openings, as C. */
static bool ends_on_the_machine(void *ctx, ShopEnd end)
{
    Snes *s = ctx;
    switch (end) {
    case SHOP_FADES_IN: m_stop_at(s, FADE_IN, READS_NOTHING); break;
    case SHOP_LEAVES: m_stop_at(s, LEAVE_THE_SHOP, READS_NOTHING); break;
    case SHOP_OPENS_THE_GOODS: NATIVE_TIME_AS(s, 0xC2DC7B, menu_open_goods_list(s)); break;
    case SHOP_OPENS_THE_LIST_FOR_SALE: NATIVE_TIME_AS(s, 0xC2DE98, menu_open_for_sale_list(s)); break;
    case SHOP_REFUSES: menu_play_refusal_sound(s); break;
    case SHOP_REFUSES_A_SLOT: m_stop_at_own(s, REFUSAL_SOUND, READS_A_HIGH); break;
    }
    return false;
}

static const ShopSteps on_the_machine = {
    read_cursor_on_the_machine, tick_on_the_machine, wait_on_the_machine, registers_on_the_machine,
    run_on_the_machine,         note_on_the_machine, ends_on_the_machine,
};

static void step(Snes *s, ShopSequence *run)
{
    if (run(state_of(s), m_platform(s), &on_the_machine, s))
        m_rts(s);
}

/* A routine that saves P and sets the widths as it starts, and puts P back
 * as it returns. */
static void saving_p(Snes *s, bool a8, bool x8, ShopSequence *run)
{
    menu_save_p(s);
    menu_set_widths(s, a8, x8);
    if (!run(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The entries --------------------------------------------------------------------------------- */

// $C2:D52B
void menu_open_shop_entry(Snes *s) { step(s, menu_open_shop_with); }

// $C2:DACA
/* It keeps the index registers' width as it starts. */
void menu_set_up_shop(Snes *s) { saving_p(s, true, m_x8(s), menu_set_up_shop_with); }

// $C2:DD7C
void menu_show_shop_item_panel(Snes *s) { saving_p(s, false, false, menu_show_shop_item_panel_with); }

// $C2:D546
void menu_choose_shop_tab_entry(Snes *s) { step(s, menu_choose_shop_tab_with); }

// $C2:DD40
/* A list's frame, the list's count in Y. */
void menu_frame_shop_list(Snes *s)
{
    menu_shop_frame_list(state_of(s), (uint8_t)s->cpu.y);
    m_rts(s);
}

// $C2:DC7B
void menu_open_goods_list(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_shop_open_the_goods(state_of(s), m_platform(s));
    ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:DECC
/* The list for sale made, framed, and settled. */
void menu_list_for_sale(Snes *s)
{
    menu_shop_make_the_list_for_sale(state_of(s));
    m_rts(s);
}

// $C2:DE98
void menu_open_for_sale_list(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_shop_open_the_list_for_sale(state_of(s), m_platform(s));
    menu_restore_p(s);
    ORIGINAL_EFFECT(leave_queued(s, MENU_UPLOAD_FRONT_LAYER));
    m_rts(s);
}

// $C2:D58B
void menu_choose_goods_entry(Snes *s) { step(s, menu_choose_goods_with); }

// $C2:D645
void menu_choose_for_sale_entry(Snes *s) { step(s, menu_choose_for_sale_with); }

// $C2:D618
void menu_choose_how_many_to_buy_entry(Snes *s) { step(s, menu_choose_how_many_to_buy_with); }

// $C2:D690
void menu_choose_how_many_to_sell_entry(Snes *s) { step(s, menu_choose_how_many_to_sell_with); }

// $C2:D6C3
void menu_choose_shop_member_entry(Snes *s) { step(s, menu_choose_shop_member_with); }

// $C2:E0A5
void menu_open_shop_member(Snes *s) { saving_p(s, true, false, menu_open_shop_member_with); }

// $C2:E0F1
void menu_show_shop_member_slots(Snes *s) { saving_p(s, false, false, menu_show_shop_member_slots_with); }

// $C2:E108
void menu_switch_shop_member(Snes *s) { saving_p(s, true, true, menu_switch_shop_member_with); }

// $C2:D715
void menu_choose_shop_slot_entry(Snes *s) { step(s, menu_choose_shop_slot_with); }

// $C2:D778
void menu_choose_shop_equipment_entry(Snes *s) { step(s, menu_choose_shop_equipment_with); }

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
    menu_shop_describe_nothing(state_of(s));
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

/* The same 16-bit, after a slide: the place loaded as a word, which the
 * cursor's sound keeps the high byte of. */
static void leave_cursor_compared_as_words(Snes *s)
{
    const GameState *g = state_of(s);
    s->cpu.a = (uint16_t)(menu_byte_the_mode_before_left(g) << 8 | menu_cursor_frame_start(g));
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

/* The panel's last member: A's high byte where the figures' working out
 * left it (its attack's sum's, before the figures' drawing divides again),
 * or the record's place's, as the character's record was found (the
 * sequence notes which). */
static void leave_panel_member(Snes *s, uint16_t left_by_the_last)
{
    s->cpu.a = (uint16_t)((left_by_the_last & 0xFF00) | (s->cpu.a & 0xFF));
    leave_a_low(s, 0xFF);
}

/* The inventory's routines run with 8-bit index registers, which leaves
 * Y's high byte clear. */
static void leave_inventory_changed(Snes *s) { s->cpu.y &= 0x00FF; }

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
static void leave_place_added(Snes *s, bool selling)
{
    const GameState *g = state_of(s);
    leave_added(s, menu_cursor_index(g), selling ? menu_shop_sell_top(g) : menu_shop_buy_top(g), false);
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

/* The guards' state halves, for the program as C. */
bool menu_shop_can_be_opened(const GameState *g) { return opens(g); }
bool menu_shop_is_on_the_tabs(const GameState *g) { return on_the_tabs(g); }
bool menu_shop_is_on_a_list(const GameState *g, bool selling_from_it) { return on_a_list(g, selling_from_it); }
bool menu_shop_is_on_the_quantity(const GameState *g) { return quantity(g); }
bool menu_shop_is_on_the_members(const GameState *g) { return on_the_members(g); }
bool menu_shop_member_can_be_chosen(const GameState *g) { return choosing_a_member(g) && menu_figures_can_be_worked_out(g); }
bool menu_shop_member_can_be_equipped(const GameState *g) { return equipping(g) && menu_figures_can_be_worked_out(g); }
