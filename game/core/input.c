/* The controller, and the play time clock: the per-frame input work that the
 * menus, the field and battle share.
 *
 * Once a frame the game takes the controller's buttons into three sets, in its
 * own order (Buttons, core/input.h): the buttons held; the buttons pressed,
 * which were not held the frame before; and the repeating buttons, the pressed
 * ones plus, every so many frames while buttons stay held, all of the held
 * ones again, so that holding a direction keeps a cursor moving.
 *
 * Each set is kept a second time through the player's button assignments
 * (the settings screen's). An assignment is a group of buttons that stands for
 * one of the game's buttons while the whole group is held, so code that reads
 * the assigned sets sees the button the player chose, not the one pressed.
 *
 * Holding L, R, Select and Start together resets the console.
 *
 * The same work advances the play time clock, which runs a frame at a time
 * unless the game has stopped it.
 *
 * The console reads the controller by itself early in the vertical blank, and
 * the game waits for that to finish before it takes the buttons; the port
 * leaves the wait to the original (the entries at the end), and other work
 * that runs the frame's input again (input_take_frame) goes on without it. */
#include "core/input.h"

#include "charge.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The buttons ------------------------------------------------------------------------------ */

/* Where each of the controller's buttons sits in the game's order. */
static const struct {
    PadBits on_controller;
    Buttons in_game;
} BUTTON_ORDER[] = {
    { IN_BUTTON_A, BUTTON_A },           { IN_BUTTON_X, BUTTON_X },
    { IN_BUTTON_L, BUTTON_L },           { IN_BUTTON_R, BUTTON_R },
    { IN_BUTTON_B, BUTTON_B },           { IN_BUTTON_Y, BUTTON_Y },
    { IN_BUTTON_SELECT, BUTTON_SELECT }, { IN_BUTTON_START, BUTTON_START },
    { IN_BUTTON_UP, BUTTON_UP },         { IN_BUTTON_DOWN, BUTTON_DOWN },
    { IN_BUTTON_LEFT, BUTTON_LEFT },     { IN_BUTTON_RIGHT, BUTTON_RIGHT },
};

static Buttons in_game_order(PadBits pad)
{
    Buttons buttons = 0;
    for (unsigned i = 0; i < sizeof BUTTON_ORDER / sizeof BUTTON_ORDER[0]; i++)
        if (pad & BUTTON_ORDER[i].on_controller)
            buttons |= BUTTON_ORDER[i].in_game;
    return buttons;
}

/* A frame of the repeat countdown, returning the buttons that repeat on it.
 * Buttons held from one frame to the next repeat when the countdown runs out, and the countdown starts again from the repeat delay the mode has
 * set; with no button held through, it waits from the start. The countdown is
 * a byte: one that is already out when buttons are held runs 256 frames, as the
 * original's does. */
static Buttons advance_repeat(GameState *g, Buttons held_through)
{
    if (held_through) {
        uint8_t left = (uint8_t)(input_repeat_countdown(g) - 1);
        if (left != 0) {
            set_input_repeat_countdown(g, left);
            return 0;
        }
    }
    set_input_repeat_countdown(g, input_repeat_delay(g));
    return held_through;
}

// $C2:84EC
/* Take this frame's buttons: held, pressed and repeating, remembering last
 * frame's held ones to tell which are new. Not yet, while the console is still
 * reading the controller; and the reset buttons, held alone, reset instead,
 * once the held buttons are taken. */
ControllerRead input_read_controller(GameState *g, Platform *pf)
{
    if (!in_ready(pf, PF_AT(0xC284F9)))
        return CONTROLLER_NOT_READY;
    PadBits pad = in_read(pf, PF_AT(0xC28502), 0);
    Buttons before = input_held(g);
    Buttons held = in_game_order(pad);
    set_input_previous(g, before);
    set_input_held(g, held);
    if (pad == RESET_BUTTONS)
        return CONTROLLER_RESET;
    Buttons pressed = (Buttons)(held & ~before);
    set_input_pressed(g, pressed);
    set_input_repeat(g, (Buttons)(pressed | advance_repeat(g, (Buttons)(held & before))));
    return CONTROLLER_READ;
}

/* ---- The player's button assignments ------------------------------------------------------------ */

/* Which of the game's buttons each of the eight assignments stands for. */
static const Buttons ASSIGNMENT_STANDS_FOR[] = {
    BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_SELECT, BUTTON_Y, BUTTON_START, BUTTON_R, BUTTON_L,
};

/* The direction pad isn't assigned: its byte of a set passes through whole. */
enum { DIRECTION_PAD_BYTE = 0xFF00 };

/* A set of buttons as the player's assignments make it: each of the game's
 * buttons is in it when all of its assignment's group is (a group of none is
 * always). */
static Buttons through_assignments(const GameState *g, Buttons buttons)
{
    Buttons assigned = buttons & DIRECTION_PAD_BYTE;
    for (unsigned assignment = 0; assignment < sizeof ASSIGNMENT_STANDS_FOR / sizeof ASSIGNMENT_STANDS_FOR[0];
         assignment++) {
        Buttons group = button_assignments(g, assignment);
        if ((buttons & group) == group)
            assigned |= ASSIGNMENT_STANDS_FOR[assignment];
    }
    return assigned;
}

// $C2:8545, $C2:8555
/* This frame's pressed, held and repeating buttons, through the assignments. */
static void assign_buttons(GameState *g)
{
    set_input_mapped(g, ASSIGNED_PRESSED, through_assignments(g, input_pressed(g)));
    set_input_mapped(g, ASSIGNED_HELD, through_assignments(g, input_held(g)));
    set_input_mapped(g, ASSIGNED_REPEATING, through_assignments(g, input_repeat(g)));
}

/* ---- The play time clock ------------------------------------------------------------------------ */

/* The clock's digits, from frames to tens of hours, and where each rolls over
 * into the next. */
typedef struct {
    uint8_t frames, seconds, minutes, ten_minutes, hours, ten_hours;
} PlayTime;
enum { FRAMES_PER_SECOND = 60, SECONDS_PER_MINUTE = 60, TENS = 10, TEN_MINUTES_PER_HOUR = 6 };

static PlayTime play_time(const GameState *g)
{
    return (PlayTime){
        .frames = play_time_frames(g),   .seconds = play_time_seconds(g), .minutes = play_time_minutes(g),
        .ten_minutes = play_time_ten_minutes(g), .hours = play_time_hours(g), .ten_hours = play_time_ten_hours(g),
    };
}

static void set_play_time(GameState *g, const PlayTime *t)
{
    set_play_time_frames(g, t->frames);
    set_play_time_seconds(g, t->seconds);
    set_play_time_minutes(g, t->minutes);
    set_play_time_ten_minutes(g, t->ten_minutes);
    set_play_time_hours(g, t->hours);
    set_play_time_ten_hours(g, t->ten_hours);
}

/* One up on a digit; true where it rolls over to 0 and carries into the next.
 * A digit already past where it rolls over rolls over at once. */
static bool count_up(uint8_t *digit, uint8_t rolls_over_at)
{
    uint8_t next = (uint8_t)(*digit + 1);
    *digit = next < rolls_over_at ? next : 0;
    return next >= rolls_over_at;
}

// $C2:85AE
/* A frame of play time, unless the clock is stopped. Past 99:59:59 and its
 * last frame it runs round to zero and is marked as having done so. */
void input_tick_play_time(GameState *g)
{
    if (play_time_stopped(g))
        return;
    PlayTime t = play_time(g);
    bool ran_round = count_up(&t.frames, FRAMES_PER_SECOND) && count_up(&t.seconds, SECONDS_PER_MINUTE)
                     && count_up(&t.minutes, TENS) && count_up(&t.ten_minutes, TEN_MINUTES_PER_HOUR)
                     && count_up(&t.hours, TENS) && count_up(&t.ten_hours, TENS);
    set_play_time(g, &t);
    if (ran_round)
        set_play_time_overflowed(g, 1);
}

bool input_take_frame(GameState *g, Platform *pf)
{
    if (input_read_controller(g, pf) != CONTROLLER_READ)
        return false;
    assign_buttons(g);
    input_tick_play_time(g);
    return true;
}

/* ---- The entries -------------------------------------------------------------------------------- */

/* The controller's cells are on the direct page at $0000, which the controller
 * read sets itself and the button assignment is always entered with. */
enum { CONTROLLER_PAGE = 0x0000 };

/* The controller read works on its page with data bank 0, whatever its caller
 * had, and leaves them so. */
static void on_controller_page(Snes *s)
{
    s->cpu.d = CONTROLLER_PAGE;
    s->cpu.db = 0x00;
}

/* Where the controller read stops rather than returns: at the original's loop
 * on the console's status while its read is still under way ($C2:84F9, the
 * wait this port hands back; docs/TODO.md), which tests the status against an
 * 8-bit A of 1; or at the reset code, jumped to with 16-bit registers. Either
 * way on the controller's page. */
void input_stop_reading(Snes *s, ControllerRead read)
{
    on_controller_page(s);
    if (read == CONTROLLER_NOT_READY) {
        m_sep(s, CPU_FLAG_M);
        m_set_a8(s, 0x01);
        m_stop_at_own(s, 0xC284F9, DC_X | DC_N | DC_V | DC_Z | DC_C | DC_AH);
    } else {
        m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
        m_stop_at_own(s, 0xC0FF00, DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C | DC_DB | DC_D);
    }
}

// $C2:84EC
/* Called once a frame, from input_update or from the per-frame work at
 * $C2:8473, which reads the second controller as well. It comes back with
 * 16-bit A and 8-bit index registers. */
void input_read_joypad(Snes *s)
{
    on_controller_page(s);
    ControllerRead read = input_read_controller(state_of(s), m_platform(s));
    if (read != CONTROLLER_READ) {
        input_stop_reading(s, read);
        return;
    }
    m_rep(s, CPU_FLAG_M);
    m_sep(s, CPU_FLAG_X);
    m_rts(s);
}

bool input_map_buttons_runs(Snes *s) { return m_direct_page(s) == CONTROLLER_PAGE; }

// $C2:8545
void input_map_buttons(Snes *s)
{
    assign_buttons(state_of(s));
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_rts(s);
}

// $C2:85AE
void play_time_tick(Snes *s)
{
    input_tick_play_time(state_of(s));
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_rts(s);
}

/* The per-frame work saves every register of its caller and gives them back:
 * the caller's data bank, direct page, index registers at their width and
 * flags, then its 16-bit accumulator and the flags again with 16-bit
 * registers. The port leaves them alone instead, and builds the frame (with
 * the return into the per-frame work from the controller read) only where the
 * controller read stops inside it. */
static void push_at_index_width(Snes *s, uint16_t v)
{
    if (m_x8(s))
        m_push8(s, (uint8_t)v);
    else
        m_push16(s, v);
}

static void enter_saved_frame(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    push_at_index_width(s, c->x);
    push_at_index_width(s, c->y);
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push8(s, c->p);
    m_push16(s, 0x84DD);   /* the return from the JSR at $C2:84DB */
    on_controller_page(s);
}

/* The original's own time, saving and restoring the registers and calling its
 * three parts: the bytes pushed and pulled again. */
enum { SAVED_BYTES = 11, RETURN_BYTES = 3 * 2, SAVE_AND_RESTORE_ACCESSES = 2 * (SAVED_BYTES + RETURN_BYTES) };

// $C2:84D2
/* The per-frame input work: the controller, the button assignments and the
 * play time clock. */
void input_update(Snes *s)
{
    GameState *g = state_of(s);
    ControllerRead read;
    NATIVE_TIME_AS(s, 0xC284EC, read = input_read_controller(g, m_platform(s)));
    if (read != CONTROLLER_READ) {
        enter_saved_frame(s);
        input_stop_reading(s, read);
        return;
    }
    NATIVE_TIME_AS(s, 0xC28545, assign_buttons(g));
    NATIVE_TIME_AS(s, 0xC285AE, input_tick_play_time(g));
    ORIGINAL_EFFECT(charge_as(g, 0xC284D2, SAVE_AND_RESTORE_ACCESSES));
    m_rts(s);
}
