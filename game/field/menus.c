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
 * the party's places, and hands the machine to the menus; when they are done
 * it sets itself up again, brings back the characters of a party the menu
 * changed (field_bring_the_party_back), and fades in.
 *
 * A script can ask for a menu too (event $C8), which the main loop's requests
 * open here (field_open_script_menu): the field fades out as the leader walks
 * on, and the menu it names opens as these do.
 *
 * The ports it calls run as C: the blanking and the interrupts' handlers
 * (core/display.c, field/interrupts.c) directly, and the line effects'
 * builder, the Start assignment, the party's return and the leader's walk
 * through field_run, which runs each under the frame the original's call
 * pushes and calls out where the entry's guard would turn the state away. The
 * frame wait, the menus, the field's setup and its fade in are the original's
 * routines, called out to from the original's own calls; so is the palette
 * animation, through the original's jump at $FD:FFF7 to it
 * (PALETTE_ANIMATION), which isn't an entry. */
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
#include "field/screen_setup.h"
#include "core/display.h"
#include "core/input.h"
#include "event/event.h"
#include "event/variables.h"
#include "field/bridge.h"
#include "field/parts.h"
#include "port/machine.h"
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

/* The menus' modes the field opens, by what A holds as they start: the main
 * menu, the Start assignment's, and the four a script can ask for, named by
 * their numbers until the menus' side names them (the menus turn each into a
 * screen through $C2:808F: 0 into their mode 14, 2 into 12, 3 into 13, 6 into
 * 11). */
typedef enum { MENU_MODE_0 = 0, MAIN_MENU = 1, MENU_MODE_2 = 2, MENU_MODE_3 = 3, START_MENU = 5, MENU_MODE_6 = 6 } FieldMenu;

/* What the field stops for: the pause, and the three ways a menu is opened
 * -- the menu button, the Start assignment and a script -- each with its
 * own copy of the original's calls. */
typedef enum { PAUSED, OPENED_BY_MENU_BUTTON, OPENED_BY_START, OPENED_BY_SCRIPT } FieldStop;

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

/* The field's steps around the pause and a menu, run as C or on the machine
 * (below), each from the original's call of it for the stop it is in: the
 * line effects' tables built and the wait for the next frame, a frame of the
 * fade out; the leader's walk under the control the fade keeps and the
 * palettes animated, a frame of a script's fade out; the screen dark; the
 * field put aside, the menus, and the field set up again; the party's
 * characters started over where the menu changed the party; the sprites
 * built afresh; and the fade in. Each is false where it went no further. */
typedef struct {
    bool (*effects_built)(void *ctx, FieldStop stop);
    bool (*wait)(void *ctx, FieldStop stop);
    bool (*walk)(void *ctx, uint8_t control);
    bool (*palettes)(void *ctx);
    void (*dark)(void *ctx);
    bool (*put_aside)(void *ctx, FieldStop stop);
    bool (*menus)(void *ctx, FieldStop stop, FieldMenu menu, MenuArgument argument);
    bool (*set_up)(void *ctx, FieldStop stop);
    bool (*party_back)(void *ctx, FieldStop stop);
    bool (*sprites)(void *ctx, FieldStop stop);
    bool (*fade_in)(void *ctx, FieldStop stop);
} StopSteps;

/* Where a menu leaves the field: back in the main loop's pass, the pass over
 * (a script's menu ends with the fade in, which goes back to the main loop
 * itself), or gone elsewhere (a step went no further). */
typedef enum { MENU_WENT_ELSEWHERE, MENU_BACK_IN_THE_PASS, MENU_BACK_IN_THE_LOOP } MenuReturn;

/* ---- The pause -------------------------------------------------------------------------------- */

// $C0:18E8
/* Every frame of the pause the effect tables are built again at half the
 * brightness, and read from their first set. */
enum { DISPLAY_FIRST_SET = 0 };   /* field_display_flags: the first set, no setup */

static bool pause(GameState *g, const StopSteps *steps, void *ctx)
{
    dim_for_pause(g);
    for (;;) {
        if (!steps->wait(ctx, PAUSED))
            return false;
        if (start_pressed(g))
            break;
        show_paused(g);
        if (!steps->effects_built(ctx, PAUSED))
            return false;
        set_field_display_flags(g, DISPLAY_FIRST_SET);
    }
    end_pause(g);
    return true;
}

/* ---- Opening a menu --------------------------------------------------------------------------- */

/* The field's own interrupt handlers back after a menu had the machine. */
static void take_the_interrupts_back(GameState *g)
{
    TIMED_AS(g, FIELD_INSTALL_NMI_ENTRY, field_take_the_vblank_interrupt(g));
    TIMED_AS(g, FIELD_INSTALL_IRQ_ENTRY, field_take_the_line_interrupt(g));
}

// $C0:19C7 (the menu button's is $C0:195E)
/* From the menus on: the field set up again and the party brought back, and
 * faded in. */
static bool menu_and_back(GameState *g, const StopSteps *steps, void *ctx, FieldStop by)
{
    if (!steps->menus(ctx, by, openings[by].menu, openings[by].argument))
        return false;
    steps->dark(ctx);
    take_the_interrupts_back(g);
    if (!steps->set_up(ctx, by) || !steps->party_back(ctx, by))
        return false;
    if (openings[by].answers_script)
        script_menu_shown(g);
    if (!steps->sprites(ctx, by) || !steps->fade_in(ctx, by))
        return false;
    start_play_clock(g);
    return true;
}

// $C0:193E (the menu button's), $C0:19A7 (the Start assignment's)
/* The field fades out a step a frame, is put aside, and the menu opens. */
static bool open_menu(GameState *g, const StopSteps *steps, void *ctx, FieldStop by)
{
    while (fade_out_step(g))
        if (!steps->effects_built(ctx, by) || !steps->wait(ctx, by))
            return false;
    steps->dark(ctx);
    if (!steps->put_aside(ctx, by))
        return false;
    return menu_and_back(g, steps, ctx, by);
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

// $C0:0CC7
/* The menu a script asked for: the field fades out a step a frame as the
 * leader walks on without the player's control (so it stands) and the
 * palettes animate, keeps its places, and opens it; after it the field is set
 * up again and the script's wait is over, and it fades back in. The Start
 * assignment's menu goes on as the Start assignment's does. */
static MenuReturn open_script_menu(GameState *g, const StopSteps *steps, void *ctx)
{
    while (field_dims_a_step(g)) {
        uint8_t control = field_player_control(g);
        hand_the_party_to_the_player(g, false);
        if (!steps->walk(ctx, control))
            return MENU_WENT_ELSEWHERE;
        /* the byte as it was: event $E3 sets it from its operand, any value */
        set_field_player_control(g, control);
        if (!steps->palettes(ctx) || !steps->wait(ctx, OPENED_BY_SCRIPT))
            return MENU_WENT_ELSEWHERE;
    }
    steps->dark(ctx);
    if (!steps->put_aside(ctx, OPENED_BY_SCRIPT))
        return MENU_WENT_ELSEWHERE;
    ScriptMenu asked = script_menu(g);
    if (asked.menu == START_MENU)
        return menu_and_back(g, steps, ctx, OPENED_BY_START) ? MENU_BACK_IN_THE_PASS : MENU_WENT_ELSEWHERE;
    if (!steps->menus(ctx, OPENED_BY_SCRIPT, asked.menu, asked.argument))
        return MENU_WENT_ELSEWHERE;
    steps->dark(ctx);
    take_the_interrupts_back(g);
    if (!steps->set_up(ctx, OPENED_BY_SCRIPT) || !steps->sprites(ctx, OPENED_BY_SCRIPT))
        return MENU_WENT_ELSEWHERE;
    script_menu_shown(g);
    return steps->fade_in(ctx, OPENED_BY_SCRIPT) ? MENU_BACK_IN_THE_LOOP : MENU_WENT_ELSEWHERE;
}

/* ---- The party after a menu ------------------------------------------------------------------- */

/* The characters' objects start again in this order, each from its own call:
 * the original runs Frog's (character 4) before Robo's (character 3). It
 * can't be seen in play: a start runs its character's object script to its
 * first return, the command that loads the character ($C0:41E4-$C0:4214),
 * which writes that object's cells and its character's entry and nothing the
 * next start reads. */
static const struct {
    uint8_t character;
    uint32_t site;
} character_starts[] = {
    { 0, 0xC01A24 }, { 1, 0xC01A2D }, { 2, 0xC01A36 }, { 4, 0xC01A3F },
    { 3, 0xC01A48 }, { 5, 0xC01A51 }, { 6, 0xC01A5A },
};
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

/* A character's object started over in its script, the current object, by
 * its row of character_starts; false where it went no further. */
typedef bool (*StartCharacterOver)(void *ctx, unsigned row);

// $C0:1A03
/* After a menu. Where the menu changed the party, every character on the map
 * starts again in its script, so that the objects show who is in the party
 * now. */
static bool bring_the_party_back(GameState *g, StartCharacterOver start_over, void *ctx)
{
    if (!party_changed(g))
        return true;
    object_recognise_current(g);
    for (unsigned i = 0; i < CHARACTER_STARTS; i++) {
        FieldLink link = field_link_stored(field_character_objects(g, character_starts[i].character));
        if (!field_link_is_object(link))
            continue;
        object_make_current(g, field_linked_object(link));
        if (!start_over(ctx, i))
            return false;
    }
    party_taken_as_loaded(g);
    put_party_back_where_kept(g);
    return true;
}

/* ---- The buttons, each frame ------------------------------------------------------------------ */

// $C0:1985
/* The Start assignment: an open dialog window moves to the other end of the
 * screen; with none open, the Start assignment's menu opens once the story
 * is far enough on. */
static bool press_start(GameState *g, const StopSteps *steps, void *ctx)
{
    if (dialog_window_open(g)) {
        move_dialog_window(g);
        return true;
    }
    return !(start_opens_menu(g) && menus_may_open(g)) || open_menu(g, steps, ctx, OPENED_BY_START);
}

/* The Start assignment's step: as C, or on the machine its entry, from the
 * original's call. */
typedef bool (*PressStart)(void *ctx);

// $C0:18D9
/* Once a frame: Start pauses, the Start assignment does what it does, the
 * menu button opens the main menu, and otherwise a choice waiting takes the
 * buttons. False where a step didn't come back. */
static bool pause_and_menus(GameState *g, const StopSteps *steps, PressStart start, void *ctx)
{
    if (start_pauses(g) && !pause(g, steps, ctx))
        return false;
    if (assigned_pressed(g, ASSIGNED_START) && !start(ctx))
        return false;
    if (!assigned_pressed(g, ASSIGNED_MENU)) {
        if (dialog_choice_waiting(g))
            TIMED_AS(g, FIELD_TAKE_CHOICE_BUTTONS_ENTRY, field_take_choice_buttons(g, dialog_choice_active(g)));
        return true;
    }
    return !menus_may_open(g) || open_menu(g, steps, ctx, OPENED_BY_MENU_BUTTON);
}

/* ---- As C ------------------------------------------------------------------------------------- */

/* The routines the steps stand for, which time them. */
enum {
    MENUS = 0xC28000,               /* the menus, at the mode in A */
    FRAME_WAIT = 0xC0EC60,          /* waits for the next frame */
    OBJECT_START = 0xC0597D,        /* the running object starts over in its script */
    LINE_EFFECTS = 0xFDC2C1,
    PRESS_START = 0xC01985,
    BRING_THE_PARTY_BACK = 0xC01A03,
    WALK = 0xC0881E,
    PALETTE_ANIMATION = 0xFDFFF7,
};

typedef struct {
    GameState *game;
    Platform *pf;
    bool game_starts_again;
} StopAsC;

static bool effects_built_as_c(void *ctx, FieldStop stop)
{
    (void)stop;
    StopAsC *c = ctx;
    TIMED_AS(c->game, LINE_EFFECTS, field_line_effects_build(c->game));
    return true;
}

static bool wait_as_c(void *ctx, FieldStop stop)
{
    (void)stop;
    StopAsC *c = ctx;
    TIMED_AS(c->game, FRAME_WAIT, field_finish_the_frame(c->game, c->pf));
    return true;
}

static bool walk_as_c(void *ctx, uint8_t control)
{
    (void)control;
    StopAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, WALK, done = field_the_leader_walks(c->game));
    return done;
}

static bool palettes_as_c(void *ctx)
{
    StopAsC *c = ctx;
    TIMED_AS(c->game, PALETTE_ANIMATION, palette_animate_a_frame(c->game));
    return true;
}

static void dark_as_c(void *ctx)
{
    StopAsC *c = ctx;
    TIMED_AS(c->game, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(c->pf));
}

static bool put_aside_as_c(void *ctx, FieldStop stop)
{
    (void)stop;
    StopAsC *c = ctx;
    if (!field_party_can_be_kept(c->game)) {
        program_needs_the_machine(c->game, FIELD_PUT_ASIDE_ENTRY, "a party the port doesn't keep for a menu");
        return false;
    }
    TIMED_AS(c->game, FIELD_PUT_ASIDE_ENTRY, field_put_aside(c->game));
    return true;
}

/* An argument not given is what keeping the field left in the menus'
 * argument (field_put_aside_for_a_menu): past the party's places. */
enum { LEFT_BY_KEEPING = 2 * PARTY_PLACES };

/* Where the menus start the game again from its reset (a new game set up,
 * or a game loaded), the field starts anew. */
static bool menus_as_c(void *ctx, FieldStop stop, FieldMenu menu, MenuArgument argument)
{
    (void)stop;
    StopAsC *c = ctx;
    MenuModeEnd end = MENUS_WENT_NO_FURTHER;
    TIMED_AS(c->game, MENUS,
             end = menu_mode_runs(c->game, c->pf, (uint8_t)menu, argument.given ? argument.value : LEFT_BY_KEEPING));
    c->game_starts_again = end == MENUS_START_THE_GAME_AGAIN;
    return end == MENUS_CLOSED;
}

static bool set_up_as_c(void *ctx, FieldStop stop)
{
    (void)stop;
    StopAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, FIELD_SET_UP_AGAIN_ENTRY, done = field_set_up_again_as_c(c->game, c->pf));
    return done;
}

static bool start_character_over_as_c(void *ctx, unsigned row)
{
    (void)row;
    StopAsC *c = ctx;
    EventAsC e = { c->game, c->pf };
    EventRunner r = event_runner_as_c(&e);
    uint16_t at;
    EventTurnEnd end = TURN_OVER;
    TIMED_AS(c->game, OBJECT_START, end = event_restart_the_running_object(c->game, &r, &at));
    if (end == TURN_OVER)
        return true;
    if (end != TURN_HANDED_BACK)
        program_needs_the_machine(c->game, OBJECT_START, "a character's script the event engine hands to the original");
    return false;
}

static bool party_links_readable(const GameState *g);

static bool party_back_as_c(void *ctx, FieldStop stop)
{
    (void)stop;
    StopAsC *c = ctx;
    if (!party_links_readable(c->game)) {
        program_needs_the_machine(c->game, BRING_THE_PARTY_BACK, "a party the port doesn't bring back");
        return false;
    }
    bool done = false;
    TIMED_AS(c->game, BRING_THE_PARTY_BACK, done = bring_the_party_back(c->game, start_character_over_as_c, c));
    return done;
}

static bool sprites_as_c(void *ctx, FieldStop stop)
{
    (void)stop;
    StopAsC *c = ctx;
    TIMED_AS(c->game, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, sprites_all_to_build_afresh(c->game));
    return true;
}

static bool fade_in_as_c(void *ctx, FieldStop stop)
{
    (void)stop;
    StopAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, FIELD_FADE_IN_AFTER_A_MENU_ENTRY, done = field_fade_in_after_a_menu_as_c(c->game, c->pf));
    return done;
}

static const StopSteps as_c = { effects_built_as_c, wait_as_c,  walk_as_c,      palettes_as_c,
                                dark_as_c,          put_aside_as_c, menus_as_c, set_up_as_c,
                                party_back_as_c,    sprites_as_c, fade_in_as_c };

static bool press_start_as_c(void *ctx)
{
    StopAsC *c = ctx;
    bool back = false;
    TIMED_AS(c->game, PRESS_START, back = press_start(c->game, &as_c, c));
    return back;
}

bool field_pause_and_menus_as_c(GameState *g, Platform *pf)
{
    StopAsC c = { g, pf, false };
    return pause_and_menus(g, &as_c, press_start_as_c, &c);
}

FieldPass field_script_menu_opened(GameState *g, Platform *pf)
{
    StopAsC c = { g, pf, false };
    switch (open_script_menu(g, &as_c, &c)) {
    case MENU_BACK_IN_THE_PASS: return FIELD_PASS_GOES_ON;
    case MENU_BACK_IN_THE_LOOP: return FIELD_PASS_GOES_ROUND;
    default: return c.game_starts_again ? FIELD_STARTS_ANEW : FIELD_PASS_STOPS;
    }
}

/* ---- On the machine --------------------------------------------------------------------------- */

enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
    READS_THE_MODE = DC_AH | DC_Y | DC_C,               /* the menus keep the flags and take A and X */
    READS_THE_WALK = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
};

/* Each stop's own copy of the original's calls, by where each is. The
 * pause's has only its effects and its wait; a script's fade out walks and
 * animates the palettes, and its fade in is the original's jump, which
 * returns to the main loop itself. */
static const struct {
    uint32_t effects, wait, keep, menus, set_up, party, sprites, fade_in;
} sites[] = {
    [PAUSED] = { 0xC01905, 0xC018F4, 0, 0, 0, 0, 0, 0 },
    [OPENED_BY_MENU_BUTTON] = { 0xC0194D, 0xC01953, 0xC0195B, 0xC01960, 0xC01975, 0xC01978, 0xC0197B, 0xC0197E },
    [OPENED_BY_START] = { 0xC019B6, 0xC019BC, 0xC019C4, 0xC019CE, 0xC019E3, 0xC019E6, 0xC019F9, 0xC019FC },
    [OPENED_BY_SCRIPT] = { 0, 0xC00CE5, 0xC00CED, 0xC00D18, 0xC00D2D, 0, 0xC00D30, 0 },
};
enum { SCRIPT_FADE_WALK = 0xC00CDB, SCRIPT_FADE_PALETTES = 0xC00CE1 };
enum { FADE_IN_TAIL = FIELD_FADE_IN_AFTER_A_MENU_ENTRY, FADE_IN_READS = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* The line effect tables are built with 8-bit index registers. */
static bool effects_built_on_the_machine(void *ctx, FieldStop stop)
{
    Snes *s = ctx;
    m_sep(s, CPU_FLAG_X);
    if (!field_run_long(s, sites[stop].effects, LINE_EFFECTS, READS_NOTHING, field_build_line_effect_tables,
                        on_the_field_page(s, true)))
        return false;
    m_rep(s, CPU_FLAG_X);
    return true;
}

static bool wait_on_the_machine(void *ctx, FieldStop stop)
{
    Snes *s = ctx;
    return field_wait_frame(s, sites[stop].wait, FRAME_WAIT, READS_NOTHING, on_the_field_page(s, false));
}

/* The original keeps the control on the stack over the walk, which the
 * walk's own call-outs and stops see, as shadow mode compares them. */
static bool walk_on_the_machine(void *ctx, uint8_t control)
{
    Snes *s = ctx;
    return field_run_holding(s, SCRIPT_FADE_WALK, WALK, READS_THE_WALK, field_walk_the_leader,
                             on_the_field_page(s, false), control);
}

static bool palettes_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return bridge_call_long(s, SCRIPT_FADE_PALETTES, PALETTE_ANIMATION, READS_NOTHING, on_the_field_page(s, false));
}

static void dark_on_the_machine(void *ctx) { display_go_dark(ctx); }

static bool put_aside_on_the_machine(void *ctx, FieldStop stop)
{
    Snes *s = ctx;
    return field_run(s, sites[stop].keep, FIELD_PUT_ASIDE_ENTRY, READS_NOTHING, field_put_aside_for_a_menu,
                     on_the_field_page(s, false));
}

/* The menus take the mode in A with its high byte clear, and N and Z as its
 * load leaves them (N clear, Z where the mode is 0), since their entry keeps
 * the flags; X is the argument of the screen they open on. */
static bool menus_on_the_machine(void *ctx, FieldStop stop, FieldMenu menu, MenuArgument argument)
{
    Snes *s = ctx;
    if (argument.given)
        s->cpu.x = argument.value;
    s->cpu.a = (uint16_t)menu;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, menu == MENU_MODE_0);
    return bridge_call_long(s, sites[stop].menus, MENUS, READS_THE_MODE, on_the_field_page(s, false));
}

static bool set_up_on_the_machine(void *ctx, FieldStop stop)
{
    Snes *s = ctx;
    return field_run(s, sites[stop].set_up, FIELD_SET_UP_AGAIN_ENTRY, READS_NOTHING, field_set_up_after_a_menu,
                     on_the_field_page(s, false));
}

static bool party_back_on_the_machine(void *ctx, FieldStop stop)
{
    Snes *s = ctx;
    return field_run(s, sites[stop].party, BRING_THE_PARTY_BACK, READS_THE_OVERFLOW, field_bring_the_party_back,
                     on_the_field_page(s, false));
}

static bool sprites_on_the_machine(void *ctx, FieldStop stop)
{
    Snes *s = ctx;
    return field_run(s, sites[stop].sprites, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, READS_NOTHING,
                     field_build_the_sprites_afresh, on_the_field_page(s, false));
}

/* A script's fade in is the original's jump, which returns to the main loop
 * itself: the port goes no further either way. */
static bool fade_in_on_the_machine(void *ctx, FieldStop stop)
{
    Snes *s = ctx;
    if (stop != OPENED_BY_SCRIPT)
        return field_run(s, sites[stop].fade_in, FIELD_FADE_IN_AFTER_A_MENU_ENTRY, READS_NOTHING,
                         field_fade_in_after_a_menu, on_the_field_page(s, false));
    if (!field_go_on_with(s, FADE_IN_TAIL, field_fade_in_after_a_menu))
        m_stop_at_own(s, FADE_IN_TAIL, FADE_IN_READS);
    return false;
}

static const StopSteps on_the_machine = {
    effects_built_on_the_machine, wait_on_the_machine,    walk_on_the_machine,     palettes_on_the_machine,
    dark_on_the_machine,          put_aside_on_the_machine, menus_on_the_machine, set_up_on_the_machine,
    party_back_on_the_machine,    sprites_on_the_machine, fade_in_on_the_machine,
};

enum { PRESS_START_CALL = 0xC0191D };

static bool press_start_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return field_run(s, PRESS_START_CALL, PRESS_START, READS_NOTHING, field_press_start, on_the_field_page(s, false));
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The field's page, 8-bit A, 16-bit X. What it leaves in the registers is the
 * routines' it called, or what it came in with: A's high byte and the
 * overflow are read after it. */
void field_pause_and_menus(Snes *s)
{
    if (pause_and_menus(state_of(s), &on_the_machine, press_start_on_the_machine, s))
        m_rts(s);
}

/* As for field_pause_and_menus; the story's test leaves the overflow its
 * subtraction makes (field/leaves.c). */
void field_press_start(Snes *s)
{
    GameState *g = state_of(s);
    if (!dialog_window_open(g))
        m_set_flag(s, CPU_FLAG_V, field_story_test_overflow(g));
    if (press_start(g, &on_the_machine, s))
        m_rts(s);
}

/* True where it came back to the main loop's pass, false where the port
 * stopped, or the fade in has gone back to the main loop itself. */
bool field_open_script_menu(Snes *s) { return open_script_menu(state_of(s), &on_the_machine, s) == MENU_BACK_IN_THE_PASS; }

/* A character's script started over from its own call. */
static bool start_character_over_on_the_machine(void *ctx, unsigned row)
{
    Snes *s = ctx;
    return field_run(s, character_starts[row].site, OBJECT_START, READS_THE_OVERFLOW,
                     event_restart_the_running_object_entry, on_the_field_page(s, false));
}

/* The field's page, 8-bit A, 16-bit X; the overflow goes on to the scripts'
 * starts as it came. */
void field_bring_the_party_back(Snes *s)
{
    if (bring_the_party_back(state_of(s), start_character_over_on_the_machine, s))
        m_rts(s);
}

/* The guard: the field's page with the data bank $00, and the party's places
 * objects the tables hold, or none. */
static bool party_links_readable(const GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (field_link_is_object(link) && !field_link_names_object(link))
            return false;
    }
    return true;
}

bool field_party_can_come_back(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = party_links_readable(state_of(s));
    vars_peek(false);
    return ok;
}

/* The Start assignment's guard: the field's page with the data bank $00, and
 * the dialog window at the top, the bottom or shut (the swap takes no other
 * place). */
bool field_start_can_be_pressed(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = dialog_window_at(state_of(s)) < WINDOW_POSITIONS;
    vars_peek(false);
    return ok;
}
