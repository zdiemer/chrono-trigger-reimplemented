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

/* ---- The routines it calls on the machine ---------------------------------------------------- */

/* The original's routines, and the engine's restart of the running object
 * (event/bridge.c), run as C. */
enum {
    MENUS = 0xC28000,               /* the menus, at the mode in A */
    FRAME_WAIT = 0xC0EC60,          /* waits for the next frame */
    OBJECT_START = 0xC0597D,        /* the running object starts over in its script */
};

/* Ports this file runs through field_run, by the routines they stand for. */
enum { LINE_EFFECTS = 0xFDC2C1, PRESS_START = 0xC01985, BRING_THE_PARTY_BACK = 0xC01A03 };

enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
    READS_THE_MODE = DC_AH | DC_Y | DC_C,               /* the menus keep the flags and take A and X */
};

static bool wait_frame(Snes *s, uint32_t site)
{
    return field_wait_frame(s, site, FRAME_WAIT, READS_NOTHING, on_the_field_page(s, false));
}

/* The field put aside before a menu, and set up again after it
 * (field/location.c). */
static bool put_the_field_aside(Snes *s, uint32_t site)
{
    return field_run(s, site, FIELD_PUT_ASIDE_ENTRY, READS_NOTHING, field_put_aside_for_a_menu,
                     on_the_field_page(s, false));
}
static bool set_the_field_up(Snes *s, uint32_t site)
{
    return field_run(s, site, FIELD_SET_UP_AGAIN_ENTRY, READS_NOTHING, field_set_up_after_a_menu,
                     on_the_field_page(s, false));
}

/* The line effect tables are built with 8-bit index registers. */
static bool rebuild_line_effects(Snes *s, uint32_t site)
{
    m_sep(s, CPU_FLAG_X);
    if (!field_run_long(s, site, LINE_EFFECTS, READS_NOTHING, field_build_line_effect_tables,
                        on_the_field_page(s, true)))
        return false;
    m_rep(s, CPU_FLAG_X);
    return true;
}

/* The field's own interrupt handlers back after a menu had the machine. */
static void take_the_interrupts_back(GameState *g)
{
    TIMED_AS(g, FIELD_INSTALL_NMI_ENTRY, field_take_the_vblank_interrupt(g));
    TIMED_AS(g, FIELD_INSTALL_IRQ_ENTRY, field_take_the_line_interrupt(g));
}

/* ---- The pause -------------------------------------------------------------------------------- */

enum { PAUSE_WAIT = 0xC018F4, PAUSE_LINE_EFFECTS = 0xC01905 };

// $C0:18E8
/* Every frame of the pause the effect tables are built again at half the
 * brightness, and read from their first set. */
enum { DISPLAY_FIRST_SET = 0 };   /* field_display_flags: the first set, no setup */
static bool pause(Snes *s)
{
    GameState *g = state_of(s);
    dim_for_pause(g);
    for (;;) {
        if (!wait_frame(s, PAUSE_WAIT))
            return false;
        if (start_pressed(g))
            break;
        show_paused(g);
        if (!rebuild_line_effects(s, PAUSE_LINE_EFFECTS))
            return false;
        set_field_display_flags(g, DISPLAY_FIRST_SET);
    }
    end_pause(g);
    return true;
}

/* ---- Opening a menu --------------------------------------------------------------------------- */

/* The menus' modes the field opens, by what A holds as they start: the main
 * menu, the Start assignment's, and the four a script can ask for, named by
 * their numbers until the menus' side names them (the menus turn each into a
 * screen through $C2:808F: 0 into their mode 14, 2 into 12, 3 into 13, 6 into
 * 11). */
typedef enum { MENU_MODE_0 = 0, MAIN_MENU = 1, MENU_MODE_2 = 2, MENU_MODE_3 = 3, START_MENU = 5, MENU_MODE_6 = 6 } FieldMenu;

/* The two ways a menu is opened: the menu, whether the screen it opens on is
 * given no argument (the menus' caller hands one in X: a shop's number, a party
 * member; the main menu's is whatever keeping the field left there), and
 * whether a menu a script asked for counts as shown. */
typedef enum { OPENED_BY_MENU_BUTTON, OPENED_BY_START } MenuOpener;
static const struct {
    FieldMenu menu;
    bool no_argument, answers_script;
} openings[] = {
    [OPENED_BY_MENU_BUTTON] = { MAIN_MENU, false, false },
    [OPENED_BY_START] = { START_MENU, true, true },
};

/* Each opening's own copy of the original's calls, by where each is. */
static const struct {
    uint32_t fade_effects, fade_wait, keep, menus, set_up, party, sprites, fade_in;
} opening_sites[] = {
    [OPENED_BY_MENU_BUTTON] = { 0xC0194D, 0xC01953, 0xC0195B, 0xC01960, 0xC01975, 0xC01978, 0xC0197B, 0xC0197E },
    [OPENED_BY_START] = { 0xC019B6, 0xC019BC, 0xC019C4, 0xC019CE, 0xC019E3, 0xC019E6, 0xC019F9, 0xC019FC },
};

/* The menus take the mode in A with its high byte clear, and N and Z as its
 * load leaves them (N clear, Z where the mode is 0), since their entry keeps
 * the flags; X is the argument of the screen they open on. */
static bool call_the_menus(Snes *s, uint32_t site, FieldMenu menu)
{
    s->cpu.a = (uint16_t)menu;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, menu == MENU_MODE_0);
    return bridge_call_long(s, site, MENUS, READS_THE_MODE, on_the_field_page(s, false));
}

// $C0:19C7 (the menu button's is $C0:195E)
/* From the menus on: the field set up again and the party brought back, and
 * faded in. False where a call didn't come back as the field runs. */
static bool menu_and_back(Snes *s, MenuOpener opener)
{
    GameState *g = state_of(s);
    if (openings[opener].no_argument)
        s->cpu.x = 0;
    if (!call_the_menus(s, opening_sites[opener].menus, openings[opener].menu))
        return false;
    display_go_dark(s);
    take_the_interrupts_back(g);
    if (!set_the_field_up(s, opening_sites[opener].set_up) ||
        !field_run(s, opening_sites[opener].party, BRING_THE_PARTY_BACK, READS_THE_OVERFLOW,
                   field_bring_the_party_back, on_the_field_page(s, false)))
        return false;
    if (openings[opener].answers_script)
        script_menu_shown(g);
    if (!field_run(s, opening_sites[opener].sprites, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, READS_NOTHING,
                   field_build_the_sprites_afresh, on_the_field_page(s, false)) ||
        !field_run(s, opening_sites[opener].fade_in, FIELD_FADE_IN_AFTER_A_MENU_ENTRY, READS_NOTHING,
                   field_fade_in_after_a_menu, on_the_field_page(s, false)))
        return false;
    start_play_clock(g);
    return true;
}

// $C0:193E (the menu button's), $C0:19A7 (the Start assignment's)
static bool open_menu(Snes *s, MenuOpener opener)
{
    GameState *g = state_of(s);
    while (fade_out_step(g))
        if (!rebuild_line_effects(s, opening_sites[opener].fade_effects) || !wait_frame(s, opening_sites[opener].fade_wait))
            return false;
    display_go_dark(s);
    if (!put_the_field_aside(s, opening_sites[opener].keep))
        return false;
    return menu_and_back(s, opener);
}

/* ---- The menus a script asks for (event $C8) --------------------------------------------------- */

/* What the script asked for (field_script_menu): 0 the Start assignment's
 * menu; $01-$7F the menus' mode 0 where it is odd, 6 where it is even; $80-$FF
 * the mode its top two bits give, with the screen's argument in its low six. */
typedef struct {
    FieldMenu menu;
    bool has_argument;
    uint8_t argument;
} ScriptMenu;

enum { ARGUMENT_GIVEN = 0x80, ARGUMENT_BITS = 0x3F, MODE_SHIFT = 6, ODD = 0x01 };

static ScriptMenu script_menu(const GameState *g)
{
    uint8_t asked = field_script_menu(g);
    if (asked == 0)
        return (ScriptMenu){ .menu = START_MENU };
    if (asked & ARGUMENT_GIVEN)
        return (ScriptMenu){ .menu = (FieldMenu)(asked >> MODE_SHIFT), .has_argument = true,
                             .argument = (uint8_t)(asked & ARGUMENT_BITS) };
    return (ScriptMenu){ .menu = (asked & ODD) ? MENU_MODE_0 : MENU_MODE_6 };
}

/* The fade out before a script's menu: a step a frame, the leader walking on
 * each without the player's control (so it stands) and the palettes
 * animating. The original keeps the control on the stack over the walk, which
 * the walk's own call-outs and stops see, as shadow mode compares them. */
enum { SCRIPT_FADE_WALK = 0xC00CDB, SCRIPT_FADE_PALETTES = 0xC00CE1, SCRIPT_FADE_WAIT = 0xC00CE5,
       WALK = 0xC0881E, PALETTE_ANIMATION = 0xFDFFF7, READS_THE_WALK = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool fade_out_walking(Snes *s)
{
    GameState *g = state_of(s);
    while (field_dims_a_step(g)) {
        uint8_t control = field_player_control(g);
        hand_the_party_to_the_player(g, false);
        if (!field_run_holding(s, SCRIPT_FADE_WALK, WALK, READS_THE_WALK, field_walk_the_leader,
                               on_the_field_page(s, false), control))
            return false;
        /* the byte as it was: event $E3 sets it from its operand, any value */
        set_field_player_control(g, control);
        if (!bridge_call_long(s, SCRIPT_FADE_PALETTES, PALETTE_ANIMATION, READS_NOTHING, on_the_field_page(s, false)) ||
            !wait_frame(s, SCRIPT_FADE_WAIT))
            return false;
    }
    return true;
}

enum { SCRIPT_KEEP = 0xC00CED, SCRIPT_MENUS = 0xC00D18, SCRIPT_SET_UP = 0xC00D2D, SCRIPT_SPRITES = 0xC00D30 };

// $C0:0CC7
/* The menu a script asked for: the field fades out as the leader walks on,
 * keeps its places, and opens it; after it the field is set up again and the
 * script's wait is over, and it fades back in. The Start assignment's menu goes
 * on as the Start assignment's does. False either way: the port stopped at a
 * call that didn't come back as the field runs, or the fade in, which the
 * original jumps to, has returned to the main loop itself. */
enum { FADE_IN_TAIL = FIELD_FADE_IN_AFTER_A_MENU_ENTRY, FADE_IN_READS = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C };

bool field_open_script_menu(Snes *s)
{
    GameState *g = state_of(s);
    if (!fade_out_walking(s))
        return false;
    display_go_dark(s);
    if (!put_the_field_aside(s, SCRIPT_KEEP))
        return false;
    ScriptMenu asked = script_menu(g);
    if (asked.menu == START_MENU)
        return menu_and_back(s, OPENED_BY_START);
    if (asked.has_argument)
        s->cpu.x = asked.argument;
    if (!call_the_menus(s, SCRIPT_MENUS, asked.menu))
        return false;
    display_go_dark(s);
    take_the_interrupts_back(g);
    if (!set_the_field_up(s, SCRIPT_SET_UP) ||
        !field_run(s, SCRIPT_SPRITES, FIELD_BUILD_THE_SPRITES_AFRESH_ENTRY, READS_NOTHING,
                   field_build_the_sprites_afresh, on_the_field_page(s, false)))
        return false;
    script_menu_shown(g);
    if (!field_go_on_with(s, FADE_IN_TAIL, field_fade_in_after_a_menu))
        m_stop_at_own(s, FADE_IN_TAIL, FADE_IN_READS);
    return false;
}

/* ---- The entries ------------------------------------------------------------------------------ */

enum { PRESS_START_CALL = 0xC0191D };

// $C0:18D9
/* The field's page, 8-bit A, 16-bit X. What it leaves in the registers is the
 * routines' it called, or what it came in with: A's high byte and the
 * overflow are read after it. */
void field_pause_and_menus(Snes *s)
{
    GameState *g = state_of(s);
    if (start_pauses(g) && !pause(s))
        return;
    if (assigned_pressed(g, ASSIGNED_START) &&
        !field_run(s, PRESS_START_CALL, PRESS_START, READS_NOTHING, field_press_start, on_the_field_page(s, false)))
        return;
    if (!assigned_pressed(g, ASSIGNED_MENU)) {
        if (dialog_choice_waiting(g))
            TIMED_AS(g, FIELD_TAKE_CHOICE_BUTTONS_ENTRY, field_take_choice_buttons(g, dialog_choice_active(g)));
    } else if (menus_may_open(g) && !open_menu(s, OPENED_BY_MENU_BUTTON)) {
        return;
    }
    m_rts(s);
}

// $C0:1985
/* The Start assignment. As for field_pause_and_menus; the story's test
 * leaves the overflow its subtraction makes (field/leaves.c). */
void field_press_start(Snes *s)
{
    GameState *g = state_of(s);
    if (dialog_window_open(g)) {
        move_dialog_window(g);
        m_rts(s);
        return;
    }
    m_set_flag(s, CPU_FLAG_V, field_story_test_overflow(g));
    if (start_opens_menu(g) && menus_may_open(g) && !open_menu(s, OPENED_BY_START))
        return;
    m_rts(s);
}

/* ---- The party after a menu ------------------------------------------------------------------- */

/* The characters' objects start again in this order, each from its own call:
 * the original runs Frog's (character 4) before Robo's (character 3). It
 * can't be seen in play: a start runs its character's object script to its
 * first return, the command that loads the character ($C0:41E4-$C0:4214),
 * which writes that object's cells and its character's entry and nothing the
 * next start reads. */
enum { CHARACTERS = FIELD_CHARACTER_OBJECTS_COUNT };
static const struct {
    uint8_t character;
    uint32_t site;
} character_starts[] = {
    { 0, 0xC01A24 }, { 1, 0xC01A2D }, { 2, 0xC01A36 }, { 4, 0xC01A3F },
    { 3, 0xC01A48 }, { 5, 0xC01A51 }, { 6, 0xC01A5A },
};

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

// $C0:1A03
/* After a menu. Where the menu changed the party, every character on the map
 * starts again in its script, so that the objects show who is in the party
 * now. The field's page, 8-bit A, 16-bit X; the overflow goes on to the
 * scripts' starts as it came. */
void field_bring_the_party_back(Snes *s)
{
    GameState *g = state_of(s);
    if (party_changed(g)) {
        object_recognise_current(g);
        for (unsigned i = 0; i < sizeof character_starts / sizeof character_starts[0]; i++) {
            FieldLink link = field_link_stored(field_character_objects(g, character_starts[i].character));
            if (!field_link_is_object(link))
                continue;
            object_make_current(g, field_linked_object(link));
            if (!field_run(s, character_starts[i].site, OBJECT_START, READS_THE_OVERFLOW,
                           event_restart_the_running_object_entry, on_the_field_page(s, false)))
                return;
        }
        party_taken_as_loaded(g);
        put_party_back_where_kept(g);
    }
    m_rts(s);
}

/* The guard: the field's page with the data bank $00, and the party's places
 * objects the tables hold, or none. */
bool field_party_can_come_back(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = true;
    for (unsigned place = 0; ok && place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        ok = !field_link_is_object(link) || field_link_names_object(link);
    }
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
