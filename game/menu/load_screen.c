/* The load screen: where every game starts after the title, by going on from
 * a save slot or starting a new one.
 *
 * It is menu screen 14, and its main task runs one of four steps a frame
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
 * (menu/saves.c); leaving the menus
 * for the game is the menus' own exit ($C2:8320), where the port stops. */
#include "menu/load_screen.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/menu_data.h"
#include "menu/save_screen.h"
#include "menu/saves.h"
#include "menu/vram.h"
#include "menu/typeset.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/native.h"

/* ---- Drawing the screen ------------------------------------------------------------------------ */

/* The screen's window command lists: its windows; the panel the chosen slot's
 * summary sits in; the fourth row's two choices; the erase question, and the
 * panel it covers put back. */
enum { SCREEN_WINDOWS = 0xC0ED, SUMMARY_PANEL = 0xC322, NEW_GAME_CHOICES = 0xC36B, ERASE_QUESTION = 0xC391,
       ERASE_QUESTION_CLOSED = 0xC191 };

/* The screen's uploads (records in bank C2, menu_data.h): the three layers'
 * tilemaps as a whole, the text's graphics, and the parts the steps redraw. */
enum {
    UPLOAD_TEXT_GRAPHICS = 0xFC53, UPLOAD_BG1 = 0xFBCE, UPLOAD_BG2 = 0xFBF8, UPLOAD_BG3 = 0xFC14,
    UPLOAD_BG1_QUESTION = 0xFBEA, UPLOAD_BG2_ROWS = 0xFBFF, UPLOAD_BG3_ROWS = 0xFC1B, UPLOAD_BG2_SUMMARY = 0xFC0D,
    UPLOAD_NEW_GAME_PLUS = 0xE88A,
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

/* What the rows' drawing leaves in the drawing's cells: the box's size in
 * the operands, and the cells the box's frame and inside leave. */
static void leave_box(GameState *g, uint16_t box, MenuWindowStyle style)
{
    set_menu_window_operands(g, 4, SLOT_BOX.columns);
    set_menu_window_operands(g, 5, SLOT_BOX.rows);
    menu_leave_window_cells(g, box, SLOT_BOX, style);
}

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
    ORIGINAL_EFFECT(leave_box(g, box, style));
    set_menu_window_design(g, (uint8_t)(game_settings(g, 1) & DESIGN_BITS));
}

/* The slots' labels' first tiles, a slot's $20 after the one before's; the
 * rows' drawing leaves the one after the last, where a label drawn after them
 * would start. */
static void keep_next_label_tile(GameState *g, uint8_t first)
{
    set_menu_graphic_first_tile(g, (uint16_t)((menu_graphic_first_tile(g) & 0xFF00) | first));
}

/* For the original's code and shadow mode: it draws each row from the
 * drawing origin and counts the slots in the slot shown's cell, which it
 * leaves past the last. */
static void leave_rows_drawn(GameState *g)
{
    set_menu_draw_origin(g, (uint16_t)(SLOT_ROWS_AT + MENU_SAVE_SLOTS * WINDOW_ROW_BYTES));
    set_menu_save_slot_shown(g, MENU_SAVE_SLOTS);
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
    ORIGINAL_EFFECT(leave_rows_drawn(g));
}

/* The screen's windows and the slots' rows, as the load screen and the save
 * screen draw them both. */
void menu_draw_the_slots(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, SCREEN_WINDOWS);
    TIMED_AS(g, 0xC2D10D, menu_draw_slot_rows(g));
}

// $C2:D10D
void menu_draw_slot_rows_entry(Snes *s)
{
    menu_draw_slot_rows(state_of(s));
    m_rts(s);
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
    menu_queue_fixed_upload(g, UPLOAD_TEXT_GRAPHICS);
    menu_queue_fixed_upload(g, UPLOAD_BG1);
    menu_queue_fixed_upload(g, UPLOAD_BG2);
    menu_queue_fixed_upload(g, UPLOAD_BG3);
}

/* The flags the screen's drawing has as it calls the summary's drawing and
 * showing, which those save as they start: the last load before each. */
static void leave_start_slot_loaded(Snes *s, uint8_t slot)
{
    menu_leave_load(s, slot);
    m_set_flag(s, CPU_FLAG_C, true);   /* the rows' last count, at three */
}

// $C2:E7C3
/* Draw the load screen: its windows, the slots' rows, the summary of the slot
 * the cursor starts on, the panel around it, and the rest. */
static bool draw_load_screen(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_set_widths(s, true, m_x8(s));
    menu_draw_the_slots(g, pf);
    uint8_t slot = slot_to_start_on(g, pf);
    set_menu_save_slot_shown(g, slot);
    set_menu_cursor_index(g, slot);
    ORIGINAL_EFFECT(leave_start_slot_loaded(s, slot));
    if (!menu_run(s, 0xC2E7E8, 0xC2D36C, menu_draw_save_slot))
        return false;
    set_menu_text_attribute(g, 0);
    menu_draw_windows(g, pf, SUMMARY_PANEL);
    if (!menu_run(s, 0xC2E7F3, 0xC2E743, menu_show_save_slot))
        return false;
    finish_screen(g, pf);
    return true;
}

void menu_draw_load_screen(Snes *s)
{
    menu_save_p(s);
    if (!draw_load_screen(s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* The dispatch's return: every step returns into the main task's yield. A
 * step that goes back to choosing leaves 16-bit widths behind it. */
static void leave_wide(Snes *s) { menu_set_widths(s, false, false); }

// $C2:E613
/* The screen's first step: drawn, then on to choosing, and the fade in
 * started by the menus' own task start ($C2:83B2, the original's), where the
 * port stops. */
enum { FADE_IN_START = 0xC283B2 };
enum { FADE_IN_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

void menu_open_load_screen(Snes *s)
{
    GameState *g = state_of(s);
    if (!menu_run(s, 0xC2E613, 0xC2E7C3, menu_draw_load_screen))
        return;
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    m_stop_at(s, FADE_IN_START, FADE_IN_IGNORES);
}

/* ---- Choosing ---------------------------------------------------------------------------------- */

/* The steps. */
enum { STEP_CHOOSING = 1, STEP_CARRY_ON_FROM = 2, STEP_ERASING = 3 };

/* The buttons (menu/cursor.c): choosing is bit 7, going back bit 6. */
enum { CHOSE = 0x80, WENT_BACK = 0x40 };

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
    menu_queue_fixed_upload(g, UPLOAD_BG1_QUESTION);
    menu_queue_fixed_upload(g, UPLOAD_BG2_ROWS);
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

static void ask_which_to_carry_on_from(GameState *g)
{
    /* counted in its first byte: the second, read with it, never carries */
    uint16_t taken = menu_save_header_taken(g);
    set_menu_save_header_taken(g, (uint16_t)((taken & 0xFF00) | (uint8_t)(taken + 1)));
    set_menu_load_destination(g, QUESTION_LABEL_AT);
}

/* The question's label, once drawn: two strips of it over the header. */
enum { LABEL_TOP_AT = 0x52C8, LABEL_TOP_COLUMNS = 8, LABEL_TOP_TILE = 0x3021,
       LABEL_BOTTOM_AT = 0x5348, LABEL_BOTTOM_COLUMNS = 9, LABEL_BOTTOM_TILE = 0x3062 };

static void show_question_label(GameState *g)
{
    TIMED_AS(g, 0xC2FBB4, (void)menu_lay_out_strip(g, LABEL_TOP_AT, LABEL_TOP_COLUMNS, LABEL_TOP_TILE));
    TIMED_AS(g, 0xC2FBB4, (void)menu_lay_out_strip(g, LABEL_BOTTOM_AT, LABEL_BOTTOM_COLUMNS, LABEL_BOTTOM_TILE));
    menu_queue_fixed_upload(g, UPLOAD_NEW_GAME_PLUS);
    menu_queue_fixed_upload(g, UPLOAD_BG3_ROWS);
}

/* Leaving the menus for the game: the sound driver told whether to play in
 * stereo (the settings' bit 3), the menus' frame count kept for the battle's
 * random numbers; then the menus' exit ($C2:8320), where the port stops. */
enum { SOUND_OUTPUT = 0xF3, STEREO_SETTING = 0x08 };
enum { DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
enum { EXIT_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static void leave_for_the_game(Snes *s)
{
    GameState *g = state_of(s);
    MenuMachine m = menu_machine_now(s);
    au_command(m_platform(s), PF_CALL_DC(0xC2E6A1, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = SOUND_OUTPUT, .p1 = (uint8_t)(game_settings(g, 0) & STEREO_SETTING), .count = 1 });
    if (!menu_back(s, 0xC2E6A5, m))
        return;
    set_frames_at_leaving(g, menu_frame_counter(g));
    m_stop_at_own(s, 0xC28320, EXIT_IGNORES);
}

/* ---- The steps on the machine ------------------------------------------------------------------ */

/* The steps are bridges: they run the menus' routines that make calls of
 * their own (the cursor's reading, the slot's showing, the sounds, a New
 * Game+'s load) as the original's JSRs reach them. Where one of those routines saves P as it
 * starts, the flags the original has there are on the stack under the call
 * that follows, where shadow mode compares them, so each step leaves them
 * first, from the comparison or test the original has just made. */

static void leave_compare(Snes *s, uint8_t a, uint8_t b) { menu_leave_compare(s, a, b); }
static void leave_load(Snes *s, uint8_t v) { menu_leave_load(s, v); }
static void leave_buttons(Snes *s) { menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s))); }
static void leave_moved(Snes *s)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}
static void leave_carry(Snes *s, bool c) { m_set_flag(s, CPU_FLAG_C, c); }

/* Read the cursor, and leave the flags of the buttons' test after it. */
static void read_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
    ORIGINAL_EFFECT(leave_buttons(s));
}

static bool tick(Snes *s, uint32_t at) { return menu_run(s, at, 0xC2EAC2, menu_play_cursor_sound); }

/* No choice this frame: the slot the cursor is on shown, and a tick where it
 * moved. */
static bool show_and_tick(Snes *s, uint32_t show_at, uint32_t tick_at)
{
    GameState *g = state_of(s);
    if (!menu_run(s, show_at, 0xC2E743, menu_show_save_slot))
        return false;
    if (menu_cursor_frame_start(g) == menu_cursor_index(g))
        return true;
    ORIGINAL_EFFECT(leave_moved(s));
    return tick(s, tick_at);
}

/* A slot chosen: the game goes on in it. */
static void choose_slot(GameState *g, uint8_t slot)
{
    set_menu_save_slot_shown(g, slot);
    set_save_slot_chosen(g, slot);
}

/* The load comes back with A's high byte clear, which the sound command's
 * call carries on to the menus' exit. */
static void leave_loaded(Snes *s) { menu_leave_a_high_byte_clear(s); }

/* Its game loaded; then out to the game. */
static void load_and_leave(Snes *s, uint8_t slot)
{
    GameState *g = state_of(s);
    choose_slot(g, slot);
    TIMED_AS(g, 0xFFFAA3, saves_load_game(g, m_platform(s), slot));
    ORIGINAL_EFFECT(leave_loaded(s));
    leave_for_the_game(s);
}

/* A New Game+ carries on from the slot's game; then out to the game. */
static void carry_on_and_leave(Snes *s, uint8_t slot)
{
    choose_slot(state_of(s), slot);
    if (!menu_run_long(s, 0xC2E6D8, 0xFFFAED, menu_load_carried_over))
        return;
    leave_for_the_game(s);
}

/* The fourth row chosen: a new game's state (the original's), marked new;
 * then a New Game+ asks which game to carry on from, and a new game leaves
 * for the game, to be saved in the first empty slot. */
static void start_new_game(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    start_the_new_game(g, pf);
    if (new_game_plus_chosen(g, pf)) {
        set_menu_save_slot_in_use(g, FOURTH_ROW, 0);
        ORIGINAL_EFFECT(leave_compare(s, NEW_GAME_PLUS_X, NEW_GAME_PLUS_X));
        if (!menu_run(s, 0xC2E67D, 0xC2E841, menu_ask_which_to_carry_on_from))
            return;
        set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
        m_rts(s);
        return;
    }
    set_save_slot_chosen(g, slot_for_a_new_game(g));
    leave_for_the_game(s);
}

// $C2:E61B
/* Choosing: Select and Start held on the second controller ask to erase the
 * slot the cursor is on; otherwise the cursor moves and shows each slot, and
 * a choice loads the slot's game or starts a new one. */
void menu_choose_on_load_screen(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t second = (uint8_t)(second_controller(g) >> 8), slot = menu_cursor_index(g);
    if (erase_asked(g) && slot < FOURTH_ROW) {
        set_menu_screen_step(g, STEP_ERASING);
        ORIGINAL_EFFECT(leave_load(s, STEP_ERASING));
        ORIGINAL_EFFECT(leave_carry(s, false));
        if (!tick(s, 0xC2E62C))
            return;
        ask_to_erase(g, m_platform(s));
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(second != ERASE_BUTTONS ? leave_compare(s, second, ERASE_BUTTONS) : leave_compare(s, slot, FOURTH_ROW));
    read_cursor(s);
    if (!(menu_cursor_buttons(g) & CHOSE)) {
        if (show_and_tick(s, 0xC2E63A, 0xC2E643))
            m_rts(s);
        return;
    }
    if (!tick(s, 0xC2E647))
        return;
    menu_set_widths(s, true, true);
    slot = menu_cursor_index(g);
    if (slot < FOURTH_ROW)
        load_and_leave(s, slot);
    else
        start_new_game(s);
}

// $C2:E6AE
/* Which game a New Game+ carries on from: as choosing, without the fourth
 * row; going back returns to choosing, with the cursor on the fourth row and
 * the header given back. */
enum { HEADER_ROWS_AT = 0x52C0, HEADER_ROWS_BYTES = 0x100 };

static void back_to_choosing(GameState *g)
{
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) - 1));
    set_menu_save_slot_in_use(g, FOURTH_ROW, (uint8_t)(menu_save_slot_in_use(g, FOURTH_ROW) - 1));
    set_menu_cursor_index(g, FOURTH_ROW);
    for (unsigned i = 0; i < HEADER_ROWS_BYTES; i++)
        set_menu_carry_on_question(g, i, 0);
    set_menu_save_header_taken(g, 0);
    menu_queue_fixed_upload(g, UPLOAD_BG3_ROWS);
}

void menu_choose_game_to_carry_on_from(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    uint8_t buttons = menu_cursor_buttons(g);
    if (!(buttons & (CHOSE | WENT_BACK))) {
        if (show_and_tick(s, 0xC2E6B8, 0xC2E6C1))
            m_rts(s);
        return;
    }
    uint32_t tick_at = (buttons & CHOSE) ? 0xC2E6C5 : 0xC2E6DF;
    if (!tick(s, tick_at))
        return;
    if (!(buttons & CHOSE)) {
        back_to_choosing(g);
        ORIGINAL_EFFECT(leave_wide(s));
        m_rts(s);
        return;
    }
    menu_set_widths(s, true, true);
    uint8_t slot = menu_cursor_index(g);
    if (slot < FOURTH_ROW) {
        carry_on_and_leave(s, slot);
        return;
    }
    set_save_slot_chosen(g, slot_for_a_new_game(g));
    leave_for_the_game(s);
}

// $C2:E705
/* Erasing: yes or no to the question; going back is a no. Either way the
 * question closes and the cursor goes back to the slot. */
void menu_answer_erase(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    uint8_t buttons = menu_cursor_buttons(g);
    if (!(buttons & (CHOSE | WENT_BACK))) {
        if (menu_cursor_index(g) != menu_cursor_frame_start(g)) {
            ORIGINAL_EFFECT(leave_compare(s, menu_cursor_index(g), menu_cursor_frame_start(g)));
            if (!tick(s, 0xC2E715))
                return;
        }
        m_rts(s);
        return;
    }
    if (!(buttons & CHOSE))
        set_menu_cursor_index(g, ERASE_NO);
    menu_draw_windows(g, m_platform(s), ERASE_QUESTION_CLOSED);
    if (menu_cursor_index(g) == ERASE_YES) {
        ORIGINAL_EFFECT(leave_compare(s, ERASE_YES, ERASE_YES));
        if (!menu_run(s, 0xC2E729, 0xC2E8B7, menu_erase_slot))
            return;
    }
    set_menu_line_effects(g, ANSWERED_LINE_EFFECTS);
    set_menu_cursor_index(g, menu_kept_cursor(g));
    set_menu_screen_step(g, STEP_CHOOSING);
    ORIGINAL_EFFECT(leave_load(s, STEP_CHOOSING));
    ORIGINAL_EFFECT(leave_carry(s, true));
    if (!tick(s, 0xC2E73A))
        return;
    menu_queue_fixed_upload(g, UPLOAD_BG2_SUMMARY);
    m_rts(s);
}

/* The question's label is set by the typesetter (menu_set_label) into the
 * header's graphic, with 8-bit A. */

// $C2:E841
void menu_ask_which_to_carry_on_from(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, false);
    ask_which_to_carry_on_from(g);
    TIMED_AS(g, 0xC2F90C, (void)menu_set_label(g, QUESTION_LABEL_TABLE, QUESTION_LABEL, QUESTION_LABEL_AT));
    show_question_label(g);
    menu_restore_p(s);
    m_rts(s);
}

/* ---- Erasing a slot ------------------------------------------------------------------------------ */

/* The slot's row redrawn empty: its label cleared and drawn again with the
 * empty slot's (by $C2:D28D, the original's, from the JSR at $C2:E903), and
 * after a frame ($C2:821E, the task's yield) queued: the label's two rows of
 * graphic, $200 bytes at $7E:7000 on, a slot's $100 words of VRAM from $7900
 * on. The record goes where the summary is drawn from. */
enum { EMPTY_LABELS_AT = 0x7000, EMPTY_LABEL_BYTES = 0x200, EMPTY_LABELS_VRAM = 0x7900, EMPTY_LABEL_VRAM_WORDS = 0x100 };
enum { MARKED_RECORD_BANK = 0x7E, FOURTH_ROW_LEFT_X = 0x08 };
enum { YIELD_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The slot's game gone: the slot no longer in use, its mark on the cartridge
 * spoilt; with no game left, the game no longer counts as finished and the
 * fourth row's cursor goes back to its first choice. */
static void forget_slot(GameState *g, Platform *pf, uint8_t slot)
{
    set_menu_save_slot_in_use(g, slot, 0);
    set_menu_save_slot_design(g, slot, 0);
    saves_spoil_mark(pf, PF_AT(0xC2E8C6), slot, (uint8_t)(slot * 2));
    if (!any_slot_in_use(g)) {
        saves_forget_finished(pf, PF_AT(0xC2E8DA));
        set_menu_cursor_places_x(g, FOURTH_ROW, FOURTH_ROW_LEFT_X);
    }
}

/* The label's upload. The original builds its record where the summary is
 * drawn from and queues it from there, and keeps the slot's rows in its
 * first cell. */
static MenuUpload label_upload(uint8_t slot)
{
    return (MenuUpload){ (uint16_t)(EMPTY_LABELS_VRAM + slot * EMPTY_LABEL_VRAM_WORDS),
                         (uint32_t)MARKED_RECORD_BANK << 16 | (uint16_t)(EMPTY_LABELS_AT + slot * EMPTY_LABEL_BYTES),
                         EMPTY_LABEL_BYTES, 0 };
}

static void leave_label_record(GameState *g, uint8_t slot)
{
    MenuUpload u = label_upload(slot);
    const uint8_t record[7] = { (uint8_t)u.vram, (uint8_t)(u.vram >> 8), (uint8_t)u.source, (uint8_t)(u.source >> 8),
                                (uint8_t)(u.source >> 16), (uint8_t)u.size, (uint8_t)(u.size >> 8) };
    for (unsigned i = 0; i < sizeof record; i++)
        set_menu_save_summary_byte(g, i, record[i]);
    set_menu_scratch_erased_slot_rows(g, (uint16_t)(slot << 8));
}

/* The slot's window's drawing leaves the player's design in A. */
static void leave_design(Snes *s) { s->cpu.a = (uint8_t)(game_settings(state_of(s), 1) & DESIGN_BITS); }

enum { LABEL_LOADER_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };   /* $C2:D28D */

// $C2:E8B7
void menu_erase_slot(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    uint8_t slot = menu_save_slot_shown(g);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    forget_slot(g, pf, slot);
    ORIGINAL_EFFECT(leave_label_record(g, slot));
    menu_set_widths(s, false, false);
    if (!menu_call(s, 0xC2E903, 0xC2D28D, LABEL_LOADER_IGNORES))
        return;
    if (!menu_call(s, 0xC2E906, 0xC2821E, YIELD_IGNORES))
        return;
    TIMED_AS(g, 0xC2838E, menu_queue_upload(g, label_upload(slot)));
    TIMED_AS(g, 0xC2D32C, draw_slot_window(g, slot));
    ORIGINAL_EFFECT(leave_design(s));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:D32C
void menu_draw_slot_window(Snes *s)
{
    GameState *g = state_of(s);
    draw_slot_window(g, menu_save_slot_shown(g));
    ORIGINAL_EFFECT(leave_design(s));
    m_rts(s);
}

/* ---- The guards --------------------------------------------------------------------------------- */

/* The screen runs on the menus' page with the data bank on work RAM's; what
 * the game gives its steps besides is what the routines they run take: a
 * cursor on one of the four rows with a place to move to, at least one slot
 * with a game and summaries whose members are characters (menu/save_screen.c),
 * the sounds' cells, and room in the upload queue for what a step queues. */
enum { QUEUE_BYTES = 0x100, MOST_UPLOADS_A_STEP = 6, MOST_UPLOADS_OPENING = 8 };

static bool queue_takes(Snes *s, unsigned uploads)
{
    vars_peek(true);
    unsigned queued = menu_vram_queue_bytes(state_of(s));
    vars_peek(false);
    return queued % MENU_UPLOAD_BYTES == 0 && queued + MENU_UPLOAD_BYTES * uploads <= QUEUE_BYTES;
}

/* The screen's cursor moves between its four rows: each row's neighbours are
 * rows (the top bit marks a push that leaves them, which the screen's places
 * don't have). */
enum { LEAVES_THE_ROWS = 0x80 };

static bool is_row(uint8_t place) { return (uint8_t)(place & ~LEAVES_THE_ROWS) <= FOURTH_ROW; }

static bool cursor_stays_on_the_rows(Snes *s)
{
    const GameState *g = state_of(s);
    vars_peek(true);
    bool ok = true;
    for (uint8_t row = 0; row <= FOURTH_ROW; row++)
        ok = ok && is_row(menu_cursor_places_up(g, row)) && is_row(menu_cursor_places_down(g, row))
             && is_row(menu_cursor_places_left(g, row)) && is_row(menu_cursor_places_right(g, row));
    vars_peek(false);
    return ok;
}

/* The erase question's answer: the cursor on yes or no, whose neighbours are
 * the two answers, the slot the question is over one of the three and the
 * cursor kept for going back on one of the rows. */
static bool is_answer(uint8_t place) { return place == ERASE_YES || place == ERASE_NO; }

bool menu_erase_is_answerable(Snes *s)
{
    if (!menu_on_page_work_ram(s) || !menu_sounds_on_page(s) || !queue_takes(s, MOST_UPLOADS_A_STEP)
        || !menu_cursor_is_movable(s) || !menu_palette_change_counts(s))
        return false;
    const GameState *g = state_of(s);
    vars_peek(true);
    uint8_t at = menu_cursor_index(g);
    bool ok = is_answer(at) && is_answer((uint8_t)(menu_cursor_places_up(g, at) & ~LEAVES_THE_ROWS))
              && is_answer((uint8_t)(menu_cursor_places_down(g, at) & ~LEAVES_THE_ROWS))
              && is_answer((uint8_t)(menu_cursor_places_left(g, at) & ~LEAVES_THE_ROWS))
              && is_answer((uint8_t)(menu_cursor_places_right(g, at) & ~LEAVES_THE_ROWS))
              && menu_save_slot_shown(g) < MENU_SAVE_SLOTS && menu_kept_cursor(g) <= FOURTH_ROW;
    vars_peek(false);
    return ok;
}

bool menu_load_screen_runs(Snes *s)
{
    return menu_on_page_work_ram(s) && menu_cursor_is_movable(s) && cursor_stays_on_the_rows(s)
           && menu_save_slots_are_showable(s) && menu_sounds_on_page(s) && queue_takes(s, MOST_UPLOADS_A_STEP);
}

/* The opening's drawing: the slot the cartridge says was saved last is one of
 * the rows. (The colour math records it builds its table from are the
 * screen's own lists'.) */
bool menu_load_screen_draws(Snes *s)
{
    if (!menu_on_page_work_ram(s) || !queue_takes(s, MOST_UPLOADS_OPENING))
        return false;
    vars_peek(true);
    bool ok = sv_peek8(m_platform(s), SAVES_LAST_SLOT) <= FOURTH_ROW && menu_save_slots_can_show(state_of(s));
    vars_peek(false);
    return ok;
}

/* A slot's window is one of the three rows', and the colours it loads can be
 * counted as a change (menu/windows.c). The guards look at the save memory
 * without the machine's time. */
bool menu_slot_window_is_drawable(Snes *s)
{
    if (!menu_on_page_work_ram(s) || !menu_palette_change_counts(s))
        return false;
    vars_peek(true);
    bool ok = menu_save_slot_shown(state_of(s)) < MENU_SAVE_SLOTS;
    vars_peek(false);
    return ok;
}
