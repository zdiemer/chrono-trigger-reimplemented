/* The settings screen on the original's machine: its registered entries, the
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
 * stops at the main menu's return ($C2:9A98), and the panel waits a frame
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
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    YIELD = 0xC2821E,     /* the task waits for the next frame */
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

static bool yield(Snes *s, uint32_t site) { return menu_call(s, site, YIELD, READS_NOTHING); }
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

static void leave_a_low(Snes *s, uint8_t v);
static void leave_flags(Snes *s, uint8_t loaded, bool overflow, bool carry);
static void leave_queued(Snes *s);
static void leave_buttons_tested(Snes *s);
static void leave_editing_tested(Snes *s);
static void leave_packed(Snes *s);
static void leave_option_doubled(Snes *s, SettingsOption option);
static void leave_buttons_loaded(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_cursor_compared_with_the_start(Snes *s);
static void leave_meter_shown(Snes *s);
static void leave_design_loaded(Snes *s);
static void leave_design_toggled(Snes *s, bool was_choosing);
static void leave_gauge_taken(Snes *s);
static void leave_gauge_left(Snes *s);
static void leave_rows_kept(Snes *s);
static void leave_rows_left_unkept(Snes *s);
static void leave_start_tested(Snes *s);
static void leave_up_or_down_tested(Snes *s);
static void leave_row_changed(Snes *s, uint8_t paging_before);
static void leave_panel_frame(Snes *s);

/* ---- Opening the screen -------------------------------------------------------------------------- */

// $C2:BF3D
/* A tick, then the screen. */
void menu_open_settings(Snes *s)
{
    if (!tick(s, 0xC2BF3D))
        return;
    menu_settings_open(state_of(s), m_platform(s));
    m_rts(s);
}

/* ---- The options --------------------------------------------------------------------------------- */

// $C2:BF45
/* Going back leaves for the main menu, unless a speed or the design is
 * being set: then the option takes it back, as it takes every other input;
 * a tick where the cursor moved. */
void menu_choose_setting(Snes *s)
{
    GameState *g = state_of(s);
    menu_settings_hold_line_effects(g);
    read_cursor(s);
    if (menu_settings_leaving(g)) {
        ORIGINAL_EFFECT(leave_editing_tested(s));
        menu_settings_leave(g);
        ORIGINAL_EFFECT(leave_packed(s));
        m_stop_at(s, MAIN_MENU, READS_NOTHING);
        return;
    }
    if (menu_asked(g, MENU_GO_BACK))
        ORIGINAL_EFFECT(leave_editing_tested(s));
    else
        ORIGINAL_EFFECT(leave_buttons_tested(s));
    if (!menu_run(s, 0xC2BF52, TAKE_OPTION, menu_take_setting_option))
        return;
    bool moved = menu_cursor_index(g) != menu_cursor_frame_start(g);
    ORIGINAL_EFFECT(leave_cursor_compared(s));
    if (moved && !tick(s, 0xC2BF5B))
        return;
    m_rts(s);
}

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

// $C2:BFD4
/* The option the cursor is on handled, then shown; a tick where the player
 * chose. */
void menu_take_setting_option(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    SettingsOption option = menu_settings_take_option(g);
    ORIGINAL_EFFECT(leave_option_doubled(s, option));
    menu_save_p(s);
    if (!menu_run(s, 0xC2BFE0, OPTION_HANDLERS[option].routine, OPTION_HANDLERS[option].entry))
        return;
    menu_restore_p(s);
    menu_settings_show_option(g);
    ORIGINAL_EFFECT(leave_queued(s));
    ORIGINAL_EFFECT(leave_buttons_loaded(s));
    if (menu_asked(g, MENU_CHOOSE) && !tick(s, 0xC2BFFA))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* The sound driver told the mode the player chose (stereo or mono). */
enum { SOUND_OUTPUT = 0xF3 };

// $C2:C017
void menu_set_sound_output(Snes *s)
{
    GameState *g = state_of(s);
    if (menu_settings_flip_option(g, SETTINGS_STEREO)) {
        MenuMachine m = menu_machine_now(s);
        au_command(m_platform(s), PF_CALL_DC(0xC2C033, SOUND_DRIVER, DRIVER_IGNORES),
                   (AudioCommand){ .command = SOUND_OUTPUT, .p1 = menu_settings_sound_output(g), .count = 1 });
        if (!menu_back(s, 0xC2C037, m))
            return;
    }
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

/* The meter a call steps is its setting's cell, as the original's callers
 * hand it over in X. */
enum { BATTLE_SPEED_CELL = 0x0D8B, MESSAGE_SPEED_CELL = 0x0D90, WINDOW_DESIGN_CELL = 0x0D8C };

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
/* A push left or right steps the meter, with a tick. */
void menu_step_setting_meter(Snes *s)
{
    GameState *g = state_of(s);
    SettingsOption meter = SETTINGS_BATTLE_SPEED;
    (void)meter_of(s->cpu.x, &meter);
    bool stepped = meter == SETTINGS_WINDOW_DESIGN ? menu_settings_step_design(g) : menu_settings_step_meter(g, meter);
    if (!stepped) {
        ORIGINAL_EFFECT(s->cpu.a = 0);
        ORIGINAL_EFFECT(leave_flags(s, 0, m_flag(s, CPU_FLAG_V), m_flag(s, CPU_FLAG_C)));
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(leave_meter_shown(s));
    if (!tick(s, 0xC2C17F))
        return;
    m_rts(s);
}

static bool step_meter(Snes *s, uint32_t site, uint16_t cell)
{
    s->cpu.x = cell;
    return menu_run(s, site, STEP_METER, menu_step_setting_meter);
}

// $C2:C0B9
void menu_step_battle_speed(Snes *s)
{
    if (!step_meter(s, 0xC2C0BC, BATTLE_SPEED_CELL))
        return;
    menu_settings_leave_the_meters(state_of(s));
    m_rts(s);
}

// $C2:C0D1
void menu_step_message_speed(Snes *s)
{
    if (!step_meter(s, 0xC2C0D4, MESSAGE_SPEED_CELL))
        return;
    menu_settings_leave_the_meters(state_of(s));
    m_rts(s);
}

// $C2:C12C
/* While the design is being chosen its meter steps and the windows take
 * it; choosing or going back starts or ends the choosing, with a tick. */
void menu_step_window_design(Snes *s)
{
    GameState *g = state_of(s);
    bool was_choosing = menu_settings_choosing_design(g);
    if (was_choosing) {
        if (!step_meter(s, 0xC2C134, WINDOW_DESIGN_CELL))
            return;
        menu_settings_show_design(g);
        ORIGINAL_EFFECT(leave_design_loaded(s));
    }
    if (menu_settings_toggle_design_choice(g)) {
        ORIGINAL_EFFECT(leave_design_toggled(s, was_choosing));
        if (!tick(s, 0xC2C15A))
            return;
    }
    menu_settings_hold_cursor_on_the_design(g);
    m_rts(s);
}

/* ---- The battle gauge's layouts ------------------------------------------------------------------ */

// $C2:BF9F
/* Choosing or going back returns to the options with a tick, which the
 * original jumps to; a move to another layout takes it, with a tick. */
void menu_choose_gauge_layout(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    switch (menu_settings_choose_gauge_layout(g)) {
    case GAUGE_CHOSEN:
        menu_settings_back_from_the_gauge(g);
        ORIGINAL_EFFECT(leave_gauge_left(s));
        menu_play_cursor_sound(s);
        return;
    case GAUGE_POINTED_AT:
        ORIGINAL_EFFECT(leave_cursor_compared_with_the_start(s));
        m_rts(s);
        return;
    case GAUGE_MOVED:
        menu_settings_take_gauge_layout(g);
        ORIGINAL_EFFECT(leave_queued(s));
        ORIGINAL_EFFECT(leave_gauge_taken(s));
        if (!tick(s, 0xC2BFC1))
            return;
        m_rts(s);
        return;
    }
}

/* ---- The button rows ----------------------------------------------------------------------------- */

// $C2:BF6D, $C2:C183
/* Leaving the rows (with a tick) goes back to the controls. Otherwise
 * Start or a push up or down (with a tick) puts back or keeps; then the
 * row's button steps with a push left or right, with a tick. */
void menu_choose_buttons(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    SettingsButtonsInput input = menu_settings_take_buttons_input(g);
    if (input == BUTTONS_KEPT_AND_LEFT || input == BUTTONS_LEFT) {
        if (input == BUTTONS_KEPT_AND_LEFT) {
            menu_settings_keep_the_rows(g);
            ORIGINAL_EFFECT(leave_rows_kept(s));
        } else
            ORIGINAL_EFFECT(leave_rows_left_unkept(s));
        if (!tick(s, 0xC2BF8C))
            return;
        menu_settings_leave_the_buttons(g);
        ORIGINAL_EFFECT(leave_queued(s));
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (input == BUTTONS_RESET) {
        ORIGINAL_EFFECT(leave_start_tested(s));
        if (!tick(s, 0xC2C18C))
            return;
        menu_settings_reset_buttons(g);
    } else if (input == BUTTONS_KEPT) {
        ORIGINAL_EFFECT(leave_up_or_down_tested(s));
        if (!tick(s, 0xC2C1B2))
            return;
        menu_settings_keep_buttons(g);
    }
    menu_set_widths(s, true, true);
    if (menu_settings_give_row_button(g)) {
        uint8_t paging_before = menu_settings_buttons(g, SETTINGS_PAGE_ONE_WAY);
        menu_settings_show_row_change(g);
        ORIGINAL_EFFECT(leave_row_changed(s, paging_before));
        if (!tick(s, 0xC2C1FD))
            return;
    }
    menu_restore_p(s);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    ORIGINAL_EFFECT(leave_queued(s));
    m_rts(s);
}

/* ---- The main menu's panel ----------------------------------------------------------------------- */

// $C2:C2FF
/* The panel's frame, a frame's wait, then the rest of it. */
void menu_show_settings_panel(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_settings_panel_frame(g, m_platform(s));
    menu_set_widths(s, false, false);
    ORIGINAL_EFFECT(leave_panel_frame(s));
    if (!yield(s, 0xC2C337))
        return;
    menu_settings_panel_show(g, m_platform(s));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:C397
void menu_put_settings_panel_away(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_settings_panel_put_away(state_of(s));
    menu_restore_p(s);
    m_rts(s);
}

/* ---- What the original's instructions leave (scaffolding) --------------------------------------- */

static void leave_a_low(Snes *s, uint8_t v) { s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | v); }

/* N and Z of a value loaded, with V and C as given. */
static void leave_flags(Snes *s, uint8_t loaded, bool overflow, bool carry)
{
    menu_leave_load(s, loaded);
    m_set_flag(s, CPU_FLAG_V, overflow);
    m_set_flag(s, CPU_FLAG_C, carry);
}

static void leave_queued(Snes *s) { menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER); }

/* The buttons tested for going back, which they don't ask for, with the
 * accumulator left by the pad's test before it ($C2:BF4D, $C2:BF84). */
static void leave_buttons_tested(Snes *s) { menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s))); }

/* Going back: whether a speed or the design is being set, the boxes' two
 * cells together ($C2:BF5F); the test for going back left V. */
static void leave_editing_tested(Snes *s)
{
    const GameState *g = state_of(s);
    menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(g));
    uint8_t either = (uint8_t)(menu_settings_boxes(g, SETTINGS_SETTING_SPEEDS) | menu_settings_boxes(g, SETTINGS_CHOOSING_DESIGN));
    leave_a_low(s, either);
    menu_leave_load(s, either);
}

/* The packing leaves the third byte's gauge in A. */
static void leave_packed(Snes *s) { leave_a_low(s, game_settings(state_of(s), 2)); }

/* The option's handler is reached with the option doubled in A and X, its
 * flags those of the doubling. */
static void leave_option_doubled(Snes *s, SettingsOption option)
{
    uint8_t doubled = (uint8_t)(option << 1);
    s->cpu.a = doubled;
    s->cpu.x = doubled;
    leave_flags(s, doubled, m_flag(s, CPU_FLAG_V), (option & 0x80) != 0);
}

/* The test of whether the player chose, after the layer's queueing. */
static void leave_buttons_loaded(Snes *s)
{
    uint8_t buttons = menu_cursor_buttons(state_of(s));
    leave_a_low(s, buttons);
    menu_leave_load(s, buttons);
}

/* Whether the cursor moved: where it was compared with where it is. */
static void leave_cursor_compared(Snes *s)
{
    const GameState *g = state_of(s);
    leave_a_low(s, menu_cursor_frame_start(g));
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}

/* The same compared the other way round. */
static void leave_cursor_compared_with_the_start(Snes *s)
{
    const GameState *g = state_of(s);
    leave_a_low(s, menu_cursor_index(g));
    menu_leave_compare(s, menu_cursor_index(g), menu_cursor_frame_start(g));
}

/* The meters' drawing leaves the last colouring's swapped count in A and
 * the flags of its load of the design's cell with the one after it. */
static void leave_meter_shown(Snes *s)
{
    const GameState *g = state_of(s);
    s->cpu.a = 0x0100;
    uint16_t cells = (uint16_t)(menu_window_design(g) | menu_setting_keep_battle_command(g) << 8);
    m_set_flag(s, CPU_FLAG_N, (cells & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_Z, cells == 0);
}

/* The design's colours leave their block move's count run out in A, and
 * the flags of the style's load. */
static void leave_design_loaded(Snes *s)
{
    s->cpu.a = 0xFFFF;
    menu_leave_load(s, 0x10);
}

/* The choosing flipped, its box's new value in A over what the design's
 * loading or the option's doubling left. */
static void leave_design_toggled(Snes *s, bool was_choosing)
{
    uint8_t choosing = menu_settings_boxes(state_of(s), 6);
    s->cpu.a = (uint16_t)((was_choosing ? 0xFF00 : 0) | choosing);
    menu_leave_load(s, choosing);
}

/* The layout taken from the cursor (less the first layout's place, which
 * can't borrow), then the layer's record loaded for its queueing, whose
 * place is past the bank's middle. */
static void leave_gauge_taken(Snes *s)
{
    leave_flags(s, 0x80, false, true);
}

/* Back to the options: the options' step in A. */
static void leave_gauge_left(Snes *s)
{
    leave_a_low(s, SETTINGS_ON_THE_OPTIONS);
    menu_leave_load(s, SETTINGS_ON_THE_OPTIONS);
}

/* A push left: the rows kept with a block move, the push's test's flags. */
static void leave_rows_kept(Snes *s)
{
    s->cpu.a = 0xFFFF;
    menu_leave_load(s, MENU_PUSHED_LEFT);
}

/* Going back: the test for a push left found none, the buttons' test for
 * going back found it. */
static void leave_rows_left_unkept(Snes *s)
{
    leave_a_low(s, 0);
    menu_leave_bit_test(s, 0, menu_cursor_buttons(state_of(s)));
}

/* Start found pressed. */
static void leave_start_tested(Snes *s)
{
    uint8_t pressed = (uint8_t)input_pressed(state_of(s));
    leave_a_low(s, pressed);
    m_set_flag(s, CPU_FLAG_N, (pressed & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_Z, false);
}

/* A push up or down found. */
static void leave_up_or_down_tested(Snes *s)
{
    uint8_t pushes = (uint8_t)(menu_cursor_moves(state_of(s)) & (MENU_PUSHED_UP | MENU_PUSHED_DOWN));
    leave_a_low(s, pushes);
    menu_leave_load(s, pushes);
}

/* A row changed: A holds the first row's change as its drawing left it, and
 * the flags are those of the shoulders' last tests: whether confirm or
 * cancel took L (with their top two bits), and whether the first shoulder's
 * button, R where they left R free, came after L in the buttons' order. */
static void leave_row_changed(Snes *s, uint8_t paging_before)
{
    enum { BUTTON_R = 0x10, BUTTON_L = 0x20, NO_CHANGE = 0xFFFF };
    const GameState *g = state_of(s);
    unsigned row = (uint8_t)(menu_cursor_index(g) - SETTINGS_FIRST_BUTTON_ROW);
    uint8_t first = menu_settings_buttons(g, 0);
    bool changes = first != (row == 0 ? 0xFF : own_button_assignments(g, 0));
    s->cpu.a = changes ? menu_button_glyph_of(g, first) : NO_CHANGE;
    uint8_t taken = (uint8_t)(first | menu_settings_buttons(g, 1));
    uint8_t paging = (taken & BUTTON_R) ? paging_before : BUTTON_R;
    m_set_flag(s, CPU_FLAG_N, (taken & 0x80) != 0);
    m_set_flag(s, CPU_FLAG_V, (taken & 0x40) != 0);
    m_set_flag(s, CPU_FLAG_Z, !(taken & BUTTON_L));
    m_set_flag(s, CPU_FLAG_C, paging >= BUTTON_L);
}

/* The panel's frame: the boxes' block move run out, the flags of its count. */
static void leave_panel_frame(Snes *s)
{
    s->cpu.a = 0xFFFF;
    s->cpu.x = 0x0D90;
    s->cpu.y = 0x0F13;
    menu_leave_load(s, 1);
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

static bool opens(const GameState *g)
{
    return menu_screen_step(g) == SETTINGS_OPENING && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT
           && menu_window_list_drawable(g, 0xC1F6);
}

bool menu_settings_screen_opens(Snes *s) { return on_the_menus_page(s) && menu_sounds_on_page(s) && peek(s, opens); }

static bool options_step(const GameState *g) { return menu_settings_on_an_option(g) && menu_settings_describable(g); }

bool menu_settings_options_step_runs(Snes *s)
{
    return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, options_step);
}

static bool option_takeable(const GameState *g)
{
    return place_among(menu_cursor_index(g), 0, SETTINGS_OPTION_COUNT) && menu_settings_boxes(g, SETTINGS_CHOOSING_GAUGE) == 0
           && menu_settings_describable(g);
}

bool menu_setting_option_is_takeable(Snes *s) { return on_the_menus_page(s) && peek(s, option_takeable); }

static bool buttons_step(const GameState *g) { return menu_settings_on_a_button_row(g); }

bool menu_settings_buttons_step_runs(Snes *s)
{
    return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, buttons_step);
}

static bool gauge_step(const GameState *g) { return menu_settings_on_a_layout(g) && menu_settings_describable(g); }

bool menu_settings_gauge_step_runs(Snes *s) { return on_the_menus_page(s) && menu_cursor_is_movable(s) && peek(s, gauge_step); }

bool menu_setting_meter_steps(Snes *s)
{
    SettingsOption meter;
    return on_the_menus_page(s) && meter_of(s->cpu.x, &meter);
}

static bool panel_shows(const GameState *g) { return menu_window_list_drawable(g, 0xC19C); }

bool menu_settings_panel_shows(Snes *s) { return on_the_menus_page(s) && peek(s, panel_shows); }

bool menu_settings_panel_goes(Snes *s) { return on_the_menus_page(s); }
