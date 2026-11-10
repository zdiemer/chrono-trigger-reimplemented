/* Save-screen sequences on the machine-facing C path. The panel, cursor
 * and summary use checked calls; task waits retain real return frames so
 * a save can be suspended and resumed at a frame boundary. The state
 * operations and sequence definitions live in saving.c. */
#include "menu/saving.h"

#include "charge.h"
#include "core/program.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/fades.h"
#include "menu/load_screen.h"
#include "menu/main_menu.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/save_screen.h"
#include "menu/vram.h"
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

/* The save confirmation uses the same answer code as the screen. */

enum { ANSWER_YES = 3 };

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

/* The save keeps a real return frame while its tasks can yield. The old
 * comparison-only frame had no charged accesses; creating and removing
 * these saveable bytes retains that time. Its retired entry's contract
 * required binary arithmetic, 8-bit A, 16-bit indexes, the menu page/bank
 * and a drawable save slot, with no code-image guard. */
static bool save(void *ctx, SavingPlace at)
{
    Snes *s = machine(ctx);
    uint32_t site = places[at].site;
    uint32_t before = native_uncounted_begin(s);
    m_push16(s, (uint16_t)(site + MENU_JSR_SIZE - 1));
    m_jump_to(s, SAVE_THE_GAME);
    bool takes = !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D)) == CPU_FLAG_M &&
                 menu_slot_can_be_saved_in(s);
    native_uncounted_end(s, before);
    if (!takes) {
        program_breaks_a_contract(state_of(s), SAVE_THE_GAME, "a save outside its slot contract");
        m_stop_at(s, SAVE_THE_GAME, READS_A_HIGH);
        return false;
    }
    bool saved = false;
    TIMED_AS(state_of(s), SAVE_THE_GAME, saved = save_up_to_the_last_wait(s));
    if (!saved)
        return false;
    before = native_uncounted_begin(s);
    (void)m_pull16(s);
    native_uncounted_end(s, before);
    return menu_yield(s, site, MENU_TASK_WAITS_A_FRAME, THE_WAIT_KEEPS_ALL_BUT_A);
}

/* The sound is a call out to the driver, which must come back to the
 * save's code; the pan in A, which the driver keeps. */
static bool saved_sound(void *ctx)
{
    Snes *s = machine(ctx);
    MenuMachine m = menu_machine(s);
    (void)0;
    menu_send_the_saved_sound(m_platform(s));
    return menu_back(s, 0xC2CF4A, m);
}

static bool summary_read_back(void *ctx) { return menu_run(machine(ctx), 0xC2CF58, READ_THE_SLOT_SUMMARY, menu_read_slot_summary); }

static void show_the_slot_saved_now(Snes *s)
{
    menu_show_the_slot_saved_now(state_of(s));
    (void)0;
}

static bool show_the_slot_saved(void *ctx)
{
    Snes *s = machine(ctx);
    TIMED_AS(state_of(s), SHOW_THE_SLOT_SAVED, show_the_slot_saved_now(s));
    return true;
}

static void note(void *ctx, SavingNote n, uint8_t value)
{
    (void)value;
    Snes *s = machine(ctx);
    switch (n) {
    /* the refusal's test's flags, which the tick saves */
    case SAVING_NOTE_NOT_REFUSED: (void)0; break;
    case SAVING_NOTE_LAID_OUT: charge_machine_accesses(state_of(s), 1); break;
    /* the slot's test's flags, which the tick saves */
    case SAVING_NOTE_SLOT_HOLDS: (void)0; break;
    case SAVING_NOTE_QUESTION_ASKED:
    case SAVING_NOTE_QUESTION_PUT_AWAY: charge_machine_accesses(state_of(s), 1); break;
    case SAVING_NOTE_ANSWER_COMPARED: charge_machine_accesses(state_of(s), 1); break;
    case SAVING_NOTE_ANSWER_MOVED: charge_machine_accesses(state_of(s), 2); break;
    case SAVING_NOTE_MOVED_AMONG: charge_machine_accesses(state_of(s), 1); break;
    }
}

/* Continue the steps as C. Jumped-to entries retain their guards and stop
 * for the original where refused; the slots' frame has its own guard too. */
static bool ends(void *ctx, SavingEnd end)
{
    Snes *s = machine(ctx);
    switch (end) {
    case SAVING_FADES_IN: menu_go_on_in(s, FADE_IN, menu_start_the_fade_in_entry, READS_NOTHING); break;
    case SAVING_LEAVES_THE_MENUS: menu_go_on_in(s, LEAVE_THE_MENUS, menu_close_entry, READS_NOTHING); break;
    case SAVING_BACK_TO_THE_MAIN_MENU: menu_go_on_in(s, MAIN_MENU, menu_back_to_the_main_menu_entry, READS_NOTHING); break;
    case SAVING_SAVES_IN_THE_SLOT_JUMPED: menu_go_on_in(s, SAVE_IN_THE_SLOT, menu_save_in_the_slot, READS_A_HIGH); break;
    case SAVING_SAVES_IN_THE_SLOT: TIMED_AS(state_of(s), SAVE_IN_THE_SLOT, menu_save_in_the_slot(s)); break;
    case SAVING_MOVES_AMONG_THE_SLOTS:
        if (menu_slot_under_the_cursor_shows(state_of(s)))
            TIMED_AS(state_of(s), MOVE_AMONG_THE_SLOTS, menu_move_among_the_slots(s));
        else
            m_stop_at_own(s, MOVE_AMONG_THE_SLOTS, READS_A_HIGH);
        break;
    case SAVING_BUZZES:
        /* The refusal returns to the main menu's first step before the
         * buzzer saves the flags on the task's stack. */
        charge_machine_accesses(state_of(s), 1);
        TIMED_AS(state_of(s), REFUSAL_SOUND, menu_play_refusal_sound(s));
        break;
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

bool menu_saving_opens(Snes *s) { return peek(s, menu_saving_can_open); }
