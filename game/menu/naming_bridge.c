/* Naming a character on the original's machine: the screen's steps, the
 * routines of them that tick or buzz, and their guards (game/menu/naming.c
 * is the naming, and its steps).
 *
 * The steps are naming.c's, run here with the machine's answers to what they
 * ask of the menus: the cursor's tick and the refusal's buzz are siblings run
 * as the original's JSRs reach them (menu_run), each saving P as it starts and
 * calling the sound driver, so the flags the original has there are on the
 * stack, where shadow mode compares them; the routines a step reaches with a
 * JSR and that tick are entries of their own, run the same way. The fade in,
 * the fade out and the leaving of the menus are jumps the steps stop at.
 * Before each of those calls a step leaves the flags the original's own tests
 * left (scaffolding, at the end of the file, at the steps' notes). */
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

static void leave_buttons_tested(Snes *s);
static void leave_mode_described(Snes *s, uint8_t shown);
static void leave_moved_on_the_grid(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_answer(Snes *s, bool given);
static void leave_name_found(Snes *s);
static void leave_start_pressed(Snes *s);
static void leave_pressed_loaded(Snes *s);

/* ---- What the steps ask of the menus, on the machine ---------------------------------------------- */

static void read_cursor(void *ctx) { menu_step_reads_the_cursor(ctx); }

static bool sound(void *ctx, uint32_t site, MenuSound sound)
{
    Snes *s = ctx;
    return sound == MENU_SOUND_REFUSAL ? menu_run(s, site, REFUSAL_SOUND, menu_play_refusal_sound)
                                       : menu_run(s, site, CURSOR_SOUND, menu_play_cursor_sound);
}

/* The routines' entries, by routine, and the JSRs the steps reach them by. */
static const struct {
    void (*entry)(Snes *);
    uint32_t site;
} routine_entries[] = {
    [NAMING_MOVE_ON_THE_GRID] = { menu_move_on_the_name_grid, 0xC2E221 },
    [NAMING_TYPE_ON_THE_GRID] = { menu_type_on_the_name_grid, 0xC2E28A },
    [NAMING_ACCEPT_THE_NAME] = { menu_accept_the_name, 0xC2E22C },
    [NAMING_CHECK_THE_NAME] = { menu_check_the_name, 0xC2E36F },
};

static bool routine(void *ctx, NamingRoutine r)
{
    return menu_run(ctx, routine_entries[r].site, menu_naming_routine_address(r), routine_entries[r].entry);
}

static void note(void *ctx, NamingNote n, uint8_t value)
{
    Snes *s = ctx;
    switch (n) {
    case NAMING_NOTE_PRESSED_LOADED: ORIGINAL_EFFECT(leave_pressed_loaded(s)); break;
    case NAMING_NOTE_MODE_DESCRIBED: ORIGINAL_EFFECT(leave_mode_described(s, value)); break;
    case NAMING_NOTE_BUTTONS_TESTED: ORIGINAL_EFFECT(leave_buttons_tested(s)); break;
    case NAMING_NOTE_CURSOR_COMPARED: ORIGINAL_EFFECT(leave_cursor_compared(s)); break;
    case NAMING_NOTE_LETTERS_LOADED: ORIGINAL_EFFECT(menu_leave_load(s, value)); break;
    case NAMING_NOTE_MOVED_ON_THE_GRID: ORIGINAL_EFFECT(leave_moved_on_the_grid(s)); break;
    case NAMING_NOTE_NAME_QUEUED: ORIGINAL_EFFECT(menu_leave_queued_upload(s, MENU_UPLOAD_FRONT_LAYER)); break;
    case NAMING_NOTE_NAME_FOUND: ORIGINAL_EFFECT(leave_name_found(s)); break;
    case NAMING_NOTE_ANSWERED: ORIGINAL_EFFECT(leave_answer(s, value != 0)); break;
    case NAMING_NOTE_NAME_KEPT: ORIGINAL_EFFECT(menu_leave_a_high_byte_set(s)); break; /* the block move that kept it */
    case NAMING_NOTE_START_PRESSED: ORIGINAL_EFFECT(leave_start_pressed(s)); break;
    }
}

/* The steps' ends are the original's jumps, which the port stops at. */
static bool ends(void *ctx, NamingEnd end)
{
    Snes *s = ctx;
    switch (end) {
    case NAMING_FADES_IN: m_stop_at(s, FADE_IN, READS_NOTHING); break;
    case NAMING_FADES_OUT: m_stop_at_own(s, FADE_OUT, READS_A_HIGH); break;
    case NAMING_LEAVES_THE_MENUS: m_stop_at(s, LEAVE_THE_MENUS, READS_NOTHING); break;
    }
    return false;
}

static const NamingSteps on_the_machine = { read_cursor, sound, routine, note, ends };

/* ---- The steps -------------------------------------------------------------------------------------- */

static void step(Snes *s, NamingStep at)
{
    if (menu_naming_step(state_of(s), m_platform(s), at, &on_the_machine, s))
        m_rts(s);
}

// $C2:E201
void menu_open_the_naming(Snes *s) { step(s, NAMING_OPEN_THE_NAMING); }
// $C2:E1ED
void menu_open_naming_screen(Snes *s) { step(s, NAMING_OPEN_THE_SCREEN); }
// $C2:E209
void menu_wait_to_name(Snes *s) { step(s, NAMING_WAIT_TO_NAME); }
// $C2:E236
void menu_choose_battle_mode(Snes *s) { step(s, NAMING_CHOOSE_BATTLE_MODE); }
// $C2:E21E
void menu_name_the_character(Snes *s) { step(s, NAMING_NAME_THE_CHARACTER); }

/* The routines; the moving and the accepting save P as they start, and put
 * it back as they end. */
static void routine_entry(Snes *s, NamingRoutine r, bool keeps_p)
{
    if (keeps_p) {
        menu_save_p(s);
        menu_set_widths(s, true, m_x8(s));
    }
    if (!menu_naming_routine(state_of(s), m_platform(s), r, &on_the_machine, s))
        return;
    if (keeps_p)
        menu_restore_p(s);
    m_rts(s);
}

// $C2:E2F4
void menu_type_on_the_name_grid(Snes *s) { routine_entry(s, NAMING_TYPE_ON_THE_GRID, false); }
// $C2:E267
void menu_move_on_the_name_grid(Snes *s) { routine_entry(s, NAMING_MOVE_ON_THE_GRID, true); }
// $C2:E3E0
void menu_check_the_name(Snes *s) { routine_entry(s, NAMING_CHECK_THE_NAME, false); }
// $C2:E363
void menu_accept_the_name(Snes *s) { routine_entry(s, NAMING_ACCEPT_THE_NAME, true); }

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

/* What the steps take (naming.c), on the menus' page. */
static bool peek(Snes *s, bool (*test)(const GameState *))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_naming_opens(Snes *s) { return peek(s, menu_naming_names_one); }
bool menu_naming_runs(Snes *s) { return peek(s, menu_naming_is_whole); }
bool menu_naming_name_is_checkable(Snes *s) { return peek(s, menu_naming_is_whole); }
bool menu_naming_step_runs(Snes *s) { return peek(s, menu_naming_is_whole) && menu_cursor_is_movable(s); }
