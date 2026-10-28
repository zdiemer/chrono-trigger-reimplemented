/* The party exchange on the original's machine: the steps' answers, their
 * entries, the gate's screen's opening, and their guards (game/menu/exchange.c
 * is the exchange, and its sequences).
 *
 * The sequences are exchange.c's, run here with the machine's answers to
 * what they ask. What waits for a frame is the menus' task switch, called
 * out: the panel draws over three frames, the reserve's slide over six, and
 * the gate's screen waits for its song's task (menu/sound.c) before it fades
 * in. The cursor's tick and the refusal's buzz are siblings run as the
 * original's JSRs reach them (menu_run): each saves P and calls the sound
 * driver, so the flags the original has there are on the stack, where shadow
 * mode compares them. The cursor's move and the screen's layout, which can
 * slide the reserve, are entries of their own, run the same way; so are the
 * gate's swirl (menu/gate_screen.c), the records' sprite load
 * (menu/sprites.c) and the song's changes. The main menu, the fade in, the
 * layout after the opening's tick and the leaving of the menus are jumps the
 * steps stop at.
 *
 * Where the original waits inside routines the sequences run inline -- the
 * slide inside the reserve's scroll inside the cursor's move, the gate's
 * sprite load inside the panel's window routine -- their returns and saved
 * P bytes are on the stack, and the machine's answers put them there. Before
 * each call a sequence leaves the registers and flags the original's own
 * instructions left (scaffolding, at the notes). */
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

/* The original's call at each place, and for a wait, the widths its code
 * has before it and sets after it (the code between is C, which reads none
 * of them). */
typedef enum { KEEP_WIDTHS, WIDE } Widths;
static const struct {
    uint32_t site;
    Widths before, after;
} places[EXCHANGE_PLACES] = {
    [EXCHANGE_AT_THE_OPENING_TICK] = { 0xC2C642, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_FIRST_MOVE] = { 0xC2CB74, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_FIRST_S_MOVE] = { 0xC2C667, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_FIRST_S_MOVE_TICK] = { 0xC2C670, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_FIRST_CHOSEN_TICK] = { 0xC2C674, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_SECOND_S_MOVE] = { 0xC2C6C3, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_SECOND_S_MOVE_TICK] = { 0xC2C6CC, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_SECOND_CHOSEN_TICK] = { 0xC2C6DE, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_GATE_S_MOVE] = { 0xC2C7EA, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_GATE_S_MOVE_TICK] = { 0xC2C7F3, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_SLIDE] = { 0xC2C936, WIDE, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_SLIDE_TICK] = { 0xC2C944, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_PANEL_LISTED] = { 0xC2CA2D, KEEP_WIDTHS, KEEP_WIDTHS },
    [EXCHANGE_AT_THE_ROWS_DRAWN] = { 0xC2CA33, KEEP_WIDTHS, WIDE },
    [EXCHANGE_AT_THE_GATE_SONG_WAIT] = { 0xC2C7D0, KEEP_WIDTHS, KEEP_WIDTHS },
};

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

/* ---- What the sequences ask of the menus, on the machine ----------------------------------------- */

static Snes *machine(void *ctx) { return ((ExchangeOnTheMachine *)ctx)->s; }

static void read_cursor(void *ctx) { menu_step_reads_the_cursor(machine(ctx)); }

static bool tick(void *ctx, ExchangePlace at) { return menu_run(machine(ctx), places[at].site, CURSOR_SOUND, menu_play_cursor_sound); }

static bool wait(void *ctx, ExchangePlace at)
{
    Snes *s = machine(ctx);
    if (places[at].before == WIDE)
        menu_set_widths(s, false, false);
    if (!menu_wait_a_frame_from(s, places[at].site))
        return false;
    if (places[at].after == WIDE)
        menu_set_widths(s, false, false);
    return true;
}

static bool move_the_cursor(void *ctx, ExchangePlace at)
{
    return menu_run(machine(ctx), places[at].site, MOVE_THE_CURSOR, menu_move_the_exchange_cursor);
}

/* The gate's sprite's record, the screens' eighth, which its load runs on
 * as its direct page. */
enum { GATE_RECORD_PAGE = 0x19C0 };

/* The gate's sprite loaded on its record, inside the panel's window routine
 * ($C2:CA67, called through its table from $C2:C9F7 with the panel's P
 * saved), which keeps the direct page on the stack as it moves it to the
 * record. */
static bool gate_sprite(void *ctx)
{
    Snes *s = machine(ctx);
    ORIGINAL_EFFECT(leave_gate_sprite_record(s));
    return menu_run(s, 0xC2CAB0, LOAD_A_RECORD_SPRITE, menu_load_record_sprite);
}

static bool place_song_back(void *ctx)
{
    return menu_run(machine(ctx), 0xC2C7FF, SWITCH_BACK_TO_THE_FIELD_SONG, menu_switch_back_to_the_field_song_entry);
}

/* The gate's screen's routines, each where the original calls it from. */
static bool run_on_the_machine(void *ctx, ExchangeRoutine r)
{
    Snes *s = machine(ctx);
    switch (r) {
    case EXCHANGE_SWITCHES_TO_THE_GATE_SONG:
        return menu_run_any_widths(s, 0xC2C7B6, SWITCH_TO_THE_GATE_SONG, menu_switch_to_the_gate_song_entry);
    case EXCHANGE_SHOWS_THE_PANEL: return menu_run_any_widths(s, 0xC2C7B9, SHOW_THE_PANEL, menu_show_exchange_panel_entry);
    case EXCHANGE_LAYS_OUT_THE_GATE_SCREEN: return menu_run(s, 0xC2C7C5, LAY_OUT_THE_SCREEN, menu_lay_out_the_exchange);
    case EXCHANGE_DRAWS_THE_SWIRL: return menu_run_long(s, 0xC2CE86, DRAW_THE_SWIRL, menu_draw_gate_screen_entry);
    case EXCHANGE_STARTS_THE_GATE_SCREEN: return menu_run(s, 0xC2C7DA, START_THE_GATE_SCREEN, menu_start_gate_screen_entry);
    }
    return false;
}

/* The slide waits for its frames inside two calls the cursor's move makes:
 * to the reserve's scroll ($C2:C85B, from $C2:C816), which saves P, and
 * from that to the slide itself ($C2:C8BA, from $C2:C883 back or $C2:C8B5
 * on), which saves it again. */
static void slide(void *ctx, ExchangeSlideFrame frame)
{
    Snes *s = machine(ctx);
    switch (frame) {
    case EXCHANGE_SLIDE_BEGINS_BACK:
    case EXCHANGE_SLIDE_BEGINS_ON: {
        bool back = frame == EXCHANGE_SLIDE_BEGINS_BACK;
        m_push16(s, 0xC816 + MENU_JSR_SIZE - 1);
        ORIGINAL_EFFECT(leave_scroll_started(s, back));
        menu_save_p(s);
        menu_set_widths(s, false, false);
        m_push16(s, (uint16_t)((back ? 0xC883 : 0xC8B5) + MENU_JSR_SIZE - 1));
        ORIGINAL_EFFECT(leave_slide_called(s));
        menu_save_p(s);
        break;
    }
    case EXCHANGE_SLIDE_ENDED:
        menu_set_widths(s, false, false);
        ORIGINAL_EFFECT(menu_leave_a_high_byte_clear(s));
        break;
    case EXCHANGE_SLIDE_LEFT:
        menu_restore_p(s);
        (void)m_pull16(s);
        menu_restore_p(s);
        (void)m_pull16(s);
        break;
    /* The panel's window routine ($C2:CA67, called through its table from
     * $C2:C9F7 with the panel's P saved) keeps the direct page on the stack
     * as it moves it to the record. */
    case EXCHANGE_ON_THE_GATE_S_RECORD:
        ORIGINAL_EFFECT(leave_panel_picked(s));
        menu_save_p(s);
        m_push16(s, 0xC9F7 + MENU_JSR_SIZE - 1);
        m_push16(s, s->cpu.d);
        s->cpu.d = GATE_RECORD_PAGE;
        break;
    case EXCHANGE_OFF_THE_GATE_S_RECORD:
        s->cpu.d = m_pull16(s);
        (void)m_pull16(s);
        menu_restore_p(s);
        break;
    /* The swirl is drawn from $C2:CE86, a routine of its own called from
     * $C2:C7C8. */
    case EXCHANGE_SWIRL_BEGINS: m_push16(s, 0xC7C8 + MENU_JSR_SIZE - 1); break;
    case EXCHANGE_SWIRL_ENDS: (void)m_pull16(s); break;
    }
}

static void widths(void *ctx, ExchangeWidths w)
{
    Snes *s = machine(ctx);
    switch (w) {
    case EXCHANGE_BYTE_WIDE: menu_set_widths(s, true, true); break;
    case EXCHANGE_INDEX_WIDE: menu_set_widths(s, true, false); break;
    case EXCHANGE_WIDE: menu_set_widths(s, false, false); break;
    }
}

static void note(void *ctx, ExchangeNote n)
{
    Snes *s = machine(ctx);
    switch (n) {
    case EXCHANGE_NOTE_REFUSED: ORIGINAL_EFFECT(leave_refused(s)); break;
    case EXCHANGE_NOTE_CHOOSING_STARTED: ORIGINAL_EFFECT(leave_choosing_started(s)); break;
    case EXCHANGE_NOTE_BAR_WORKED_OUT: ORIGINAL_EFFECT(leave_bar_worked_out(s)); break;
    case EXCHANGE_NOTE_SCREEN_SHOWN: ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_BACK_LAYER)); break;
    case EXCHANGE_NOTE_INPUT_TESTED: ORIGINAL_EFFECT(leave_input_tested(s)); break;
    case EXCHANGE_NOTE_ROW_TESTED: ORIGINAL_EFFECT(leave_row_tested(s)); break;
    case EXCHANGE_NOTE_SWAPPED: ORIGINAL_EFFECT(leave_swapped(s)); break;
    case EXCHANGE_NOTE_CURSOR_COMPARED: ORIGINAL_EFFECT(leave_cursor_compared(s)); break;
    case EXCHANGE_NOTE_STEP_COUNTED: ORIGINAL_EFFECT(leave_step_counted(s)); break;
    case EXCHANGE_NOTE_SWIRL_SHOWN: ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_TEXT_LAYER_WHOLE)); break;
    }
}

/* The steps' ends are the original's jumps, which the port stops at. */
static bool ends(void *ctx, ExchangeEnd end)
{
    Snes *s = machine(ctx);
    switch (end) {
    case EXCHANGE_BUZZES: m_stop_at(s, REFUSAL_SOUND, READS_A_HIGH); break;
    case EXCHANGE_LAYS_OUT_THE_SCREEN: m_stop_at(s, LAY_OUT_THE_SCREEN, READS_NOTHING); break;
    case EXCHANGE_BACK_TO_THE_MAIN_MENU: m_stop_at(s, MAIN_MENU, READS_NOTHING); break;
    case EXCHANGE_LEAVES_THE_MENUS: m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING); break;
    case EXCHANGE_FADES_IN: m_stop_at(s, FADE_IN, READS_NOTHING); break;
    }
    return false;
}

const ExchangeSteps menu_exchange_on_the_machine = { read_cursor, tick, wait, move_the_cursor, gate_sprite,
                                                     place_song_back, slide, widths, note, ends, run_on_the_machine };

/* ---- The entries --------------------------------------------------------------------------------- */

static bool run(Snes *s, bool (*sequence)(GameState *, Platform *, const ExchangeSteps *, void *))
{
    ExchangeOnTheMachine m = { s };
    return sequence(state_of(s), m_platform(s), &menu_exchange_on_the_machine, &m);
}

static void step(Snes *s, bool (*sequence)(GameState *, Platform *, const ExchangeSteps *, void *))
{
    if (run(s, sequence))
        m_rts(s);
}

// $C2:C805
/* It saves P as it starts and puts it back as it ends, and runs with byte
 * wide registers; the window lines' upload is the last it queues. */
void menu_move_the_exchange_cursor(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, true);
    if (!run(s, menu_move_the_exchange_cursor_with))
        return;
    menu_restore_p(s);
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_WINDOW_LINES));
    m_rts(s);
}

// $C2:CAF3
void menu_lay_out_the_exchange(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(s, menu_lay_out_the_exchange_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:C627
/* It starts with byte wide registers. */
void menu_open_exchange_entry(Snes *s)
{
    menu_set_widths(s, true, true);
    step(s, menu_open_exchange_with);
}

// $C2:C65D
void menu_choose_first_to_exchange_entry(Snes *s) { step(s, menu_choose_first_to_exchange_with); }

// $C2:C7E0
void menu_choose_first_at_the_gate_entry(Snes *s) { step(s, menu_choose_first_at_the_gate_with); }

// $C2:C6B9
void menu_choose_second_to_exchange_entry(Snes *s) { step(s, menu_choose_second_to_exchange_with); }

// $C2:C9E5
/* It saves P as it starts and puts it back as it ends. */
void menu_show_exchange_panel_entry(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(s, menu_show_exchange_panel_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:CAE6
void menu_put_exchange_panel_away(Snes *s)
{
    menu_exchange_panel_put_away(state_of(s));
    m_rts(s);
}

/* ---- The gate's screen ---------------------------------------------------------------------------- */

// $C2:C7AC
void menu_open_exchange_at_the_gate_entry(Snes *s) { step(s, menu_open_exchange_at_the_gate_with); }

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

/* The steps run on the menus' page with the data bank on work RAM's; the
 * rest is what they take of the state (exchange.c). */
static bool peek(Snes *s, bool (*test)(const GameState *))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_exchange_opens(Snes *s) { return peek(s, menu_exchange_can_open); }

bool menu_exchange_lays_out(Snes *s) { return peek(s, menu_exchange_can_lay_out); }

bool menu_exchange_cursor_moves(Snes *s) { return peek(s, menu_exchange_cursor_can_move); }

bool menu_exchange_step_runs(Snes *s) { return peek(s, menu_exchange_step_can_run); }

bool menu_exchange_panel_shows(Snes *s) { return peek(s, menu_exchange_panel_can_show); }

bool menu_exchange_panel_goes(Snes *s) { return menu_on_page_work_ram(s); }

bool menu_exchange_opens_at_the_gate(Snes *s) { return peek(s, menu_exchange_can_open_at_the_gate); }
