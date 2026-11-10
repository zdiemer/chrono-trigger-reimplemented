/* The settings screen on the original's machine: its checked legacy adapters, the
 * bridges that run what game/menu/settings_screen.c decides between the
 * calls that are still the original's, and their guards.
 *
 * Each step reads the cursor and asks settings_screen.c what the frame's
 * input does. The ticks are the cursor's sound, a sibling run as the
 * original's JSR reaches it (menu_run): it saves P as it starts and calls
 * the sound driver, so the flags the original has at each tick are on the
 * stack there, where shadow mode compares them, and A's high byte goes to
 * the driver. The options' handling is an entry of its own, run as the
 * original's JSR from the options' step reaches it, and it runs each
 * option's handler the same way through a C table (the original's JSR
 * through its table at $C2:BFFF): the stereo's handler sends the sound
 * driver the mode, and the meters' and the design's step their meter
 * through a sibling of their own that ticks. Going back from the options
 * calls the main menu's return as C behind its guard, and the panel waits a frame
 * between its frame and the rest (the menus' task switch).
 *
 * Before each of those calls a bridge leaves the registers and flags the
 * original's own instructions left there, worked out from the state (the
 * leaves at the end of the file): scaffolding, which the native build
 * compiles out. */
#include "menu/settings_screen.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/main_menu.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    MAIN_MENU = 0xC29A98, /* back to the main menu */
    CURSOR_SOUND = 0xC2EAC2,
    TAKE_OPTION = 0xC2BFD4,
    STEP_METER = 0xC2C164,
    SOUND_DRIVER = 0xC70004,
};

enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, CURSOR_SOUND, menu_play_cursor_sound); }

/* The cursor read as C, leaving what the original's reading leaves. */
static void read_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    menu_charge_cursor_read(s, r);
}

static void charge_row_changed(Snes *s, uint8_t paging_before);

/* ---- The opening and the options: what the sequences ask, on the machine ----------------------- */

static const uint32_t TICKS_AT[] = {
    [SETTINGS_AT_THE_OPENING] = 0xC2BF3D, [SETTINGS_AT_THE_CURSOR_MOVED] = 0xC2BF5B, [SETTINGS_AT_AN_OPTION_CHOSEN] = 0xC2BFFA,
    [SETTINGS_AT_A_METER_STEPPED] = 0xC2C17F, [SETTINGS_AT_THE_DESIGN_TOGGLED] = 0xC2C15A,
    [SETTINGS_AT_A_GAUGE_TAKEN] = 0xC2BFC1,   [SETTINGS_AT_THE_BUTTONS_LEFT] = 0xC2BF8C,
    [SETTINGS_AT_THE_BUTTONS_RESET] = 0xC2C18C, [SETTINGS_AT_THE_BUTTONS_KEPT] = 0xC2C1B2,
    [SETTINGS_AT_A_ROW_CHANGED] = 0xC2C1FD,
};

/* The meter a call steps is its setting's cell, as the original's callers
 * hand it over in X, from each meter's call. */
enum { BATTLE_SPEED_CELL = 0x0D8B, MESSAGE_SPEED_CELL = 0x0D90, WINDOW_DESIGN_CELL = 0x0D8C };
static const struct {
    uint32_t site;
    uint16_t cell;
} METERS[SETTINGS_OPTION_COUNT] = {
    [SETTINGS_BATTLE_SPEED] = { 0xC2C0BC, BATTLE_SPEED_CELL },
    [SETTINGS_MESSAGE_SPEED] = { 0xC2C0D4, MESSAGE_SPEED_CELL },
    [SETTINGS_WINDOW_DESIGN] = { 0xC2C134, WINDOW_DESIGN_CELL },
};

/* Each option's handler, by the option: the original's routine, which the
 * JSR through its table at $C2:BFFF reaches, and its entry. */
static const struct {
    uint32_t routine;
    void (*entry)(Snes *);
} OPTION_HANDLERS[SETTINGS_OPTION_COUNT] = {
    [SETTINGS_STEREO] = { 0xC2C017, menu_set_sound_output },
    [SETTINGS_ACTIVE_TIME] = { 0xC2C06E, menu_flip_active_time },
    [SETTINGS_KEEP_MENU_CURSORS] = { 0xC2C038, menu_flip_keep_menu_cursors },
    [SETTINGS_KEEP_BATTLE_COMMAND] = { 0xC2C04A, menu_flip_keep_battle_command },
    [SETTINGS_KEEP_BATTLE_LISTS] = { 0xC2C05C, menu_flip_keep_battle_lists },
    [SETTINGS_SPEEDS] = { 0xC2C0A7, menu_choose_speeds },
    [SETTINGS_WINDOW_DESIGN] = { 0xC2C12C, menu_step_window_design },
    [SETTINGS_BATTLE_GAUGE] = { 0xC2C080, menu_choose_battle_gauge },
    [SETTINGS_CONTROLS] = { 0xC2C0E9, menu_take_controls },
    [SETTINGS_DESCRIPTIONS] = { 0xC2C095, menu_flip_descriptions },
    [SETTINGS_BATTLE_SPEED] = { 0xC2C0B9, menu_step_battle_speed },
    [SETTINGS_MESSAGE_SPEED] = { 0xC2C0D1, menu_step_message_speed },
};

static void read_cursor_on_the_machine(void *ctx) { read_cursor(ctx); }
static bool tick_on_the_machine(void *ctx, SettingsPlace at) { return tick(ctx, TICKS_AT[at]); }

static bool take_the_option_on_the_machine(void *ctx)
{
    return menu_run(ctx, 0xC2BF52, TAKE_OPTION, menu_take_setting_option);
}

static bool handle_on_the_machine(void *ctx, SettingsOption option)
{
    return menu_run(ctx, 0xC2BFE0, OPTION_HANDLERS[option].routine, OPTION_HANDLERS[option].entry);
}

static bool back_to_the_main_menu_on_the_machine(void *ctx)
{
    menu_go_on_in(ctx, MAIN_MENU, menu_back_to_the_main_menu_entry, READS_NOTHING);
    return false;
}

static bool step_meter_on_the_machine(void *ctx, SettingsOption meter)
{
    Snes *s = ctx;
    s->cpu.x = METERS[meter].cell;
    return menu_run(s, METERS[meter].site, STEP_METER, menu_step_setting_meter);
}

/* The flags the original's tests leave, and the P the taking saves round
 * the option's handler. */
static void note_on_the_machine(void *ctx, SettingsNote note, uint8_t value)
{
    Snes *s = ctx;
    switch (note) {
    case SETTINGS_NOTE_EDITING_TESTED: charge_machine_accesses(state_of(s), 3); break;
    case SETTINGS_NOTE_PACKED: charge_machine_accesses(state_of(s), 1); break;
    case SETTINGS_NOTE_BUTTONS_TESTED: charge_machine_accesses(state_of(s), 1); break;
    case SETTINGS_NOTE_CURSOR_COMPARED: charge_machine_accesses(state_of(s), 3); break;
    case SETTINGS_NOTE_OPTION_PICKED:
        (void)0;
        menu_save_p(s);
        break;
    case SETTINGS_NOTE_HANDLED: menu_restore_p(s); break;
    case SETTINGS_NOTE_SHOWN:
        (void)0;
        charge_machine_accesses(state_of(s), 1);
        break;
    case SETTINGS_NOTE_METER_UNMOVED:
        (void)0;
        (void)0;
        break;
    case SETTINGS_NOTE_METER_SHOWN: charge_machine_accesses(state_of(s), 2); break;
    case SETTINGS_NOTE_DESIGN_LOADED: (void)0; break;
    case SETTINGS_NOTE_DESIGN_TOGGLED: charge_machine_accesses(state_of(s), 1); break;
    case SETTINGS_NOTE_GAUGE_LEFT: (void)0; break;
    case SETTINGS_NOTE_GAUGE_POINTED: charge_machine_accesses(state_of(s), 3); break;
    case SETTINGS_NOTE_GAUGE_TAKEN:
        (void)0;
        (void)0;
        break;
    case SETTINGS_NOTE_ROWS_KEPT: (void)0; break;
    case SETTINGS_NOTE_ROWS_LEFT_UNKEPT: charge_machine_accesses(state_of(s), 1); break;
    case SETTINGS_NOTE_QUEUED: (void)0; break;
    case SETTINGS_NOTE_START_TESTED: charge_machine_accesses(state_of(s), 2); break;
    case SETTINGS_NOTE_UP_OR_DOWN_TESTED: charge_machine_accesses(state_of(s), 1); break;
    case SETTINGS_NOTE_ROW_CHANGED: charge_row_changed(s, value); break;
    }
}

/* The widths and P the button rows' step sets. */
static void registers_on_the_machine(void *ctx, SettingsPlace at)
{
    Snes *s = ctx;
    switch (at) {
    case SETTINGS_AT_THE_BUTTONS_TAKEN:
        menu_save_p(s);
        menu_set_widths(s, true, m_x8(s));
        break;
    case SETTINGS_AT_A_ROW_S_BUTTON: menu_set_widths(s, true, true); break;
    case SETTINGS_AT_THE_BUTTONS_DONE: menu_restore_p(s); break;
    default: break;
    }
}

/* The original jumps to the cursor's sound, which returns to the step's
 * caller. */
static bool ends_with_a_tick_on_the_machine(void *ctx)
{
    menu_play_cursor_sound(ctx);
    return false;
}

static const SettingsSteps on_the_machine = {
    read_cursor_on_the_machine, tick_on_the_machine,
    take_the_option_on_the_machine, handle_on_the_machine,
    back_to_the_main_menu_on_the_machine, note_on_the_machine,
    step_meter_on_the_machine, registers_on_the_machine, ends_with_a_tick_on_the_machine,
};

static bool run(Snes *s, bool (*with)(GameState *, Platform *, const SettingsSteps *, void *))
{
    return with(state_of(s), m_platform(s), &on_the_machine, s);
}

/* ---- Opening the screen, the options ------------------------------------------------------------- */

// $C2:BF3D
void menu_open_settings_entry(Snes *s)
{
    if (run(s, menu_open_settings_with))
        m_rts(s);
}

// $C2:BF45
void menu_choose_setting_entry(Snes *s)
{
    if (run(s, menu_choose_setting_with))
        m_rts(s);
}

// $C2:BFD4
/* It saves P as it starts, and puts it back as it ends. */
void menu_take_setting_option(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(s, menu_take_setting_option_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* The sound driver told the mode the player chose (stereo or mono), from
 * the original's call, as C and on the machine. */
enum { SOUND_OUTPUT = 0xF3 };

void menu_settings_send_sound_output(GameState *g, Platform *pf)
{
    au_command(pf, PF_CALL_DC(0xC2C033, SOUND_DRIVER, DRIVER_IGNORES),
               (AudioCommand){ .command = SOUND_OUTPUT, .p1 = menu_settings_sound_output(g), .count = 1 });
}

static bool send_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    MenuMachine m = menu_machine(s);
    menu_settings_send_sound_output(state_of(s), m_platform(s));
    return menu_back(s, 0xC2C037, m);
}

// $C2:C017
void menu_set_sound_output(Snes *s)
{
    if (menu_settings_set_sound_output(state_of(s), send_on_the_machine, s))
        m_rts(s);
}

#define FLIP_ENTRY(name, option)                         \
    void name(Snes *s)                                   \
    {                                                    \
        (void)menu_settings_flip_option(state_of(s), option); \
        m_rts(s);                                        \
    }
// $C2:C06E
FLIP_ENTRY(menu_flip_active_time, SETTINGS_ACTIVE_TIME)
// $C2:C038
FLIP_ENTRY(menu_flip_keep_menu_cursors, SETTINGS_KEEP_MENU_CURSORS)
// $C2:C04A
FLIP_ENTRY(menu_flip_keep_battle_command, SETTINGS_KEEP_BATTLE_COMMAND)
// $C2:C05C
FLIP_ENTRY(menu_flip_keep_battle_lists, SETTINGS_KEEP_BATTLE_LISTS)
// $C2:C095
FLIP_ENTRY(menu_flip_descriptions, SETTINGS_DESCRIPTIONS)
#undef FLIP_ENTRY

// $C2:C0A7
void menu_choose_speeds(Snes *s)
{
    menu_settings_take_speeds(state_of(s));
    m_rts(s);
}

// $C2:C080
void menu_choose_battle_gauge(Snes *s)
{
    menu_settings_take_gauge(state_of(s));
    m_rts(s);
}

// $C2:C0E9
void menu_take_controls(Snes *s)
{
    (void)menu_settings_take_controls(state_of(s));
    m_rts(s);
}

/* ---- The meters ---------------------------------------------------------------------------------- */

static bool meter_of(uint16_t cell, SettingsOption *meter)
{
    switch (cell) {
    case BATTLE_SPEED_CELL: *meter = SETTINGS_BATTLE_SPEED; return true;
    case MESSAGE_SPEED_CELL: *meter = SETTINGS_MESSAGE_SPEED; return true;
    case WINDOW_DESIGN_CELL: *meter = SETTINGS_WINDOW_DESIGN; return true;
    }
    return false;
}

// $C2:C164
/* The meter it steps is the setting's cell its caller hands over in X. */
void menu_step_setting_meter(Snes *s)
{
    SettingsOption meter = SETTINGS_BATTLE_SPEED;
    (void)meter_of(s->cpu.x, &meter);
    if (menu_step_setting_meter_with(state_of(s), meter, &on_the_machine, s))
        m_rts(s);
}

// $C2:C0B9
void menu_step_battle_speed(Snes *s)
{
    if (menu_step_speed_with(state_of(s), SETTINGS_BATTLE_SPEED, &on_the_machine, s))
        m_rts(s);
}

// $C2:C0D1
void menu_step_message_speed(Snes *s)
{
    if (menu_step_speed_with(state_of(s), SETTINGS_MESSAGE_SPEED, &on_the_machine, s))
        m_rts(s);
}

// $C2:C12C
void menu_step_window_design(Snes *s)
{
    if (menu_step_window_design_with(state_of(s), &on_the_machine, s))
        m_rts(s);
}

/* ---- The battle gauge's layouts ------------------------------------------------------------------ */

// $C2:BF9F
void menu_choose_gauge_layout_entry(Snes *s)
{
    if (run(s, menu_choose_gauge_layout_with))
        m_rts(s);
}

/* ---- The button rows ----------------------------------------------------------------------------- */

// $C2:BF6D, $C2:C183
void menu_choose_buttons_entry(Snes *s)
{
    if (run(s, menu_choose_buttons_with))
        m_rts(s);
}

/* ---- The main menu's panel ----------------------------------------------------------------------- */

// $C2:C2FF
/* The panel's frame, a frame's wait, then the rest of it. */
void menu_show_settings_panel(Snes *s)
{
    menu_save_p(s);
    MainMenuOnTheMachine m = { s, 0, 0 };
    if (!menu_show_settings_panel_with(state_of(s), m_platform(s), &menu_main_menu_on_the_machine, &m))
        return;
    menu_restore_p(s);
    m_rts(s);
}

static void charge_row_changed(Snes *s, uint8_t paging_before)
{
    (void)paging_before;
    GameState *g = state_of(s);
    unsigned row = (uint8_t)(menu_cursor_index(g) - SETTINGS_FIRST_BUTTON_ROW);
    uint8_t first = menu_settings_buttons(g, 0);
    bool changes = first != (row == 0 ? 0xFF : own_button_assignments(g, 0));
    charge_machine_accesses(g, 1);
    if (changes)
        charge_machine_rom_accesses(g, 2);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

static bool place_among(uint8_t place, uint8_t first, uint8_t count) { return place >= first && place < first + count; }

bool menu_settings_on_an_option(const GameState *g)
{
    return place_among(menu_cursor_index(g), 0, SETTINGS_OPTION_COUNT) && menu_cursor_kept_among(g, 0, SETTINGS_OPTION_COUNT, false)
           && menu_settings_boxes(g, SETTINGS_CHOOSING_GAUGE) == 0 && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT;
}

bool menu_settings_on_a_layout(const GameState *g)
{
    return place_among(menu_cursor_index(g), SETTINGS_FIRST_GAUGE_PLACE, SETTINGS_GAUGE_LAYOUTS)
           && menu_cursor_kept_among(g, SETTINGS_FIRST_GAUGE_PLACE, SETTINGS_GAUGE_LAYOUTS, false)
           && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT;
}

bool menu_settings_on_a_button_row(const GameState *g)
{
    return place_among(menu_cursor_index(g), SETTINGS_FIRST_BUTTON_ROW, MENU_SETTINGS_BUTTON_ROWS)
           && menu_cursor_kept_among(g, SETTINGS_FIRST_BUTTON_ROW, MENU_SETTINGS_BUTTON_ROWS, true) && menu_zero(g) == 0
           && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT;
}

/* The description's typing got no further than its row, a tile at a time. */
bool menu_settings_describable(const GameState *g)
{
    uint16_t typed = menu_description_line_from(g);
    return typed < 0x40 && !(typed & 1);
}

static bool on_the_menus_page(Snes *s) { return menu_on_page_work_ram(s); }

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_settings_can_open(const GameState *g)
{
    return menu_screen_step(g) == SETTINGS_OPENING && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT
           && menu_window_list_drawable(g, 0xC1F6);
}

bool menu_settings_screen_opens(Snes *s)
{
    return on_the_menus_page(s) && menu_sounds_on_page(s) && peek(s, menu_settings_can_open);
}

static bool options_step(const GameState *g) { return menu_settings_on_an_option(g) && menu_settings_describable(g); }

bool menu_settings_can_choose(const GameState *g) { return options_step(g) && menu_cursor_can_move(g); }

bool menu_settings_options_step_runs(Snes *s)
{
    return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, options_step);
}

bool menu_settings_option_can_be_taken(const GameState *g)
{
    return place_among(menu_cursor_index(g), 0, SETTINGS_OPTION_COUNT) && menu_settings_boxes(g, SETTINGS_CHOOSING_GAUGE) == 0
           && menu_settings_describable(g);
}

bool menu_setting_option_is_takeable(Snes *s) { return on_the_menus_page(s) && peek(s, menu_settings_option_can_be_taken); }

static bool buttons_step(const GameState *g) { return menu_settings_on_a_button_row(g); }

bool menu_settings_buttons_step_runs(Snes *s)
{
    return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, buttons_step);
}

static bool gauge_step(const GameState *g) { return menu_settings_on_a_layout(g) && menu_settings_describable(g); }

bool menu_settings_gauge_step_runs(Snes *s) { return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, gauge_step); }
bool menu_settings_can_choose_buttons(const GameState *g) { return buttons_step(g); }
bool menu_settings_can_choose_a_layout(const GameState *g) { return gauge_step(g); }

bool menu_setting_meter_steps(Snes *s)
{
    SettingsOption meter;
    return on_the_menus_page(s) && meter_of(s->cpu.x, &meter);
}

bool menu_settings_panel_shows(Snes *s) { return on_the_menus_page(s) && peek(s, menu_settings_panel_can_show); }

bool menu_settings_panel_goes(Snes *s) { return on_the_menus_page(s); }
