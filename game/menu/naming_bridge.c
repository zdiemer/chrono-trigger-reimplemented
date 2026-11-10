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
 * the fade out and the leaving of the menus are C continuations.
 * Before each of those calls a step leaves the flags the original's own tests
 * left (scaffolding, at the end of the file, at the steps' notes). */
#include "menu/naming.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/fades.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

enum {
    LEAVE_THE_MENUS = 0xC282B2, /* the menus close, back to the field */
    FADE_IN = 0xC283B2,         /* the screen's fade in started */
    FADE_OUT = 0xC283CA,        /* the screen fades out */
    CURSOR_SOUND = 0xC2EAC2,
    REFUSAL_SOUND = 0xC2EACC,
};
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static void charge_mode_described(Snes *s, uint8_t shown);
static void charge_moved_on_the_grid(Snes *s);

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
    case NAMING_NOTE_PRESSED_LOADED: charge_machine_accesses(state_of(s), 2); break;
    case NAMING_NOTE_MODE_DESCRIBED: charge_mode_described(s, value); break;
    case NAMING_NOTE_BUTTONS_TESTED: charge_machine_accesses(state_of(s), 1); break;
    case NAMING_NOTE_CURSOR_COMPARED: charge_machine_accesses(state_of(s), 2); break;
    case NAMING_NOTE_LETTERS_LOADED: (void)0; break;
    case NAMING_NOTE_MOVED_ON_THE_GRID: charge_moved_on_the_grid(s); break;
    case NAMING_NOTE_NAME_QUEUED: (void)0; break;
    case NAMING_NOTE_NAME_FOUND: (void)0; break;
    case NAMING_NOTE_ANSWERED: (void)0; break;
    case NAMING_NOTE_NAME_KEPT: (void)0; break; /* the block move that kept it */
    case NAMING_NOTE_START_PRESSED: (void)0; break;
    }
}

/* The steps continue into their siblings as C, behind the siblings' guards. */
static bool ends(void *ctx, NamingEnd end)
{
    Snes *s = ctx;
    switch (end) {
    case NAMING_FADES_IN: menu_go_on_in(s, FADE_IN, menu_start_the_fade_in_entry, READS_NOTHING); break;
    case NAMING_FADES_OUT:
        /* Choosing the battle mode stopped the screen's tasks. The fade's
         * wait keeps X, the record of the last task stopped. */
        (void)0;
        TIMED_AS(state_of(s), FADE_OUT, menu_fade_the_screen_out(s));
        break;
    case NAMING_LEAVES_THE_MENUS: menu_go_on_in(s, LEAVE_THE_MENUS, menu_close_entry, READS_NOTHING); break;
    }
    return false;
}

static const NamingSteps on_the_machine = { read_cursor, sound, routine, note, ends };

/* ---- The steps -------------------------------------------------------------------------------------- */

static void step(Snes *s, NamingStep at)
{
    if (menu_naming_step_with(state_of(s), m_platform(s), at, &on_the_machine, s))
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

static void charge_mode_described(Snes *s, uint8_t shown)
{
    GameState *g = state_of(s);
    if (menu_description_item(g) != shown)
        charge_machine_accesses(g, 1);
}

static void charge_moved_on_the_grid(Snes *s)
{
    GameState *g = state_of(s);
    if (!(menu_cursor_direction(g) & MENU_CURSOR_NOT_PUSHED))
        charge_machine_accesses(g, 4);
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
