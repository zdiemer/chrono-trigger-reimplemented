/* The settings screen and the main menu's panel for it: what the game does
 * (game/menu/settings_screen.c) and the entries that run it on the original's
 * machine (game/menu/settings_screen_bridge.c). */
#ifndef CT_GAME_MENU_SETTINGS_SCREEN_H
#define CT_GAME_MENU_SETTINGS_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/audio.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The screen's options, as its cursor's places number them: the ten in the
 * left window, then the two speeds' meters the speeds option leads to. The
 * battle gauge's three layouts and the six button rows are places of their
 * own after them, which the screen's other steps move the cursor among. */
typedef enum {
    SETTINGS_STEREO = 0,
    SETTINGS_ACTIVE_TIME = 1,
    SETTINGS_KEEP_MENU_CURSORS = 2,
    SETTINGS_KEEP_BATTLE_COMMAND = 3,
    SETTINGS_KEEP_BATTLE_LISTS = 4,
    SETTINGS_SPEEDS = 5,
    SETTINGS_WINDOW_DESIGN = 6,
    SETTINGS_BATTLE_GAUGE = 7,
    SETTINGS_CONTROLS = 8,
    SETTINGS_DESCRIPTIONS = 9,
    SETTINGS_BATTLE_SPEED = 10,
    SETTINGS_MESSAGE_SPEED = 11,
    SETTINGS_OPTION_COUNT = 12,
} SettingsOption;

enum { SETTINGS_FIRST_GAUGE_PLACE = 12, SETTINGS_GAUGE_LAYOUTS = 3, SETTINGS_FIRST_BUTTON_ROW = 15 };

/* The boxes that say an option is being set, by the option's setting's
 * place among the boxes: the gauge's layout being chosen, the speeds being
 * set, the window's design being chosen. */
enum { SETTINGS_CHOOSING_GAUGE = 4, SETTINGS_SETTING_SPEEDS = 5, SETTINGS_CHOOSING_DESIGN = 6 };

/* The screen's steps, as its main task runs them (menu_screen_step). */
typedef enum {
    SETTINGS_OPENING = 0,
    SETTINGS_ON_THE_OPTIONS = 1,
    SETTINGS_ON_THE_BUTTONS = 2,
    SETTINGS_ON_THE_GAUGE = 3,
} SettingsStep;

/* ---- Opening it --------------------------------------------------------------------------------- */

/* The screen's windows, the cursor on the option it was left on, the
 * description to be typed afresh, the third layer scrolled into place, and
 * the sprites cleared but the cursor. */
void menu_settings_open(GameState *g, Platform *pf);

/* ---- The options -------------------------------------------------------------------------------- */

/* The options' line effects, held every frame. Going back leaves for the
 * main menu unless a speed or the window design is being set, which the
 * option then takes back itself; leaving packs the settings into the
 * player's own. */
void menu_settings_hold_line_effects(GameState *g);
bool menu_settings_leaving(const GameState *g);
void menu_settings_leave(GameState *g);

/* The option the cursor is on is kept, then what the frame's input does to
 * it: each option has its kind's handler (the bridge's table of them,
 * settings_screen_bridge.c), and then it is shown (its description, the
 * boxes). */
SettingsOption menu_settings_take_option(GameState *g);
void menu_settings_show_option(GameState *g);

/* An on-off option chosen flips, its box with it (whether it was chosen);
 * the sound output's mode, which the sound driver is then told. */
bool menu_settings_flip_option(GameState *g, SettingsOption option);
uint8_t menu_settings_sound_output(const GameState *g);

/* The speeds: chosen, the cursor goes to the battle speed's meter. On a
 * meter, left and right step its speed round the eight (whether it moved:
 * a tick), and choosing or going back returns to the speeds. */
void menu_settings_take_speeds(GameState *g);
bool menu_settings_step_meter(GameState *g, SettingsOption meter);
void menu_settings_leave_the_meters(GameState *g);

/* The window design: while it is being chosen left and right step through
 * the eight designs and the windows take each at once; choosing or going
 * back starts or ends the choosing (whether it did: a tick), and the
 * cursor holds against pushes meanwhile. */
bool menu_settings_choosing_design(const GameState *g);
bool menu_settings_step_design(GameState *g);
void menu_settings_show_design(GameState *g);
bool menu_settings_toggle_design_choice(GameState *g);
void menu_settings_hold_cursor_on_the_design(GameState *g);

/* The battle gauge: chosen, the cursor goes to the layout in use (the
 * gauge's step). */
void menu_settings_take_gauge(GameState *g);

/* The controls: choosing flips between the game's buttons and the
 * player's own, going straight to the button rows for the player's own; a
 * push right from the controls goes there too (or, with the game's
 * buttons, to the option to the right). Whether the cursor went to the
 * rows. */
bool menu_settings_take_controls(GameState *g);

/* ---- The battle gauge's layouts (the gauge's step) -------------------------------------------- */

typedef enum { GAUGE_POINTED_AT, GAUGE_CHOSEN, GAUGE_MOVED } SettingsGaugeChoice;
SettingsGaugeChoice menu_settings_choose_gauge_layout(const GameState *g);

/* Back to the options; or the layout the cursor moved to taken, described
 * and shown. */
void menu_settings_back_from_the_gauge(GameState *g);
void menu_settings_take_gauge_layout(GameState *g);

/* ---- The button rows (the buttons' step) --------------------------------------------------------- */

/* The assignment the first shoulder pages lists with (button_assignments'
 * order). */
enum { SETTINGS_PAGE_ONE_WAY = 6 };

typedef enum {
    BUTTONS_KEPT_AND_LEFT, /* a push left keeps the rows' changes and leaves them */
    BUTTONS_LEFT,          /* going back leaves them, the change not kept */
    BUTTONS_RESET,         /* Start puts the game's own buttons back */
    BUTTONS_KEPT,          /* up and down keep the row's change */
    BUTTONS_UNTOUCHED,
} SettingsButtonsInput;
SettingsButtonsInput menu_settings_take_buttons_input(const GameState *g);

/* The rows' changes kept as the player's own buttons; back to the controls
 * option, the buttons in use and the rows shown. */
void menu_settings_keep_the_rows(GameState *g);
void menu_settings_leave_the_buttons(GameState *g);

/* The game's buttons put back as the player's own, or the rows' changes
 * kept; then the buttons in use and the rows shown. */
void menu_settings_reset_buttons(GameState *g);
void menu_settings_keep_buttons(GameState *g);

/* Left and right step the cursor's row through the buttons it can be
 * given, and the assignment that had the button takes the row's (whether
 * the row changed: a tick); then the shoulders keep paging and the rows
 * show the changes. */
bool menu_settings_give_row_button(GameState *g);
void menu_settings_show_row_change(GameState *g);

/* The buttons in use (the player's own where the settings say, else the
 * game's) and the rows drawn from them; a button's glyph on a row. */
void menu_settings_use_buttons(GameState *g);
uint16_t menu_button_glyph_of(const GameState *g, uint8_t button);

/* ---- The main menu's panel ----------------------------------------------------------------------- */

/* With the main menu's cursor on the settings, the screen is drawn beside
 * it: its frame first and the boxes taken from the settings; a frame later
 * its windows, the boxes, the meters, the gauge, the buttons, its colours
 * and the colour math. The main menu leaving it takes the description's
 * icon back. */
void menu_settings_panel_frame(GameState *g, Platform *pf);
void menu_settings_panel_show(GameState *g, Platform *pf);
void menu_settings_panel_put_away(GameState *g);

/* ---- What the steps can run on (the guards') ----------------------------------------------------- */

bool menu_settings_on_an_option(const GameState *g);
bool menu_settings_on_a_layout(const GameState *g);
bool menu_settings_on_a_button_row(const GameState *g);
bool menu_settings_describable(const GameState *g);
/* The opening, the options' step (the cursor movable besides), an option
 * taken: what their entries' guards take of the state. */
bool menu_settings_can_open(const GameState *g);
bool menu_settings_can_choose(const GameState *g);
bool menu_settings_option_can_be_taken(const GameState *g);

/* ---- The opening and the options, one sequence for the machine and for C ---------------------- */

/* The screen's opening ($C2:BF3D), the options' step ($C2:BF45) and an
 * option taken ($C2:BFD4) are each one sequence, which the machine's entries
 * (settings_screen_bridge.c) and the program as C both run, asking the menus
 * for what differs through SettingsSteps.
 *
 * The places the sequences tick from. */
typedef enum {
    SETTINGS_AT_THE_OPENING, SETTINGS_AT_THE_CURSOR_MOVED, SETTINGS_AT_AN_OPTION_CHOSEN,
    SETTINGS_AT_A_METER_STEPPED,    /* a speed's or the design's meter stepped */
    SETTINGS_AT_THE_DESIGN_TOGGLED, /* the design's choosing started or ended */
    SETTINGS_AT_A_GAUGE_TAKEN,      /* another gauge layout taken */
    SETTINGS_AT_THE_BUTTONS_LEFT,   /* back from the button rows to the controls */
    SETTINGS_AT_THE_BUTTONS_TAKEN,  /* the button rows' input taken */
    SETTINGS_AT_THE_BUTTONS_RESET,  /* the buttons put back as they were */
    SETTINGS_AT_THE_BUTTONS_KEPT,   /* the buttons kept, the cursor moved */
    SETTINGS_AT_A_ROW_S_BUTTON,     /* a row's button stepped */
    SETTINGS_AT_A_ROW_CHANGED,      /* ... and shown changed */
    SETTINGS_AT_THE_BUTTONS_DONE,   /* the rows' step done */
} SettingsPlace;

/* Where the machine leaves what the original's tests left (scaffolding),
 * and the P an option's taking saves round its handler. */
typedef enum {
    SETTINGS_NOTE_EDITING_TESTED,  /* whether a speed or the design is being set, tested */
    SETTINGS_NOTE_PACKED,          /* the settings packed as the screen is left */
    SETTINGS_NOTE_BUTTONS_TESTED,  /* the cursor's buttons tested */
    SETTINGS_NOTE_CURSOR_COMPARED, /* the cursor compared with where it started */
    SETTINGS_NOTE_OPTION_PICKED,   /* this option's handler picked */
    SETTINGS_NOTE_HANDLED,         /* its handler done */
    SETTINGS_NOTE_SHOWN,           /* the option shown, the buttons loaded */
    SETTINGS_NOTE_METER_UNMOVED,   /* a meter not stepped */
    SETTINGS_NOTE_METER_SHOWN,     /* a meter stepped and drawn */
    SETTINGS_NOTE_DESIGN_LOADED,   /* the design taken by the windows */
    SETTINGS_NOTE_DESIGN_TOGGLED,  /* the design's choosing toggled (it was on: 1) */
    SETTINGS_NOTE_GAUGE_LEFT,      /* back from the gauge layouts */
    SETTINGS_NOTE_GAUGE_POINTED,   /* the cursor compared with where it started */
    SETTINGS_NOTE_GAUGE_TAKEN,     /* a gauge layout taken */
    SETTINGS_NOTE_ROWS_KEPT,       /* the button rows kept as the player leaves */
    SETTINGS_NOTE_ROWS_LEFT_UNKEPT, /* ... or left as they were */
    SETTINGS_NOTE_QUEUED,          /* the front layer queued */
    SETTINGS_NOTE_START_TESTED,    /* Start tested */
    SETTINGS_NOTE_UP_OR_DOWN_TESTED, /* a push up or down tested */
    SETTINGS_NOTE_ROW_CHANGED,     /* a row's button changed (the paging's before) */
} SettingsNote;

/* What the sequences ask: the cursor read, a tick, the option taken, the
 * option's handler, the way back to the main menu, a note, a meter stepped
 * ($C2:C164). Each false where it went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, SettingsPlace at);
    bool (*take_the_option)(void *ctx);
    bool (*handle)(void *ctx, SettingsOption option);
    bool (*back_to_the_main_menu)(void *ctx);
    void (*note)(void *ctx, SettingsNote note, uint8_t value);
    bool (*step_meter)(void *ctx, SettingsOption meter);
    void (*registers_at)(void *ctx, SettingsPlace at);
    /* The step ends on the cursor's sound, its last. */
    bool (*ends_with_a_tick)(void *ctx);
} SettingsSteps;

bool menu_open_settings_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx);
bool menu_choose_setting_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx);
bool menu_take_setting_option_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx);
/* A meter stepped by a push ($C2:C164), a speed's handler ($C2:C0B9,
 * $C2:C0D1) and the design's ($C2:C12C). */
bool menu_step_setting_meter_with(GameState *g, SettingsOption meter, const SettingsSteps *n, void *ctx);
bool menu_step_speed_with(GameState *g, SettingsOption speed, const SettingsSteps *n, void *ctx);
bool menu_step_window_design_with(GameState *g, const SettingsSteps *n, void *ctx);
/* The gauge's layouts ($C2:BF9F) and the button rows ($C2:BF6D). */
bool menu_choose_gauge_layout_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx);
bool menu_choose_buttons_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx);
bool menu_choose_gauge_layout(GameState *g, Platform *pf);
bool menu_choose_buttons(GameState *g, Platform *pf);
/* Their guards' state halves (settings_screen_bridge.c). */
bool menu_settings_can_choose_buttons(const GameState *g);
bool menu_settings_can_choose_a_layout(const GameState *g);

/* The stereo option's handler ($C2:C017): flipped where chosen, and then
 * the sound driver told the mode (menu_settings_send_sound_output), through
 * `send`; false where it went no further. */
bool menu_settings_set_sound_output(GameState *g, bool (*send)(void *ctx), void *ctx);
void menu_settings_send_sound_output(GameState *g, Platform *pf);

/* As C, from the menus' main task; false where the program went no
 * further. */
bool menu_open_settings(GameState *g, Platform *pf);
bool menu_choose_setting(GameState *g, Platform *pf);

/* ---- The entries (game/menu/settings_screen_bridge.c) ------------------------------------------- */

void menu_open_settings_entry(Snes *s);
void menu_choose_setting_entry(Snes *s);
void menu_choose_buttons_entry(Snes *s);
void menu_choose_gauge_layout_entry(Snes *s);
void menu_take_setting_option(Snes *s);
void menu_set_sound_output(Snes *s);
void menu_flip_active_time(Snes *s);
void menu_flip_keep_menu_cursors(Snes *s);
void menu_flip_keep_battle_command(Snes *s);
void menu_flip_keep_battle_lists(Snes *s);
void menu_flip_descriptions(Snes *s);
void menu_choose_speeds(Snes *s);
void menu_step_battle_speed(Snes *s);
void menu_step_message_speed(Snes *s);
void menu_step_window_design(Snes *s);
void menu_choose_battle_gauge(Snes *s);
void menu_take_controls(Snes *s);
void menu_step_setting_meter(Snes *s);
void menu_show_settings_panel(Snes *s);
void menu_put_settings_panel_away(Snes *s);

bool menu_settings_screen_opens(Snes *s);
bool menu_settings_options_step_runs(Snes *s);
bool menu_setting_option_is_takeable(Snes *s);
bool menu_settings_buttons_step_runs(Snes *s);
bool menu_settings_gauge_step_runs(Snes *s);
bool menu_setting_meter_steps(Snes *s);
bool menu_settings_panel_shows(Snes *s);
bool menu_settings_panel_goes(Snes *s);

#endif
