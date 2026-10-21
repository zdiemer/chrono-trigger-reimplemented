/* Saving the game from the menus: the main menu's panel for it, which is the
 * save screen as it opens, the save screen the main menu opens
 * (MENU_SCREEN_SAVE, menu/menu.h), and the one a save point opens on
 * (MENU_SCREEN_SAVE_POINT).
 *
 * The save screen is the load screen's three slots (menu/load_screen.c):
 * their windows and rows, the summary of the slot the game was last saved
 * in (menu/save_screen.c), the slots' rows' per-line effect and the
 * screen's colours. It is drawn over two frames, the slots first and then
 * the summary, the panels' tasks held meanwhile. A save point's save screen
 * opens on it over the main menu's backdrop, and going back from it closes
 * the menus; the main menu's opens only where the game may be saved without
 * a save point, and going back from it returns to the main menu.
 *
 * On either, choosing an empty slot saves in it at once; choosing one that
 * holds a game first asks whether to save over it. Saving plays its sound,
 * writes the game to the cartridge and, a frame later, reads the slot's
 * summary back and shows it, the slot's window drawn afresh in the design it
 * was saved with. */
#include "menu/saving.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/items.h"
#include "menu/load_screen.h"
#include "menu/main_menu.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/save_screen.h"
#include "menu/saves.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "overworld/overworld.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "sound/sound.h"
#include "vars.h"

/* ---- Drawing ------------------------------------------------------------------------------------- */

/* The cursor's task held three frames while the screen is drawn. */
enum { HOLD_THE_CURSOR = 3 };

/* The front layer below its top eight rows, which the summary is drawn
 * into afresh. */
enum { TILES_A_ROW = 32, SUMMARY_FROM_ROW = 8, SUMMARY_ROWS = 24 };

/* The text layer's scroll: a line up. */
enum { TEXT_LAYER = 2, A_LINE_UP = 0xFFFF };

/* The colours past the windows' (the load screen's own), into the
 * palette's third row's second half. */
enum { TEXT_COLOURS_AT = 2 * 16 };

/* The screen's per-line effects: the colour math and slots 3 and 4. */
enum { SAVE_SCREEN_LINE_EFFECTS = 0x19 };

/* Nothing described: the description window's item and its staleness. */
enum { NOTHING_DESCRIBED = 0xFF };

/* The first frame's: the slots' windows and rows. */
static void draw_the_slots(GameState *g, Platform *pf)
{
    menu_draw_the_slots(g, pf);
    menu_queue_fixed_upload(g, MENU_UPLOAD_SLOT_LABELS);
    set_menu_palette_changed(g, 0);
    set_menu_tasks_countdown(g, MENU_CURSOR_TASK, HOLD_THE_CURSOR);
}

/* The second frame's: the front layer cleared for the summary, nothing
 * described, the slot last saved in shown, and the cursor on it. */
static void make_room_for_the_summary(GameState *g)
{
    for (unsigned tile = SUMMARY_FROM_ROW * TILES_A_ROW; tile < (SUMMARY_FROM_ROW + SUMMARY_ROWS) * TILES_A_ROW; tile++)
        set_menu_front_tilemap(g, tile, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * SUMMARY_ROWS * TILES_A_ROW - 2));
    set_menu_description_item(g, NOTHING_DESCRIBED);
    set_menu_description_stale(g, NOTHING_DESCRIBED);
    uint8_t slot = save_slot_chosen(g);
    set_menu_save_slot_shown(g, slot);
    set_menu_save_slot_marked(g, slot);
}

/* The third frame's: the text layer a line up, the colours, the sprites
 * cleared, the colour math, the effects, and the layers queued. */
static void finish_the_screen(GameState *g, Platform *pf)
{
    set_menu_scroll_y(g, TEXT_LAYER, A_LINE_UP);
    for (unsigned i = 0; i < MENU_LOAD_SCREEN_TEXT_COLOURS; i++)
        set_menu_palette(g, TEXT_COLOURS_AT + i, menu_load_screen_text_colour(g, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_LOAD_SCREEN_TEXT_COLOURS));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    set_menu_line_effects(g, SAVE_SCREEN_LINE_EFFECTS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

// $C2:D0DB
void menu_put_save_panel_away_now(GameState *g) { set_menu_line_effects(g, SAVE_SCREEN_LINE_EFFECTS); }

/* A save point's save screen: its windows over the panel, the slots' rows'
 * effect with the cursor on the slot shown, and the cursor's sprite. */
enum { SAVE_SCREEN_WINDOWS = 0xC138 };

// $C2:D0E5
void menu_lay_out_save_screen(GameState *g, Platform *pf)
{
    set_menu_cursor_index(g, menu_save_slot_marked(g));
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, SAVE_SCREEN_WINDOWS));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    set_menu_line_effects(g, SAVE_SCREEN_LINE_EFFECTS);
    TIMED_AS(g, 0xC2CFFB, menu_put_slot_rows_effect(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
    TIMED_AS(g, 0xC2E923, menu_clear_sprites_but_cursor(g));
}

/* ---- Saving, and the question before a game is saved over ----------------------------------- */

/* The save screens' steps: the opening, choosing a slot (the second), then
 * the question whether to save over the game in it. */
enum { CHOOSING_A_SLOT = 1 };

/* Back on the main menu, choosing again: its second step. */
enum { MAIN_MENU_CHOOSING = 1 };

/* The main menu saves only where it doesn't take a save point: not where the
 * menus noted the place as one of the locations (menu/items.c), and with no
 * vehicle out (the Epoch, a dactyl, or the record between them). */
static bool out(uint8_t vehicle_state) { return (vehicle_state & VEHICLE_OUT) != 0; }

static bool saving_refused_here(const GameState *g)
{
    return menu_place_is_noted_as_a_location(g) || out(overworld_vehicle_state(g, VEHICLE_EPOCH)) ||
           out(overworld_second_vehicle_state(g)) || out(overworld_vehicle_state(g, VEHICLE_DACTYL));
}

static void back_to_the_main_menu(GameState *g)
{
    set_menu_screen(g, MENU_SCREEN_MAIN_MENU);
    set_menu_screen_step(g, MAIN_MENU_CHOOSING);
}

static void next_step(GameState *g) { set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1)); }

/* The question's window over the slots, its cursor on "yes" (its first
 * place, the screen's fourth), and the line effects with the window's; then
 * the slots' windows back, the cursor on the slot it was on. */
enum { QUESTION_WINDOWS = 0xC16F, WINDOWS_WITHOUT_THE_QUESTION = 0xC191 };
enum { ANSWER_YES = 3 };
enum { LINE_EFFECTS_WITH_THE_QUESTION = 0x75, LINE_EFFECTS_AFTER_THE_QUESTION = 0x59 };

// $C2:D4BB
static void ask_whether_to_save_over(GameState *g, Platform *pf)
{
    set_menu_cursor_index(g, ANSWER_YES);
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, QUESTION_WINDOWS));
    set_menu_line_effects(g, LINE_EFFECTS_WITH_THE_QUESTION);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

// $C2:D4D5
static void put_the_question_away(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, WINDOWS_WITHOUT_THE_QUESTION));
    set_menu_line_effects(g, LINE_EFFECTS_AFTER_THE_QUESTION);
    set_menu_cursor_index(g, menu_kept_cursor(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

/* A slot chosen that holds a game: the question, the slot kept to come back
 * to. */
static void about_to_save_over(GameState *g, Platform *pf)
{
    set_menu_kept_cursor(g, menu_cursor_index(g));
    next_step(g);
    ask_whether_to_save_over(g, pf);
}

/* The slot saved in: its window drawn afresh, and its tilemap and the slots'
 * chapters sent. */
// $C2:CF21
static void show_the_slot_saved(GameState *g)
{
    TIMED_AS(g, 0xC2D32C, menu_redraw_slot_window(g, menu_save_slot_shown(g)));
    menu_queue_fixed_upload(g, MENU_UPLOAD_SLOT_TILEMAP_2);
    menu_queue_fixed_upload(g, MENU_UPLOAD_CHAPTER_LABELS);
}

/* The slot's game marked as there before the save writes it. */
enum { SLOT_HOLDS_A_GAME = 0xFF };

/* ---- On the original's machine ------------------------------------------------------------------- */

enum {
    LEAVE_THE_MENUS = 0xC282B2, /* the menus close, back to the field */
    FADE_IN = 0xC283B2,         /* the screen's fade in started */
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


// $C2:D065
/* The slots, a frame, the summary of the slot last saved in, a frame, and
 * the rest. */
void menu_show_save_panel(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    draw_the_slots(g, pf);
    if (!menu_wait_a_frame_from(s, 0xC2D07E))
        return;
    make_room_for_the_summary(g);
    menu_set_widths(s, true, false);
    TIMED_AS(g, 0xC2D36C, menu_draw_the_slot_shown(g, pf));
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
    set_menu_palette_changed(g, 0);
    if (!menu_wait_a_frame_from(s, 0xC2D0AA))
        return;
    finish_the_screen(g, pf);
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
/* The main menu's backdrop, the save screen over it, then on to choosing as
 * it fades in. */
void menu_open_save_screen(Snes *s)
{
    GameState *g = state_of(s);
    TIMED_AS(g, 0xC29AAD, menu_draw_main_menu_backdrop(g));
    if (!menu_run(s, 0xC2D4FB, 0xC2D065, menu_show_save_panel))
        return;
    TIMED_AS(g, 0xC2D0E5, menu_lay_out_save_screen(g, m_platform(s)));
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    m_stop_at(s, FADE_IN, READS_NOTHING);
}

// $C2:D506
/* Choosing a slot saves in it (menu_save_in_the_slot, where this stops),
 * going back closes the menus, and otherwise the slot under the cursor is
 * shown as it moves among them. */
void menu_choose_on_save_screen(Snes *s)
{
    GameState *g = state_of(s);
    menu_step_reads_the_cursor_and_tests_its_buttons(s);
    if (menu_asked(g, MENU_CHOOSE))
        m_stop_at_own(s, SAVE_IN_THE_SLOT, READS_A_HIGH);
    else if (menu_asked(g, MENU_GO_BACK))
        m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING);
    else if (menu_slot_under_the_cursor_shows(g))
        NATIVE_TIME_AS(s, MOVE_AMONG_THE_SLOTS, menu_move_among_the_slots(s));
    else
        m_stop_at_own(s, MOVE_AMONG_THE_SLOTS, READS_A_HIGH);
}

static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, CURSOR_SOUND, menu_play_cursor_sound); }

/* What the steps' last loads leave in the flags they come back with
 * (scaffolding). */
static void leave_the_step(Snes *s) { menu_leave_load(s, menu_screen_step(state_of(s))); }

// $C2:CEDC
/* The main menu's save screen opens: where the game can't be saved here, a
 * buzz and back to the main menu; otherwise a tick and the screen laid out
 * over the panel. */
void menu_open_saving(Snes *s)
{
    GameState *g = state_of(s);
    if (saving_refused_here(g)) {
        back_to_the_main_menu(g);
        m_stop_at_own(s, REFUSAL_SOUND, READS_A_HIGH);
        return;
    }
    ORIGINAL_EFFECT(menu_leave_load(s, 0));   /* the refusal's test's flags, which the tick saves */
    if (!tick(s, 0xC2CEF5))
        return;
    next_step(g);
    TIMED_AS(g, 0xC2D0E5, menu_lay_out_save_screen(g, m_platform(s)));
    ORIGINAL_EFFECT(leave_the_step(s));
    m_rts(s);
}

/* The saved sound: the effect on the effects' second pair of voices, from
 * the middle, sent from the save's own call to the driver. */
enum { SAVED_SOUND = 0x8A, DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The save's last wait keeps every register but A on the task's stack. */
enum { THE_WAIT_KEEPS_ALL_BUT_A = DC_A };

static bool play_the_saved_sound(Snes *s)
{
    MenuMachine m = menu_machine_now(s);
    ORIGINAL_EFFECT(m_set_a8(s, AU_CENTRE));
    au_command(m_platform(s), PF_CALL_DC(0xC2CF46, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = SOUND_EFFECT_ON_VOICES_4_5, .p1 = SAVED_SOUND, .p2 = AU_CENTRE, .count = 2 });
    return menu_back(s, 0xC2CF4A, m);
}

// $C2:CF37
/* Save the game in the slot shown: the sound, the slot marked as holding a
 * game, the game written; a frame on, the slot's summary read back; and a
 * frame's wait, from which the task goes on into its caller. */
void menu_save_the_game(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t slot = menu_save_slot_shown(g);
    if (!play_the_saved_sound(s))
        return;
    set_menu_save_slot_in_use(g, slot, SLOT_HOLDS_A_GAME);
    TIMED_AS(g, 0xFFF9FB, saves_save_game(g, m_platform(s), slot));
    if (!menu_wait_a_frame_from(s, 0xC2CF55))
        return;
    if (!menu_run(s, 0xC2CF58, READ_THE_SLOT_SUMMARY, menu_read_slot_summary))
        return;
    m_stop_at_own(s, MENU_TASK_WAITS_A_FRAME, THE_WAIT_KEEPS_ALL_BUT_A);
}

/* The save, from its JSR at `site`: run as C where its port runs, and
 * otherwise the original's, which reads A's high byte as its call to the
 * driver does. True only where the original's came back, frames later: the
 * port stops in its last wait. */
static bool save_from(Snes *s, uint32_t site)
{
    if (menu_runs_as_port(s, SAVE_THE_GAME, menu_save_the_game))
        return menu_run(s, site, SAVE_THE_GAME, menu_save_the_game);
    return menu_call(s, site, SAVE_THE_GAME, READS_A_HIGH);
}

/* The slot saved in shown, and the upload's registers left as its queueing
 * leaves them. */
static void finish_showing_the_slot_saved(Snes *s)
{
    show_the_slot_saved(state_of(s));
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_CHAPTER_LABELS));
    ORIGINAL_EFFECT(m_set_nz16(s, MENU_UPLOAD_CHAPTER_LABELS));
}

// $C2:CF21
/* After the save: the slot saved in shown. */
void menu_show_the_slot_saved(Snes *s)
{
    finish_showing_the_slot_saved(s);
    m_rts(s);
}

// $C2:CF0A
/* The slot under the cursor chosen to save in: one that holds a game asks
 * first; an empty one is saved in at once. */
void menu_save_in_the_slot(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t holds = menu_save_slot_in_use(g, menu_save_slot_shown(g));
    if (holds != 0) {
        ORIGINAL_EFFECT(menu_leave_load(s, holds));   /* the slot's test's flags, which the tick saves */
        if (!tick(s, 0xC2CF11))
            return;
        about_to_save_over(g, m_platform(s));
        ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_BACK_LAYER));
        ORIGINAL_EFFECT(leave_the_step(s));
        m_rts(s);
        return;
    }
    if (!save_from(s, 0xC2CF1E))
        return;
    NATIVE_TIME_AS(s, SHOW_THE_SLOT_SAVED, finish_showing_the_slot_saved(s));
    m_rts(s);
}

// $C2:CEFD
/* Choosing a slot on the main menu's save screen: choosing one saves in it,
 * going back returns to the main menu, and otherwise the slot under the
 * cursor is shown as it moves among them. */
void menu_choose_slot_to_save_in(Snes *s)
{
    GameState *g = state_of(s);
    menu_step_reads_the_cursor_and_tests_its_buttons(s);
    if (menu_asked(g, MENU_CHOOSE))
        NATIVE_TIME_AS(s, SAVE_IN_THE_SLOT, menu_save_in_the_slot(s));
    else if (menu_asked(g, MENU_GO_BACK))
        m_stop_at(s, MAIN_MENU, READS_NOTHING);
    else if (menu_slot_under_the_cursor_shows(g))
        NATIVE_TIME_AS(s, MOVE_AMONG_THE_SLOTS, menu_move_among_the_slots(s));
    else
        m_stop_at_own(s, MOVE_AMONG_THE_SLOTS, READS_A_HIGH);
}

/* "No", or going back: a tick, the question put away, choosing a slot again. */
// $C2:CF88
static void answer_no(Snes *s)
{
    GameState *g = state_of(s);
    if (!tick(s, 0xC2CF88))
        return;
    set_menu_screen_step(g, CHOOSING_A_SLOT);
    put_the_question_away(g, m_platform(s));
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_BACK_LAYER));
    ORIGINAL_EFFECT(leave_the_step(s));
    m_rts(s);
}

// $C2:CF61
/* The question whether to save over the game in the slot: "yes" saves, and
 * then puts the question away and shows the slot; "no" or going back puts it
 * away; otherwise a tick where the cursor moved. */
void menu_answer_whether_to_save_over(Snes *s)
{
    GameState *g = state_of(s);
    menu_step_reads_the_cursor_and_tests_its_buttons(s);
    if (menu_asked(g, MENU_CHOOSE)) {
        bool yes = menu_cursor_index(g) == ANSWER_YES;
        ORIGINAL_EFFECT(menu_leave_compare(s, menu_cursor_index(g), ANSWER_YES));
        if (!yes) {
            answer_no(s);
            return;
        }
        if (!save_from(s, 0xC2CF7B))
            return;
        set_menu_screen_step(g, CHOOSING_A_SLOT);
        put_the_question_away(g, m_platform(s));
        NATIVE_TIME_AS(s, SHOW_THE_SLOT_SAVED, finish_showing_the_slot_saved(s));
        m_rts(s);
        return;
    }
    if (menu_asked(g, MENU_GO_BACK)) {
        answer_no(s);
        return;
    }
    uint8_t at = menu_cursor_index(g), was = menu_cursor_frame_start(g);
    ORIGINAL_EFFECT(menu_leave_compare(s, at, was));
    if (at != was && !tick(s, 0xC2CF71))
        return;
    m_rts(s);
}

// $C2:CF92
/* A step no code moves the save screens to: the cursor hidden for the frame,
 * and either button puts the question away as "no" does. */
void menu_close_the_question_on_a_button(Snes *s)
{
    GameState *g = state_of(s);
    set_menu_cursor_hidden(g, (uint8_t)(menu_cursor_hidden(g) - 1));
    menu_step_reads_the_cursor_and_tests_its_buttons(s);
    if (menu_asked(g, MENU_CHOOSE) || menu_asked(g, MENU_GO_BACK))
        answer_no(s);
    else
        m_rts(s);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

bool menu_save_panel_shows(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = save_slot_chosen(state_of(s)) < MENU_SAVE_SLOTS;
    vars_peek(false);
    return ok;
}

/* The slot shown one of the three, on the menus' page with the data bank on
 * work RAM's: what saving in it, and drawing its window, take. */
bool menu_slot_can_be_saved_in(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_save_slot_shown(state_of(s)) < MENU_SAVE_SLOTS;
    vars_peek(false);
    return ok;
}

/* ... and a slot saved in shown: its window's palette change counted too. */
bool menu_slot_saved_shows(Snes *s) { return menu_slot_can_be_saved_in(s) && menu_slot_window_is_drawable(s); }

/* The main menu's save screen opens on the slot marked, which its layout
 * takes (or refuses with a buzz, which takes nothing), where the place's note
 * is one the menus' opening makes. */
bool menu_saving_opens(Snes *s)
{
    if (!menu_save_screen_lays_out(s))
        return false;
    vars_peek(true);
    bool ok = menu_place_note_is_known(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_save_screen_lays_out(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_save_slot_marked(state_of(s)) < MENU_SAVE_SLOTS;
    vars_peek(false);
    return ok;
}
