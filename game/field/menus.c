/* Field mode: the pause, and the menus opened from the field.
 *
 * Once a frame the field's main loop looks at the buttons here
 * (field_pause_and_menus):
 * - Start pauses the field while the player has the party (and no script has
 *   locked the pause): the picture dims to half and the play time clock
 *   stops until Start is pressed again.
 * - The Start assignment moves an open dialog window between the top of the
 *   screen and the bottom; with none open, and once the story is far enough
 *   on, it opens the menus at their mode 5 (field_press_start).
 * - X opens the main menu (the menus' mode 1).
 * - Otherwise, while a dialog waits on a choice, the choice takes the
 *   buttons.
 * A menu opens only while the player has the party, no choice is waiting and
 * no script has locked the menus. The field fades out a step a frame, keeps
 * the party's places, and hands the screen to the menus; when they are done
 * it sets itself up again, brings back the characters of a party the menu
 * changed (field_bring_the_party_back), and fades in.
 *
 * A script can ask for a menu too (event $C8), which the main loop's requests
 * open here (field_open_script_menu): the field fades out as the leader walks
 * on, and the menu it names opens as these do.
 *
 * Each step is a call of the routine it is, timed as that routine; where a
 * step's state is one the port doesn't take, the program reports it and goes
 * no further. */
#include "field/menus.h"
#include "field/dialog_window.h"
#include "field/field.h"
#include "field/line_effects.h"
#include "field/main_loop.h"
#include "field/object.h"
#include "field/party.h"
#include "field/sprites.h"
#include "field/screen_setup.h"
#include "field/walk.h"

#include "charge.h"
#include "core/palette.h"
#include "core/program.h"
#include "event/engine.h"
#include "menu/display.h"
#include "core/display.h"
#include "core/input.h"
#include "event/variables.h"
#include "field/bridge.h"
#include "field/parts.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The field's state ------------------------------------------------------------------------ */

static bool start_pressed(const GameState *g) { return (input_pressed(g) & BUTTON_START) != 0; }

/* The buttons the menus answer to, through the player's assignments. */
enum { ASSIGNED_START = BUTTON_START, ASSIGNED_MENU = BUTTON_X };

static bool assigned_pressed(const GameState *g, Buttons button)
{
    return (input_mapped(g, ASSIGNED_PRESSED) & button) != 0;
}

static bool start_pauses(const GameState *g)
{
    return start_pressed(g) && !field_pause_is_locked(g) && party_is_the_players(g);
}

/* Paused, the picture shows at half its brightness. */
static void dim_for_pause(GameState *g)
{
    set_field_brightness_before_pause(g, field_brightness(g));
    set_field_brightness(g, (uint8_t)(field_brightness(g) >> 1));
    stop_play_clock(g);
}

static void show_paused(GameState *g) { set_field_brightness(g, (uint8_t)(field_brightness_before_pause(g) >> 1)); }

static void end_pause(GameState *g)
{
    set_field_brightness(g, field_brightness_before_pause(g));
    start_play_clock(g);
}

static bool menus_may_open(const GameState *g)
{
    return party_is_the_players(g) && !dialog_choice_waiting(g) && !field_menus_are_locked(g);
}

/* The Start assignment's menu waits on the story's progress (field.h). */
static bool start_opens_menu(const GameState *g) { return story_progress(g) >= FIELD_START_MENU_FROM_STORY; }

/* An open dialog window goes from the top of the screen to the bottom, or
 * back. */
static bool dialog_window_open(const GameState *g) { return dialog_window_at(g) != WINDOW_NONE; }
static void move_dialog_window(GameState *g)
{
    WindowPosition to = dialog_window_at(g) == WINDOW_AT_TOP ? WINDOW_AT_BOTTOM : WINDOW_AT_TOP;
    set_dialog_window_position(g, (uint8_t)to);
}

/* A step of the fade out before a menu, the clock stopped at every step. */
static bool fade_out_step(GameState *g)
{
    stop_play_clock(g);
    return field_dims_a_step(g);
}

/* A menu a script had asked for is taken as shown when the Start
 * assignment's menu opens, and the script waiting on it goes on. */
static void script_menu_shown(GameState *g)
{
    if (!field_requested(g, FIELD_REQUEST_MENUS))
        return;
    set_field_requests(g, (uint8_t)(field_requests(g) & ~FIELD_REQUEST_MENUS));
    set_field_effect_status(g, (uint8_t)(field_effect_status(g) | FIELD_SCRIPT_MENU_SHOWN));
}

/* ---- The menus and the field's steps around them ---------------------------------------------- */

/* The menus' modes the field opens, by the number they start with: the main
 * menu, the Start assignment's, and the four a script can ask for, named by
 * their numbers until the menus' side names them (the menus turn each into a
 * screen through $C2:808F: 0 into their mode 14, 2 into 12, 3 into 13, 6 into
 * 11). */
typedef enum {
    MENU_MODE_0 = 0,
    MAIN_MENU = 1,
    MENU_MODE_2 = 2,
    MENU_MODE_3 = 3,
    START_MENU = 5,
    MENU_MODE_6 = 6,
} FieldMenu;

/* The two buttons that open a menu, the menu button and the Start
 * assignment, each with its own copy of the original's calls (a script's
 * menu goes its own way, below). */
typedef enum { OPENED_BY_MENU_BUTTON, OPENED_BY_START } MenuOpener;

/* The screen a menu opens on takes an argument (a shop's number, a party
 * member) where it is given one; the main menu's is whatever keeping the
 * field left there. */
typedef struct {
    bool given;
    uint8_t value;
} MenuArgument;

/* What the menu button and the Start assignment open, and whether a menu a
 * script asked for counts as shown after it. */
static const struct {
    FieldMenu menu;
    MenuArgument argument;
    bool answers_script;
} openings[] = {
    [OPENED_BY_MENU_BUTTON] = { MAIN_MENU, { false, 0 }, false },
    [OPENED_BY_START] = { START_MENU, { true, 0 }, true },
};

/* ---- The field's steps around the pause and a menu --------------------------------------------- */

/* The routines the steps stand for, which time them. */
enum {
    MENUS = 0xC28000,               /* the menus, at the mode they are given */
    FRAME_WAIT = 0xC0EC60,          /* waits for the next frame */
    OBJECT_START = 0xC0597D,        /* the running object starts over in its script */
    LINE_EFFECTS = 0xFDC2C1,
    PRESS_START = 0xC01985,
    BRING_THE_PARTY_BACK = 0xC01A03,
    WALK = 0xC0881E,
    PALETTE_ANIMATION = 0xFDFFF7,
};

/* A frame of the fade out: the line effects' tables built, and the wait for
 * the next frame. */
static void build_the_line_effects(GameState *g) { TIMED_AS(g, LINE_EFFECTS, field_line_effects_build(g)); }

static void wait_for_the_next_frame(GameState *g, Platform *pf)
{
    TIMED_AS(g, FRAME_WAIT, field_finish_the_frame(g, pf));
}

/* The screen dark. */
static void go_dark(GameState *g, Platform *pf) { TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf)); }

/* The field put aside for a menu, where the party is one the port keeps. */
static bool put_aside(GameState *g)
{
    if (!field_party_can_be_kept(g)) {
        program_breaks_a_contract(g, FIELD_PUT_ASIDE_ENTRY, "a party the port doesn't keep for a menu");
        return false;
    }
    TIMED_AS(g, FIELD_PUT_ASIDE_ENTRY, field_put_aside(g));
    return true;
}

/* Where a menu leaves the field: back in the main loop's pass, the pass over
 * (a script's menu ends with the fade in, which goes back to the main loop
 * itself), the game started again from its reset (a new game set up, or a
 * game loaded, where the field starts anew), or gone elsewhere (a step went
 * no further). */
typedef enum { MENU_WENT_ELSEWHERE, MENU_BACK_IN_THE_PASS, MENU_BACK_IN_THE_LOOP, MENU_STARTS_THE_GAME_AGAIN } MenuReturn;

/* An argument not given is what keeping the field left in the menus'
 * argument (field_put_aside_for_a_menu): past the party's places. */
enum { LEFT_BY_KEEPING = 2 * PARTY_PLACES };

static MenuReturn run_the_menus(GameState *g, Platform *pf, FieldMenu menu, MenuArgument argument)
{
    MenuModeEnd end = MENUS_WENT_NO_FURTHER;
    TIMED_AS(g, MENUS, end = menu_mode_runs(g, pf, (uint8_t)menu, argument.given ? argument.value : LEFT_BY_KEEPING));
    switch (end) {
    case MENUS_CLOSED: return MENU_BACK_IN_THE_PASS;
    case MENUS_START_THE_GAME_AGAIN: return MENU_STARTS_THE_GAME_AGAIN;
    default: return MENU_WENT_ELSEWHERE;
    }
}

/* The field set up again, the sprites built afresh, and the fade in. */
static bool set_up_again(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, FIELD_SET_UP_AGAIN_ENTRY, done = field_set_up_again(g, pf));
    return done;
}

static void build_the_sprites_afresh(GameState *g)
{
    TIMED_AS(g, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, sprites_all_to_build_afresh(g));
}

static bool fade_in(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, FIELD_FADE_IN_AFTER_A_MENU_ENTRY, done = field_fade_in_after_a_menu(g, pf));
    return done;
}

/* ---- The pause -------------------------------------------------------------------------------- */

// $C0:18E8
/* Every frame of the pause the effect tables are built again at half the
 * brightness, and read from their first set. */
enum { DISPLAY_FIRST_SET = 0 };   /* field_display_flags: the first set, no setup */

static void pause(GameState *g, Platform *pf)
{
    dim_for_pause(g);
    for (;;) {
        wait_for_the_next_frame(g, pf);
        if (start_pressed(g))
            break;
        show_paused(g);
        build_the_line_effects(g);
        set_field_display_flags(g, DISPLAY_FIRST_SET);
    }
    end_pause(g);
}

/* ---- The party after a menu ------------------------------------------------------------------- */

/* The characters' objects start again in this order, each from its own call:
 * the original runs Frog's (character 4) before Robo's (character 3). It
 * can't be seen in play: a start runs its character's object script to its
 * first return, the command that loads the character ($C0:41E4-$C0:4214),
 * which writes that object's cells and its character's entry and nothing the
 * next start reads. */
static const uint8_t character_starts[] = { 0, 1, 2, 4, 3, 5, 6 };
enum { CHARACTER_STARTS = sizeof character_starts / sizeof character_starts[0] };

static bool party_changed(const GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        if (party_members(g, place) != field_party_as_loaded(g, place))
            return true;
    return false;
}

/* Each member back where the field kept it before the menu: its place on the
 * map, its sprites' priorities, and its place on the leader's trail. */
static void put_party_back_where_kept(GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (!field_link_is_object(link))
            continue;
        FieldObject member = field_linked_object(link);
        object_place(g, member, (FieldPosition){ field_kept_party_x(g, place), field_kept_party_y(g, place) });
        object_take_sprite_priorities(g, member, (SpritePriorities){ field_kept_party_priorities_lower(g, place),
                                                                    field_kept_party_priorities_upper(g, place) });
    }
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        set_field_trail_position(g, place, field_kept_trail_positions(g, place));
}

/* A character's object started over in its script, the current object;
 * false where it went no further. */
static bool start_the_character_over(GameState *g, Platform *pf)
{
    uint16_t at;
    EventTurnEnd end = TURN_OVER;
    TIMED_AS(g, OBJECT_START, end = event_restart_the_running_object(g, pf, &at));
    if (end == TURN_OVER)
        return true;
    if (end != TURN_HANDED_BACK)
        program_breaks_a_contract(g, OBJECT_START, "a character's script the event engine hands to the original");
    return false;
}

// $C0:1A03
/* After a menu. Where the menu changed the party, every character on the map
 * starts again in its script, so that the objects show who is in the party
 * now. */
static bool bring_the_party_back(GameState *g, Platform *pf)
{
    if (!party_changed(g))
        return true;
    object_recognise_current(g);
    for (unsigned i = 0; i < CHARACTER_STARTS; i++) {
        FieldLink link = field_link_stored(field_character_objects(g, character_starts[i]));
        if (!field_link_is_object(link))
            continue;
        object_make_current(g, field_linked_object(link));
        if (!start_the_character_over(g, pf))
            return false;
    }
    party_taken_as_loaded(g);
    put_party_back_where_kept(g);
    return true;
}

static bool party_links_readable(const GameState *g);

/* The party brought back, where it is one the port brings back. */
static bool party_back(GameState *g, Platform *pf)
{
    if (!party_links_readable(g)) {
        program_breaks_a_contract(g, BRING_THE_PARTY_BACK, "a party the port doesn't bring back");
        return false;
    }
    bool done = false;
    TIMED_AS(g, BRING_THE_PARTY_BACK, done = bring_the_party_back(g, pf));
    return done;
}

/* ---- Opening a menu --------------------------------------------------------------------------- */

/* The field's own interrupt handlers back after a menu. */
static void take_the_interrupts_back(GameState *g)
{
    TIMED_AS(g, FIELD_INSTALL_NMI_ENTRY, field_take_the_vblank_interrupt(g));
    TIMED_AS(g, FIELD_INSTALL_IRQ_ENTRY, field_take_the_line_interrupt(g));
}

// $C0:19C7 (the menu button's is $C0:195E)
/* From the menus on: the field set up again and the party brought back, and
 * faded in. */
static bool back_from_menu(GameState *g, Platform *pf, MenuOpener by)
{
    go_dark(g, pf);
    take_the_interrupts_back(g);
    if (!set_up_again(g, pf) || !party_back(g, pf))
        return false;
    if (openings[by].answers_script)
        script_menu_shown(g);
    build_the_sprites_afresh(g);
    if (!fade_in(g, pf))
        return false;
    start_play_clock(g);
    return true;
}

static MenuReturn menu_and_back(GameState *g, Platform *pf, MenuOpener by)
{
    MenuReturn menus = run_the_menus(g, pf, openings[by].menu, openings[by].argument);
    if (menus != MENU_BACK_IN_THE_PASS)
        return menus;
    return back_from_menu(g, pf, by) ? MENU_BACK_IN_THE_PASS : MENU_WENT_ELSEWHERE;
}

// $C0:193E (the menu button's), $C0:19A7 (the Start assignment's)
/* The field fades out a step a frame, is put aside, and the menu opens. */
static bool open_menu(GameState *g, Platform *pf, MenuOpener by)
{
    while (fade_out_step(g)) {
        build_the_line_effects(g);
        wait_for_the_next_frame(g, pf);
    }
    go_dark(g, pf);
    if (!put_aside(g))
        return false;
    return menu_and_back(g, pf, by) == MENU_BACK_IN_THE_PASS;
}

/* ---- The menus a script asks for (event $C8) --------------------------------------------------- */

/* What the script asked for (field_script_menu): 0 the Start assignment's
 * menu; $01-$7F the menus' mode 0 where it is odd, 6 where it is even; $80-$FF
 * the mode its top two bits give, with the screen's argument in its low six. */
typedef struct {
    FieldMenu menu;
    MenuArgument argument;
} ScriptMenu;

enum { ARGUMENT_GIVEN = 0x80, ARGUMENT_BITS = 0x3F, MODE_SHIFT = 6, ODD = 0x01 };

static ScriptMenu script_menu(const GameState *g)
{
    uint8_t asked = field_script_menu(g);
    if (asked == 0)
        return (ScriptMenu){ .menu = START_MENU };
    if (asked & ARGUMENT_GIVEN)
        return (ScriptMenu){ (FieldMenu)(asked >> MODE_SHIFT), { true, (uint8_t)(asked & ARGUMENT_BITS) } };
    return (ScriptMenu){ .menu = (asked & ODD) ? MENU_MODE_0 : MENU_MODE_6 };
}

/* Restore the field after a script's menu. The fade returns to the main
 * loop, so there is no suspended menu-opening call to resume. */
static MenuReturn back_from_script_menu(GameState *g, Platform *pf)
{
    go_dark(g, pf);
    take_the_interrupts_back(g);
    if (!set_up_again(g, pf))
        return MENU_WENT_ELSEWHERE;
    build_the_sprites_afresh(g);
    script_menu_shown(g);
    return fade_in(g, pf) ? MENU_BACK_IN_THE_LOOP : MENU_WENT_ELSEWHERE;
}

// $C0:0CC7
/* The menu a script asked for: the field fades out a step a frame as the
 * leader walks on without the player's control (so it stands) and the
 * palettes animate, keeps its places, and opens it; after it the field is set
 * up again and the script's wait is over, and it fades back in. The Start
 * assignment's menu goes on as the Start assignment's does. */
static MenuReturn open_script_menu(GameState *g, Platform *pf)
{
    while (field_dims_a_step(g)) {
        uint8_t control = field_player_control(g);
        hand_the_party_to_the_player(g, false);
        bool done = false;
        TIMED_AS(g, WALK, done = field_the_leader_walks(g));
        if (!done)
            return MENU_WENT_ELSEWHERE;
        /* the byte as it was: event $E3 sets it from its operand, any value */
        set_field_player_control(g, control);
        TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g));
        if (!done)
            return MENU_WENT_ELSEWHERE;
        wait_for_the_next_frame(g, pf);
    }
    go_dark(g, pf);
    if (!put_aside(g))
        return MENU_WENT_ELSEWHERE;
    ScriptMenu asked = script_menu(g);
    if (asked.menu == START_MENU)
        return menu_and_back(g, pf, OPENED_BY_START);
    MenuReturn menus = run_the_menus(g, pf, asked.menu, asked.argument);
    if (menus != MENU_BACK_IN_THE_PASS)
        return menus;
    return back_from_script_menu(g, pf);
}

/* ---- The buttons, each frame ------------------------------------------------------------------ */

// $C0:1985
/* The Start assignment: an open dialog window moves to the other end of the
 * screen; with none open, the Start assignment's menu opens once the story
 * is far enough on. */
static bool press_start(GameState *g, Platform *pf)
{
    if (dialog_window_open(g)) {
        move_dialog_window(g);
        return true;
    }
    return !(start_opens_menu(g) && menus_may_open(g)) || open_menu(g, pf, OPENED_BY_START);
}

// $C0:18D9
/* Once a frame: Start pauses, the Start assignment does what it does, the
 * menu button opens the main menu, and otherwise a choice waiting takes the
 * buttons. False where a step didn't come back. */
bool field_pause_and_menus(GameState *g, Platform *pf)
{
    if (start_pauses(g))
        pause(g, pf);
    if (assigned_pressed(g, ASSIGNED_START)) {
        bool back = false;
        TIMED_AS(g, PRESS_START, back = press_start(g, pf));
        if (!back)
            return false;
    }
    if (!assigned_pressed(g, ASSIGNED_MENU)) {
        if (dialog_choice_waiting(g))
            TIMED_AS(g, FIELD_TAKE_CHOICE_BUTTONS_ENTRY, field_take_choice_buttons(g, dialog_choice_active(g)));
        return true;
    }
    return !menus_may_open(g) || open_menu(g, pf, OPENED_BY_MENU_BUTTON);
}

FieldPass field_script_menu_opened(GameState *g, Platform *pf)
{
    switch (open_script_menu(g, pf)) {
    case MENU_BACK_IN_THE_PASS: return FIELD_PASS_GOES_ON;
    case MENU_BACK_IN_THE_LOOP: return FIELD_PASS_GOES_ROUND;
    case MENU_STARTS_THE_GAME_AGAIN: return FIELD_STARTS_ANEW;
    default: return FIELD_PASS_STOPS;
    }
}

/* Whether the port keeps the party for a menu: the party's places objects
 * the tables hold, or none. */
static bool party_links_readable(const GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (field_link_is_object(link) && !field_link_names_object(link))
            return false;
    }
    return true;
}
