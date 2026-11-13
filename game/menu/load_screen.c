/* The load screen: where every game starts after the title, by going on from
 * a save slot or starting a new one.
 *
 * It is the menus' screen MENU_SCREEN_LOAD (menu/menu.h), and its main task runs one of four steps a frame
 * (menu_screen_step):
 *
 * - opening: the screen drawn -- its windows, a row for each of the three
 *   slots in the design the slot's game was saved with, the summary of the
 *   slot saved last (menu/save_screen.c), and once the game has been finished
 *   a fourth row with two choices -- then faded in;
 * - choosing: the cursor moves between the slots that hold a game and the
 *   fourth row. Choosing a slot loads its game and leaves the menus for it;
 *   choosing the fourth row starts a new game, saved later in the first empty
 *   slot, or (its second choice) a New Game+, which first asks which game to
 *   carry on from;
 * - which game a New Game+ carries on from: the slots again, without the
 *   fourth row; going back returns to choosing;
 * - erasing: held Select and Start on the second controller, over a slot,
 *   ask whether to erase it, and a yes spoils the slot's mark on the
 *   cartridge. No movie reaches it.
 *
 * Loading a game and the state a new one starts from are the save code's
 * (menu/saves.c); leaving the menus for the game is the menus' own exit
 * ($C2:8320).
 *
 * Each step is one sequence (load_screen.h), calling the menus' cursor,
 * ticks and waits and the routines it goes on to, each of those run only on
 * the state it handles (below). */
#include "menu/load_screen.h"

#include "charge.h"
#include "core/program.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/fades.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/save_screen.h"
#include "menu/saves.h"
#include "menu/vram.h"
#include "menu/typeset.h"
#include "menu/windows.h"
#include "platform/audio.h"
#include "port/vars_ram.h"

/* ---- Drawing the screen ------------------------------------------------------------------------ */

/* The screen's window command lists: its windows; the panel the chosen slot's
 * summary sits in; the fourth row's two choices; the erase question, and the
 * panel it covers put back. */
enum {
    SCREEN_WINDOWS = 0xC0ED,
    SUMMARY_PANEL = 0xC322,
    NEW_GAME_CHOICES = 0xC36B,
    ERASE_QUESTION = 0xC391,
    ERASE_QUESTION_CLOSED = 0xC191,
};

/* ---- The slots' rows --------------------------------------------------------------------------- */

/* A slot's row: its number, its name label (drawn with the dialog's font when
 * the menus opened, $C2:D2C4), and a window in the design the slot's game was
 * saved with, the slot's own palette row, with a small box at its left end.
 * The rows start at $7E:4EC6 in BG3's tilemap, four tile rows apart. */
enum { SLOT_ROWS_AT = 0x4EC6, ROW_TILE_ROWS = 4, LABEL_TILES_A_SLOT = 0x20, NUMBER_COLUMNS = 2, LABEL_COLUMNS = 16 };
enum { NUMBER_ATTRIBUTE = 0x3000, LABEL_AFTER_NUMBER = 5 };

/* The slot's window: 24 tiles by 4 at $7E:3E8C in BG2's, four rows a slot,
 * and the box 4 by 4 at its row's third column. */
enum { SLOT_WINDOWS_AT = 0x3E8C, WINDOW_ROW_BYTES = ROW_TILE_ROWS * MENU_TILEMAP_ROW_BYTES };
enum { ROW_START_BITS = 0xFFC0, BOX_COLUMN_BYTES = 4 };
static const MenuWindowSize SLOT_WINDOW = { 24, 4 }, SLOT_BOX = { 4, 4 };

/* The window design the player chose: the menus' own windows take it back
 * after a slot's row is drawn in the slot's. */
enum { DESIGN_BITS = 7 };

static void draw_slot_window_itself(GameState *g, uint16_t at, MenuWindowStyle style)
{
    menu_draw_window_frame(g, at, SLOT_WINDOW, style);
    menu_fill_window_inside(g, at, SLOT_WINDOW, style);
    menu_load_window_colours(g, style);
}

// $C2:D32C
/* A slot's window, in the design its game was saved with and the slot's own
 * palette row (the one after the slot's number, in the tiles from $300 on),
 * and the box at its left end in the same style; then the player's own
 * design back. */
enum { SLOT_TILES = 3 };

static void draw_slot_window(GameState *g, uint8_t slot)
{
    set_menu_window_design(g, menu_save_slot_design(g, slot));
    uint16_t at = (uint16_t)(SLOT_WINDOWS_AT + (slot & 3) * WINDOW_ROW_BYTES);
    set_menu_window_place(g, at);
    MenuWindowStyle style = menu_design_style(g, (uint8_t)(((slot + 1) << 2) | SLOT_TILES));
    set_menu_window_style(g, style);
    TIMED_AS(g, 0xC2ECDB, draw_slot_window_itself(g, at, style));
    uint16_t box = (uint16_t)((at & ROW_START_BITS) | BOX_COLUMN_BYTES);
    set_menu_window_place(g, box);
    TIMED_AS(g, 0xC2EDF6, menu_draw_window_frame(g, box, SLOT_BOX, style));
    TIMED_AS(g, 0xC2EE7F, menu_fill_window_inside(g, box, SLOT_BOX, style));
    set_menu_window_design(g, (uint8_t)(game_settings(g, 1) & DESIGN_BITS));
}

/* The slots' labels' first tiles, a slot's $20 after the one before's; the
 * rows' drawing leaves the one after the last, where a label drawn after them
 * would start. */
static void keep_next_label_tile(GameState *g, uint8_t first)
{
    set_menu_graphic_first_tile(g, (uint16_t)((menu_graphic_first_tile(g) & 0xFF00) | first));
}

// $C2:D10D, $C2:D131
/* The three slots' rows. */
void menu_draw_slot_rows(GameState *g)
{
    uint16_t row = SLOT_ROWS_AT;
    for (uint8_t slot = 0; slot < MENU_SAVE_SLOTS; slot++, row = (uint16_t)(row + WINDOW_ROW_BYTES)) {
        uint16_t number = (uint16_t)(NUMBER_ATTRIBUTE | (slot * NUMBER_COLUMNS + 1));
        uint16_t after = row;
        TIMED_AS(g, 0xC2FBB4, after = menu_lay_out_strip(g, row, NUMBER_COLUMNS, number));
        TIMED_AS(g, 0xC2FB97, menu_lay_out_label(g, (uint16_t)(after + LABEL_AFTER_NUMBER * MENU_TILE_BYTES),
                                                 LABEL_COLUMNS, (uint8_t)(LABEL_TILES_A_SLOT * (slot + 1))));
        TIMED_AS(g, 0xC2D32C, draw_slot_window(g, slot));
    }
    keep_next_label_tile(g, (uint8_t)(LABEL_TILES_A_SLOT * (MENU_SAVE_SLOTS + 1)));
    /* it counts the slots in the slot shown's cell, which it leaves past the
     * last: no slot shown until the screen picks one */
    set_menu_save_slot_shown(g, MENU_SAVE_SLOTS);
}

/* The screen's windows and the slots' rows, as the load screen and the save
 * screen draw them both. */
void menu_draw_the_slots(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, SCREEN_WINDOWS);
    TIMED_AS(g, 0xC2D10D, menu_draw_slot_rows(g));
}

/* ---- Opening the screen ------------------------------------------------------------------------ */

/* The slot the cursor starts on: the one saved last, or the fourth row where
 * no slot holds a game. */
enum { FOURTH_ROW = 3 };

static bool any_slot_in_use(const GameState *g)
{
    return (menu_save_slot_in_use(g, 0) | menu_save_slot_in_use(g, 1) | menu_save_slot_in_use(g, 2)) != 0;
}

static uint8_t slot_to_start_on(GameState *g, Platform *pf)
{
    uint8_t last = saves_last_slot(pf, PF_AT(0xC2E7CF));
    return any_slot_in_use(g) ? last : FOURTH_ROW;
}

/* The screen's colours past the windows': the first half of palette row 2,
 * and row 7. */
static void load_screen_colours(GameState *g)
{
    enum { ROW_2_LOWER = 2 * 16, ROW_7 = 7 * 16 };
    for (unsigned i = 0; i < MENU_LOAD_SCREEN_TEXT_COLOURS; i++)
        set_menu_palette(g, ROW_2_LOWER + i, menu_load_screen_text_colour(g, i));
    for (unsigned i = 0; i < MENU_LOAD_SCREEN_ROW_7_COLOURS; i++)
        set_menu_palette(g, ROW_7 + i, menu_load_screen_row_7_colour(g, i));
}

/* The screen's per-line effects: the colour math table and slots 1, 3 and 4. */
enum { LOAD_SCREEN_LINE_EFFECTS = 0x1B };

/* After the chosen slot's summary: its panel, the colour math table, the
 * fourth row's choices once the game has been finished, the colours, and the
 * whole screen queued. */
static void finish_screen(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    if (saves_finished(pf, PF_AT(0xC2E7F9)))
        menu_draw_windows(g, pf, NEW_GAME_CHOICES);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    set_menu_line_effects(g, LOAD_SCREEN_LINE_EFFECTS);
    load_screen_colours(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_SLOT_LABELS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_WHOLE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER_WHOLE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER_WHOLE);
}

static bool show_the_slot(GameState *g, Platform *pf);
static bool draw(GameState *g, Platform *pf);
static void ask_which_to_carry_on_from(GameState *g);
static bool load(GameState *g, Platform *pf);
static bool carry_on(GameState *g, Platform *pf);
static bool empty_label(GameState *g);
static void ends(GameState *g, Platform *pf, LoadScreenEnd end);

// $C2:E7C3
/* Draw the load screen: its windows, the slots' rows, the summary of the slot
 * the cursor starts on, the panel around it, and the rest. */
bool menu_draw_load_screen_with(GameState *g, Platform *pf)
{
    menu_draw_the_slots(g, pf);
    uint8_t slot = slot_to_start_on(g, pf);
    set_menu_save_slot_shown(g, slot);
    set_menu_cursor_index(g, slot);
    TIMED_AS(g, 0xC2D36C, menu_draw_the_slot_shown(g, pf));
    set_menu_text_attribute(g, 0);
    menu_draw_windows(g, pf, SUMMARY_PANEL);
    if (!show_the_slot(g, pf))
        return false;
    finish_screen(g, pf);
    return true;
}

// $C2:E613
/* The screen's first step: drawn, then on to choosing as it fades in. */
bool menu_open_load_screen_with(GameState *g, Platform *pf)
{
    if (!draw(g, pf))
        return false;
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    ends(g, pf, LOAD_FADES_IN);
    return true;
}

/* ---- Choosing ---------------------------------------------------------------------------------- */

/* The steps. */
enum { STEP_CHOOSING = 1, STEP_CARRY_ON_FROM = 2, STEP_ERASING = 3 };

/* Holding Select and Start on the second controller asks to erase the slot
 * the cursor is on. */
enum { ERASE_BUTTONS = 0x30 };

static bool erase_asked(const GameState *g) { return (uint8_t)(second_controller(g) >> 8) == ERASE_BUTTONS; }

/* The erase question's two answers are cursor places 4 (yes) and 5 (no). */
enum { ERASE_YES = 4, ERASE_NO = 5, QUESTION_LINE_EFFECTS = 0x7F, ANSWERED_LINE_EFFECTS = 0x5B };

// $C2:E891
/* Ask whether to erase the slot the cursor is on: the question's window over
 * the summary, the cursor on yes. */
static void ask_to_erase(GameState *g, Platform *pf)
{
    uint8_t slot = menu_cursor_index(g);
    set_menu_save_slot_shown(g, slot);
    set_menu_kept_cursor(g, slot);
    set_menu_cursor_index(g, ERASE_YES);
    set_menu_line_effects(g, QUESTION_LINE_EFFECTS);
    menu_draw_windows(g, pf, ERASE_QUESTION);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_LOWER_HALF);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

/* The slot a new game will be saved in: the first that holds no game, or the
 * first slot where all do. */
static uint8_t slot_for_a_new_game(const GameState *g)
{
    for (uint8_t slot = 0; slot < MENU_SAVE_SLOTS; slot++)
        if (menu_save_slot_in_use(g, slot) == 0)
            return slot;
    return 0;
}

/* The fourth row's two choices stand $70 apart; the second, New Game+, is at
 * $78 across. */
enum { NEW_GAME_PLUS_X = 0x78 };

static bool new_game_plus_chosen(GameState *g, Platform *pf)
{
    return saves_finished(pf, PF_AT(0xC2E66D)) && menu_cursor_places_x(g, FOURTH_ROW) == NEW_GAME_PLUS_X;
}

/* A new game: the state it starts from, marked new in the story's memory. */
static void start_the_new_game(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2956E, saves_start_a_new_game(g));
    saves_note_how_it_starts(g, SAVES_NEW_GAME);
    set_times_finished(g, saves_times_finished(pf, PF_AT(0xC2E666)));
}

// $C2:E841
/* New Game+: the slots again, without the fourth row, to choose the game it
 * carries on from; the slot's header gives way to the question's label. */
enum { QUESTION_LABEL = 3, QUESTION_LABEL_AT = 0x5E00, QUESTION_LABEL_TABLE = 0xFFCF3B };

/* The question's label, once drawn: two strips of it over the header. */
enum {
    LABEL_TOP_AT = 0x52C8,
    LABEL_TOP_COLUMNS = 8,
    LABEL_TOP_TILE = 0x3021,
    LABEL_BOTTOM_AT = 0x5348,
    LABEL_BOTTOM_COLUMNS = 9,
    LABEL_BOTTOM_TILE = 0x3062,
};

void menu_ask_which_game_to_carry_on_from(GameState *g)
{
    /* counted in its first byte: the second, read with it, never carries */
    uint16_t taken = menu_save_header_taken(g);
    set_menu_save_header_taken(g, (uint16_t)((taken & 0xFF00) | (uint8_t)(taken + 1)));
    set_menu_load_destination(g, QUESTION_LABEL_AT);
    TIMED_AS(g, 0xC2F90C, (void)menu_set_label(g, QUESTION_LABEL_TABLE, QUESTION_LABEL, QUESTION_LABEL_AT));
    TIMED_AS(g, 0xC2FBB4, (void)menu_lay_out_strip(g, LABEL_TOP_AT, LABEL_TOP_COLUMNS, LABEL_TOP_TILE));
    TIMED_AS(g, 0xC2FBB4, (void)menu_lay_out_strip(g, LABEL_BOTTOM_AT, LABEL_BOTTOM_COLUMNS, LABEL_BOTTOM_TILE));
    menu_queue_fixed_upload(g, MENU_UPLOAD_CARRY_ON_LABEL);
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
}

/* Leaving the menus for the game: the sound driver told whether to play in
 * stereo (the settings' bit 3), the menus' frame count kept for the battle's
 * random numbers; then out of the menus. */
enum { SOUND_OUTPUT = 0xF3, STEREO_SETTING = 0x08 };

void menu_send_the_sound_output(GameState *g, Platform *pf)
{
    au_command(pf, PF_CALL(0xC2E6A1, 0xC70004),
               (AudioCommand){ .command = SOUND_OUTPUT, .p1 = (uint8_t)(game_settings(g, 0) & STEREO_SETTING), .count = 1 });
}

static bool leave_for_the_game(GameState *g, Platform *pf)
{
    menu_send_the_sound_output(g, pf);
    set_frames_at_leaving(g, menu_frame_counter(g));
    ends(g, pf, LOAD_LEAVES_FOR_THE_GAME);
    return true;
}

/* No choice this frame: the slot the cursor is on shown, and a tick where it
 * moved. */
static bool show_and_tick(GameState *g, Platform *pf)
{
    if (!show_the_slot(g, pf))
        return false;
    if (menu_cursor_frame_start(g) != menu_cursor_index(g))
        menu_tick(g, pf);
    return true;
}

/* A slot chosen: the game goes on in it. */
static void choose_slot(GameState *g, uint8_t slot)
{
    set_menu_save_slot_shown(g, slot);
    set_save_slot_chosen(g, slot);
}

/* Its game loaded; then out to the game. */
static bool load_and_leave(GameState *g, Platform *pf, uint8_t slot)
{
    choose_slot(g, slot);
    if (!load(g, pf))
        return false;
    return leave_for_the_game(g, pf);
}

/* A New Game+ carries on from the slot's game; then out to the game. */
static bool carry_on_and_leave(GameState *g, Platform *pf, uint8_t slot)
{
    choose_slot(g, slot);
    return carry_on(g, pf) && leave_for_the_game(g, pf);
}

/* A new game, saved later in the first slot with no game: out to it. */
static bool leave_for_a_new_game(GameState *g, Platform *pf)
{
    set_save_slot_chosen(g, slot_for_a_new_game(g));
    return leave_for_the_game(g, pf);
}

/* The fourth row chosen: a new game's state, marked new; then a New Game+
 * asks which game to carry on from, and a new game leaves for the game. */
static bool start_new_game(GameState *g, Platform *pf)
{
    start_the_new_game(g, pf);
    if (!new_game_plus_chosen(g, pf))
        return leave_for_a_new_game(g, pf);
    set_menu_save_slot_in_use(g, FOURTH_ROW, 0);
    ask_which_to_carry_on_from(g);
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    return true;
}

// $C2:E61B
/* Choosing: Select and Start held on the second controller ask to erase the
 * slot the cursor is on; otherwise the cursor moves and shows each slot, and
 * a choice loads the slot's game or starts a new one. */
bool menu_choose_on_load_screen_with(GameState *g, Platform *pf)
{
    if (erase_asked(g) && menu_cursor_index(g) < FOURTH_ROW) {
        set_menu_screen_step(g, STEP_ERASING);
        menu_tick(g, pf);
        ask_to_erase(g, pf);
        return true;
    }
    menu_read_the_cursor_for_a_step(g);
    if (!menu_asked(g, MENU_CHOOSE))
        return show_and_tick(g, pf);
    menu_tick(g, pf);
    uint8_t slot = menu_cursor_index(g);
    return slot < FOURTH_ROW ? load_and_leave(g, pf, slot) : start_new_game(g, pf);
}

// $C2:E6AE
/* Which game a New Game+ carries on from: as choosing, without the fourth
 * row; going back returns to choosing, with the cursor on the fourth row and
 * the header given back. */
enum { HEADER_ROWS_BYTES = 0x100 };

static void back_to_choosing(GameState *g)
{
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) - 1));
    set_menu_save_slot_in_use(g, FOURTH_ROW, (uint8_t)(menu_save_slot_in_use(g, FOURTH_ROW) - 1));
    set_menu_cursor_index(g, FOURTH_ROW);
    for (unsigned i = 0; i < HEADER_ROWS_BYTES; i++)
        set_menu_carry_on_question(g, i, 0);
    set_menu_save_header_taken(g, 0);
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
}

bool menu_choose_game_to_carry_on_from_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    bool chose = menu_asked(g, MENU_CHOOSE);
    if (!chose && !menu_asked(g, MENU_GO_BACK))
        return show_and_tick(g, pf);
    menu_tick(g, pf);
    if (!chose) {
        back_to_choosing(g);
        return true;
    }
    uint8_t slot = menu_cursor_index(g);
    return slot < FOURTH_ROW ? carry_on_and_leave(g, pf, slot) : leave_for_a_new_game(g, pf);
}

// $C2:E705
/* Erasing: yes or no to the question; going back is a no. Either way the
 * question closes and the cursor goes back to the slot. */
bool menu_answer_erase_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    bool chose = menu_asked(g, MENU_CHOOSE);
    if (!chose && !menu_asked(g, MENU_GO_BACK)) {
        if (menu_cursor_index(g) == menu_cursor_frame_start(g))
            return true;
        menu_tick(g, pf);
        return true;
    }
    if (!chose)
        set_menu_cursor_index(g, ERASE_NO);
    menu_draw_windows(g, pf, ERASE_QUESTION_CLOSED);
    if (menu_cursor_index(g) == ERASE_YES) {
        if (!menu_erase_slot(g, pf))
            return false;
    }
    set_menu_line_effects(g, ANSWERED_LINE_EFFECTS);
    set_menu_cursor_index(g, menu_kept_cursor(g));
    set_menu_screen_step(g, STEP_CHOOSING);
    menu_tick(g, pf);
    menu_queue_fixed_upload(g, MENU_UPLOAD_SLOT_TILEMAP_2);
    return true;
}

/* ---- Erasing a slot ------------------------------------------------------------------------------ */

enum { MARKED_RECORD_BANK = 0x7E, FOURTH_ROW_LEFT_X = 0x08 };

/* The slot's game gone: the slot no longer in use, its mark on the cartridge
 * spoilt; with no game left, the game no longer counts as finished and the
 * fourth row's cursor goes back to its first choice. */
void menu_forget_slot(GameState *g, Platform *pf, uint8_t slot)
{
    set_menu_save_slot_in_use(g, slot, 0);
    set_menu_save_slot_design(g, slot, 0);
    saves_spoil_mark(pf, PF_AT(0xC2E8C6), slot, (uint8_t)(slot * 2));
    if (!any_slot_in_use(g)) {
        saves_forget_finished(pf, PF_AT(0xC2E8DA));
        set_menu_cursor_places_x(g, FOURTH_ROW, FOURTH_ROW_LEFT_X);
    }
}

// $C2:E8B7
/* The slot's game forgotten, its row drawn empty and, a frame on, its
 * label (the slot's chapter's two rows of graphic, menu_data.h) queued and
 * its window drawn again. */
bool menu_erase_slot_with(GameState *g, Platform *pf)
{
    uint8_t slot = menu_save_slot_shown(g);
    menu_forget_slot(g, pf, slot);
    if (!empty_label(g))
        return false;
    menu_task_waits(g, 1);
    MenuUpload label = menu_slot_label_upload(slot);
    TIMED_AS(g, 0xC2838E, menu_queue_upload(g, label));
    TIMED_AS(g, 0xC2D32C, menu_redraw_slot_window(g, slot));
    return true;
}

/* The label's upload, from where the slots' chapter labels are drawn. */
MenuUpload menu_slot_label_upload(uint8_t slot)
{
    uint16_t from = (uint16_t)(MENU_CHAPTER_LABELS_AT + slot * MENU_CHAPTER_LABEL_BYTES);
    return (MenuUpload){ (uint16_t)(MENU_CHAPTER_LABELS_VRAM + slot * MENU_CHAPTER_LABEL_VRAM_WORDS),
                         (uint32_t)MARKED_RECORD_BANK << 16 | from, MENU_CHAPTER_LABEL_BYTES, 0 };
}

void menu_redraw_slot_window(GameState *g, uint8_t slot) { draw_slot_window(g, slot); }

/* ---- What the steps take ------------------------------------------------------------------------- */

/* The screen runs with room in the upload queue for what a step queues; what
 * the game gives its steps besides is what the routines they run take: a
 * cursor on one of the four rows with a place to move to, at least one slot
 * with a game and summaries whose members are characters (menu/save_screen.c). */
enum { MOST_UPLOADS_A_STEP = 6, MOST_UPLOADS_OPENING = 8 };

/* The screen's cursor moves between its four rows: each row's neighbours are
 * rows (the top bit marks a push that leaves them, which the screen's places
 * don't have). */
enum { LEAVES_THE_ROWS = 0x80 };

static bool is_row(uint8_t place) { return (uint8_t)(place & ~LEAVES_THE_ROWS) <= FOURTH_ROW; }

static bool cursor_stays_on_the_rows(const GameState *g)
{
    for (uint8_t row = 0; row <= FOURTH_ROW; row++)
        if (!is_row(menu_cursor_places_up(g, row)) || !is_row(menu_cursor_places_down(g, row))
            || !is_row(menu_cursor_places_left(g, row)) || !is_row(menu_cursor_places_right(g, row)))
            return false;
    return true;
}

bool menu_load_screen_can_choose(const GameState *g)
{
    return menu_cursor_can_move(g) && cursor_stays_on_the_rows(g) && menu_load_screen_slot_can_show(g)
           && menu_upload_queue_takes(g, MOST_UPLOADS_A_STEP);
}

/* The opening's drawing: the slot the cartridge says was saved last is one of
 * the rows. (The colour math records it builds its table from are the
 * screen's own lists'.) The save memory is looked at without its time. */
bool menu_load_screen_can_draw(const GameState *g, Platform *pf)
{
    return menu_upload_queue_takes(g, MOST_UPLOADS_OPENING) && sv_peek8(pf, SAVES_LAST_SLOT) <= FOURTH_ROW
           && menu_save_slots_can_show(g);
}

/* The erase question's answer: the cursor on yes or no, whose neighbours are
 * the two answers, the slot the question is over one of the three and the
 * cursor kept for going back on one of the rows. */
static bool is_answer(uint8_t place) { return place == ERASE_YES || place == ERASE_NO; }

/* Erasing redraws the slot's window, whose colours take a palette change. */
bool menu_slot_can_be_erased(const GameState *g)
{
    return menu_palette_changes_have_room(g) && menu_save_slot_shown(g) < MENU_SAVE_SLOTS;
}

bool menu_erase_can_be_answered(const GameState *g)
{
    uint8_t at = menu_cursor_index(g);
    return menu_upload_queue_takes(g, MOST_UPLOADS_A_STEP) && menu_cursor_can_move(g)
           && menu_palette_changes_have_room(g) && is_answer(at)
           && is_answer((uint8_t)(menu_cursor_places_up(g, at) & ~LEAVES_THE_ROWS))
           && is_answer((uint8_t)(menu_cursor_places_down(g, at) & ~LEAVES_THE_ROWS))
           && is_answer((uint8_t)(menu_cursor_places_left(g, at) & ~LEAVES_THE_ROWS))
           && is_answer((uint8_t)(menu_cursor_places_right(g, at) & ~LEAVES_THE_ROWS))
           && menu_save_slot_shown(g) < MENU_SAVE_SLOTS && menu_kept_cursor(g) <= FOURTH_ROW;
}

/* ---- The steps' services ----------------------------------------------------------------------- */

/* The routines the steps reach, each run only on the state it handles;
 * otherwise the program goes no further. */
static bool takes(GameState *g, bool ok, uint32_t routine, const char *what)
{
    if (!ok)
        program_breaks_a_contract(g, routine, what);
    return ok;
}

static bool show_the_slot(GameState *g, Platform *pf)
{
    if (!takes(g, PORT_CHECK(menu_load_screen_slot_can_show(g)), 0xC2E743, "a save slot the port doesn't show"))
        return false;
    bool back = false;
    TIMED_AS(g, 0xC2E743, back = menu_show_save_slot_with(g, pf));
    return back;
}

static bool draw(GameState *g, Platform *pf)
{
    if (!takes(g, PORT_CHECK(menu_load_screen_can_draw(g, pf)), 0xC2E7C3, "a load screen the port doesn't draw"))
        return false;
    bool back = false;
    TIMED_AS(g, 0xC2E7C3, back = menu_draw_load_screen_with(g, pf));
    return back;
}

static void ask_which_to_carry_on_from(GameState *g)
{
    TIMED_AS(g, 0xC2E841, menu_ask_which_game_to_carry_on_from(g));
}

static bool load(GameState *g, Platform *pf)
{
    if (!takes(g, PORT_CHECK(menu_save_code_can_run(g)), 0xFFFAA3, "a saved game the port doesn't load"))
        return false;
    TIMED_AS(g, 0xFFFAA3, saves_load_game(g, pf, menu_save_slot_shown(g)));
    return true;
}

static bool carry_on(GameState *g, Platform *pf)
{
    if (!takes(g, PORT_CHECK(menu_carry_over_can_run(g)), 0xFFFAED, "a New Game+ the port doesn't carry over"))
        return false;
    TIMED_AS(g, 0xFFFAED, saves_carry_over(g, pf, menu_save_slot_shown(g)));
    return true;
}

static bool empty_label(GameState *g)
{
    bool drawn = false;
    TIMED_AS(g, 0xC2D28D, drawn = menu_draw_empty_label(g));
    return drawn;
}

/* The steps' ends: the fade in's task started, or
 * the menus closed for the game (which never comes back to the screen). */
static void ends(GameState *g, Platform *pf, LoadScreenEnd end)
{
    switch (end) {
    case LOAD_FADES_IN: TIMED_AS(g, 0xC283B2, menu_start_the_fade_in(g)); break;
    case LOAD_LEAVES_FOR_THE_GAME: menu_close(g, pf, true); break;
    }
}

/* A step, run only on the state it handles. */
static bool step(GameState *g, Platform *pf, bool ok, uint32_t routine,
                 bool (*run)(GameState *, Platform *))
{
    if (!takes(g, ok, routine, "a load screen the port doesn't take"))
        return false;
    return run(g, pf);
}

bool menu_open_load_screen(GameState *g, Platform *pf)
{
    return step(g, pf, menu_load_screen_can_draw(g, pf), 0xC2E613, menu_open_load_screen_with);
}

bool menu_choose_on_load_screen(GameState *g, Platform *pf)
{
    return step(g, pf, menu_load_screen_can_choose(g), 0xC2E61B, menu_choose_on_load_screen_with);
}

bool menu_choose_game_to_carry_on_from(GameState *g, Platform *pf)
{
    return step(g, pf, menu_load_screen_can_choose(g), 0xC2E6AE, menu_choose_game_to_carry_on_from_with);
}

bool menu_answer_erase(GameState *g, Platform *pf)
{
    return step(g, pf, menu_erase_can_be_answered(g), 0xC2E705, menu_answer_erase_with);
}

bool menu_erase_slot(GameState *g, Platform *pf)
{
    return step(g, pf, menu_slot_can_be_erased(g), 0xC2E8B7, menu_erase_slot_with);
}
