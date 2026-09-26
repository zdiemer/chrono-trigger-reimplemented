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
 * The blanking and the interrupts' handlers are C (core/display.c,
 * field/interrupts.c). The frame wait, the menus, the field's setup and its
 * fade in are the original's routines, called out to from the original's own
 * calls; so are the line effects' builder, the Start assignment and the
 * party's return, which are ports, for the reasons beside their calls. */
#include "field/menus.h"
#include "field/field.h"
#include "field/object.h"
#include "field/party.h"

#include "charge.h"
#include "core/display.h"
#include "core/input.h"
#include "event/variables.h"
#include "field/bridge.h"
#include "field/parts.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The field's state ------------------------------------------------------------------------ */

enum { PLAY_CLOCK_STOPPED = 0xFF, PLAY_CLOCK_RUNS = 0x00 };

static void stop_play_clock(GameState *g) { set_play_time_stopped(g, PLAY_CLOCK_STOPPED); }
static void start_play_clock(GameState *g) { set_play_time_stopped(g, PLAY_CLOCK_RUNS); }

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

/* A step of the fade out before a menu: false once the picture is dark (a
 * brightness with its top bit set counts as dark, as the original's signed
 * test has it). The clock is stopped at every step. */
enum { DARK = 0, BRIGHTNESS_SIGN = 0x80 };

static bool fade_out_step(GameState *g)
{
    stop_play_clock(g);
    uint8_t brightness = field_brightness(g);
    if (brightness == DARK || (brightness & BRIGHTNESS_SIGN))
        return false;
    set_field_brightness(g, (uint8_t)(brightness - 1));
    return true;
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

/* The original's routines. */
enum {
    FRAME_WAIT = 0xC0EC60,          /* waits for the next frame */
    KEEP_THE_FIELD = 0xC0011B,      /* keeps the party's places and the field's state for after the menu */
    MENUS = 0xC28000,               /* the menus, at the mode in A */
    SET_THE_FIELD_UP = 0xC001A5,    /* the field's graphics, objects and display again */
    SPRITE_LISTS = 0xC0B192,
    FADE_IN = 0xC02824,
    CHOICE = 0xC01ADF,              /* a dialog's choice taking the buttons */
    OBJECT_START = 0xC0597D,        /* runs the current object's start in its script */
};

/* Ports called as the original calls them, on purpose. The line effects'
 * builder leaves registers its entry works out, and the overflow among them
 * is read after the pause (field/line_effects.c); the Start assignment and the
 * party's return make call-outs of their own, which must be made under their
 * own return address, where shadow mode pairs them with the original's. */
enum {
    LINE_EFFECTS = 0xFDC2C1,        /* field_build_line_effect_tables */
    PRESS_START = 0xC01985,         /* field_press_start */
    BRING_THE_PARTY_BACK = 0xC01A03 /* field_bring_the_party_back */
};
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
    READS_THE_MODE = DC_AH | DC_Y | DC_C,               /* the menus keep the flags and take A and X */
    READS_THE_CHOICE = DC_AH | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    return bridge_call(s, site, routine, dont_care, on_the_field_page(s, false));
}

static bool wait_frame(Snes *s, uint32_t site) { return call(s, site, FRAME_WAIT, READS_NOTHING); }

/* The line effect tables are built with 8-bit index registers. */
static bool rebuild_line_effects(Snes *s, uint32_t site)
{
    m_sep(s, CPU_FLAG_X);
    if (!bridge_call_long(s, site, LINE_EFFECTS, READS_NOTHING, on_the_field_page(s, true)))
        return false;
    m_rep(s, CPU_FLAG_X);
    return true;
}

/* The screen blanked and the interrupts stopped, as a mode does before it
 * changes what the screen shows (core/display.c); the CPU's interrupts masked
 * and the data bank $00, as the original's routine leaves the machine. */
static void blank_the_screen(Snes *s)
{
    TIMED_AS(state_of(s), DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(m_platform(s)));
    m_set_flag(s, CPU_FLAG_I, true);
    s->cpu.db = 0x00;
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

/* The menus' modes the field opens, by what A holds as they start. */
typedef enum { MAIN_MENU = 1, START_MENU = 5 } FieldMenu;

/* The two ways a menu is opened, each with its own copy of the calls. They
 * differ in the menu, in the X the menus are given (the Start assignment's 0,
 * the menu button's what keeping the field left), and in whether a menu a
 * script asked for counts as shown. */
typedef enum { OPENED_BY_MENU_BUTTON, OPENED_BY_START } MenuOpener;
static const struct {
    uint32_t mode, clears_x, answers_script;
    uint32_t fade_effects, fade_wait, keep, menus, set_up, party, sprites, fade_in;
} openings[] = {
    [OPENED_BY_MENU_BUTTON] = { MAIN_MENU, 0, 0, 0xC0194D, 0xC01953, 0xC0195B, 0xC01960, 0xC01975, 0xC01978,
                                0xC0197B, 0xC0197E },
    [OPENED_BY_START] = { START_MENU, 1, 1, 0xC019B6, 0xC019BC, 0xC019C4, 0xC019CE, 0xC019E3, 0xC019E6, 0xC019F9,
                          0xC019FC },
};

// $C0:193E (the menu button's), $C0:19A7 (the Start assignment's)
/* The menus take the mode in A with its high byte clear, and N and Z as its
 * load leaves them (clear: the mode is positive and not 0), since their entry
 * keeps the flags. False where a call didn't come back as the field runs. */
static bool open_menu(Snes *s, MenuOpener opener)
{
    GameState *g = state_of(s);
    while (fade_out_step(g))
        if (!rebuild_line_effects(s, openings[opener].fade_effects) || !wait_frame(s, openings[opener].fade_wait))
            return false;
    blank_the_screen(s);
    if (!call(s, openings[opener].keep, KEEP_THE_FIELD, READS_NOTHING))
        return false;
    s->cpu.a = (uint16_t)openings[opener].mode;
    if (openings[opener].clears_x)
        s->cpu.x = 0;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    if (!bridge_call_long(s, openings[opener].menus, MENUS, READS_THE_MODE, on_the_field_page(s, false)))
        return false;
    blank_the_screen(s);
    take_the_interrupts_back(g);
    if (!call(s, openings[opener].set_up, SET_THE_FIELD_UP, READS_NOTHING) ||
        !call(s, openings[opener].party, BRING_THE_PARTY_BACK, READS_THE_OVERFLOW))
        return false;
    if (openings[opener].answers_script)
        script_menu_shown(g);
    if (!call(s, openings[opener].sprites, SPRITE_LISTS, READS_NOTHING) ||
        !call(s, openings[opener].fade_in, FADE_IN, READS_NOTHING))
        return false;
    start_play_clock(g);
    return true;
}

/* ---- The entries ------------------------------------------------------------------------------ */

enum { PRESS_START_CALL = 0xC0191D, CHOICE_CALL = 0xC0192B };

// $C0:18D9
/* The field's page, 8-bit A, 16-bit X. What it leaves in the registers is the
 * routines' it called, or what it came in with: A's high byte and the
 * overflow are read after it. */
void field_pause_and_menus(Snes *s)
{
    GameState *g = state_of(s);
    if (start_pauses(g) && !pause(s))
        return;
    if (assigned_pressed(g, ASSIGNED_START) && !call(s, PRESS_START_CALL, PRESS_START, READS_NOTHING))
        return;
    if (!assigned_pressed(g, ASSIGNED_MENU)) {
        if (dialog_choice_waiting(g)) {
            m_set_a8(s, dialog_choice_active(g));
            if (!call(s, CHOICE_CALL, CHOICE, READS_THE_CHOICE))
                return;
        }
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

static void party_loaded(GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        set_field_party_as_loaded(g, place, party_members(g, place));
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
            if (!call(s, character_starts[i].site, OBJECT_START, READS_THE_OVERFLOW))
                return;
        }
        party_loaded(g);
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
