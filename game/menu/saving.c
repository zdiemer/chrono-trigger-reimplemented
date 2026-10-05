/* Saving the game from the menus: the main menu's panel for it, which is the
 * save screen as it opens, and the save screen a save point opens on (menu
 * screen 11).
 *
 * The save screen is the load screen's three slots (menu/load_screen.c):
 * their windows and rows, the summary of the slot the game was last saved
 * in (menu/save_screen.c), the slots' rows' per-line effect and the
 * screen's colours. It is drawn over two frames, the slots first and then
 * the summary, the panels' tasks held meanwhile. A save point's save screen
 * opens on it over the main menu's backdrop; choosing a slot there saves in
 * it (the original's, $C2:CF0A), and going back closes the menus. */
#include "menu/saving.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/load_screen.h"
#include "menu/main_menu.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/save_screen.h"
#include "menu/saves.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- Drawing ------------------------------------------------------------------------------------- */

/* The slots' labels' graphic, and the cursor's task held three frames
 * while the screen is drawn. */
enum { UPLOAD_SLOT_LABELS = 0xFC53, HOLD_THE_CURSOR = 3 };

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
    menu_queue_fixed_upload(g, UPLOAD_SLOT_LABELS);
    set_menu_palette_changed(g, 0);
    set_menu_tasks_countdown(g, MENU_CURSOR_TASK, HOLD_THE_CURSOR);
}

/* The second frame's: the front layer cleared for the summary, nothing
 * described, the slot last saved in shown, and the cursor on it. */
static uint8_t make_room_for_the_summary(GameState *g)
{
    for (unsigned tile = SUMMARY_FROM_ROW * TILES_A_ROW; tile < (SUMMARY_FROM_ROW + SUMMARY_ROWS) * TILES_A_ROW; tile++)
        set_menu_front_tilemap(g, tile, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * SUMMARY_ROWS * TILES_A_ROW - 2));
    set_menu_description_item(g, NOTHING_DESCRIBED);
    set_menu_description_stale(g, NOTHING_DESCRIBED);
    uint8_t slot = save_slot_chosen(g);
    set_menu_save_slot_shown(g, slot);
    set_menu_save_slot_next(g, slot);
    return slot;
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
    set_menu_cursor_index(g, menu_save_slot_next(g));
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, SAVE_SCREEN_WINDOWS));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    set_menu_line_effects(g, SAVE_SCREEN_LINE_EFFECTS);
    TIMED_AS(g, 0xC2CFFB, menu_put_slot_rows_effect(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
    TIMED_AS(g, 0xC2E923, menu_clear_sprites_but_cursor(g));
}

/* ---- On the original's machine ------------------------------------------------------------------- */

enum {
    YIELD = 0xC2821E,           /* the task waits for the next frame */
    LEAVE_THE_MENUS = 0xC282B2, /* the menus close, back to the field */
    FADE_IN = 0xC283B2,         /* the screen's fade in started */
    SAVE_IN_THE_SLOT = 0xC2CF0A,
    MOVE_AMONG_THE_SLOTS = 0xC2CFA2,
};
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool yield(Snes *s, uint32_t site) { return menu_call(s, site, YIELD, READS_NOTHING); }

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
    if (!yield(s, 0xC2D07E))
        return;
    uint8_t slot = make_room_for_the_summary(g);
    menu_set_widths(s, true, false);
    ORIGINAL_EFFECT(menu_leave_load(s, slot));
    if (!menu_run(s, 0xC2D09E, 0xC2D36C, menu_draw_save_slot))
        return;
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
    set_menu_palette_changed(g, 0);
    if (!yield(s, 0xC2D0AA))
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
/* Choosing a slot saves in it, going back closes the menus, and a push
 * moves among the slots (the save screen's own, the original's). */
void menu_choose_on_save_screen(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
    if (menu_asked(g, MENU_CHOOSE))
        m_stop_at_own(s, SAVE_IN_THE_SLOT, READS_A_HIGH);
    else if (menu_asked(g, MENU_GO_BACK))
        m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING);
    else
        m_stop_at_own(s, MOVE_AMONG_THE_SLOTS, READS_A_HIGH);
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

bool menu_save_screen_lays_out(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_save_slot_next(state_of(s)) < MENU_SAVE_SLOTS;
    vars_peek(false);
    return ok;
}
