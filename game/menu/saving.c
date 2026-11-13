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
 * was saved with.
 *
 * Each step is one sequence (saving.h), calling the menus' cursor, ticks
 * and waits and the routines it goes on to, each of those run only on the
 * state it handles (below). */
#include "menu/saving.h"

#include "charge.h"
#include "core/program.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/fades.h"
#include "menu/items.h"
#include "menu/load_screen.h"
#include "menu/main_menu.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/save_screen.h"
#include "menu/saves.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "overworld/overworld.h"
#include "platform/audio.h"
#include "sound/sound.h"
#include "port/vars_ram.h"
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
    charge_block_move(g, 2 * SUMMARY_ROWS * TILES_A_ROW - 2);
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
    charge_block_move(g, 2 * MENU_LOAD_SCREEN_TEXT_COLOURS);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    set_menu_line_effects(g, SAVE_SCREEN_LINE_EFFECTS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

// $C2:D0DB
void menu_put_save_panel_away(GameState *g) { set_menu_line_effects(g, SAVE_SCREEN_LINE_EFFECTS); }

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

// $C2:CF21
void menu_show_the_slot_saved(GameState *g) { show_the_slot_saved(g); }

/* The saved sound: the effect on the effects' second pair of voices, from
 * the middle, sent from the save's own call to the driver. */
enum { SAVED_SOUND = 0x8A };

void menu_send_the_saved_sound(Platform *pf)
{
    au_command(pf, PF_CALL(0xC2CF46, 0xC70004),
               (AudioCommand){ .command = SOUND_EFFECT_ON_VOICES_4_5, .p1 = SAVED_SOUND, .p2 = AU_CENTRE, .count = 2 });
}

static bool panel(GameState *g, Platform *pf);
static bool lay_out(GameState *g, Platform *pf);
static bool save(GameState *g, Platform *pf);
static bool ends(GameState *g, Platform *pf, SavingEnd end);

/* ---- The panel and a save point's save screen ---------------------------------------------------- */

// $C2:D065
/* The slots, a frame, the summary of the slot last saved in, a frame, and
 * the rest. */
bool menu_show_save_panel_with(GameState *g, Platform *pf)
{
    draw_the_slots(g, pf);
    menu_task_waits(g, 1);
    make_room_for_the_summary(g);
    TIMED_AS(g, 0xC2D36C, menu_draw_the_slot_shown(g, pf));
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
    set_menu_palette_changed(g, 0);
    menu_task_waits(g, 1);
    finish_the_screen(g, pf);
    return true;
}

// $C2:D4F8
/* The main menu's backdrop, the save screen over it, then on to choosing as
 * it fades in. */
bool menu_open_save_screen_with(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC29AAD, menu_draw_main_menu_backdrop(g));
    if (!panel(g, pf) || !lay_out(g, pf))
        return false;
    next_step(g);
    return ends(g, pf, SAVING_FADES_IN);
}

// $C2:D506
/* Choosing a slot saves in it, going back closes the menus, and otherwise
 * the slot under the cursor is shown as it moves among them. */
bool menu_choose_on_save_screen_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    if (menu_asked(g, MENU_CHOOSE))
        return ends(g, pf, SAVING_SAVES_IN_THE_SLOT_JUMPED);
    if (menu_asked(g, MENU_GO_BACK))
        return ends(g, pf, SAVING_LEAVES_THE_MENUS);
    return ends(g, pf, SAVING_MOVES_AMONG_THE_SLOTS);
}

/* ---- The main menu's save screen ----------------------------------------------------------------- */

// $C2:CEDC
/* The main menu's save screen opens: where the game can't be saved here, a
 * buzz and back to the main menu; otherwise a tick and the screen laid out
 * over the panel. */
bool menu_open_saving_with(GameState *g, Platform *pf)
{
    if (saving_refused_here(g)) {
        back_to_the_main_menu(g);
        return ends(g, pf, SAVING_BUZZES);
    }
    menu_tick(g, pf);
    next_step(g);
    if (!lay_out(g, pf))
        return false;
    return true;
}

// $C2:CEFD
/* Choosing a slot on the main menu's save screen: choosing one saves in it,
 * going back returns to the main menu, and otherwise the slot under the
 * cursor is shown as it moves among them. */
bool menu_choose_slot_to_save_in_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    if (menu_asked(g, MENU_CHOOSE))
        return ends(g, pf, SAVING_SAVES_IN_THE_SLOT);
    if (menu_asked(g, MENU_GO_BACK))
        return ends(g, pf, SAVING_BACK_TO_THE_MAIN_MENU);
    return ends(g, pf, SAVING_MOVES_AMONG_THE_SLOTS);
}

/* ---- Saving, and the question before a game is saved over ----------------------------------- */

// $C2:CF37
/* Save the game in the slot shown: the sound, the slot marked as holding a
 * game, the game written; a frame on, the slot's summary read back. A
 * frame's wait follows, which the original jumps to, so that the task goes
 * on from it into the save's caller. */
bool menu_save_the_game_with(GameState *g, Platform *pf)
{
    uint8_t slot = menu_save_slot_shown(g);
    menu_send_the_saved_sound(pf);
    set_menu_save_slot_in_use(g, slot, SLOT_HOLDS_A_GAME);
    TIMED_AS(g, 0xFFF9FB, saves_save_game(g, pf, slot));
    menu_task_waits(g, 1);
    return menu_slot_summary_read_back(g, pf);
}

static bool show_the_saved_slot(GameState *g);

// $C2:CF0A
/* The slot under the cursor chosen to save in: one that holds a game asks
 * first; an empty one is saved in at once, and shown. */
bool menu_save_in_the_slot_with(GameState *g, Platform *pf)
{
    uint8_t holds = menu_save_slot_in_use(g, menu_save_slot_shown(g));
    if (holds != 0) {
        menu_tick(g, pf);
        about_to_save_over(g, pf);
        return true;
    }
    return save(g, pf) && show_the_saved_slot(g);
}

// $C2:CF88
/* "No", or going back: a tick, the question put away, choosing a slot again. */
static bool answer_no(GameState *g, Platform *pf)
{
    menu_tick(g, pf);
    set_menu_screen_step(g, CHOOSING_A_SLOT);
    put_the_question_away(g, pf);
    return true;
}

// $C2:CF61
/* The question whether to save over the game in the slot: "yes" saves, and
 * then puts the question away and shows the slot; "no" or going back puts it
 * away; otherwise a tick where the cursor moved. */
bool menu_answer_whether_to_save_over_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    if (menu_asked(g, MENU_CHOOSE)) {
        if (menu_cursor_index(g) != ANSWER_YES)
            return answer_no(g, pf);
        if (!save(g, pf))
            return false;
        set_menu_screen_step(g, CHOOSING_A_SLOT);
        put_the_question_away(g, pf);
        return show_the_saved_slot(g);
    }
    if (menu_asked(g, MENU_GO_BACK))
        return answer_no(g, pf);
    if (menu_cursor_index(g) != menu_cursor_frame_start(g))
        menu_tick(g, pf);
    return true;
}

// $C2:CF92
/* A step no code moves the save screens to: the cursor hidden for the frame,
 * and either button puts the question away as "no" does. */
bool menu_close_the_question_on_a_button_with(GameState *g, Platform *pf)
{
    set_menu_cursor_hidden(g, (uint8_t)(menu_cursor_hidden(g) - 1));
    menu_read_the_cursor_for_a_step(g);
    if (menu_asked(g, MENU_CHOOSE) || menu_asked(g, MENU_GO_BACK))
        return answer_no(g, pf);
    return true;
}

/* ---- What the steps take ------------------------------------------------------------------------- */

/* The panel, and a save point's save screen, show the slot the game was
 * last saved in, one of the three. */
bool menu_save_panel_can_show(const GameState *g) { return save_slot_chosen(g) < MENU_SAVE_SLOTS; }

/* The save screen opens on the slot marked. */
bool menu_save_screen_can_lay_out(const GameState *g) { return menu_save_slot_marked(g) < MENU_SAVE_SLOTS; }

/* The main menu's save screen opens on the slot marked, which its layout
 * takes (or refuses with a buzz, which takes nothing), where the place's note
 * is one the menus' opening makes. */
bool menu_saving_can_open(const GameState *g) { return menu_save_screen_can_lay_out(g) && menu_place_note_is_known(g); }

/* Saving in the slot shown, one of the three; and a slot saved in shown,
 * its window's palette change counted too. */
bool menu_slot_can_be_saved(const GameState *g) { return menu_save_slot_shown(g) < MENU_SAVE_SLOTS; }

bool menu_slot_saved_can_show(const GameState *g)
{
    return menu_slot_can_be_saved(g) && menu_palette_changes_have_room(g);
}

/* ---- The steps' services ------------------------------------------------------------------------- */

/* The routines the steps reach, each run only on the state it handles;
 * otherwise the program goes no further. */
static bool takes(GameState *g, bool ok, uint32_t routine, const char *what)
{
    if (!ok)
        program_breaks_a_contract(g, routine, what);
    return ok;
}

static bool panel(GameState *g, Platform *pf)
{
    bool back = false;
    TIMED_AS(g, 0xC2D065, back = menu_show_save_panel(g, pf));
    return back;
}

static bool lay_out(GameState *g, Platform *pf)
{
    if (!takes(g, PORT_CHECK(menu_save_screen_can_lay_out(g)), 0xC2D0E5, "a save screen the port doesn't lay out"))
        return false;
    TIMED_AS(g, 0xC2D0E5, menu_lay_out_save_screen(g, pf));
    return true;
}

/* The save, and then its last wait for the next frame. */
static bool save(GameState *g, Platform *pf)
{
    if (!takes(g, menu_slot_can_be_saved(g) && menu_save_code_can_run(g), 0xC2CF37,
               "a save the port doesn't make"))
        return false;
    bool saved = false;
    TIMED_AS(g, 0xC2CF37, saved = menu_save_the_game_with(g, pf));
    if (saved)
        menu_task_waits(g, 1);
    return saved;
}

static bool show_the_saved_slot(GameState *g)
{
    if (!takes(g, PORT_CHECK(menu_slot_saved_can_show(g)), 0xC2CF21, "a slot saved the port doesn't show"))
        return false;
    TIMED_AS(g, 0xC2CF21, menu_show_the_slot_saved(g));
    return true;
}

/* The steps' ends: the fade in's task started, the menus closed (which never
 * comes back), the main menu chosen on again, the save in the slot or the
 * slots' frame (each where the state is one it handles), or the buzz. */
static bool ends(GameState *g, Platform *pf, SavingEnd end)
{
    bool back = true;
    switch (end) {
    case SAVING_FADES_IN: TIMED_AS(g, 0xC283B2, menu_start_the_fade_in(g)); break;
    case SAVING_LEAVES_THE_MENUS: menu_close(g, pf, false); break;
    case SAVING_BACK_TO_THE_MAIN_MENU: back = menu_back_to_the_main_menu(g, pf); break;
    case SAVING_SAVES_IN_THE_SLOT_JUMPED:
    case SAVING_SAVES_IN_THE_SLOT:
        if (!takes(g, PORT_CHECK(menu_slot_can_be_saved(g)), 0xC2CF0A, "a slot the port doesn't save in"))
            return false;
        TIMED_AS(g, 0xC2CF0A, back = menu_save_in_the_slot_with(g, pf));
        break;
    case SAVING_MOVES_AMONG_THE_SLOTS:
        if (!takes(g, PORT_CHECK(menu_slot_under_the_cursor_shows(g)), 0xC2CFA2, "a save slot the port doesn't show"))
            return false;
        TIMED_AS(g, 0xC2CFA2, back = menu_move_among_the_slots_with(g, pf));
        break;
    case SAVING_BUZZES: menu_buzz(g, pf); break;
    }
    return back;
}

/* The panel, where the state is one it handles. */
bool menu_show_save_panel(GameState *g, Platform *pf)
{
    if (!takes(g, PORT_CHECK(menu_save_panel_can_show(g)), 0xC2D065, "a save panel the port doesn't show"))
        return false;
    return menu_show_save_panel_with(g, pf);
}

/* A step, run only on the state it handles. */
static bool step(GameState *g, Platform *pf, bool ok, uint32_t routine,
                 bool (*run)(GameState *, Platform *))
{
    if (!takes(g, ok, routine, "a save screen the port doesn't take"))
        return false;
    return run(g, pf);
}

bool menu_open_save_screen(GameState *g, Platform *pf)
{
    return step(g, pf, menu_save_panel_can_show(g), 0xC2D4F8, menu_open_save_screen_with);
}

bool menu_choose_on_save_screen(GameState *g, Platform *pf)
{
    return step(g, pf, menu_cursor_can_move(g), 0xC2D506, menu_choose_on_save_screen_with);
}

bool menu_open_saving(GameState *g, Platform *pf)
{
    return step(g, pf, menu_saving_can_open(g), 0xC2CEDC, menu_open_saving_with);
}

bool menu_choose_slot_to_save_in(GameState *g, Platform *pf)
{
    return step(g, pf, menu_cursor_can_move(g), 0xC2CEFD, menu_choose_slot_to_save_in_with);
}

bool menu_answer_whether_to_save_over(GameState *g, Platform *pf)
{
    return step(g, pf, menu_cursor_can_move(g), 0xC2CF61, menu_answer_whether_to_save_over_with);
}

bool menu_close_the_question_on_a_button(GameState *g, Platform *pf)
{
    return step(g, pf, menu_cursor_can_move(g), 0xC2CF92, menu_close_the_question_on_a_button_with);
}
