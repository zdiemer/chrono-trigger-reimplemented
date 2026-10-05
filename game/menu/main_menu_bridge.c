/* The main menu on the original's machine: its steps, the panels' showing
 * and putting away, and their guards (game/menu/main_menu.c is the menu).
 *
 * Each option's panel is its screen's own (menu/<screen>.c), a machine-
 * shaped entry that calls out and waits for frames, so the main menu runs
 * it as the original's JSR through its table reaches it (menu_run): the
 * panels' table here is that table, by option. The cursor's tick and the
 * leaving of the menus, and the fade in as the menu opens, are jumps the
 * steps stop at. */
#include "menu/main_menu.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/equipment.h"
#include "menu/item_screen.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/saving.h"
#include "menu/settings_screen.h"
#include "menu/tech_screen.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    LEAVE_THE_MENUS = 0xC282B2,   /* the menus close, back to the field */
    FADE_IN = 0xC283B2,           /* the screen's fade in started */
    CURSOR_SOUND = 0xC2EAC2,
};
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_AND_CARRY = DC_X | DC_Y | DC_N | DC_V | DC_Z,
};

static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, CURSOR_SOUND, menu_play_cursor_sound); }

/* The cursor read as C, leaving what the original's reading leaves. */
static void read_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
}

/* ---- The panels ---------------------------------------------------------------------------------- */

/* Each option's panel, shown and put away, by its screen's entries; the
 * party exchange's are still the original's, called out to. */
typedef struct {
    uint32_t at;
    void (*entry)(Snes *);
    bool (*can_run)(Snes *);
} PanelEntry;
static const struct {
    PanelEntry show, put_away;
} PANELS[MENU_MAIN_MENU_OPTIONS] = {
    [MAIN_MENU_EQUIPMENT] = { { 0xC2A051, menu_show_equipment_panel, menu_equipment_panel_shows },
                              { 0xC2A101, menu_show_panel_colours, menu_panel_colours_show } },
    [MAIN_MENU_ITEMS] = { { 0xC2AD50, menu_show_items_panel, menu_items_panel_shows },
                          { 0xC2ADC3, menu_put_items_panel_away, menu_items_panel_goes } },
    [MAIN_MENU_TECHS] = { { 0xC2B8DA, menu_show_techs_panel, menu_techs_panel_shows },
                          { 0xC2B960, menu_put_techs_panel_away, menu_techs_panel_goes } },
    [MAIN_MENU_SETTINGS] = { { 0xC2C2FF, menu_show_settings_panel, menu_settings_panel_shows },
                             { 0xC2C397, menu_put_settings_panel_away, menu_settings_panel_goes } },
    [MAIN_MENU_EXCHANGE] = { { 0xC2C9E5, NULL, NULL }, { 0xC2CAE6, NULL, NULL } },
    [MAIN_MENU_SAVING] = { { 0xC2D065, menu_show_save_panel, menu_save_panel_shows },
                           { 0xC2D0DB, menu_put_save_panel_away, menu_on_page_work_ram } },
};

/* The exchange's panels read the overflow and the carry the pick leaves:
 * the carry clear (the option doubled), the overflow the caller's. */
enum { READS_V_AND_C = DC_A | DC_X | DC_Y | DC_N | DC_Z };

static bool run_panel(Snes *s, uint32_t site, PanelEntry p)
{
    return p.entry ? menu_run(s, site, p.at, p.entry) : menu_call(s, site, p.at, READS_V_AND_C);
}

/* For the original's code and shadow mode: it picks the panel by the
 * option doubled, in A and X, which the panel saves the flags of as it
 * starts. */
static void leave_option_doubled(Snes *s, uint8_t option)
{
    s->cpu.a = (uint16_t)(2 * option);
    s->cpu.x = (uint16_t)(2 * option);
    menu_leave_load(s, (uint8_t)(2 * option));
    m_set_flag(s, CPU_FLAG_C, false);
}

// $C2:9B54
/* The option's panel, then the main menu over it. */
void menu_show_main_menu_panel(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t option = menu_main_menu_option(g);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    ORIGINAL_EFFECT(leave_option_doubled(s, option));
    if (!run_panel(s, 0xC29B5D, PANELS[option].show))
        return;
    TIMED_AS(g, 0xC29B8F, menu_draw_main_menu(g, m_platform(s)));
    ORIGINAL_EFFECT(menu_leave_a_high_byte_set(s));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:9B71
/* The option's panel put away, the main menu over the screen again, and the
 * back layer queued. */
void menu_put_main_menu_panel_away(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t option = menu_main_menu_option(g);
    ORIGINAL_EFFECT(leave_option_doubled(s, option));
    if (!run_panel(s, 0xC29B77, PANELS[option].put_away))
        return;
    TIMED_AS(g, 0xC29B8F, menu_draw_main_menu(g, m_platform(s)));
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_BACK_LAYER));
    m_rts(s);
}

/* ---- The steps ----------------------------------------------------------------------------------- */

/* For the original's code and shadow mode: the flags its own tests leave
 * as it runs the panel, which saves them as it starts. Opening, they are
 * the techs' member list's row folded into its first three: a compare of
 * the row with three (the row and the list's scroll added, which sets the
 * overflow), and a load of none where it doesn't fit. */
typedef struct {
    uint8_t row, scroll;
} RowAndScroll;

static RowAndScroll note_techs_row(const GameState *g)
{
    vars_peek(true);
    RowAndScroll r = { menu_tech_member_row(g), menu_setting_keep_menu_cursors(g) ? menu_tech_member_scroll(g) : 0 };
    vars_peek(false);
    return r;
}

enum { MEMBER_ROWS = 3 };

static void leave_techs_row_folded(Snes *s, RowAndScroll r)
{
    uint8_t member = (uint8_t)(r.row + r.scroll);
    m_set_flag(s, CPU_FLAG_V, ((r.row ^ member) & (r.scroll ^ member) & 0x80) != 0);
    menu_leave_compare(s, member, MEMBER_ROWS);
    if (member >= MEMBER_ROWS)
        menu_leave_load(s, 0);
}

/* Moving, they are a compare of the cursor with the option it was on, the
 * go-back button tested, and a load of the panels' tasks. */
static void leave_moved(Snes *s, uint8_t from)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_index(g), from);
    m_set_flag(s, CPU_FLAG_V, (menu_cursor_buttons(g) & MENU_GO_BACK) != 0);
    menu_leave_load(s, 0x74);
}

// $C2:99CE
/* The menu opened, its panel, then on to choosing as it fades in. */
void menu_open_main_menu(Snes *s)
{
    GameState *g = state_of(s);
    RowAndScroll techs_row = note_techs_row(g);
    (void)techs_row;
    if (!menu_main_menu_open(g)) {
        m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING);
        return;
    }
    ORIGINAL_EFFECT(leave_techs_row_folded(s, techs_row));
    if (!menu_run(s, 0xC29A13, 0xC29B54, menu_show_main_menu_panel))
        return;
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    m_stop_at(s, FADE_IN, READS_NOTHING);
}

// $C2:9A1B
/* Choosing an option opens its screen and going back closes the menus; the
 * cursor moved to another option shows its panel, with a tick. */
void menu_choose_on_main_menu(Snes *s)
{
    GameState *g = state_of(s);
    set_menu_cursor_index(g, menu_main_menu_option(g));
    read_cursor(s);
    switch (menu_main_menu_choose(g)) {
    case MAIN_MENU_OPTION_CHOSEN: menu_main_menu_open_option(g, m_platform(s)); break;
    case MAIN_MENU_LEFT: m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING); return;
    case MAIN_MENU_STAYED: menu_main_menu_keep_the_time(g); break;
    case MAIN_MENU_MOVED: {
        uint8_t from = menu_main_menu_option(g);
        (void)from;
        menu_main_menu_move_to(g, (MainMenuOption)menu_cursor_index(g));
        ORIGINAL_EFFECT(leave_moved(s, from));
        if (!menu_run(s, 0xC29A70, 0xC29B54, menu_show_main_menu_panel) || !tick(s, 0xC29A73))
            return;
        break;
    }
    }
    m_rts(s);
}

// $C2:9A98
/* Back from a screen: its panel put away, the main menu chosen on again,
 * and a tick. */
void menu_back_to_the_main_menu(Snes *s)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, false);
    menu_main_menu_come_back(g);
    if (!menu_run(s, 0xC29AA7, 0xC29B71, menu_put_main_menu_panel_away))
        return;
    m_stop_at_own(s, CURSOR_SOUND, READS_A_AND_CARRY);
}

// $C2:9AAD
void menu_draw_main_menu_backdrop_entry(Snes *s)
{
    menu_draw_main_menu_backdrop(state_of(s));
    m_rts(s);
}

// $C2:9B8F
void menu_draw_main_menu_entry(Snes *s)
{
    menu_draw_main_menu(state_of(s), m_platform(s));
    ORIGINAL_EFFECT(menu_leave_a_high_byte_set(s));
    m_rts(s);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

static bool on_an_option(const GameState *g) { return menu_main_menu_option(g) < MENU_MAIN_MENU_OPTIONS; }

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

/* The menu opens on an option, with the screens' member lists' rows within
 * their three. */
static bool opens(const GameState *g)
{
    return on_an_option(g) || (menu_member_count(g) != 0 && menu_setting_keep_menu_cursors(g) == 0);
}

bool menu_main_menu_opens(Snes *s) { return peek(s, opens); }

/* Choosing: the cursor on an option, and every push keeping it among the
 * six. */
static bool choosing(const GameState *g)
{
    return on_an_option(g) && menu_cursor_kept_among(g, 0, MENU_MAIN_MENU_OPTIONS, false);
}

bool menu_main_menu_step_runs(Snes *s) { return peek(s, choosing) && menu_cursor_is_movable(s); }

bool menu_main_menu_comes_back(Snes *s) { return peek(s, on_an_option) && menu_main_menu_panel_goes(s); }

static bool drawable(const GameState *g) { return on_an_option(g) && menu_main_menu_drawable(g); }

bool menu_main_menu_draws(Snes *s) { return peek(s, drawable); }

/* A panel's step: the option's panel runs as its own entry would (the
 * exchange's, still the original's, as it is) and the main menu is drawn
 * over it. */
static bool panel_runs(Snes *s, bool put_away)
{
    if (!menu_main_menu_draws(s))
        return false;
    vars_peek(true);
    uint8_t option = menu_main_menu_option(state_of(s));
    vars_peek(false);
    const PanelEntry *p = put_away ? &PANELS[option].put_away : &PANELS[option].show;
    return p->can_run == NULL || p->can_run(s);
}

bool menu_main_menu_panel_shows(Snes *s) { return panel_runs(s, false); }

bool menu_main_menu_panel_goes(Snes *s) { return panel_runs(s, true); }
