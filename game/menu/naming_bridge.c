/* Naming a character on the original's machine: the screen's steps, the
 * routines of them that tick or buzz, and their guards (game/menu/naming.c
 * is the naming).
 *
 * The cursor's tick and the refusal's buzz are siblings run as the
 * original's JSRs reach them (menu_run): each saves P as it starts and
 * calls the sound driver, so the flags the original has there are on the
 * stack, where shadow mode compares them; the routines a step reaches with
 * a JSR and that tick are entries of their own, run the same way. The fade
 * in, the fade out and the leaving of the menus are jumps the steps stop at.
 * Before each of those calls a bridge leaves the flags the original's own
 * tests left (scaffolding, at the end of the file). */
#include "menu/naming.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

enum {
    LEAVE_THE_MENUS = 0xC282B2, /* the menus close, back to the field */
    FADE_IN = 0xC283B2,         /* the screen's fade in started */
    FADE_OUT = 0xC283CA,        /* the screen fades out */
    CURSOR_SOUND = 0xC2EAC2,
    REFUSAL_SOUND = 0xC2EACC,
};
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, CURSOR_SOUND, menu_play_cursor_sound); }

static void read_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
}

static void leave_buttons_tested(Snes *s);
static void leave_mode_described(Snes *s, uint8_t shown);

/* The battle mode under the cursor described. */
static void describe_the_mode(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t shown = menu_description_item(g);
    TIMED_AS(g, 0xC2E5D3, menu_naming_describe_the_battle_mode(g));
    ORIGINAL_EFFECT(leave_mode_described(s, shown));
}
static void leave_moved_on_the_grid(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_answer(Snes *s, bool given);
static void leave_name_found(Snes *s);
static void leave_start_pressed(Snes *s);
static void leave_pressed_loaded(Snes *s);

/* The steps, as the screen's table has them. */
enum { NAMING = 1, BATTLE_MODE = 2, WAITING_TO_NAME = 4 };

/* ---- Opening ------------------------------------------------------------------------------------- */

// $C2:E201
/* The naming drawn, then on to its wait as it fades in. */
void menu_open_the_naming(Snes *s)
{
    GameState *g = state_of(s);
    TIMED_AS(g, 0xC2E40E, menu_naming_open(g, m_platform(s)));
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    m_stop_at(s, FADE_IN, READS_NOTHING);
}

// $C2:E1ED
/* At a new game's first naming, the battle mode's screen first; otherwise
 * the naming. */
void menu_open_naming_screen(Snes *s)
{
    GameState *g = state_of(s);
    if (!menu_naming_asks_the_battle_mode_first(g)) {
        menu_open_the_naming(s);
        return;
    }
    TIMED_AS(g, 0xC2E576, menu_naming_open_the_battle_mode(g, m_platform(s)));
    set_menu_screen_step(g, BATTLE_MODE);
    m_stop_at(s, FADE_IN, READS_NOTHING);
}

// $C2:E209
/* Any button takes the cursor to the naming, with a tick. */
void menu_wait_to_name(Snes *s)
{
    GameState *g = state_of(s);
    if (menu_naming_waits(g)) {
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(leave_pressed_loaded(s));
    if (!tick(s, 0xC2E20E))
        return;
    menu_naming_take_the_cursor(g);
    m_rts(s);
}

// $C2:E236
/* The mode on the cursor described; choosing one sets it and goes on to
 * the naming as the screen fades out; a tick where the cursor moved. */
void menu_choose_battle_mode(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    describe_the_mode(s);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    if (menu_asked(g, MENU_CHOOSE)) {
        menu_naming_choose_the_battle_mode(g);
        m_stop_at_own(s, FADE_OUT, READS_A_HIGH);
        return;
    }
    bool moved = menu_cursor_index(g) != menu_cursor_frame_start(g);
    ORIGINAL_EFFECT(leave_cursor_compared(s));
    if (moved && !tick(s, 0xC2E248))
        return;
    m_rts(s);
}

/* ---- Naming -------------------------------------------------------------------------------------- */

// $C2:E2F4
/* A letter typed or taken back, with a tick; the name drawn again. */
void menu_type_on_the_name_grid(Snes *s)
{
    GameState *g = state_of(s);
    NamingTyped t = menu_naming_type(g);
    if (t == NAMING_LETTER_TAKEN_BACK)
        ORIGINAL_EFFECT(menu_leave_load(s, (uint8_t)menu_name_letters(g)));
    if (t != NAMING_NOTHING_TYPED && !tick(s, 0xC2E30D))
        return;
    menu_naming_draw_the_name(g, m_platform(s));
    m_rts(s);
}

// $C2:E267
/* The cursor moved about the grid, with a tick where it did; the typing;
 * the name and its cursor shown. */
void menu_move_on_the_name_grid(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    bool moved = menu_naming_move_on_the_grid(g);
    ORIGINAL_EFFECT(leave_moved_on_the_grid(s));
    if (moved && !tick(s, 0xC2E287))
        return;
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    if (!menu_run(s, 0xC2E28A, 0xC2E2F4, menu_type_on_the_name_grid))
        return;
    menu_naming_show_the_name(g);
    ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:E3E0
/* The name kept checked against every character's: where someone has it,
 * the refusal's buzz. */
void menu_check_the_name(Snes *s)
{
    GameState *g = state_of(s);
    bool someone = menu_naming_someone_has_it(g);
    if (someone) {
        ORIGINAL_EFFECT(leave_name_found(s));
        if (!menu_run(s, 0xC2E3FB, REFUSAL_SOUND, menu_play_refusal_sound))
            return;
    }
    ORIGINAL_EFFECT(leave_answer(s, !someone));
    m_rts(s);
}

// $C2:E363
/* The name kept and checked, and given where it can be: A none where it
 * was given, $FF where it wasn't. */
void menu_accept_the_name(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_naming_keep_the_name(g);
    ORIGINAL_EFFECT(menu_leave_a_high_byte_set(s)); /* the block move that kept the name */
    if (!menu_run(s, 0xC2E36F, 0xC2E3E0, menu_check_the_name))
        return;
    bool given = !menu_naming_someone_has_it(g) && menu_naming_give_the_name(g);
    ORIGINAL_EFFECT(leave_answer(s, given));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:E21E
/* A frame of the naming; Start accepts the name, and the menus close once
 * it is given. */
void menu_name_the_character(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    if (!menu_run(s, 0xC2E221, 0xC2E267, menu_move_on_the_name_grid))
        return;
    if (!menu_naming_started(g)) {
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(leave_start_pressed(s));
    if (!menu_run(s, 0xC2E22C, 0xC2E363, menu_accept_the_name))
        return;
    if (menu_naming_name_given(g))
        m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING);
    else
        m_rts(s);
}

/* ---- The entries of the naming's C ---------------------------------------------------------------- */

// $C2:E40E
void menu_naming_open_entry(Snes *s)
{
    menu_naming_open(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:E545
void menu_write_out_the_name_grid(Snes *s)
{
    menu_naming_write_out_the_grid(state_of(s));
    m_rts(s);
}

// $C2:E34A
void menu_place_the_names_cursor(Snes *s)
{
    menu_naming_place_the_names_cursor(state_of(s));
    m_rts(s);
}

// $C2:E576
void menu_open_the_battle_mode(Snes *s)
{
    menu_naming_open_the_battle_mode(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:E5D3
void menu_describe_the_battle_mode(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t shown = menu_description_item(g);
    menu_naming_describe_the_battle_mode(g);
    ORIGINAL_EFFECT(leave_mode_described(s, shown));
    m_rts(s);
}

/* ---- What the original's tests leave (scaffolding) ----------------------------------------------- */

static void leave_buttons_tested(Snes *s) { menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s))); }

/* The description leaves A clear where it started typing another mode's,
 * and otherwise the text it found shown. */
static void leave_mode_described(Snes *s, uint8_t shown)
{
    if (menu_description_item(state_of(s)) != shown)
        menu_leave_typing_started(s);
    else
        m_set_a8(s, shown);
}

/* For the original's code and shadow mode: the cursor's new place, its
 * line or column the grid's edge and the step multiplied added (with what
 * the add overflowed), compared with where it stood; the tick saves those
 * flags as it starts, and the typing the carry; A holds the line or column
 * placed. Where nothing was pushed, only the way (none) was loaded. */
static void leave_moved_on_the_grid(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t way = menu_cursor_direction(g);
    if (way & MENU_CURSOR_NOT_PUSHED) {
        s->cpu.a = way;
        menu_leave_load(s, way);
        return;
    }
    uint16_t before = menu_name_cursor_before(g), after = menu_naming_cursor_place(g);
    bool on_a_row = way == MENU_CURSOR_WENT_UP || way == MENU_CURSOR_WENT_DOWN;
    uint8_t placed = on_a_row ? (uint8_t)(after >> 8) : (uint8_t)after;
    uint8_t edge = on_a_row ? MENU_NAME_GRID_TOP : MENU_NAME_GRID_LEFT;
    uint8_t stepped = (uint8_t)(placed - edge);
    uint16_t difference = (uint16_t)(before - after);
    m_set_flag(s, CPU_FLAG_N, (difference & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_Z, difference == 0);
    m_set_flag(s, CPU_FLAG_C, before >= after);
    m_set_flag(s, CPU_FLAG_V, ((edge ^ placed) & (stepped ^ placed) & 0x80) != 0);
    s->cpu.a = placed;
}

static void leave_cursor_compared(Snes *s)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}

/* The buttons pressed, loaded as a word. */
static void leave_pressed_loaded(Snes *s)
{
    uint16_t pressed = input_pressed(state_of(s));
    m_set_flag(s, CPU_FLAG_N, (pressed & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_Z, pressed == 0);
}

/* The check's and the accepting's answer: none where the name may be
 * given, $FF where it may not. */
/* For the original's code and shadow mode: the name found matched to its
 * end (the last letter compared equal), which the buzz saves the flags of. */
static void leave_name_found(Snes *s)
{
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    m_set_flag(s, CPU_FLAG_C, true);
}

/* ... and Start found among the buttons pressed, which the accepting saves
 * the flags of. */
static void leave_start_pressed(Snes *s)
{
    m_set_a8(s, 1);
    menu_leave_load(s, 1);
}

static void leave_answer(Snes *s, bool given)
{
    uint8_t answer = given ? 0x00 : 0xFF;
    m_set_a8(s, answer);
    menu_leave_load(s, answer);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

/* The naming names one of the seven characters or the Epoch; its grid's
 * cursor within the grid, and the name typed no longer than five
 * letters. */
enum { NAMED_ONES = 8, GRID_ROWS = 8, GRID_COLUMNS = 10, NAME_LETTERS = 5 };

static bool naming(const GameState *g)
{
    return (uint8_t)menu_screen_argument(g) < NAMED_ONES && menu_naming_keeps_the_named_ones_record(g)
           && menu_name_grid_row(g) < GRID_ROWS
           && menu_name_grid_column(g) < GRID_COLUMNS && menu_name_letters(g) <= NAME_LETTERS;
}

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

static bool opens(const GameState *g) { return (uint8_t)menu_screen_argument(g) < NAMED_ONES; }

bool menu_naming_opens(Snes *s) { return peek(s, opens); }
bool menu_naming_runs(Snes *s) { return peek(s, naming); }
bool menu_naming_name_is_checkable(Snes *s) { return peek(s, naming); }
bool menu_naming_step_runs(Snes *s) { return peek(s, naming) && menu_cursor_is_movable(s); }
