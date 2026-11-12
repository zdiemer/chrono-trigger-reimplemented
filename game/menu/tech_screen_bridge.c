/* The techs screen on the original's machine: its checked legacy adapters, the
 * bridges that run what game/menu/tech_screen.c decides between the calls to
 * what is still the original's, and their guards.
 *
 * Each step reads the cursor, asks tech_screen.c what the frame's input
 * does, and carries it out: the sounds are siblings, run as the original's
 * JSRs reach them (menu_run); the list's frame is C, called as C; a step
 * that waits for the next frame waits in the menus' task switch, parked
 * until its task goes on (menu_wait_a_frame_from). A refused choice jumps
 * to the refusal's sound, as the original's does, and going back from the
 * members calls the main menu's continuation as C behind its guard.
 *
 * The routines a step reaches with a JSR that call out themselves (the moves
 * among the members and the member list's slide, the list's slide, the
 * lister's sprite, the descriptions, the window a tech is used from and the
 * pointing in it) are entries of their own, run through menu_run so that the original's frames stand under
 * their calls out: shadow mode compares the stack there, the flags each saves
 * as it starts among it. So before running one, and before a sound (which
 * saves them too), a bridge leaves the flags the original's own instructions
 * left, worked out from the state (the leaves at the end of the file):
 * scaffolding, which the native build compiles out. */
#include "menu/tech_screen.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/healing.h"
#include "menu/held_cursors.h"
#include "menu/item_screen.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/main_menu.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/party.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ----------------------------------------------------------------------------- */

/* The original's routines the steps call, by what they do. */
enum {
    WAIT_FRAMES = 0xC2822B,            /* the task waits for the frames in A */
    MAIN_MENU = 0xC29A98,              /* back to the main menu */
};

/* What they read (the calling convention checks each): most nothing but the
 * page, the data bank and memory; the stopping of tasks and the wait for
 * frames A. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EAC2, menu_play_cursor_sound); }

/* A refusal the original jumps to: its sound returns to the step's caller. */
static void refuse_and_return(Snes *s) { menu_play_refusal_sound(s); }

/* The cursor read as C, leaving what the original's reading leaves. */
static void read_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    menu_charge_cursor_read(s, r);
}

static void charge_kind_switched(Snes *s, uint8_t kind_before);
static void charge_tech_looked_at(Snes *s, TechsUse use);

static void charge_description_asked(Snes *s);
static void charge_list_slid(Snes *s, bool slid_up);
static void charge_help_looked_for(Snes *s);

/* ---- Opening the screen ------------------------------------------------------------------------ */

/* ---- The opening and the members: what the sequences ask, on the machine ----------------------- */

static const uint32_t TICKS_AT[] = {
    [TECHS_AT_THE_OPENING] = 0xC2B35D, [TECHS_AT_ANOTHER_LIST] = 0xC2B386, [TECHS_AT_A_MEMBER_CHOSEN] = 0xC2B392,
    [TECHS_AT_A_TECH_POINTED] = 0xC2B71B, [TECHS_AT_A_TECH_BROUGHT_IN] = 0xC2B780, [TECHS_AT_A_TECH_CHOSEN] = 0xC2B43A,
    [TECHS_AT_THE_LIST_LEFT] = 0xC2B3C5, [TECHS_AT_THE_TARGETS_LEFT] = 0xC2B49C,
};

static void read_cursor_on_the_machine(void *ctx) { read_cursor(ctx); }
static bool tick_on_the_machine(void *ctx, TechsPlace at) { return tick(ctx, TICKS_AT[at]); }

static bool refuse_on_the_machine(void *ctx)
{
    refuse_and_return(ctx);
    return false;
}

static bool move_among_members_on_the_machine(void *ctx)
{
    return menu_run(ctx, 0xC2B37B, 0xC2B520, menu_move_among_tech_members);
}

static bool slide_members_on_the_machine(void *ctx) { return menu_run(ctx, 0xC2B54A, 0xC2B568, menu_slide_tech_members); }
static bool describe_the_kind_on_the_machine(void *ctx) { return menu_run(ctx, 0xC2B55B, 0xC2BE16, menu_describe_tech_kind); }
static bool show_the_lister_on_the_machine(void *ctx) { return menu_run(ctx, 0xC2B39F, 0xC2B9E1, menu_show_tech_lister); }

static bool back_to_the_main_menu_on_the_machine(void *ctx)
{
    menu_go_on_in(ctx, MAIN_MENU, menu_back_to_the_main_menu_entry, READS_NOTHING);
    return false;
}

/* The flags the original's tests leave, and the P the opening's set-up
 * saves and the widths it and the moves set. */
static void note_on_the_machine(void *ctx, TechsNote note, uint8_t value)
{
    Snes *s = ctx;
    (void)value;
    switch (note) {
    case TECHS_NOTE_OPENED:
        (void)0;
        menu_save_p(s);
        menu_set_widths(s, true, m_x8(s));
        break;
    case TECHS_NOTE_ROCKS_NOTED: menu_set_widths(s, false, false); break;
    case TECHS_NOTE_SET_UP: menu_restore_p(s); break;
    case TECHS_NOTE_BUTTONS_TESTED: charge_machine_accesses(state_of(s), 1); break;
    case TECHS_NOTE_SHOWN_COMPARED: charge_machine_accesses(state_of(s), 2); break;
    case TECHS_NOTE_LISTED: (void)0; break;
    case TECHS_NOTE_DESCRIPTION_STALE: (void)0; break;
    case TECHS_NOTE_KIND_SWITCHED: charge_kind_switched(s, value); break;
    case TECHS_NOTE_CURSOR_KEPT: menu_charge_cursor_kept_on_the_list(s); break;
    case TECHS_NOTE_LIST_MADE: charge_machine_accesses(state_of(s), 2); break;
    case TECHS_NOTE_KIND_DESCRIBED: menu_set_widths(s, false, false); break;
    case TECHS_NOTE_ROWS_QUEUED: (void)0; break;
    case TECHS_NOTE_MEMBERS_SLIDING: {
        (void)menu_tech_member_scroll(state_of(s));
        charge_machine_accesses(state_of(s), 1);
        break;
    }
    case TECHS_NOTE_INTO_THE_LIST: charge_machine_accesses(state_of(s), 2); break;
    case TECHS_NOTE_LISTER_TESTED: charge_machine_accesses(state_of(s), 1); break;
    case TECHS_NOTE_LINE_EFFECTS_HELD: charge_machine_accesses(state_of(s), 1); break;
    case TECHS_NOTE_LIST_ROW_COMPARED: charge_machine_accesses(state_of(s), 3); break;
    case TECHS_NOTE_LIST_TOP_LOADED: charge_machine_accesses(state_of(s), 1); break;
    case TECHS_NOTE_TECH_ROW_BROUGHT_IN: charge_machine_accesses(state_of(s), 2); break;
    case TECHS_NOTE_LIST_SLID: charge_list_slid(s, value != 0); break;
    case TECHS_NOTE_DESCRIPTION_ASKED: charge_description_asked(s); break;
    case TECHS_NOTE_TECH_LOOKED_AT: charge_tech_looked_at(s, (TechsUse)value); break;
    case TECHS_NOTE_TECH_USED: (void)0; break;
    case TECHS_NOTE_TASKS_STOPPED: (void)0; break;
    case TECHS_NOTE_OFF_THE_MEMBERS: charge_machine_accesses(state_of(s), 1); break;
    case TECHS_NOTE_A_CLEARED: (void)0; break;
    case TECHS_NOTE_LINE_TABLES_QUEUED: (void)0; break;
    case TECHS_NOTE_NO_HELP:
        (void)0;
        (void)0;
        break;
    case TECHS_NOTE_CAST: (void)0; break;
    case TECHS_NOTE_COST_TAKEN: charge_machine_accesses(state_of(s), 1); break;
    case TECHS_NOTE_USERS_MP_LOOKED_AT: charge_machine_accesses(state_of(s), 1); break;
    }
}

static const struct {
    uint32_t site;
} WAITS_AT[] = {
    [TECHS_AT_THE_MEMBERS_SLIDING] = { 0xC2B651 }, [TECHS_AT_A_PAGE_TURNED] = { 0xC2B705 },
    [TECHS_AT_THE_LIST_SLIDING] = { 0xC2B7A7 },    [TECHS_AT_THE_TARGETS_OPENING] = { 0xC2BE98 },
};

/* The cast's frames are waited through the task switch with their count in
 * A; every other wait is a frame's. */
static bool wait_on_the_machine(void *ctx, TechsPlace at)
{
    Snes *s = ctx;
    if (at == TECHS_AT_THE_CAST) {
        m_set_a8(s, MENU_TECHS_CAST_FRAMES);
        (void)0;
        return menu_yield(s, 0xC2B4F9, WAIT_FRAMES, READS_A);
    }
    return menu_wait_a_frame_from(s, WAITS_AT[at].site);
}

/* The widths and P the original's code sets at those places, and the lister's
 * record's page its sprite's load runs on. */
enum { LISTER_PAGE = 0x19C0 };

static void registers_on_the_machine(void *ctx, TechsPlace at)
{
    Snes *s = ctx;
    switch (at) {
    case TECHS_AT_THE_MEMBERS_SLIDING: menu_set_widths(s, false, false); break;
    case TECHS_AT_THE_MEMBERS_SLID: menu_set_widths(s, true, false); break;
    case TECHS_AT_THE_SLIDE_STARTING:
        menu_save_p(s);
        menu_set_widths(s, false, false);
        break;
    case TECHS_AT_THE_SLIDE_ENDED: menu_restore_p(s); break;
    case TECHS_AT_THE_LISTER_S_RECORD: s->cpu.d = LISTER_PAGE; break;
    case TECHS_AT_THE_LIST_POINTING:
        menu_save_p(s);
        menu_set_widths(s, true, m_x8(s));
        break;
    case TECHS_AT_THE_LIST_POINTED: menu_restore_p(s); break;
    case TECHS_AT_THE_LIST_SLIDING: menu_set_widths(s, false, false); break;
    case TECHS_AT_THE_LIST_SLID: menu_set_widths(s, true, false); break;
    case TECHS_AT_THE_TARGETS_CLOSING: menu_set_widths(s, false, false); break;
    default: break;
    }
}

static const TechsSteps on_the_machine;

static bool run_on_the_machine(void *ctx, TechsRoutine r)
{
    Snes *s = ctx;
    switch (r) {
    case TECHS_SLIDES_THE_ROWS_UP:
        return menu_run_or_call(s, 0xC2B594, 0xC2B5C8, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, menu_slide_tech_member_rows);
    case TECHS_SLIDES_THE_ROWS_DOWN:
        return menu_run_or_call(s, 0xC2B5C3, 0xC2B5C8, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, menu_slide_tech_member_rows);
    case TECHS_LOADS_THE_LISTER: return menu_run(s, 0xC2BA04, 0xC2BA09, menu_load_tech_lister_sprite);
    case TECHS_LOADS_THE_RECORD_SPRITE: return menu_run(s, 0xC2BA29, 0xC2F422, menu_load_record_sprite);
    case TECHS_SLIDES_THE_LIST: return menu_run(s, 0xC2B70F, 0xC2B72F, menu_slide_tech_list);
    case TECHS_DESCRIBES_THE_POINTED_TECH: return menu_run(s, 0xC2B721, 0xC2BE01, menu_describe_pointed_tech);
    case TECHS_DESCRIBES_THE_TECH: return menu_run(s, 0xC2BE11, 0xC2BE33, menu_describe_tech);
    case TECHS_OPENS_THE_TARGETS: return menu_run(s, 0xC2B488, 0xC2BE79, menu_open_tech_targets);
    case TECHS_POINTS_AT_A_TARGET: return menu_run(s, 0xC2B491, 0xC2B823, menu_point_at_tech_target);
    case TECHS_POINTS_AT_A_MEMBER_TO_RESTORE: return menu_run(s, 0xC2B826, 0xC2B83E, menu_point_at_member_to_restore_entry);
    case TECHS_HEALS_AND_SHOWS: return menu_run(s, 0xC2B4FC, 0xC28F55, menu_heal_and_show_entry);
    /* The original runs on into the window's routine, which saves P as it
     * starts and puts it back as it ends: inline, not a call. */
    case TECHS_REOPENS_THE_TARGETS:
        menu_save_p(s);
        menu_set_widths(s, false, false);
        if (!menu_open_tech_targets_with(state_of(s), m_platform(s), &on_the_machine, s))
            return false;
        menu_restore_p(s);
        return true;
    }
    return false;
}

static const TechsSteps on_the_machine = {
    read_cursor_on_the_machine,       tick_on_the_machine,
    refuse_on_the_machine,            move_among_members_on_the_machine,
    slide_members_on_the_machine,     describe_the_kind_on_the_machine,
    show_the_lister_on_the_machine,   back_to_the_main_menu_on_the_machine,
    note_on_the_machine,              wait_on_the_machine,
    registers_on_the_machine,         run_on_the_machine,
};

static bool run(Snes *s, bool (*with)(GameState *, Platform *, const TechsSteps *, void *))
{
    return with(state_of(s), m_platform(s), &on_the_machine, s);
}

// $C2:B35D, $C2:B979
void menu_open_techs_entry(Snes *s)
{
    if (run(s, menu_open_techs_with))
        m_rts(s);
}

/* ---- The members ------------------------------------------------------------------------------- */

// $C2:B5C8
/* It ends with an 8-bit accumulator. */
void menu_slide_tech_member_rows(Snes *s)
{
    menu_set_widths(s, true, false);
    if (run(s, menu_slide_tech_member_rows_with))
        m_rts(s);
}

// $C2:B568
void menu_slide_tech_members(Snes *s)
{
    if (run(s, menu_slide_tech_members_with))
        m_rts(s);
}

// $C2:BE16
/* The kind's caption in the description window, where it shows another. */
void menu_describe_tech_kind(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, false);
    menu_techs_describe_kind(g);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B520
/* Left and right switch the kind; up and down move among the members, the
 * cursor kept on their list, sliding the list past its ends; the member's
 * list made where it is another, the kind's tab, its caption, and the rows
 * sent. */
void menu_move_among_tech_members(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, true);
    if (!run(s, menu_move_among_tech_members_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:BA09
/* The record's load runs with the page on the record, as the original's
 * call has it. */
void menu_load_tech_lister_sprite(Snes *s)
{
    m_push16(s, s->cpu.d);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(s, menu_load_tech_lister_sprite_with))
        return;
    menu_restore_p(s);
    s->cpu.d = m_pull16(s);
    (void)0;
    m_rts(s);
}

// $C2:B9E1
void menu_show_tech_lister(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(s, menu_show_tech_lister_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

static void choose_member(Snes *s)
{
    if (run(s, menu_choose_tech_member_with))
        m_rts(s);
}

// $C2:B36D
void menu_choose_tech_member_entry(Snes *s) { choose_member(s); }

// $C2:B365
/* The member list's windows, and on among the members this frame. */
void menu_lay_out_tech_members_entry(Snes *s)
{
    GameState *g = state_of(s);
    menu_techs_lay_out_members(g, m_platform(s));
    TIMED_AS(state_of(s), 0xC2B36D, choose_member(s));
}

/* ---- The list ---------------------------------------------------------------------------------- */

// $C2:BE33
/* The pointed tech's description, where the window shows another. */
void menu_describe_tech(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_techs_describe_tech(g);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:BE01
void menu_describe_pointed_tech(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(s, menu_describe_pointed_tech_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B72F
void menu_slide_tech_list(Snes *s)
{
    if (run(s, menu_slide_tech_list_with))
        m_rts(s);
}

// $C2:BE79
void menu_open_tech_targets(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!run(s, menu_open_tech_targets_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B3AE
void menu_choose_tech_entry(Snes *s)
{
    if (run(s, menu_choose_tech_with))
        m_rts(s);
}

/* ---- Using a tech ------------------------------------------------------------------------------ */

/* The cursor among the members a healing is used on, on the machine. */
static bool restoring_tick_on_the_machine(void *ctx) { return tick(ctx, 0xC2B88A); }

static bool note_the_member_on_the_machine(void *ctx)
{
    return menu_run(ctx, 0xC2B866, 0xC2B870, menu_note_member_to_restore_entry);
}

static void restoring_note_on_the_machine(void *ctx, RestoringNote note, uint8_t value)
{
    (void)value;
    Snes *s = ctx;
    switch (note) {
    case RESTORING_NOTE_POINTED: charge_machine_accesses(state_of(s), 3); break;
    case RESTORING_NOTE_HELP_LOOKED_FOR: charge_help_looked_for(s); break;
    case RESTORING_NOTE_BLOCKS_SET: charge_machine_accesses(state_of(s), 1); break;
    }
}

static const RestoringSteps restoring_on_the_machine = {
    restoring_tick_on_the_machine, note_the_member_on_the_machine, restoring_note_on_the_machine,
};

// $C2:B870
void menu_note_member_to_restore_entry(Snes *s)
{
    if (menu_note_member_to_restore_with(state_of(s), &restoring_on_the_machine, s))
        m_rts(s);
}

// $C2:B83E
void menu_point_at_member_to_restore_entry(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!menu_point_at_member_to_restore_with(state_of(s), &restoring_on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B823
void menu_point_at_tech_target(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(s, menu_point_at_tech_target_with))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B48E
void menu_choose_tech_target_entry(Snes *s)
{
    if (run(s, menu_choose_tech_target_with))
        m_rts(s);
}

/* ---- The main menu's panel --------------------------------------------------------------------- */

// $C2:B8DA
void menu_show_techs_panel(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    MainMenuOnTheMachine m = { s, 0, 0 };
    if (!menu_show_techs_panel_with(state_of(s), m_platform(s), &menu_main_menu_on_the_machine, &m))
        return;
    menu_restore_p(s);
    m_rts(s);
}

static void charge_kind_switched(Snes *s, uint8_t kind_before)
{
    (void)kind_before;
    GameState *g = state_of(s);
    if (menu_pushed(g, MENU_PUSHED_RIGHT) || menu_pushed(g, MENU_PUSHED_LEFT))
        charge_machine_accesses(g, 1);
}

static void charge_tech_looked_at(Snes *s, TechsUse use)
{
    GameState *g = state_of(s);
    switch (use) {
    case TECH_REFUSED_WAITING:
        charge_machine_accesses(g, 2);
        break;
    case TECH_REFUSED_NOT_HEALING:
        charge_machine_accesses(g, 2);
        break;
    case TECH_REFUSED_SHORT_OF_MP:
    case TECH_USED:
        charge_machine_accesses(g, 4 + (menu_tech_category(g) != MENU_SINGLE_TECHS));
        break;
    }
}

/* The healing's targets worked out (its last shifts leave the carry clear),
 * then the effect's every-member bit tested. */
enum { EVERY_MEMBER_BIT = 0x08 };

void menu_charge_techs_panel_member_chosen(Snes *s, uint8_t row)
{
    GameState *g = state_of(s);
    if (row < menu_active_members(g))
        charge_machine_accesses(g, 1);
}

static void charge_description_asked(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t stale = menu_description_stale(g);
    charge_machine_accesses(g, 1 + !(stale & 0x80));
}

/* Slid: the line effects' bit loaded over what the rows' copies left in the
 * accumulator's high byte (the block moves' $FFFF up the list, the copy's
 * last source row down it), and no overflow from the copies' sums. */
enum { LINE_EFFECTS_MOVED_BIT = 0x80 };

static void charge_list_slid(Snes *s, bool slid_up)
{
    if (!slid_up)
        charge_machine_accesses(state_of(s), 2);
}

static void charge_help_looked_for(Snes *s)
{
    GameState *g = state_of(s);
    charge_machine_accesses(g, 1 + ((menu_use_effect(g) & MENU_USE_RESTORES_HP) ? 3 : 0));
}

/* ---- The guards -------------------------------------------------------------------------------- */

enum { LIST_ROWS_SHOWN = 8 };

static bool queue_takes(const GameState *g, unsigned uploads) { return menu_upload_queue_takes(g, uploads); }

static bool on_the_screen(Snes *s) { return menu_on_page_work_ram(s) && menu_sounds_on_page(s); }

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

static bool opens(const GameState *g) { return menu_techs_can_open(g); }

bool menu_techs_screen_opens(Snes *s) { return on_the_screen(s) && peek(s, opens); }

static bool members_step_runs(const GameState *g) { return menu_techs_can_move_among_members(g); }

bool menu_tech_members_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, members_step_runs); }
bool menu_tech_members_move(Snes *s) { return on_the_screen(s) && peek(s, members_step_runs); }
bool menu_tech_members_slide(Snes *s) { return on_the_screen(s) && peek(s, members_step_runs); }

static bool list_step_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_techs_list_is_pointable(g) && menu_cursor_index(g) < 8;
}

bool menu_tech_list_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, list_step_runs); }
bool menu_tech_list_slides(Snes *s) { return on_the_screen(s) && peek(s, list_step_runs); }
bool menu_tech_is_describable(Snes *s) { return on_the_screen(s) && peek(s, list_step_runs); }
bool menu_tech_lister_is_showable(Snes *s) { return on_the_screen(s) && peek(s, list_step_runs); }
bool menu_techs_lister_can_show(const GameState *g) { return list_step_runs(g); }

static bool targets_step_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_techs_list_is_pointable(g) && menu_use_first_member(g) <= menu_cursor_index(g)
           && menu_tech_user(g) < (uint8_t)menu_members_reachable(g);
}

bool menu_tech_targets_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, targets_step_runs); }
bool menu_tech_target_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, targets_step_runs); }
bool menu_tech_targets_open(Snes *s) { return on_the_screen(s) && peek(s, targets_step_runs); }
bool menu_techs_targets_can_run(const GameState *g) { return targets_step_runs(g); }

/* A member row: the loaded record a character's, its window list drawable
 * from the drawing origin. */
enum { MEMBER_ROW_LIST = 0xC01F };

static bool restoring_is_pointable(const GameState *g)
{
    uint8_t active = menu_active_members(g);
    if (active == 0 || active > MENU_MEMBER_ROWS || menu_use_first_member(g) > menu_cursor_index(g))
        return false;
    for (uint8_t member = 0; member < active; member++)
        if (menu_members(g, member) >= CHARACTER_RECORDS_COUNT)
            return false;
    return true;
}

bool menu_member_to_restore_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, restoring_is_pointable); }
bool menu_restoring_can_point(const GameState *g) { return restoring_is_pointable(g); }

bool menu_techs_panel_shows(Snes *s) { return on_the_screen(s) && peek(s, menu_techs_panel_can_show); }

bool menu_techs_panel_goes(Snes *s) { return on_the_screen(s) && peek(s, menu_techs_panel_can_go); }
