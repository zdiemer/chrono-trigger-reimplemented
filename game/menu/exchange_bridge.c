/* The party exchange on the original's machine: the screens' steps, the
 * cursor's move, the screen's and the panel's drawing, and their guards
 * (game/menu/exchange.c is the exchange).
 *
 * What waits for a frame is the machine's: the panel draws over three
 * frames, the reserve's slide over six, and the gate's screen waits for its
 * song's task (menu/sound.c) before it fades in; each wait is the menus'
 * task switch, called out. The cursor's tick and the refusal's buzz are
 * siblings run as the original's JSRs reach them (menu_run): each saves P
 * and calls the sound driver, so the flags the original has there are on
 * the stack, where shadow mode compares them. The cursor's move and the
 * screen's drawing, which can slide the reserve, are entries of their own,
 * run the same way; so are the gate's swirl (menu/gate_screen.c), the
 * records' sprite load (menu/sprites.c) and the song's changes. The main
 * menu, the fade in and the leaving of the menus are jumps the steps stop
 * at.
 *
 * Where the original waits inside routines a bridge runs inline -- the
 * slide inside the reserve's scroll inside the cursor's move, the gate's
 * sprite load inside the panel's window routine -- their returns and saved
 * P bytes are on the stack, and the bridge puts them there. Before each
 * call a bridge leaves the registers and flags the original's own
 * instructions left (scaffolding, at the end of the file). */
#include "menu/exchange.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/gate_screen.h"
#include "menu/main_menu.h"
#include "menu/menu_data.h"
#include "menu/menu.h"
#include "menu/party.h"
#include "menu/sound.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    YIELD = 0xC2821E,           /* the task waits for the next frame */
    MAIN_MENU = 0xC29A98,       /* back to the main menu */
    LEAVE_THE_MENUS = 0xC282B2, /* the menus close, back to the field */
    FADE_IN = 0xC283B2,         /* the screen's fade in started */
    CURSOR_SOUND = 0xC2EAC2,
    REFUSAL_SOUND = 0xC2EACC,
    MOVE_THE_CURSOR = 0xC2C805,
    LAY_OUT_THE_SCREEN = 0xC2CAF3,
    SHOW_THE_PANEL = 0xC2C9E5,
    SWITCH_TO_THE_GATE_SONG = 0xC2CB8B,
    SWITCH_BACK_TO_THE_FIELD_SONG = 0xC2CC0E,
    LOAD_A_RECORD_SPRITE = 0xC2F422,
    DRAW_THE_SWIRL = 0xFFF677,
    START_THE_GATE_SCREEN = 0xC2CE9D,
};
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool yield(Snes *s, uint32_t site) { return menu_call(s, site, YIELD, READS_NOTHING); }
static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, CURSOR_SOUND, menu_play_cursor_sound); }

static void leave_choosing_started(Snes *s);
static void leave_input_tested(Snes *s);
static void leave_row_tested(Snes *s);
static void leave_swapped(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_refused(Snes *s);
static void leave_scroll_started(Snes *s, bool back);
static void leave_bar_worked_out(Snes *s);
static void leave_slide_called(Snes *s);
static void leave_gate_sprite_record(Snes *s);
static void leave_panel_picked(Snes *s);
static void leave_step_counted(Snes *s);

/* ---- The cursor's move --------------------------------------------------------------------------- */

/* The slide waits for its frames inside two calls the cursor's move makes:
 * to the reserve's scroll ($C2:C85B, from $C2:C816), which saves P, and
 * from that to the slide itself ($C2:C8BA, from $C2:C883 back or $C2:C8B5
 * on), which saves it again. */
static bool slide_the_reserve(Snes *s, ExchangeSlide *slide)
{
    GameState *g = state_of(s);
    bool back = slide->step < 0;
    m_push16(s, 0xC816 + MENU_JSR_SIZE - 1);
    ORIGINAL_EFFECT(leave_scroll_started(s, back));
    menu_save_p(s);
    menu_set_widths(s, false, false);
    m_push16(s, (uint16_t)((back ? 0xC883 : 0xC8B5) + MENU_JSR_SIZE - 1));
    ORIGINAL_EFFECT(leave_slide_called(s));
    menu_save_p(s);
    while (menu_exchange_slide_a_frame(g, slide)) {
        menu_set_widths(s, false, false);
        if (!yield(s, 0xC2C936))
            return false;
    }
    menu_exchange_end_the_slide(g);
    menu_set_widths(s, false, false);
    ORIGINAL_EFFECT(menu_leave_a_high_byte_clear(s));
    if (!tick(s, 0xC2C944))
        return false;
    menu_restore_p(s);
    (void)m_pull16(s);
    menu_restore_p(s);
    (void)m_pull16(s);
    return true;
}

// $C2:C805
/* The reserve scrolled where the cursor pushed past it, then the cursor
 * settled on a member. */
void menu_move_the_exchange_cursor(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    ExchangeSlide slide;
    if (menu_exchange_scrolls(g, m_platform(s), &slide) && !slide_the_reserve(s, &slide))
        return;
    menu_set_widths(s, true, true);
    menu_exchange_settle_the_cursor(g);
    menu_restore_p(s);
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_WINDOW_LINES));
    m_rts(s);
}

/* The move, then a tick where the cursor came to rest elsewhere than the
 * frame found it. */
static void move_and_tick(Snes *s, uint32_t move_at, uint32_t tick_at)
{
    GameState *g = state_of(s);
    if (!menu_run(s, move_at, MOVE_THE_CURSOR, menu_move_the_exchange_cursor))
        return;
    bool moved = menu_cursor_frame_start(g) != menu_cursor_index(g);
    ORIGINAL_EFFECT(leave_cursor_compared(s));
    if (moved && !tick(s, tick_at))
        return;
    m_rts(s);
}

/* ---- Opening ------------------------------------------------------------------------------------- */

// $C2:CAF3
/* The screen over the panel, the cursor's first move, and the screen
 * sent. */
void menu_lay_out_the_exchange(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_exchange_draw_the_screen(g, m_platform(s));
    menu_set_widths(s, false, false);
    ORIGINAL_EFFECT(leave_bar_worked_out(s));
    if (!menu_run(s, 0xC2CB74, MOVE_THE_CURSOR, menu_move_the_exchange_cursor))
        return;
    menu_exchange_show_the_screen(g);
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_BACK_LAYER));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:C627
/* From the main menu: where there aren't two to swap, back to the main
 * menu with a buzz; otherwise the cursor on the first member it may
 * stand on, a tick, and the screen. */
void menu_open_exchange(Snes *s)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, true);
    if (!menu_exchange_has_two_to_swap(g)) {
        menu_exchange_refuse(g);
        ORIGINAL_EFFECT(leave_refused(s));
        m_stop_at(s, REFUSAL_SOUND, READS_A_HIGH);
        return;
    }
    menu_exchange_start_choosing(g, menu_exchange_cursor_kept(g));
    menu_set_widths(s, true, false);
    ORIGINAL_EFFECT(leave_choosing_started(s));
    if (!tick(s, 0xC2C642))
        return;
    m_stop_at(s, LAY_OUT_THE_SCREEN, READS_NOTHING);
}

/* The gate's swirl, its graphics unpacked by the swirl's own routine, which
 * the original calls from $C2:CE86, a routine of its own called from
 * $C2:C7C8. */
static bool show_the_swirl(Snes *s)
{
    m_push16(s, 0xC7C8 + MENU_JSR_SIZE - 1);
    if (!menu_run_long(s, 0xC2CE86, DRAW_THE_SWIRL, menu_draw_gate_screen))
        return false;
    menu_exchange_show_the_swirl(state_of(s));
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_TEXT_LAYER_WHOLE));
    (void)m_pull16(s);
    return true;
}

// $C2:C7AC
/* At the gate: laid out as the event asks, the gate's song started, the
 * panel and the screen over it, the swirl behind; once the song is playing,
 * the gate's screen started, and on to choosing as it fades in. */
void menu_open_exchange_at_the_gate(Snes *s)
{
    GameState *g = state_of(s);
    menu_exchange_lay_out_for_the_gate(g, (uint8_t)menu_screen_argument(g));
    if (!menu_run_any_widths(s, 0xC2C7B6, SWITCH_TO_THE_GATE_SONG, menu_switch_to_the_gate_song)
        || !menu_run_any_widths(s, 0xC2C7B9, SHOW_THE_PANEL, menu_show_exchange_panel))
        return;
    menu_set_widths(s, true, true);
    menu_exchange_start_choosing(g, 0);
    menu_set_widths(s, true, false);
    ORIGINAL_EFFECT(leave_step_counted(s));
    if (!menu_run(s, 0xC2C7C5, LAY_OUT_THE_SCREEN, menu_lay_out_the_exchange) || !show_the_swirl(s))
        return;
    while (menu_tasks_state(g, MENU_DESCRIPTION_TASK) != 0)
        if (!yield(s, 0xC2C7D0))
            return;
    menu_exchange_show_the_gate(g);
    if (!menu_run(s, 0xC2C7DA, START_THE_GATE_SCREEN, menu_start_gate_screen))
        return;
    m_stop_at(s, FADE_IN, READS_NOTHING);
}

/* ---- Choosing ------------------------------------------------------------------------------------ */

// $C2:C674
/* The member under the cursor chosen first, with a tick. */
static void choose_the_first(Snes *s)
{
    GameState *g = state_of(s);
    if (!tick(s, 0xC2C674))
        return;
    menu_exchange_choose_the_first(g);
    m_rts(s);
}

// $C2:C65D
/* Choosing the first; going back leaves for the main menu. */
void menu_choose_first_to_exchange(Snes *s)
{
    GameState *g = state_of(s);
    menu_step_reads_the_cursor(s);
    ORIGINAL_EFFECT(leave_input_tested(s));
    switch (menu_exchange_input(g)) {
    case EXCHANGE_CHOSEN: choose_the_first(s); return;
    case EXCHANGE_LEFT:
        menu_exchange_let_go(g);
        m_stop_at(s, MAIN_MENU, READS_NOTHING);
        return;
    case EXCHANGE_MOVED: move_and_tick(s, 0xC2C667, 0xC2C670); return;
    }
}

// $C2:C7E0
/* At the gate: someone free chosen first; going back leaves the menus, the
 * place's song back. */
void menu_choose_first_at_the_gate(Snes *s)
{
    GameState *g = state_of(s);
    menu_step_reads_the_cursor(s);
    ORIGINAL_EFFECT(leave_input_tested(s));
    switch (menu_exchange_input(g)) {
    case EXCHANGE_CHOSEN:
        ORIGINAL_EFFECT(leave_row_tested(s));
        if (menu_exchange_on_someone_free(g))
            choose_the_first(s);
        else
            m_rts(s);
        return;
    case EXCHANGE_LEFT:
        if (!menu_run(s, 0xC2C7FF, SWITCH_BACK_TO_THE_FIELD_SONG, menu_switch_back_to_the_field_song))
            return;
        m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING);
        return;
    case EXCHANGE_MOVED: move_and_tick(s, 0xC2C7EA, 0xC2C7F3); return;
    }
}

// $C2:C6B9
/* Someone free chosen second, and the two change places; then, as on
 * going back or choosing someone who stays, a tick and back to choosing
 * the first. */
void menu_choose_second_to_exchange(Snes *s)
{
    GameState *g = state_of(s);
    menu_step_reads_the_cursor(s);
    ORIGINAL_EFFECT(leave_input_tested(s));
    ExchangeInput input = menu_exchange_input(g);
    if (input == EXCHANGE_MOVED) {
        move_and_tick(s, 0xC2C6C3, 0xC2C6CC);
        return;
    }
    if (input == EXCHANGE_CHOSEN) {
        ORIGINAL_EFFECT(leave_row_tested(s));
        if (menu_exchange_on_someone_free(g)) {
            menu_exchange_choose_the_second(g);
            ORIGINAL_EFFECT(leave_swapped(s));
        }
    }
    if (!tick(s, 0xC2C6DE))
        return;
    menu_exchange_back_to_the_first(g);
    m_rts(s);
}

/* ---- The panel ----------------------------------------------------------------------------------- */

/* The gate's sprite's record, the screens' eighth, which its load runs on
 * as its direct page. */
enum { GATE_RECORD_PAGE = 0x19C0 };

/* The gate's sprite loaded on its record, inside the panel's window routine
 * ($C2:CA67, called through its table from $C2:C9F7 with the panel's P
 * saved), which keeps the direct page on the stack as it moves it to the
 * record. */
static bool load_the_gate_sprite(Snes *s)
{
    GameState *g = state_of(s);
    ORIGINAL_EFFECT(leave_panel_picked(s));
    menu_save_p(s);
    m_push16(s, 0xC9F7 + MENU_JSR_SIZE - 1);
    m_push16(s, s->cpu.d);
    s->cpu.d = GATE_RECORD_PAGE;
    menu_exchange_put_the_gate_sprite_on_its_record(g);
    ORIGINAL_EFFECT(leave_gate_sprite_record(s));
    if (!menu_run(s, 0xC2CAB0, LOAD_A_RECORD_SPRITE, menu_load_record_sprite))
        return false;
    menu_exchange_gate_sprite_loaded(g);
    s->cpu.d = m_pull16(s);
    (void)m_pull16(s);
    menu_restore_p(s);
    return true;
}

// $C2:C9E5
/* The panel over three frames: its window and the rows listed, the rows
 * drawn, then the records started and who stays dimmed. */
void menu_show_exchange_panel(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_exchange_panel_start(g, pf);
    if (menu_exchange_panel_shows_the_gate(g) && !load_the_gate_sprite(s))
        return;
    menu_set_widths(s, false, false);
    menu_exchange_panel_colours(g);
    if (!yield(s, 0xC2CA2D))
        return;
    menu_exchange_panel_draw_the_rows(g, pf);
    if (!yield(s, 0xC2CA33))
        return;
    menu_set_widths(s, false, false);
    menu_exchange_panel_finish(g, pf);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:CAE6
void menu_put_exchange_panel_away(Snes *s)
{
    menu_exchange_panel_put_away(state_of(s));
    m_rts(s);
}

/* ---- What the original's instructions leave (scaffolding) --------------------------------------- */

/* The count of those free: the two counts added (the overflow of the add)
 * and compared with two (the carry); then the step counted on (N and Z). */
static void leave_counted(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t active = menu_exchange_active_free(g), reserve = menu_exchange_reserve_count(g);
    uint8_t sum = (uint8_t)(active + reserve);
    m_set_flag(s, CPU_FLAG_V, ((active ^ sum) & (reserve ^ sum) & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_C, sum >= 2);
    menu_leave_load(s, menu_screen_step(g));
}

/* The refusal: the sum in A, X on the cursor kept. */
static void leave_refused(Snes *s)
{
    const GameState *g = state_of(s);
    leave_counted(s);
    m_set_a8(s, (uint8_t)(menu_exchange_active_free(g) + menu_exchange_reserve_count(g)));
    s->cpu.x = menu_exchange_cursor_kept(g);
}

/* The choosing started: nobody chosen ($FF loaded), X on the cursor's
 * row. */
static void leave_choosing_started(Snes *s)
{
    leave_counted(s);
    m_set_a8(s, 0xFF);
    s->cpu.x = menu_cursor_index(state_of(s));
}

/* The input tested: the buttons' choose and go back bits. */
static void leave_input_tested(Snes *s) { menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s))); }

/* The row under the cursor tested for someone who stays. */
static void leave_row_tested(Snes *s) { menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_exchange_on_cursor(state_of(s))); }

/* The swap: the flags of the second's row loaded, which it keeps round its
 * work; the members' standing, which ends it, leaves A's high byte clear. */
static void leave_swapped(Snes *s)
{
    menu_leave_load(s, menu_exchange_on_cursor(state_of(s)));
    menu_leave_a_high_byte_clear(s);
}

/* The cursor's place as the frame began, compared with where it is. */
static void leave_cursor_compared(Snes *s)
{
    const GameState *g = state_of(s);
    m_set_a8(s, menu_cursor_frame_start(g));
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}

/* The overflow of an 8-bit add or subtraction the original makes. */
static bool add_overflows(uint8_t a, uint8_t b, uint8_t sum) { return ((a ^ sum) & (b ^ sum) & 0x80) != 0; }
static bool subtraction_overflows(uint8_t a, uint8_t b, uint8_t difference) { return ((a ^ b) & (a ^ difference) & 0x80) != 0; }

/* The scroll's P: a load of the second cursor's offset; scrolling back, the
 * carry its compare of the reserve's count with five left, set; scrolling
 * on, the carry and the overflow of its count less three, and a compare
 * that didn't pass. */
enum { BACK_OFFSET = 0xD0, ON_OFFSET = 0x30, ROWS_BUT_THE_LAST = EXCHANGE_RESERVE_SHOWN - 1 };

static void leave_scroll_started(Snes *s, bool back)
{
    menu_leave_load(s, back ? BACK_OFFSET : ON_OFFSET);
    m_set_flag(s, CPU_FLAG_C, back);
    if (!back) {
        uint8_t count = menu_exchange_reserve_count(state_of(s));
        m_set_flag(s, CPU_FLAG_V, subtraction_overflows(count, ROWS_BUT_THE_LAST, (uint8_t)(count - ROWS_BUT_THE_LAST)));
    }
}

/* Before the screen's first move: a load of the dimmed colours' place (N
 * set), the carry and the overflow of the scroll bar's sums -- the reserve's
 * count less four, or where there is a bar, the thumb's place worked out as
 * its top row doubled plus the same doubled again. */
static void leave_bar_worked_out(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t count = menu_exchange_reserve_count(g);
    uint8_t past = (uint8_t)(count - EXCHANGE_RESERVE_SHOWN);
    bool carry = count >= EXCHANGE_RESERVE_SHOWN;
    bool overflow = subtraction_overflows(count, EXCHANGE_RESERVE_SHOWN, past);
    if (count > EXCHANGE_RESERVE_SHOWN) {
        uint8_t twice = (uint8_t)(2 * menu_reserve_top(g)), four_times = (uint8_t)(2 * twice);
        unsigned sum = four_times + twice + ((twice & 0x80) != 0);
        carry = sum > 0xFF;
        overflow = add_overflows(four_times, twice, (uint8_t)sum);
    }
    m_set_flag(s, CPU_FLAG_N, true);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_set_flag(s, CPU_FLAG_C, carry);
    m_set_flag(s, CPU_FLAG_V, overflow);
}

/* The slide's P: a load of the place the row coming in is drawn at, a
 * word neither zero nor negative. */
static void leave_slide_called(Snes *s)
{
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
}

/* The step counted on. */
static void leave_step_counted(Snes *s) { menu_leave_load(s, menu_screen_step(state_of(s))); }

/* The panel's window routine picked by the layout's panel bit, loaded. */
static void leave_panel_picked(Snes *s) { menu_leave_load(s, (uint8_t)(menu_exchange_layout(state_of(s)) & 0x02)); }

/* The record's place loaded as a word, its Y and X: neither zero nor
 * negative. */
static void leave_gate_sprite_record(Snes *s)
{
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

/* The party's places hold the seven characters or are empty, as the game's
 * always do. */
enum { NO_ONE = EXCHANGE_ROW_EMPTY, STAYS = EXCHANGE_ROW_STAYS };

static bool party_is_characters(const GameState *g)
{
    for (RecordPlace place = 0; place < PARTY_RECORD_PLACES; place++) {
        uint8_t who = party_at(g, place);
        if (!party_place_is_empty(who) && who >= CHARACTER_RECORDS_COUNT)
            return false;
    }
    return true;
}

/* The rows as the listing leaves them: each active row empty or one of the
 * first three places, each reserve row empty or one of the rest, every one
 * holding a member. */
static bool rows_are_listed(const GameState *g)
{
    for (unsigned r = 0; r < EXCHANGE_ROWS; r++) {
        uint8_t row = menu_exchange_rows(g, r);
        if (row & NO_ONE)
            continue;
        unsigned place = row & ~STAYS;
        bool active = r < EXCHANGE_ACTIVE_ROWS;
        if (active ? place >= EXCHANGE_ACTIVE_ROWS : (place < EXCHANGE_ACTIVE_ROWS || place >= EXCHANGE_ROWS || (row & STAYS)))
            return false;
        if (party_place_is_empty(party_at(g, (RecordPlace)place)))
            return false;
    }
    return true;
}

/* The cursor on one of its seven places (the screen's windows lay all
 * seven out whether or not the reserve shows), the reserve's top keeping
 * its rows among the six; and, once the screen is drawn, the places' links
 * up and down among the seven. */
enum { CURSOR_PLACES = EXCHANGE_FIRST_RESERVE_PLACE + EXCHANGE_RESERVE_SHOWN };

static bool cursor_is_on_a_row(const GameState *g)
{
    return menu_cursor_index(g) < CURSOR_PLACES && menu_reserve_top(g) <= EXCHANGE_RESERVE_ROWS - EXCHANGE_RESERVE_SHOWN;
}

static bool cursor_is_among_the_rows(const GameState *g)
{
    if (!cursor_is_on_a_row(g))
        return false;
    for (unsigned place = 0; place < CURSOR_PLACES; place++)
        if ((menu_cursor_places_up(g, place) & EXCHANGE_LINKED_PLACE) >= CURSOR_PLACES
            || (menu_cursor_places_down(g, place) & EXCHANGE_LINKED_PLACE) >= CURSOR_PLACES)
            return false;
    return menu_cursor_kept_among(g, 0, CURSOR_PLACES, false);
}

/* Someone may be moved, or the cursor would have nobody under it; and a
 * slide starts only with the reserve's rows listed to its count (the main
 * menu's exchange leaves the count as the screen before it did), and with
 * the second cursor's offset a byte, as a slide's end leaves it. */
static bool slides_are_sound(const GameState *g)
{
    uint8_t count = menu_exchange_reserve_count(g);
    if (count <= EXCHANGE_RESERVE_SHOWN)
        return true;
    if (count > EXCHANGE_RESERVE_ROWS || (menu_held_cursor_shift(g) >> 8) != 0)
        return false;
    for (unsigned r = 0; r < count; r++)
        if (menu_exchange_rows(g, EXCHANGE_ACTIVE_ROWS + r) & NO_ONE)
            return false;
    return true;
}

static bool exchange_is_sound(const GameState *g)
{
    return menu_zero(g) == 0 && party_is_characters(g) && rows_are_listed(g)
           && (menu_exchange_active_free(g) | menu_exchange_reserve_count(g)) != 0 && slides_are_sound(g);
}

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

/* A free row from `from` on, for the cursor to start on. */
static bool free_row_from(const GameState *g, unsigned from)
{
    for (unsigned r = from; r < EXCHANGE_ROWS; r++)
        if (!(menu_exchange_rows(g, r) & (NO_ONE | STAYS)))
            return true;
    return false;
}

static bool opens(const GameState *g)
{
    if (!menu_exchange_has_two_to_swap(g))
        return true;
    return free_row_from(g, menu_exchange_cursor_kept(g));
}

bool menu_exchange_opens(Snes *s) { return peek(s, opens); }

static bool lays_out(const GameState *g)
{
    return exchange_is_sound(g) && cursor_is_on_a_row(g) && menu_palette_changes_have_room(g);
}

bool menu_exchange_lays_out(Snes *s) { return peek(s, lays_out); }

static bool moves(const GameState *g) { return exchange_is_sound(g) && cursor_is_among_the_rows(g); }

bool menu_exchange_cursor_moves(Snes *s) { return peek(s, moves); }

/* A step: the cursor and its moves among the rows; the member chosen first,
 * where there is one, a place of the party, with the second cursor held on
 * a row. */
enum { NOBODY_CHOSEN = 0xFF };

static bool step_runs(const GameState *g)
{
    uint8_t first = menu_exchange_first(g);
    bool chosen_sound = first == NOBODY_CHOSEN || (first < EXCHANGE_ROWS && menu_held_cursor(g) < CURSOR_PLACES);
    return exchange_is_sound(g) && cursor_is_among_the_rows(g) && chosen_sound && menu_exchange_cursor_kept(g) < CURSOR_PLACES;
}

bool menu_exchange_step_runs(Snes *s) { return peek(s, step_runs) && menu_cursor_is_movable(s); }

static bool panel_shows(const GameState *g)
{
    return menu_zero(g) == 0 && party_is_characters(g) && menu_palette_changes_have_room(g);
}

bool menu_exchange_panel_shows(Snes *s) { return peek(s, panel_shows); }

bool menu_exchange_panel_goes(Snes *s) { return menu_on_page_work_ram(s); }

/* At the gate: someone free for the cursor to start on once the rows are
 * listed -- an active member the story doesn't keep, or one of the reserve
 * where the gate shows it -- and the panel's and the screen's conditions. */
static bool someone_free_in_the_party(const GameState *g)
{
    bool reserve_shown = menu_screen_argument(g) == 0;
    for (RecordPlace place = 0; place < (reserve_shown ? EXCHANGE_ROWS : EXCHANGE_ACTIVE_ROWS); place++) {
        uint8_t who = party_at(g, place);
        if (who & NO_ONE)
            continue;
        if (place >= EXCHANGE_ACTIVE_ROWS || !(story_party_kept(g) & menu_character_bit(g, who)))
            return true;
    }
    return false;
}

static bool opens_at_the_gate(const GameState *g)
{
    return panel_shows(g) && someone_free_in_the_party(g) && menu_reserve_top(g) == 0
           && menu_exchange_reserve_count(g) <= EXCHANGE_RESERVE_ROWS && (menu_held_cursor_shift(g) >> 8) == 0;
}

bool menu_exchange_opens_at_the_gate(Snes *s) { return peek(s, opens_at_the_gate); }
