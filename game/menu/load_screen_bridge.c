/* The load screen on the original's machine: its steps' answers, their
 * entries and their guards (game/menu/load_screen.c is the screen, and its
 * sequences).
 *
 * The sequences are load_screen.c's, run here with the machine's answers to
 * what they ask: the slot's showing, the screen's drawing, the cursor's tick,
 * a New Game+'s question and carrying over and a slot's erasing are each the
 * menus' own entry, run as the original's JSR (JSL) reaches it (menu_run);
 * the sound output's command to the driver is a call out. The fade in as the
 * screen opens and the menus' exit for the game are jumps the steps stop at.
 * Before each call a sequence leaves the flags the original's own tests left
 * (scaffolding, at the notes): where one of the routines it calls saves P as
 * it starts, they are on the stack under the call, where shadow mode compares
 * them. */
#include "menu/load_screen.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/save_screen.h"
#include "menu/saves.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "platform/save.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    FADE_IN_START = 0xC283B2,      /* the screen's fade in started */
    EXIT_FOR_THE_GAME = 0xC28320,  /* the menus' exit, on to the game */
    CURSOR_SOUND = 0xC2EAC2,
    SHOW_THE_SLOT = 0xC2E743,
    DRAW_THE_SCREEN = 0xC2E7C3,
    ASK_WHICH = 0xC2E841,
    CARRY_OVER = 0xFFFAED,
    ERASE_THE_SLOT = 0xC2E8B7,
    EMPTY_LABEL = 0xC2D28D,
};
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The original's call at each place. */
static const uint32_t places[LOAD_PLACES] = {
    [LOAD_AT_THE_OPENING_SHOW] = 0xC2E7F3,
    [LOAD_AT_THE_ERASE_ASKED_TICK] = 0xC2E62C,
    [LOAD_AT_THE_CHOOSING_SHOW] = 0xC2E63A,
    [LOAD_AT_THE_CHOOSING_MOVE_TICK] = 0xC2E643,
    [LOAD_AT_THE_CHOSEN_TICK] = 0xC2E647,
    [LOAD_AT_THE_CARRY_ON_SHOW] = 0xC2E6B8,
    [LOAD_AT_THE_CARRY_ON_MOVE_TICK] = 0xC2E6C1,
    [LOAD_AT_THE_CARRY_ON_CHOSEN_TICK] = 0xC2E6C5,
    [LOAD_AT_THE_CARRY_ON_BACK_TICK] = 0xC2E6DF,
    [LOAD_AT_THE_ERASE_MOVE_TICK] = 0xC2E715,
    [LOAD_AT_THE_ERASE_ANSWERED_TICK] = 0xC2E73A,
    [LOAD_AT_THE_FOURTH_ROW_FLIP_TICK] = 0xC2E777,
};

/* ---- What the original's tests leave (scaffolding) ----------------------------------------------- */

enum { FOURTH_ROW = 3, ERASE_BUTTONS = 0x30, ERASE_YES = 4, NEW_GAME_PLUS_X = 0x78 };

/* The flags the screen's drawing has as it calls the summary's drawing and
 * showing, which those save as they start: the last load before each, and
 * the rows' last count, at three. */
static void leave_start_slot_loaded(Snes *s, uint8_t slot)
{
    menu_leave_load(s, slot);
    m_set_flag(s, CPU_FLAG_C, true);
}

/* A step's number set, and the carry of the test before it. */
static void leave_step_set(Snes *s, uint8_t step, bool carry)
{
    menu_leave_load(s, step);
    m_set_flag(s, CPU_FLAG_C, carry);
}

/* Choosing tests the second controller for the erase buttons, and where
 * they are held, the cursor against the fourth row. */
static void leave_erase_tested(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t second = (uint8_t)(second_controller(g) >> 8);
    if (second != ERASE_BUTTONS)
        menu_leave_compare(s, second, ERASE_BUTTONS);
    else
        menu_leave_compare(s, menu_cursor_index(g), FOURTH_ROW);
}

static void leave_moved(Snes *s)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}

static void leave_answer_moved(Snes *s)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_index(g), menu_cursor_frame_start(g));
}

/* The slot's window's drawing leaves the player's design in A. */
enum { DESIGN_BITS = 7 };
static void leave_design(Snes *s) { s->cpu.a = (uint8_t)(game_settings(state_of(s), 1) & DESIGN_BITS); }

/* The slot's row redrawn empty: its label cleared and drawn again with the
 * empty slot's (by $C2:D28D, from the JSR at $C2:E903), and after a frame
 * ($C2:821E, the task's yield) queued: the slot's chapter's two rows of
 * graphic. The original builds the label's upload record where the summary is
 * drawn from and queues it from there, the summary's byte after the record's
 * seven going with it as its spare (which nothing reads), and keeps the
 * slot's rows in its first cell. */
enum { LABEL_LOADER_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };   /* $C2:D28D */

static void leave_label_record(GameState *g, uint8_t slot)
{
    MenuUpload u = menu_slot_label_upload(slot);
    const uint8_t record[MENU_LABEL_RECORD_BYTES] = { (uint8_t)u.vram, (uint8_t)(u.vram >> 8), (uint8_t)u.source, (uint8_t)(u.source >> 8),
                                (uint8_t)(u.source >> 16), (uint8_t)u.size, (uint8_t)(u.size >> 8) };
    for (unsigned i = 0; i < sizeof record; i++)
        set_menu_save_summary_byte(g, i, record[i]);
    set_menu_scratch_erased_slot_rows(g, (uint16_t)(slot << 8));
}

/* Going back to choosing leaves 16-bit widths behind it. */
static void leave_wide(Snes *s) { menu_set_widths(s, false, false); }

/* ---- What the sequences ask of the menus, on the machine ----------------------------------------- */

static Snes *machine(void *ctx) { return ((LoadScreenOnTheMachine *)ctx)->s; }

static void read_cursor(void *ctx) { menu_step_reads_the_cursor_and_tests_its_buttons(machine(ctx)); }

static bool tick(void *ctx, LoadScreenPlace at) { return menu_run(machine(ctx), places[at], CURSOR_SOUND, menu_play_cursor_sound); }

static bool show_the_slot(void *ctx, LoadScreenPlace at)
{
    return menu_run(machine(ctx), places[at], SHOW_THE_SLOT, menu_show_save_slot);
}

static bool draw(void *ctx) { return menu_run(machine(ctx), 0xC2E613, DRAW_THE_SCREEN, menu_draw_load_screen); }

static bool ask_which_to_carry_on_from(void *ctx) { return menu_run(machine(ctx), 0xC2E67D, ASK_WHICH, menu_ask_which_to_carry_on_from); }

/* The load is the save code's C, which the original reaches by a JSL; it
 * comes back with A's high byte clear. */
static bool load(void *ctx)
{
    Snes *s = machine(ctx);
    GameState *g = state_of(s);
    TIMED_AS(g, 0xFFFAA3, saves_load_game(g, m_platform(s), menu_save_slot_shown(g)));
    return true;
}

static bool carry_on(void *ctx) { return menu_run_long(machine(ctx), 0xC2E6D8, CARRY_OVER, menu_load_carried_over); }

static bool erase(void *ctx) { return menu_run(machine(ctx), 0xC2E729, ERASE_THE_SLOT, menu_erase_slot_entry); }

static bool empty_label(void *ctx) { return menu_call(machine(ctx), 0xC2E903, EMPTY_LABEL, LABEL_LOADER_IGNORES); }

static bool wait_a_frame(void *ctx) { return menu_wait_a_frame_from(machine(ctx), 0xC2E906); }

/* The command is a call out to the driver, which must come back to the
 * screen's code. */
static bool sound_output_sent(void *ctx)
{
    Snes *s = machine(ctx);
    MenuMachine m = menu_machine_now(s);
    menu_send_the_sound_output(state_of(s), m_platform(s));
    return menu_back(s, 0xC2E6A5, m);
}

/* From a choice on the original's code runs with byte wide registers. */
static void byte_wide(void *ctx) { menu_set_widths(machine(ctx), true, true); }

static void note(void *ctx, LoadScreenNote n, uint8_t value)
{
    Snes *s = machine(ctx);
    switch (n) {
    case LOAD_NOTE_START_SLOT: ORIGINAL_EFFECT(leave_start_slot_loaded(s, value)); break;
    case LOAD_NOTE_ERASE_ASKED: ORIGINAL_EFFECT(leave_step_set(s, value, false)); break;
    case LOAD_NOTE_NOT_ERASING: ORIGINAL_EFFECT(leave_erase_tested(s)); break;
    case LOAD_NOTE_MOVED: ORIGINAL_EFFECT(leave_moved(s)); break;
    case LOAD_NOTE_LOADED: ORIGINAL_EFFECT(menu_leave_a_high_byte_clear(s)); break;
    /* the registers as setting up the state leaves them: a new game hands
     * A's high byte on to the sound command's call, whose driver keeps it on
     * its stack */
    case LOAD_NOTE_NEW_GAME_SET_UP: ORIGINAL_EFFECT(menu_leave_new_game_set_up(s)); break;
    case LOAD_NOTE_NEW_GAME_PLUS: ORIGINAL_EFFECT(menu_leave_compare(s, NEW_GAME_PLUS_X, NEW_GAME_PLUS_X)); break;
    case LOAD_NOTE_BACK_TO_CHOOSING: ORIGINAL_EFFECT(leave_wide(s)); break;
    case LOAD_NOTE_ANSWER_MOVED: ORIGINAL_EFFECT(leave_answer_moved(s)); break;
    /* the list leaves A clear, and a no hands its high byte on to the sound's
     * call, whose driver keeps it on its stack */
    case LOAD_NOTE_ANSWERED: ORIGINAL_EFFECT(menu_leave_a_high_byte_clear(s)); break;
    case LOAD_NOTE_ERASE_YES: ORIGINAL_EFFECT(menu_leave_compare(s, ERASE_YES, ERASE_YES)); break;
    case LOAD_NOTE_CHOOSING_AGAIN: ORIGINAL_EFFECT(leave_step_set(s, value, true)); break;
    case LOAD_NOTE_FOURTH_ROW_COMPARED: ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_C, value >= FOURTH_ROW)); break;
    /* in the P the cursor's sound saves as it starts */
    case LOAD_NOTE_FOURTH_ROW_FLIPPED: ORIGINAL_EFFECT(menu_leave_load(s, value)); break;
    /* erasing saves P as it starts and puts it back as it ends, with the
     * widths it calls the label's drawing with between */
    case LOAD_NOTE_ERASING:
        menu_save_p(s);
        menu_set_widths(s, true, true);
        break;
    case LOAD_NOTE_SLOT_FORGOTTEN:
        ORIGINAL_EFFECT(leave_label_record(state_of(s), value));
        menu_set_widths(s, false, false);
        break;
    case LOAD_NOTE_ERASED:
        ORIGINAL_EFFECT(leave_design(s));
        menu_restore_p(s);
        break;
    }
}

/* The steps' ends are the original's jumps, which the port stops at. */
static bool ends(void *ctx, LoadScreenEnd end)
{
    Snes *s = machine(ctx);
    switch (end) {
    case LOAD_FADES_IN: m_stop_at(s, FADE_IN_START, READS_NOTHING); break;
    case LOAD_LEAVES_FOR_THE_GAME: m_stop_at_own(s, EXIT_FOR_THE_GAME, READS_NOTHING); break;
    }
    return false;
}

const LoadScreenSteps menu_load_screen_on_the_machine = {
    read_cursor, tick, show_the_slot, draw, ask_which_to_carry_on_from, load, carry_on, erase, empty_label,
    wait_a_frame, sound_output_sent, byte_wide, note, ends,
};

/* ---- The entries --------------------------------------------------------------------------------- */

static void step(Snes *s, bool (*run)(GameState *, Platform *, const LoadScreenSteps *, void *))
{
    LoadScreenOnTheMachine m = { s };
    if (run(state_of(s), m_platform(s), &menu_load_screen_on_the_machine, &m))
        m_rts(s);
}

// $C2:E613
void menu_open_load_screen_entry(Snes *s) { step(s, menu_open_load_screen_with); }

// $C2:E61B
void menu_choose_on_load_screen_entry(Snes *s) { step(s, menu_choose_on_load_screen_with); }

// $C2:E6AE
void menu_choose_game_to_carry_on_from_entry(Snes *s) { step(s, menu_choose_game_to_carry_on_from_with); }

// $C2:E705
void menu_answer_erase_entry(Snes *s) { step(s, menu_answer_erase_with); }

// $C2:E7C3
/* It saves P as it starts and puts it back as it ends. */
void menu_draw_load_screen(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    LoadScreenOnTheMachine m = { s };
    if (!menu_draw_load_screen_with(state_of(s), m_platform(s), &menu_load_screen_on_the_machine, &m))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:E841
/* The question's label is set by the typesetter with 8-bit A. */
void menu_ask_which_to_carry_on_from(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, false);
    menu_ask_which_game_to_carry_on_from(state_of(s));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:D10D
void menu_draw_slot_rows_entry(Snes *s)
{
    menu_draw_slot_rows(state_of(s));
    m_rts(s);
}

// $C2:D32C
void menu_draw_slot_window(Snes *s)
{
    GameState *g = state_of(s);
    menu_redraw_slot_window(g, menu_save_slot_shown(g));
    ORIGINAL_EFFECT(leave_design(s));
    m_rts(s);
}

/* ---- Erasing a slot ------------------------------------------------------------------------------ */

// $C2:E8B7
void menu_erase_slot_entry(Snes *s) { step(s, menu_erase_slot_with); }

/* ---- The guards ---------------------------------------------------------------------------------- */

/* The screen runs on the menus' page with the data bank on work RAM's, its
 * sounds' cells within reach; the rest is what its steps take of the state
 * (load_screen.c). The guards look at the save memory without the machine's
 * time. */
bool menu_load_screen_runs(Snes *s)
{
    if (!menu_on_page_work_ram(s) || !menu_sounds_on_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_load_screen_can_choose(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_erase_is_answerable(Snes *s)
{
    if (!menu_on_page_work_ram(s) || !menu_sounds_on_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_erase_can_be_answered(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_load_screen_draws(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_load_screen_can_draw(state_of(s), m_platform(s));
    vars_peek(false);
    return ok;
}

/* A slot's window is one of the three rows', and the colours it loads can be
 * counted as a change (menu/windows.c). */
bool menu_slot_window_is_drawable(Snes *s)
{
    if (!menu_on_page_work_ram(s) || !menu_palette_change_counts(s))
        return false;
    vars_peek(true);
    bool ok = menu_save_slot_shown(state_of(s)) < MENU_SAVE_SLOTS;
    vars_peek(false);
    return ok;
}
