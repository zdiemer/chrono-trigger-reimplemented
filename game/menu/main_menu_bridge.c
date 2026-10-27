/* The main menu on the original's machine: its steps' and the panels'
 * answers, their entries and their guards (game/menu/main_menu.c is the menu,
 * and its sequences).
 *
 * The sequences are main_menu.c's, run here with the machine's answers to
 * what they ask: each option's panel is its screen's own entry, which waits
 * for frames as it draws, run as the original's JSR through its table
 * reaches it (menu_run), and so are the main menu's panel, the cursor's tick
 * and the member card; a frame's wait is the task switch's, from the
 * original's call at each place. The fade in as the menu opens, the leaving
 * of the menus and the tick a step ends on are jumps the steps stop at.
 * Before each call a sequence leaves the flags the original's own tests left
 * (scaffolding, at the notes). */
#include "menu/main_menu.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/equipment.h"
#include "menu/exchange.h"
#include "menu/item_screen.h"
#include "menu/members.h"
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
    SHOW_THE_PANEL = 0xC29B54,
    MEMBER_CARD = 0xC2A22F,
};
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_AND_CARRY = DC_X | DC_Y | DC_N | DC_V | DC_Z,
};

/* Each option's panel is shown by its screen's own entry, run as the
 * original's JSR through its table reaches it. */
typedef struct {
    uint32_t at;
    void (*entry)(Snes *);
    bool (*can_run)(Snes *);
} PanelShown;
static const PanelShown PANELS_SHOWN[MENU_MAIN_MENU_OPTIONS] = {
    [MAIN_MENU_EQUIPMENT] = { 0xC2A051, menu_show_equipment_panel, menu_equipment_panel_shows },
    [MAIN_MENU_ITEMS] = { 0xC2AD50, menu_show_items_panel, menu_items_panel_shows },
    [MAIN_MENU_TECHS] = { 0xC2B8DA, menu_show_techs_panel, menu_techs_panel_shows },
    [MAIN_MENU_SETTINGS] = { 0xC2C2FF, menu_show_settings_panel, menu_settings_panel_shows },
    [MAIN_MENU_EXCHANGE] = { 0xC2C9E5, menu_show_exchange_panel, menu_exchange_panel_shows },
    [MAIN_MENU_SAVING] = { 0xC2D065, menu_show_save_panel_entry, menu_save_panel_shows },
};

/* Putting a panel away waits for nothing: each screen's is C, with its
 * entry's guard. */
static bool (*const PANEL_GOES[MENU_MAIN_MENU_OPTIONS])(Snes *) = {
    [MAIN_MENU_EQUIPMENT] = menu_panel_colours_show, [MAIN_MENU_ITEMS] = menu_items_panel_goes,
    [MAIN_MENU_TECHS] = menu_techs_panel_goes,         [MAIN_MENU_SETTINGS] = menu_settings_panel_goes,
    [MAIN_MENU_EXCHANGE] = menu_exchange_panel_goes,   [MAIN_MENU_SAVING] = menu_on_page_work_ram,
};

/* The original's call at each place, and for a wait, the widths its code
 * has at the wait and sets after it (the code between is C, which reads
 * none of them). */
typedef enum { KEEP_WIDTHS, A16_X16, A8_X16 } Widths;
static const struct {
    uint32_t site;
    Widths before, after;
} places[MAIN_MENU_PLACES] = {
    [MAIN_MENU_AT_THE_OPENING_PANEL] = { 0xC29A13, KEEP_WIDTHS, KEEP_WIDTHS },
    [MAIN_MENU_AT_THE_NEW_OPTION_PANEL] = { 0xC29A70, KEEP_WIDTHS, KEEP_WIDTHS },
    [MAIN_MENU_AT_THE_NEW_OPTION_TICK] = { 0xC29A73, KEEP_WIDTHS, KEEP_WIDTHS },
    [MAIN_MENU_AT_THE_EQUIPMENT_LAID_OUT] = { 0xC2A0AC, A16_X16, A8_X16 },
    [MAIN_MENU_AT_THE_EQUIPMENT_DRAWN] = { 0xC2A0BD, KEEP_WIDTHS, KEEP_WIDTHS },
    [MAIN_MENU_AT_THE_ITEMS_LAID_OUT] = { 0xC2AD7F, KEEP_WIDTHS, KEEP_WIDTHS },
    [MAIN_MENU_AT_THE_ITEMS_DRAWN] = { 0xC2ADA1, A8_X16, KEEP_WIDTHS },
    [MAIN_MENU_AT_THE_TECHS_LAID_OUT] = { 0xC2B8F2, KEEP_WIDTHS, KEEP_WIDTHS },
    [MAIN_MENU_AT_THE_SETTINGS_FRAMED] = { 0xC2C337, A16_X16, KEEP_WIDTHS },
};

static void set_widths(Snes *s, Widths w)
{
    if (w != KEEP_WIDTHS)
        menu_set_widths(s, w == A8_X16, false);
}

/* ---- What the original's tests leave (scaffolding) ----------------------------------------------- */

/* It picks the panel by the option doubled, in A and X, which the panel
 * saves the flags of as it starts. */
static void leave_option_doubled(Snes *s, uint8_t option)
{
    s->cpu.a = (uint16_t)(2 * option);
    s->cpu.x = (uint16_t)(2 * option);
    menu_leave_load(s, (uint8_t)(2 * option));
    m_set_flag(s, CPU_FLAG_C, false);
}

/* What putting a panel away leaves: the panel's pick, then the queue's
 * registers. */
static void leave_panel_put_away(Snes *s, uint8_t option)
{
    leave_option_doubled(s, option);
    menu_leave_queued_upload(s, MENU_UPLOAD_BACK_LAYER);
}

/* Opening, the techs' member list's row folded into its first three: a
 * compare of the row with three (the row and the list's scroll added, which
 * sets the overflow), and a load of none where it doesn't fit. The row and
 * scroll are noted before the menu opens. */
enum { MEMBER_ROWS = 3 };

static void note_techs_row(MainMenuOnTheMachine *m)
{
    const GameState *g = state_of(m->s);
    vars_peek(true);
    m->techs_row = menu_tech_member_row(g);
    m->techs_scroll = menu_setting_keep_menu_cursors(g) ? menu_tech_member_scroll(g) : 0;
    vars_peek(false);
}

static void leave_techs_row_folded(const MainMenuOnTheMachine *m)
{
    Snes *s = m->s;
    uint8_t row = m->techs_row, scroll = m->techs_scroll;
    uint8_t member = (uint8_t)(row + scroll);
    m_set_flag(s, CPU_FLAG_V, ((row ^ member) & (scroll ^ member) & 0x80) != 0);
    menu_leave_compare(s, member, MEMBER_ROWS);
    if (member >= MEMBER_ROWS)
        menu_leave_load(s, 0);
}

/* Moving, a compare of the cursor with the option it was on, the go-back
 * button tested, and a load of the panels' tasks. */
static void leave_moved(Snes *s, uint8_t from)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_index(g), from);
    m_set_flag(s, CPU_FLAG_V, (menu_cursor_buttons(g) & MENU_GO_BACK) != 0);
    menu_leave_load(s, MENU_PANELS_TASKS);
}

/* ---- What the sequences ask of the menus, on the machine ----------------------------------------- */

static Snes *machine(void *ctx) { return ((MainMenuOnTheMachine *)ctx)->s; }

static void read_cursor(void *ctx) { menu_step_reads_the_cursor(machine(ctx)); }

static bool tick(void *ctx, MainMenuPlace at)
{
    return menu_run(machine(ctx), places[at].site, CURSOR_SOUND, menu_play_cursor_sound);
}

static bool wait(void *ctx, MainMenuPlace at)
{
    Snes *s = machine(ctx);
    set_widths(s, places[at].before);
    if (!menu_wait_a_frame_from(s, places[at].site))
        return false;
    set_widths(s, places[at].after);
    return true;
}

static bool show_the_panel(void *ctx, MainMenuPlace at)
{
    return menu_run(machine(ctx), places[at].site, SHOW_THE_PANEL, menu_show_main_menu_panel);
}

static bool option_panel(void *ctx, MainMenuOption option)
{
    const PanelShown *p = &PANELS_SHOWN[option];
    return menu_run(machine(ctx), 0xC29B5D, p->at, p->entry);
}

static bool member_card(void *ctx) { return menu_run(machine(ctx), 0xC2A0C3, MEMBER_CARD, menu_draw_member_card_entry); }

static void note(void *ctx, MainMenuNote n, uint8_t value)
{
    MainMenuOnTheMachine *m = ctx;
    Snes *s = m->s;
    (void)value;
    switch (n) {
    case MAIN_MENU_NOTE_OPENING: ORIGINAL_EFFECT(note_techs_row(m)); break;
    case MAIN_MENU_NOTE_TECHS_ROW_FOLDED: ORIGINAL_EFFECT(leave_techs_row_folded(m)); break;
    case MAIN_MENU_NOTE_MOVED_FROM: ORIGINAL_EFFECT(leave_moved(s, value)); break;
    case MAIN_MENU_NOTE_OPTION_PICKED: ORIGINAL_EFFECT(leave_option_doubled(s, value)); break;
    case MAIN_MENU_NOTE_DRAWN: ORIGINAL_EFFECT(menu_leave_a_high_byte_set(s)); break;
    case MAIN_MENU_NOTE_PANEL_PUT_AWAY: ORIGINAL_EFFECT(leave_panel_put_away(s, value)); break;
    case MAIN_MENU_NOTE_EQUIPMENT_TESTED: ORIGINAL_EFFECT(menu_leave_equipment_panel_tested(s, value)); break;
    case MAIN_MENU_NOTE_TECHS_MEMBER_CHOSEN: ORIGINAL_EFFECT(menu_leave_techs_panel_member_chosen(s, value)); break;
    case MAIN_MENU_NOTE_SETTINGS_FRAMED: ORIGINAL_EFFECT(menu_leave_settings_panel_frame(s)); break;
    }
}

/* The steps' ends are the original's jumps, which the port stops at. */
static bool ends(void *ctx, MainMenuEnd end)
{
    Snes *s = machine(ctx);
    switch (end) {
    case MAIN_MENU_FADES_IN: m_stop_at(s, FADE_IN, READS_NOTHING); break;
    case MAIN_MENU_LEAVES_THE_MENUS: m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING); break;
    case MAIN_MENU_TICKS_AS_IT_ENDS: m_stop_at_own(s, CURSOR_SOUND, READS_A_AND_CARRY); break;
    }
    return false;
}

const MainMenuSteps menu_main_menu_on_the_machine = { read_cursor, tick, wait, show_the_panel, option_panel, member_card, note, ends };

/* ---- The entries --------------------------------------------------------------------------------- */

static void step(Snes *s, bool (*run)(GameState *, Platform *, const MainMenuSteps *, void *))
{
    MainMenuOnTheMachine m = { s, 0, 0 };
    if (run(state_of(s), m_platform(s), &menu_main_menu_on_the_machine, &m))
        m_rts(s);
}

// $C2:99CE
void menu_open_main_menu_entry(Snes *s) { step(s, menu_open_main_menu_with); }

// $C2:9A1B
void menu_choose_on_main_menu_entry(Snes *s) { step(s, menu_choose_on_main_menu_with); }

// $C2:9A98
void menu_back_to_the_main_menu_entry(Snes *s)
{
    menu_set_widths(s, true, false);
    step(s, menu_back_to_the_main_menu_with);
}

// $C2:9B54
/* It saves P as it starts and puts it back as it ends. */
void menu_show_main_menu_panel(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    MainMenuOnTheMachine m = { s, 0, 0 };
    if (!menu_show_main_menu_panel_with(state_of(s), m_platform(s), &menu_main_menu_on_the_machine, &m))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:9B71
void menu_put_main_menu_panel_away_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t option = menu_main_menu_option(g);
    (void)option;
    menu_put_main_menu_panel_away(g, m_platform(s));
    ORIGINAL_EFFECT(leave_panel_put_away(s, option));
    m_rts(s);
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

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

static bool on_an_option(const GameState *g) { return menu_main_menu_option(g) < MENU_MAIN_MENU_OPTIONS; }

bool menu_main_menu_opens(Snes *s) { return peek(s, menu_main_menu_can_open); }

bool menu_main_menu_step_runs(Snes *s) { return peek(s, menu_main_menu_can_choose) && menu_cursor_is_movable(s); }

static bool drawable(const GameState *g) { return on_an_option(g) && menu_main_menu_drawable(g); }

bool menu_main_menu_draws(Snes *s) { return peek(s, drawable); }

/* A panel's step: the option's panel runs as its own entry would and the
 * main menu is drawn over it. */
static bool panel_runs(Snes *s, bool put_away)
{
    if (!menu_main_menu_draws(s))
        return false;
    vars_peek(true);
    uint8_t option = menu_main_menu_option(state_of(s));
    vars_peek(false);
    return put_away ? PANEL_GOES[option](s) : PANELS_SHOWN[option].can_run(s);
}

bool menu_main_menu_panel_shows(Snes *s) { return panel_runs(s, false); }

bool menu_main_menu_panel_goes(Snes *s) { return panel_runs(s, true); }

bool menu_main_menu_comes_back(Snes *s) { return peek(s, on_an_option) && menu_main_menu_panel_goes(s); }
