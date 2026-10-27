/* Saving the game on the original's machine: the save screens' steps'
 * answers, their entries and their guards (game/menu/saving.c is saving, and
 * its sequences).
 *
 * The sequences are saving.c's, run here with the machine's answers to what
 * they ask: the panel, the cursor's tick and the summary's reading back are
 * each the menus' own entry, run as the original's JSR reaches it
 * (menu_run); a frame's wait is the task switch's, from the original's call
 * at each place; the saved sound is a call out to the driver. The save runs
 * inside the original's frame for it, its last wait a jump the original
 * makes to the task switch, so the task goes on from it into the save's
 * caller. The fade in, the leaving of the menus, the main menu and a save
 * point's jump into saving in the slot are jumps the steps stop at. Before
 * each call a sequence leaves the flags the original's own tests left
 * (scaffolding, at the notes). */
#include "menu/saving.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/load_screen.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/save_screen.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "original_frame.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    LEAVE_THE_MENUS = 0xC282B2, /* the menus close, back to the field */
    FADE_IN = 0xC283B2,         /* the screen's fade in started */
    SHOW_THE_PANEL = 0xC2D065,
    SAVE_IN_THE_SLOT = 0xC2CF0A,
    SHOW_THE_SLOT_SAVED = 0xC2CF21,
    SAVE_THE_GAME = 0xC2CF37,
    MOVE_AMONG_THE_SLOTS = 0xC2CFA2,
    MAIN_MENU = 0xC29A98,       /* back to choosing on the main menu */
    CURSOR_SOUND = 0xC2EAC2,
    REFUSAL_SOUND = 0xC2EACC,
    READ_THE_SLOT_SUMMARY = 0xC2D19F,
};
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The save's last wait keeps every register but A on the task's stack. */
enum { THE_WAIT_KEEPS_ALL_BUT_A = DC_A };

/* The original's call at each place, and for the panel's first wait the
 * widths its code sets after it (the code between is C, which reads none of
 * them). */
static const struct {
    uint32_t site;
    bool narrow_a_after;
} places[SAVING_PLACES] = {
    [SAVING_AT_THE_SLOTS_DRAWN] = { 0xC2D07E, true },
    [SAVING_AT_THE_SUMMARY_DRAWN] = { 0xC2D0AA, false },
    [SAVING_AT_THE_OPENING_TICK] = { 0xC2CEF5, false },
    [SAVING_AT_THE_QUESTION_TICK] = { 0xC2CF11, false },
    [SAVING_AT_THE_ANSWER_MOVE_TICK] = { 0xC2CF71, false },
    [SAVING_AT_THE_NO_TICK] = { 0xC2CF88, false },
    [SAVING_AT_THE_SLOT_MOVE_TICK] = { 0xC2CFAF, false },
    [SAVING_AT_THE_SAVE_IN_AN_EMPTY_SLOT] = { 0xC2CF1E, false },
    [SAVING_AT_THE_SAVE_OVER] = { 0xC2CF7B, false },
    [SAVING_AT_THE_SAVE_WRITTEN] = { 0xC2CF55, false },
};

/* ---- What the original's tests leave (scaffolding) ----------------------------------------------- */

enum { ANSWER_YES = 3 };

/* What the steps' last loads leave in the flags they come back with. */
static void leave_the_step(Snes *s) { menu_leave_load(s, menu_screen_step(state_of(s))); }

/* The question put up or away: its window list's upload queued, then the
 * step's load. */
static void leave_question_changed(Snes *s)
{
    menu_leave_queued_upload(s, MENU_UPLOAD_BACK_LAYER);
    leave_the_step(s);
}

static void leave_answer_compared(Snes *s) { menu_leave_compare(s, menu_cursor_index(state_of(s)), ANSWER_YES); }

static void leave_answer_moved(Snes *s)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_index(g), menu_cursor_frame_start(g));
}

static void leave_moved_among(Snes *s, uint8_t was) { menu_leave_compare(s, menu_cursor_index(state_of(s)), was); }

/* The slot saved in shown, and the upload's registers left as its queueing
 * leaves them. */
static void leave_slot_saved_shown(Snes *s)
{
    menu_leave_queued_upload(s, MENU_UPLOAD_CHAPTER_LABELS);
    m_set_nz16(s, MENU_UPLOAD_CHAPTER_LABELS);
}

/* ---- What the sequences ask of the menus, on the machine ----------------------------------------- */

static Snes *machine(void *ctx) { return ((SavingOnTheMachine *)ctx)->s; }

static void read_cursor(void *ctx) { menu_step_reads_the_cursor_and_tests_its_buttons(machine(ctx)); }

static bool tick(void *ctx, SavingPlace at) { return menu_run(machine(ctx), places[at].site, CURSOR_SOUND, menu_play_cursor_sound); }

static bool wait(void *ctx, SavingPlace at)
{
    Snes *s = machine(ctx);
    if (!menu_wait_a_frame_from(s, places[at].site))
        return false;
    if (places[at].narrow_a_after)
        menu_set_widths(s, true, false);
    return true;
}

static bool panel(void *ctx) { return menu_run(machine(ctx), 0xC2D4FB, SHOW_THE_PANEL, menu_show_save_panel_entry); }

static bool lay_out(void *ctx)
{
    Snes *s = machine(ctx);
    TIMED_AS(state_of(s), 0xC2D0E5, menu_lay_out_save_screen(state_of(s), m_platform(s)));
    return true;
}

/* The save up to its last wait, inside the original's frame for it. */
static bool save_up_to_the_last_wait(Snes *s)
{
    SavingOnTheMachine m = { s };
    return menu_save_the_game_with(state_of(s), m_platform(s), &menu_saving_on_the_machine, &m);
}

/* The save, from its JSR at the place: as C where its port runs, inside the
 * original's frame for it, with the last wait returning to the call's
 * return; otherwise the original's, which reads A's high byte as its call to
 * the driver does. True where it came back, frames later. */
static bool save(void *ctx, SavingPlace at)
{
    Snes *s = machine(ctx);
    uint32_t site = places[at].site;
    if (!menu_runs_as_port(s, SAVE_THE_GAME, menu_save_the_game))
        return menu_call(s, site, SAVE_THE_GAME, READS_A_HIGH);
    bool saved = false;
    NATIVE_TIME_AS(s, SAVE_THE_GAME,
                   IN_THE_ORIGINALS_FRAME(state_of(s), ORIGINAL_RETURN_BYTES, saved = save_up_to_the_last_wait(s)));
    return saved && menu_yield(s, site, MENU_TASK_WAITS_A_FRAME, THE_WAIT_KEEPS_ALL_BUT_A);
}

/* The sound is a call out to the driver, which must come back to the
 * save's code; the pan in A, which the driver keeps. */
static bool saved_sound(void *ctx)
{
    Snes *s = machine(ctx);
    MenuMachine m = menu_machine_now(s);
    ORIGINAL_EFFECT(m_set_a8(s, AU_CENTRE));
    menu_send_the_saved_sound(m_platform(s));
    return menu_back(s, 0xC2CF4A, m);
}

static bool summary_read_back(void *ctx) { return menu_run(machine(ctx), 0xC2CF58, READ_THE_SLOT_SUMMARY, menu_read_slot_summary); }

static void show_the_slot_saved_now(Snes *s)
{
    menu_show_the_slot_saved_now(state_of(s));
    ORIGINAL_EFFECT(leave_slot_saved_shown(s));
}

static bool show_the_slot_saved(void *ctx)
{
    Snes *s = machine(ctx);
    NATIVE_TIME_AS(s, SHOW_THE_SLOT_SAVED, show_the_slot_saved_now(s));
    return true;
}

static void note(void *ctx, SavingNote n, uint8_t value)
{
    Snes *s = machine(ctx);
    switch (n) {
    /* the refusal's test's flags, which the tick saves */
    case SAVING_NOTE_NOT_REFUSED: ORIGINAL_EFFECT(menu_leave_load(s, 0)); break;
    case SAVING_NOTE_LAID_OUT: ORIGINAL_EFFECT(leave_the_step(s)); break;
    /* the slot's test's flags, which the tick saves */
    case SAVING_NOTE_SLOT_HOLDS: ORIGINAL_EFFECT(menu_leave_load(s, value)); break;
    case SAVING_NOTE_QUESTION_ASKED:
    case SAVING_NOTE_QUESTION_PUT_AWAY: ORIGINAL_EFFECT(leave_question_changed(s)); break;
    case SAVING_NOTE_ANSWER_COMPARED: ORIGINAL_EFFECT(leave_answer_compared(s)); break;
    case SAVING_NOTE_ANSWER_MOVED: ORIGINAL_EFFECT(leave_answer_moved(s)); break;
    case SAVING_NOTE_MOVED_AMONG: ORIGINAL_EFFECT(leave_moved_among(s, value)); break;
    }
}

/* The steps' ends: the original's jumps, which the port stops at, or runs
 * as the entry it jumps to where that is a port (saving in the slot from
 * the main menu's screen, and the slots' frame where its guard takes the
 * state). */
static bool ends(void *ctx, SavingEnd end)
{
    Snes *s = machine(ctx);
    switch (end) {
    case SAVING_FADES_IN: m_stop_at(s, FADE_IN, READS_NOTHING); break;
    case SAVING_LEAVES_THE_MENUS: m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING); break;
    case SAVING_BACK_TO_THE_MAIN_MENU: m_stop_at(s, MAIN_MENU, READS_NOTHING); break;
    case SAVING_SAVES_IN_THE_SLOT_JUMPED: m_stop_at_own(s, SAVE_IN_THE_SLOT, READS_A_HIGH); break;
    case SAVING_SAVES_IN_THE_SLOT: NATIVE_TIME_AS(s, SAVE_IN_THE_SLOT, menu_save_in_the_slot(s)); break;
    case SAVING_MOVES_AMONG_THE_SLOTS:
        if (menu_slot_under_the_cursor_shows(state_of(s)))
            NATIVE_TIME_AS(s, MOVE_AMONG_THE_SLOTS, menu_move_among_the_slots(s));
        else
            m_stop_at_own(s, MOVE_AMONG_THE_SLOTS, READS_A_HIGH);
        break;
    case SAVING_BUZZES: m_stop_at_own(s, REFUSAL_SOUND, READS_A_HIGH); break;
    }
    return false;
}

const SavingSteps menu_saving_on_the_machine = { read_cursor, tick, wait, panel, lay_out, save, saved_sound,
                                                 summary_read_back, show_the_slot_saved, note, ends };

/* ---- The entries --------------------------------------------------------------------------------- */

static void step(Snes *s, bool (*run)(GameState *, Platform *, const SavingSteps *, void *))
{
    SavingOnTheMachine m = { s };
    if (run(state_of(s), m_platform(s), &menu_saving_on_the_machine, &m))
        m_rts(s);
}

// $C2:D065
/* It saves P as it starts and puts it back as it ends. */
void menu_show_save_panel_entry(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    SavingOnTheMachine m = { s };
    if (!menu_show_save_panel_with(state_of(s), m_platform(s), &menu_saving_on_the_machine, &m))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:D0DB
void menu_put_save_panel_away(Snes *s)
{
    menu_put_save_panel_away_now(state_of(s));
    m_rts(s);
}

// $C2:D0E5
void menu_lay_out_save_screen_entry(Snes *s)
{
    menu_lay_out_save_screen(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:D4F8
void menu_open_save_screen_entry(Snes *s) { step(s, menu_open_save_screen_with); }

// $C2:D506
void menu_choose_on_save_screen_entry(Snes *s) { step(s, menu_choose_on_save_screen_with); }

// $C2:CEDC
void menu_open_saving_entry(Snes *s) { step(s, menu_open_saving_with); }

// $C2:CEFD
void menu_choose_slot_to_save_in_entry(Snes *s) { step(s, menu_choose_slot_to_save_in_with); }

// $C2:CF61
void menu_answer_whether_to_save_over_entry(Snes *s) { step(s, menu_answer_whether_to_save_over_with); }

// $C2:CF92
void menu_close_the_question_on_a_button_entry(Snes *s) { step(s, menu_close_the_question_on_a_button_with); }

// $C2:CF0A
void menu_save_in_the_slot(Snes *s) { step(s, menu_save_in_the_slot_with); }

// $C2:CF37
void menu_save_the_game(Snes *s)
{
    if (save_up_to_the_last_wait(s))
        m_stop_at_own(s, MENU_TASK_WAITS_A_FRAME, THE_WAIT_KEEPS_ALL_BUT_A);
}

// $C2:CF21
/* After the save: the slot saved in shown. */
void menu_show_the_slot_saved(Snes *s)
{
    show_the_slot_saved_now(s);
    m_rts(s);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_save_panel_shows(Snes *s) { return peek(s, menu_save_panel_can_show); }

/* The slot shown one of the three, on the menus' page with the data bank on
 * work RAM's: what saving in it, and drawing its window, take. */
bool menu_slot_can_be_saved_in(Snes *s) { return peek(s, menu_slot_can_be_saved); }

/* ... and a slot saved in shown: its window's palette change counted too. */
bool menu_slot_saved_shows(Snes *s) { return menu_slot_can_be_saved_in(s) && menu_slot_window_is_drawable(s); }

bool menu_saving_opens(Snes *s) { return peek(s, menu_saving_can_open); }

bool menu_save_screen_lays_out(Snes *s) { return peek(s, menu_save_screen_can_lay_out); }
