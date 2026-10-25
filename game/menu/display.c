/* Menu mode: the routines that drive the display.
 *
 * Two modes live in bank C2 and each has its own vblank handler. The mode at
 * $C2:800E (the menus proper, and the load screen every save movie starts in)
 * keeps its display state as shadows in work RAM -- the layers' scroll, the
 * windows, screens and color math, the sprite table, the palette -- and sends
 * them in vblank, with the uploads it has queued and a block move a frame may
 * ask for. The mode at $C2:0000 (the Epoch's screens, the End of Time's gate,
 * the menus' map scenes) keeps the same state on the direct page and sends it
 * the same way, then runs its objects (menu/objects.c).
 *
 * Both set the screen up by a fixed recipe as they start, and a graphics
 * loader shared by the screens fills VRAM, the palette and work RAM for them.
 *
 * Everything reaches the hardware through the video and timing services; the
 * few entries that switch stacks or return from an interrupt do that in their
 * wrappers and nowhere else. */
#include "menu/display.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/fades.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/handlers.h"
#include "menu/objects.h"
#include "menu/sprites.h"
#include "core/input.h"
#include "menu/opening.h"
#include "menu/party.h"
#include "menu/saves.h"
#include "menu/screen_objects.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* The setting every upload here uses unless it says otherwise: a word at a
 * time, stepping after the high byte. */
static const VidVramPort word_port = { .step = 1, .after_high = true };

/* What a screen shows while it is set up: nothing, at full brightness. */
static const VidDisplay BLANK = { .blank = true };

bool menu_display_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }

/* Registers the entries' callees don't read, and the stops' (tools/dontcare_check.py
 * checks each). */
enum {
    NOTHING_READ = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    INPUT_IGNORES = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C,     /* $C2:8473 */
};

/* The pages the two modes' vblank handlers run on. */
enum { MENUS_PAGE = 0x0D00, OBJECT_MODE_PAGE = 0x0000 };

/* A routine called from a vblank handler comes back as the handler runs: an
 * 8-bit accumulator, 16-bit index registers, the handler's page, and a data
 * bank on the I/O registers. */
static bool back_as_the_menus_run(const Snes *s, uint16_t page)
{
    return !s->cpu.e && m_m8(s) && !m_x8(s) && s->cpu.d == page && (s->cpu.db & 0x7F) < 0x40;
}

/* ---- The windows, the screens and color math, as both modes keep them --------------------------- */

/* The sixteen register values in register order: the window masks, edges and
 * logic, the main and sub screens and their window masks, color math's
 * control and layers, and the fixed color, one channel byte at a time. */
typedef struct {
    uint8_t masks[3], edges[4], logic[2];
    uint8_t main_screen, sub_screen, main_masked, sub_masked, math_control, math_layers;
} ScreenShadows;

typedef struct {
    uint32_t windows, screens, masked, control, layers;
} ScreenSites;

static void send_screen_shadows(Platform *pf, const ScreenShadows *v, const ScreenSites *at)
{
    static const uint32_t window_fields[9] = {
        VID_WINDOWS_BG1_BG2, VID_WINDOWS_BG3_BG4, VID_WINDOWS_OBJ_BACK, VID_WINDOWS_W1_LEFT, VID_WINDOWS_W1_RIGHT,
        VID_WINDOWS_W2_LEFT, VID_WINDOWS_W2_RIGHT, VID_WINDOWS_BG_LOGIC, VID_WINDOWS_OBJ_LOGIC,
    };
    const uint8_t window_values[9] = { v->masks[0], v->masks[1], v->masks[2], v->edges[0], v->edges[1],
                                       v->edges[2], v->edges[3], v->logic[0], v->logic[1] };
    VidWindows w = { 0 };
    uint32_t fields = 0;
    for (int i = 0; i < 9; i++) {
        vid_windows_decode(&w, window_fields[i], window_values[i]);
        fields |= window_fields[i];
    }
    vid_set_windows(pf, PF_AT(at->windows), &w, fields);
    vid_set_screens(pf, PF_AT(at->screens), v->main_screen, v->sub_screen, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_windows_decode(&w, VID_WINDOWS_MAIN, v->main_masked);
    vid_windows_decode(&w, VID_WINDOWS_SUB, v->sub_masked);
    vid_set_windows(pf, PF_AT(at->masked), &w, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, v->math_control);
    vid_set_color_math(pf, PF_AT(at->control), &math, VID_MATH_CONTROL);
    vid_color_math_decode(&math, VID_MATH_LAYERS, v->math_layers);
    vid_set_color_math(pf, PF_AT(at->layers), &math, VID_MATH_LAYERS);
}

/* ---- The mode at $C2:800E ------------------------------------------------------------------------ */

/* The screen blanked and everything stopped: every interrupt but the
 * controllers' reading off, no transfers and no per-line effects. */
static void blank_and_stop(Platform *pf, const uint32_t site[4])
{
    vid_set_display(pf, PF_AT(site[0]), &BLANK);
    tm_enable_interrupts(pf, PF_AT(site[1]), TM_INT_AUTO_JOYPAD);
    vid_stop_uploads(pf, PF_AT(site[2]));
    vid_enable_line_effects(pf, PF_AT(site[3]), 0x00);
}

/* The openings the menus' callers ask for, in A as they enter: the screen
 * each opens on is the ROM's (menu_screen_opened_on). One opens on a screen
 * whose only step closes the menus again, for what opening and closing them
 * do to the party's records and the field's sprites. The first entry for the
 * load screen at power-on doesn't open the menus at all (starts_afresh). */
typedef enum {
    MENU_OPEN_LOAD_SCREEN, MENU_OPEN_MAIN_MENU, MENU_OPEN_SHOP, MENU_OPEN_NAMING, MENU_OPEN_ONLY_TO_CLOSE,
    MENU_OPEN_GATE_EXCHANGE, MENU_OPEN_SAVE_POINT,
} MenuOpening;

/* The menus' own stack, under the tasks' (menu/tasks.c), and the boot's,
 * which the game starts again on. */
enum { MENUS_STACK = 0x09FF, BOOT_STACK = 0x06FF };

/* The game started afresh: what the menus do the first time they are entered
 * for the load screen after power-on, before any game has been played. A new
 * game's state is set up, noted as done, the cartridge's record that the game
 * was finished forgotten, and the menus' frame count kept for the battle's
 * random numbers; then the game starts again from its reset, which comes to
 * the load screen through the title. */
static bool starts_afresh(const GameState *g)
{
    return menu_asked_for(g) == MENU_OPEN_LOAD_SCREEN && !story_new_game_set_up(g);
}

static void start_a_game_afresh(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2956E, saves_start_a_new_game(g));
    set_story_new_game_set_up(g, 1);
    saves_forget_finished(pf, PF_AT(0xC28053));
    saves_forget_times_finished(pf, PF_AT(0xC28057));
    set_frames_at_leaving(g, menu_frame_counter(g));
}

/* The screen the menus open on takes what the caller asked with -- a shop's
 * number, a character to name -- as its argument, in the argument's low byte. */
static void give_the_screen_what_was_asked(GameState *g)
{
    set_menu_screen_argument(g, (uint16_t)((menu_screen_argument(g) & 0xFF00) | (uint8_t)menu_asked_with(g)));
}

/* The menus open on the screen asked for, taking what the caller asked with
 * as its argument; the play time runs, held buttons repeat from the next
 * frame, and the vblank handler and the controllers' reading are on. */
static void open_on_the_screen(GameState *g, Platform *pf)
{
    MenuOpening opening = (MenuOpening)menu_asked_for(g);
    give_the_screen_what_was_asked(g);
    set_play_time_stopped(g, 0);
    set_menu_screen(g, menu_screen_opened_on(g, opening));
    set_input_repeat_delay(g, 1);
    set_input_repeat_countdown(g, 1);
    tm_enable_interrupts(pf, PF_AT(0xC28087), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
}

/* What the routines the entry runs as C or calls read of the registers
 * (tools/dontcare_check.py checks each): the opening moves to the menus'
 * page as it starts, which the port moves to first, and reads none of them;
 * nor does the tasks' start. */
enum {
    THE_TASKS_START_READING_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    THE_OPENING_SETS_ITS_PAGE = THE_TASKS_START_READING_NOTHING | DC_D,
};

/* Entering the menus reads every register the caller hands it: what it asks
 * for and with, its page, data bank, flags and stack. */
enum { MENU_MODE_START = 0xC2800E, THE_START_READS_ALL = 0 };

// $C2:8000
/* The first of the menus' bank's three ways in (the frame's input, $C2:8002,
 * and the party's services, $C2:8004, are the others), which the field and the
 * scenes call: a jump to the mode's start, which the runtime takes to the
 * start's own entry. Run here as C (menu_go_on_in), the start's call-outs
 * would be made inside this entry's comparison, and shadow mode hands them to
 * the start's, which the original reaches nested in it: every movie
 * mismatched so (chunk 11's review fixes). */
void menu_mode_enter(Snes *s) { m_stop_at(s, MENU_MODE_START, THE_START_READS_ALL); }

/* The blanks the mode's start, its return and its starting the game again
 * make, each from the original's own sites. */
static const uint32_t start_sites[4] = { 0xC28025, 0xC2802B, 0xC28031, 0xC28035 };
static const uint32_t return_sites[4] = { 0xC282C3, 0xC282C9, 0xC282CF, 0xC282D3 };
static const uint32_t restart_sites[4] = { 0xC28331, 0xC28337, 0xC2833D, 0xC28341 };

/* The mode's steps that are routines of their own, run as C or on the
 * machine (below): the game started again from its reset ($C2:832F); the
 * menus opened ($C2:8096), false where that went no further; and the tasks
 * started and their turns taken until a screen closes the menus ($C2:8190). */
typedef struct {
    void (*start_again)(void *ctx);
    bool (*opened)(void *ctx);
    void (*tasks)(void *ctx);
} ModeSteps;

// $C2:800E
/* Entering the menus: what the caller asked for kept, and the screen blanked
 * while it is set up. Then the menus open on the screen asked for and start
 * their tasks, which run until a screen closes them -- or the game starts
 * afresh. */
static void run_the_mode(GameState *g, Platform *pf, uint8_t asked_for, uint16_t asked_with, const ModeSteps *steps,
                         void *ctx)
{
    set_menu_asked_for(g, asked_for);
    set_menu_asked_with(g, asked_with);
    blank_and_stop(pf, start_sites);
    if (starts_afresh(g)) {
        start_a_game_afresh(g, pf);
        steps->start_again(ctx);
        return;
    }
    if (!steps->opened(ctx))
        return;
    open_on_the_screen(g, pf);
    steps->tasks(ctx);
}

/* ---- As C ---- */

/* The mode run by the program, and how it ended: the task closing the menus
 * says, back to their caller or the game started again from its reset. */
typedef struct {
    GameState *game;
    Platform *pf;
    MenuModeEnd end;
} ModeAsC;

static ModeAsC *running;

static void start_again(void *ctx)
{
    ModeAsC *c = ctx;
    TIMED_AS(c->game, 0xC2832F, blank_and_stop(c->pf, restart_sites));
    c->end = MENUS_START_THE_GAME_AGAIN;
}

static bool opened(void *ctx)
{
    ModeAsC *c = ctx;
    bool opened = false;
    TIMED_AS(c->game, 0xC28096, opened = menu_opens(c->game, c->pf));
    return opened;
}

/* Each frame's turns once the vblank handler has sent what the last frame
 * queued, until a task closes the menus or goes no further. */
static void tasks(void *ctx)
{
    ModeAsC *c = ctx;
    menu_tasks_open(c->game, c->pf);
    MenuTurns turns;
    do {
        while (!menu_vblank_done(c->game))
            tm_wait_frame(c->pf, PF_AT(0xC281CA));
        TIMED_AS(c->game, 0xC281CF, turns = menu_tasks_take_the_turns(c->game));
    } while (turns == MENU_TURNS_TAKEN);
    menu_tasks_end();
    if (turns != MENU_TURNS_CLOSED)
        c->end = MENUS_WENT_NO_FURTHER;
}

MenuModeEnd menu_mode_runs(GameState *g, Platform *pf, uint8_t asked_for, uint16_t asked_with)
{
    static const ModeSteps as_c = { start_again, opened, tasks };
    ModeAsC c = { g, pf, MENUS_WENT_NO_FURTHER };
    running = &c;
    run_the_mode(g, pf, asked_for, asked_with, &as_c, &c);
    running = NULL;
    return c.end;
}

/* ---- On the machine ---- */

static void start_again_on_the_machine(void *ctx) { NATIVE_TIME_AS((Snes *)ctx, 0xC2832F, menu_mode_restart(ctx)); }

/* The menus open on their own stack and page. */
static bool opened_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    s->cpu.s = MENUS_STACK;
    s->cpu.d = MENU_PAGE;
    return menu_run_or_call(s, 0xC28068, 0xC28096, THE_OPENING_SETS_ITS_PAGE, menu_open);
}

/* The tasks start with an 8-bit accumulator and index registers, the
 * interrupts let in. */
static void tasks_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    menu_set_widths(s, true, true);
    m_set_flag(s, CPU_FLAG_I, false);
    menu_go_on_in(s, 0xC28190, menu_start_the_tasks, THE_TASKS_START_READING_NOTHING);
}

/* The caller's direct page, data bank and flags kept on its stack, for the
 * menus' return to put back, and its stack kept; the menus' data bank; and
 * what it asks for and with in A and X. */
void menu_mode_start(Snes *s)
{
    static const ModeSteps on_the_machine = { start_again_on_the_machine, opened_on_the_machine,
                                              tasks_on_the_machine };
    m_set_flag(s, CPU_FLAG_I, true);
    m_set_flag(s, CPU_FLAG_C, false);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.db);
    m_push8(s, s->cpu.p);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    s->cpu.db = MENU_DATA_BANK;
    set_menu_caller_stack(state_of(s), s->cpu.s);
    run_the_mode(state_of(s), m_platform(s), (uint8_t)s->cpu.a, s->cpu.x, &on_the_machine, s);
}

// $C2:82C1
/* Leaving the menus: the screen blanked, the caller's stack and what the
 * entry kept on it put back. It leaves the stack pointer in X. */
void menu_mode_return(Snes *s)
{
    blank_and_stop(m_platform(s), return_sites);
    uint16_t stack = menu_caller_stack(state_of(s));
    s->cpu.x = stack;
    s->cpu.s = stack;
    m_pull_flags(s);
    s->cpu.db = m_pull8(s);
    s->cpu.d = m_pull16(s);
    m_set_nz16(s, s->cpu.d);
    m_set_flag(s, CPU_FLAG_I, true);
    m_rtl(s);
}

// $C2:832F
/* The menus giving up and restarting the game: the same blank, then the boot
 * stack and the reset code at $C0:0000. */
void menu_mode_restart(Snes *s)
{
    blank_and_stop(m_platform(s), restart_sites);
    s->cpu.s = BOOT_STACK;
    m_stop_at(s, 0xC00000, 0);
}

/* Closing the menus, from whichever screen closes them: every task stopped
 * but the one closing them, which goes on; what the party wears shown on the
 * field's sprites again; and the screen faded out. Then either back to the
 * caller (menu_mode_return) or, from the load screen, on to the game, which
 * starts again from its reset with the game loaded (menu_mode_restart). */
enum { EVERY_TASK = 0xFF };
enum { THE_WEAR_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The wear shown, the fade out and the leaving, run as C or on the machine
 * (below), the first two each false where it went no further. */
typedef struct {
    bool (*wear_shown)(void *ctx);
    bool (*faded_out)(void *ctx);
    void (*leave)(void *ctx);
} CloseCalls;

// $C2:82B2, $C2:8320
static void close_the_menus(GameState *g, const CloseCalls *c, void *ctx)
{
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, EVERY_TASK));
    if (c->wear_shown(ctx) && c->faded_out(ctx))
        c->leave(ctx);
}

/* As C, from inside the task that closes them: the menus' mode returns to
 * its caller, the program, saying how they closed, and the task never goes
 * on. */
typedef struct {
    GameState *game;
    Platform *pf;
    bool for_the_game;
} CloseAsC;

static bool wear_shown(void *ctx)
{
    CloseAsC *c = ctx;
    TIMED_AS(c->game, 0xC2834D, party_wear_it_shows(c->game));
    return true;
}

static bool faded_out(void *ctx)
{
    CloseAsC *c = ctx;
    TIMED_AS(c->game, 0xC283CA, menu_fade_to_black(c->game));
    return true;
}

static void leave(void *ctx)
{
    CloseAsC *c = ctx;
    if (c->for_the_game)
        TIMED_AS(c->game, 0xC2832F, blank_and_stop(c->pf, restart_sites));
    else
        TIMED_AS(c->game, 0xC282C1, blank_and_stop(c->pf, return_sites));
    if (running)
        running->end = c->for_the_game ? MENUS_START_THE_GAME_AGAIN : MENUS_CLOSED;
    menu_tasks_closed();
}

void menu_close(GameState *g, Platform *pf, bool for_the_game)
{
    static const CloseCalls as_c = { wear_shown, faded_out, leave };
    CloseAsC c = { g, pf, for_the_game };
    close_the_menus(g, &as_c, &c);
}

/* On the machine each of the two ways out calls what the original calls from
 * its own sites: the wear's, then the fade's; then the mode's return, or its
 * start of the game again. */
static bool wear_shown_back_to_the_caller(void *ctx)
{
    return menu_run_or_call(ctx, 0xC282BB, 0xC2834D, THE_WEAR_IGNORES, menu_show_what_the_party_wears);
}

static bool faded_out_back_to_the_caller(void *ctx) { return menu_fade_out_from(ctx, 0xC282BE); }

static void back_to_the_caller(void *ctx) { NATIVE_TIME_AS((Snes *)ctx, 0xC282C1, menu_mode_return(ctx)); }

static bool wear_shown_for_the_game(void *ctx)
{
    return menu_run_or_call(ctx, 0xC28329, 0xC2834D, THE_WEAR_IGNORES, menu_show_what_the_party_wears);
}

static bool faded_out_for_the_game(void *ctx) { return menu_fade_out_from(ctx, 0xC2832C); }

static void on_to_the_game(void *ctx) { NATIVE_TIME_AS((Snes *)ctx, 0xC2832F, menu_mode_restart(ctx)); }

void menu_close_entry(Snes *s)
{
    static const CloseCalls closing = { wear_shown_back_to_the_caller, faded_out_back_to_the_caller,
                                        back_to_the_caller };
    menu_set_widths(s, true, false);
    close_the_menus(state_of(s), &closing, s);
}

void menu_close_for_the_game(Snes *s)
{
    static const CloseCalls closing = { wear_shown_for_the_game, faded_out_for_the_game, on_to_the_game };
    menu_set_widths(s, true, false);
    close_the_menus(state_of(s), &closing, s);
}

/* The mode's frame: the screen blanked while it sends what the frame built,
 * unless the main loop hasn't finished one. */
enum { FRAME_SENT = 0xFF, FRAME_READY_BIT = 0x80, LINE_EFFECTS = 0x7F, BLOCK_MOVE_ASKED = 0x80 };

void menu_ask_for_the_block_move(GameState *g) { set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | BLOCK_MOVE_ASKED)); }

/* ---- The menus' vblank handler ------------------------------------------------------------------ */

/* The handler runs on a stack of its own, at the top of the menus' low RAM,
 * whatever stack the code it interrupts is on (a screen's task has its own):
 * it keeps that code's registers, notes its stack to go back to, and moves
 * (m_enter_handler_stack: keeping and putting back the interrupted code's
 * registers is the machine's part of taking an interrupt). */
enum { HANDLER_STACK = 0x1DFD };

/* The work goes on from the stack its entry built, with the flags it kept
 * there wide, as the entry leaves them (what the return pulls is as wide as
 * they say); and the interrupted stack is below the page the handler's own
 * runs down into, as every one the game has is (the screens' tasks', the
 * menus' own at $09FF), so what the return pulls from it is what the
 * interrupted code left there, not what the work's callees left below the
 * handler's stack. */
bool menu_nmi_work_runs(Snes *s)
{
    enum { KEPT_HERE = 5, HANDLER_PAGE = HANDLER_STACK & 0xFF00 };   /* kept: the accumulator, the page, the flags */
    uint8_t kept_flags = snes_bus_peek(&s->bus, (uint16_t)(s->cpu.s + 1));
    vars_peek(true);
    uint16_t interrupted = menu_interrupted_stack(state_of(s));
    vars_peek(false);
    return s->cpu.s == HANDLER_STACK - KEPT_HERE && !(kept_flags & (CPU_FLAG_M | CPU_FLAG_X)) &&
           interrupted < HANDLER_PAGE;
}

// $C2:840E (installed at the $0500 NMI stub from the table at $C2:8406)
/* The menus' vblank handler as it starts: the interrupted code's registers
 * kept and the handler on its own stack and page. Its work follows
 * (menu_nmi_work, a port of its own, where this one stops), and the vblank's
 * acknowledgement there, a compare, sets the flags before anything reads
 * them. */
void menu_nmi_enter(Snes *s)
{
    set_menu_interrupted_stack(state_of(s), m_enter_handler_stack(s, HANDLER_STACK));
    s->cpu.d = MENUS_PAGE;
    m_stop_at(s, 0xC2842A, DC_N | DC_Z | DC_C);
}

/* The vblank's two calls, run as C or on the machine (below): the frame's
 * work sent, and the frame's input taken; each false where it went no
 * further. */
typedef struct {
    bool (*frame_sent)(void *ctx);
    bool (*input_taken)(void *ctx);
} VblankCalls;

// $C2:842A (the handler's work, after its entry at $C2:840E)
/* The menus' vblank: counts the frame, sends the frame's work ($C2:85D6) with
 * the per-line effects off and turns them back on after, reads the input and
 * the timers ($C2:8473), and sets the BG mode and the brightness (0 is forced
 * blank). */
static bool menus_vblank(GameState *g, Platform *pf, const VblankCalls *c, void *ctx)
{
    (void)tm_ack_vblank(pf, PF_AT(0xC2842A));
    set_menu_frame_counter(g, (uint8_t)(menu_frame_counter(g) + 1));
    vid_set_display(pf, PF_AT(0xC28431), &BLANK);
    if (!(menu_vblank_done(g) & FRAME_READY_BIT)) {
        vid_enable_line_effects(pf, PF_AT(0xC2843C), 0x00);
        if (!c->frame_sent(ctx))
            return false;
        vid_enable_line_effects(pf, PF_AT(0xC28446), (uint8_t)(menu_line_effects(g) & LINE_EFFECTS));
        set_menu_vblank_done(g, FRAME_SENT);
    }
    if (!c->input_taken(ctx))
        return false;
    vid_set_mode(pf, PF_AT(0xC28452), &(VidMode){ .mode = 1, .bg3_priority = true });
    uint8_t brightness = menu_brightness(g);
    VidDisplay display = brightness != 0 ? vid_display_decode(brightness) : BLANK;
    vid_set_display(pf, PF_AT(0xC2845B), &display);
    if (menu_wait_frames(g) != 0)
        set_menu_wait_frames(g, (uint8_t)(menu_wait_frames(g) - 1));
    return true;
}

static uint16_t send_the_frame(GameState *g, Platform *pf);

typedef struct {
    GameState *game;
    Platform *pf;
} VblankAsC;

/* As C, the frame's work and the queue each where its guard takes them; the
 * program goes no further otherwise. */
static bool frame_sent(void *ctx)
{
    VblankAsC *c = ctx;
    if (!menu_block_move_is_a_buffer(c->game)) {
        program_needs_the_machine(c->game, 0xC285D6, "a block move over the code the menus' vblank runs it through");
        return false;
    }
    TIMED_AS(c->game, 0xC285D6, (void)send_the_frame(c->game, c->pf));
    return menu_upload_queue_sent(c->game, c->pf);
}

static bool input_taken(void *ctx)
{
    VblankAsC *c = ctx;
    bool taken = false;
    TIMED_AS(c->game, 0xC28473, taken = menu_frame_input_taken(c->game, c->pf));
    return taken;
}

void menu_vblank(GameState *g, Platform *pf)
{
    static const VblankCalls as_c = { frame_sent, input_taken };
    VblankAsC c = { g, pf };
    TIMED_AS(g, 0xC2842A, (void)menus_vblank(g, pf, &as_c, &c));
}

/* The menus' line handler returns at once. */
void menu_line(GameState *g, Platform *pf) { (void)g, (void)pf; }

/* On the machine each call is made from the original's own, and the work
 * goes on after it only where it came back as the menus run. */
static bool frame_sent_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_jsr_dc(s, 0xC2843F, 0xC285D6, NOTHING_READ);
    if (back_as_the_menus_run(s, MENUS_PAGE))
        return true;
    m_stop_dont_care(s, 0xC28442, 0);
    return false;
}

static bool input_taken_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_jsr_dc(s, 0xC2844D, 0xC28473, INPUT_IGNORES);
    if (back_as_the_menus_run(s, MENUS_PAGE))
        return true;
    m_stop_dont_care(s, 0xC28450, 0);
    return false;
}

/* Then the interrupted code has its registers and stack back, and the
 * handler returns to it. */
void menu_nmi_work(Snes *s)
{
    static const VblankCalls on_the_machine = { frame_sent_on_the_machine, input_taken_on_the_machine };
    GameState *g = state_of(s);
    if (menus_vblank(g, m_platform(s), &on_the_machine, s))
        m_leave_handler_stack(s, menu_interrupted_stack(g));
}

enum { SCROLLED_LAYERS = 3 };

/* The layers' scroll, which the original reads through the work RAM port
 * from $0D04: the port is left past the twelve bytes. */
static void send_menu_scroll(GameState *g, Platform *pf)
{
    for (unsigned bg = 0; bg < SCROLLED_LAYERS; bg++) {
        uint16_t x = menu_scroll_x(g, bg), y = menu_scroll_y(g, bg);
        vid_set_scroll(pf, PF_AT(0xC285E7), (VidLayer)bg, x, x, VID_SCROLL_H);
        vid_set_scroll(pf, PF_AT(0xC285E7), (VidLayer)bg, y, y, VID_SCROLL_V);
    }
    ORIGINAL_EFFECT(hw_port_seek(g, (uint16_t)(RAM_ABS(RAM_MENU_SCROLL) + SCROLLED_LAYERS * RAM_MENU_SCROLL_STRIDE), 0));
}

static void send_menu_screen_shadows(GameState *g, Platform *pf)
{
    uint32_t masks = menu_window_shadows_window_masks(g), edges = menu_window_shadows_window_edges(g);
    uint16_t logic = menu_window_shadows_window_logic(g);
    ScreenShadows v = {
        { (uint8_t)masks, (uint8_t)(masks >> 8), (uint8_t)(masks >> 16) },
        { (uint8_t)edges, (uint8_t)(edges >> 8), (uint8_t)(edges >> 16), (uint8_t)(edges >> 24) },
        { (uint8_t)logic, (uint8_t)(logic >> 8) },
        menu_window_shadows_main_screen(g), menu_window_shadows_sub_screen(g),
        menu_window_shadows_main_window_mask(g), menu_window_shadows_sub_window_mask(g),
        menu_window_shadows_math_control(g), menu_window_shadows_math_layers(g),
    };
    static const ScreenSites at = { 0xC28601, 0xC28601, 0xC28601, 0xC28601, 0xC28601 };
    send_screen_shadows(pf, &v, &at);
    uint8_t fixed = menu_window_shadows_fixed_color(g);
    vid_set_fixed_color(pf, PF_AT(0xC28601), (uint8_t)(fixed >> 5), (uint8_t)(fixed & 0x1F));
}

/* The block move a frame asks for: the original writes an MVN and an RTL
 * into work RAM and calls them. What it leaves in Y, the destination past
 * the bytes moved, the vblank's caller reads. */
static uint16_t move_the_block(GameState *g)
{
    enum { STUB_BLOCK_MOVE = 0x7E54, STUB_BANK_AND_RETURN = 0x6B7E };
    uint16_t from = menu_block_move_source(g), to = menu_block_move_dest(g), size = menu_block_move_size(g);
    uint32_t bytes = size ? size : 0x10000u;
    ORIGINAL_EFFECT(set_menu_block_move_stub(g, (uint32_t)STUB_BANK_AND_RETURN << 16 | STUB_BLOCK_MOVE));
    wram_copy(g, 0x7E0000u | from, 0x7E0000u | to, bytes);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
    return (uint16_t)(to + bytes);
}

// $C2:86A9
/* The working palette to CGRAM, when it has changed since it was last sent. */
void menu_send_palette(GameState *g, Platform *pf)
{
    if (menu_palette_changed(g) == 0)
        return;
    set_menu_palette_changed(g, 0);
    vid_send_palette(pf, PF_AT(0xC286BC), 0, PF_GAME(RAM_MENU_PALETTE), 256);
}

// $C2:85D6
/* The menus' vblank work: the layers' scroll, the windows, screens and color
 * math, the sprite table, a block move the frame asked for, the palette, and
 * the color math table the frame built (menu_build_math_line_table). It falls
 * through into the VRAM upload queue at $C2:8663. The block move the shadows
 * went out through leaves Y past the register block, and a block move of the
 * frame's past its destination, which is returned. */
static uint16_t send_the_frame(GameState *g, Platform *pf)
{
    send_menu_scroll(g, pf);
    send_menu_screen_shadows(g, pf);
    uint16_t y = M_DP_PPU + 0x33;
    vid_send_oam(pf, PF_AT(0xC28607), PF_GAME(RAM_OAM_BUFFER));
    if (menu_line_effects(g) & BLOCK_MOVE_ASKED) {
        set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) & ~BLOCK_MOVE_ASKED));
        y = move_the_block(g);
    }
    TIMED_AS(g, 0xC286A9, menu_send_palette(g, pf));
    uint16_t table = (uint16_t)RAM_ABS(RAM_MENU_MATH_LINE_TABLES)
                     + ((menu_math_line_table_offset(g) >> 8) ? RAM_MENU_MATH_LINE_TABLES_STRIDE : 0);
    vid_set_line_table(pf, PF_AT(0xC28660), 0, table);
    return y;
}

/* This port stops where the queue's routine starts. */
void menu_vblank_upload(Snes *s)
{
    s->cpu.y = send_the_frame(state_of(s), m_platform(s));
    s->cpu.db = 0x00;
    m_stop_at(s, 0xC28663, 0);
}

/* A block move the game asks for moves a buffer, not the code the original
 * runs it through (the stub in low RAM). */
enum { BLOCK_MOVE_STUB = 0x9690, BLOCK_MOVE_STUB_END = 0x9694 };

/* The bytes the move asked for covers from `at` on, which runs round its
 * bank as a block move does. */
static bool block_move_covers(uint32_t from, uint32_t size, uint16_t at)
{
    uint32_t bytes = size ? size : 0x10000u;
    return (uint16_t)(at - from) < bytes;
}

bool menu_block_move_is_a_buffer(const GameState *g)
{
    if (!(menu_line_effects(g) & BLOCK_MOVE_ASKED))
        return true;
    for (uint16_t at = BLOCK_MOVE_STUB; at < BLOCK_MOVE_STUB_END; at++)
        if (block_move_covers(menu_block_move_dest(g), menu_block_move_size(g), at))
            return false;
    return true;
}

/* The vblank's work runs on the menus' vblank page, with the data bank on the
 * I/O registers; the move, if any, a buffer's, and not the stack the original
 * returns on (which its move would also read its own return address from). */
bool menu_vblank_work_runs(Snes *s)
{
    enum { VBLANK_PAGE = 0x0D00, STACK_REACH = 0x10 };
    if (m_direct_page(s) != VBLANK_PAGE || !m_io_bank(s->cpu.db))
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    bool asked = (menu_line_effects(g) & BLOCK_MOVE_ASKED) != 0, buffer = menu_block_move_is_a_buffer(g);
    uint32_t from = menu_block_move_source(g), to = menu_block_move_dest(g), size = menu_block_move_size(g);
    vars_peek(false);
    if (!asked)
        return true;
    if (!buffer)
        return false;
    uint16_t stack = s->cpu.s;
    for (uint16_t at = (uint16_t)(stack - STACK_REACH); at != (uint16_t)(stack + STACK_REACH); at++)
        if (block_move_covers(to, size, at) || block_move_covers(from, size, at))
            return false;
    return true;
}

// $C2:8663
/* The queued VRAM uploads, the last queued first, which empties the queue.
 * A size of 0 is a whole 64 KiB, as the transfer registers read it. It leaves
 * the first entry's bank in Y. */
void menu_send_upload_queue(GameState *g, Platform *pf)
{
    unsigned entries = menu_vram_queue_bytes(g) / MENU_UPLOAD_BYTES;
    if (entries == 0)
        return;
    set_menu_vram_queue_bytes(g, 0);
    vid_start_vram_uploads(pf, PF_AT(0xC2867A), NULL);
    for (unsigned i = entries; i-- > 0;) {
        uint16_t size = menu_vram_queue_size(g, i);
        vid_send_vram(pf, PF_AT(0xC28686), menu_vram_queue_vram(g, i),
                      PF_GAME((uint32_t)menu_vram_queue_bank(g, i) << 16 | menu_vram_queue_source(g, i)),
                      size ? size : 0x10000u, NULL);
    }
}

bool menu_upload_queue_sent(GameState *g, Platform *pf)
{
    if (!menu_upload_queue_is_whole(g)) {
        program_needs_the_machine(g, 0xC28663, "an upload queue the port doesn't send");
        return false;
    }
    TIMED_AS(g, 0xC28663, menu_send_upload_queue(g, pf));
    return true;
}

void menu_upload_vram_queue(Snes *s)
{
    GameState *g = state_of(s);
    bool any = menu_vram_queue_bytes(g) >= MENU_UPLOAD_BYTES;
    s->cpu.y &= 0xFF;   /* the queue is read with 8-bit index registers */
    menu_send_upload_queue(g, m_platform(s));
    if (any)
        s->cpu.y = menu_vram_queue_bank(g, 0);
    m_rts(s);
}

/* What the game uploads comes from work RAM's banks or the ROM's, never
 * through the banks that mirror the I/O registers, where what a transfer reads
 * depends on when it reads it. */
static bool uploads_from_memory(uint8_t bank) { return bank == 0x7E || bank == 0x7F || (bank & 0x7F) >= 0x40; }

/* ... and never from the top of the stack, where the original's upload
 * routine saves its registers before it reads the queue. */
static bool uploads_off_the_stack(uint8_t bank, uint16_t source, uint16_t size, uint16_t sp)
{
    enum { SAVED = 8, LOW_RAM_END = 0x2000 };
    bool low_ram = bank == 0x7E || ((bank & 0x7F) < 0x40 && source < LOW_RAM_END);
    uint32_t from = source, to = (uint32_t)source + (size ? size : 0x10000u);
    return !low_ram || to <= (uint32_t)(sp - SAVED) || from > sp;
}

/* The queue's entries are whole, no more than it holds, and from memory. */
bool menu_upload_queue_is_whole(const GameState *g)
{
    uint8_t bytes = menu_vram_queue_bytes(g);
    bool ok = bytes % MENU_UPLOAD_BYTES == 0 && bytes / MENU_UPLOAD_BYTES <= MENU_VRAM_QUEUE_COUNT;
    for (unsigned i = 0; ok && i < bytes / MENU_UPLOAD_BYTES; i++)
        ok = uploads_from_memory(menu_vram_queue_bank(g, i));
    return ok;
}

/* On the machine, none from the top of the stack either. */
bool menu_vram_queue_is_whole(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool ok = menu_upload_queue_is_whole(g);
    for (unsigned i = 0; ok && i < menu_vram_queue_bytes(g) / MENU_UPLOAD_BYTES; i++)
        ok = uploads_off_the_stack(menu_vram_queue_bank(g, i), menu_vram_queue_source(g, i),
                                   menu_vram_queue_size(g, i), s->cpu.s);
    vars_peek(false);
    return ok;
}

// $C2:86A9
void menu_upload_palette(Snes *s)
{
    menu_send_palette(state_of(s), m_platform(s));
    m_rts(s);
}

/* The two color math tables alternate frame by frame, so the one showing is
 * never the one being built. An entry is a line count, then the value of the
 * color math layers and the fixed color, a word. */
enum { MATH_ENTRY_BYTES = 3, RAMP_STEPS = 16, LIST_END = 0x80, SINGLE_COLOR = 8 };

/* A byte of the tables by its offset from the first; a table built longer than
 * the two hold runs on past them, as the original's index does. */
static void put_math_byte(GameState *g, uint16_t at, uint8_t v)
{
    if ((at >> 8) < MENU_MATH_LINE_TABLES_COUNT)
        set_menu_math_line_tables_byte(g, at >> 8, at & 0xFF, v);
    else
        set_menu_math_line_tables_byte_unchecked(g, at >> 8, at & 0xFF, v);
}

static void put_math_entry(GameState *g, uint16_t at, uint8_t lines, uint16_t color)
{
    put_math_byte(g, at, lines);
    put_math_byte(g, (uint16_t)(at + 1), (uint8_t)color);
    put_math_byte(g, (uint16_t)(at + 2), (uint8_t)(color >> 8));
}

/* A ramp: sixteen steps through the ramp colors, each for the lines the
 * accumulator's whole part says. */
static uint16_t put_ramp(GameState *g, uint16_t at, uint8_t lines_a_step)
{
    uint16_t step = (uint16_t)(lines_a_step << 4);
    uint16_t accumulator = menu_math_ramp_accumulator(g);
    ORIGINAL_EFFECT(set_menu_math_ramp_step(g, step));
    for (unsigned i = 0; i < RAMP_STEPS; i++, at = (uint16_t)(at + MATH_ENTRY_BYTES)) {
        accumulator = (uint16_t)(accumulator + step);
        put_math_entry(g, at, (uint8_t)(accumulator >> 8), menu_math_ramp_colors(g, i));
        accumulator &= 0x00FF;
    }
    set_menu_math_ramp_accumulator(g, accumulator);
    return at;
}

// $C2:86DD
/* Build the per-line color math table the menus run on per-line effect slot
 * 0, and program the slot: the ramp colors refreshed from the ROM, then each
 * record a single entry or a ramp, into whichever table isn't showing. */
void menu_build_math_table(GameState *g, Platform *pf)
{
    enum { TABLE_BYTES = 0x100 };
    for (unsigned i = 0; i < MENU_MATH_RAMP_COLORS_COUNT; i++)
        set_menu_math_ramp_colors(g, i, menu_math_ramp_color(g, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_MATH_RAMP_COLORS_COUNT));
    const VidLineEffect e = { .target = VID_LINE_MATH_LAYERS, .table = PF_GAME(RAM_MENU_MATH_LINE_TABLES),
                              .indirect = false, .indirect_bank = 0x7E };
    vid_set_line_slot(pf, PF_AT(0xC286ED), 0, &e);
    uint16_t at = menu_math_line_table_offset(g) == 0 ? TABLE_BYTES : 0;
    set_menu_math_line_table_offset(g, at);
    set_menu_math_ramp_accumulator(g, 0);
    for (unsigned r = 0; !(menu_math_records_lines(g, r) & LIST_END); r++) {
        if (menu_math_records_lines(g, r) != 0) {
            at = put_ramp(g, at, menu_math_records_value(g, r));
            continue;
        }
        put_math_entry(g, at, menu_math_records_value(g, r), menu_math_ramp_colors(g, SINGLE_COLOR));
        at = (uint16_t)(at + MATH_ENTRY_BYTES);
    }
}

/* A list the game builds ends within its records and fits a table. */
bool menu_math_list_fits(Snes *s)
{
    GameState *g = state_of(s);
    unsigned bytes = 0;
    bool ends = false;
    vars_peek(true);
    for (unsigned r = 0; r < MENU_MATH_RECORDS_COUNT && !ends; r++) {
        ends = (menu_math_records_lines(g, r) & LIST_END) != 0;
        if (!ends)
            bytes += menu_math_records_lines(g, r) != 0 ? RAMP_STEPS * MATH_ENTRY_BYTES : MATH_ENTRY_BYTES;
    }
    vars_peek(false);
    return ends && bytes <= MENU_MATH_LINE_TABLES_BYTES;
}

void menu_build_math_line_table(Snes *s)
{
    menu_build_math_table(state_of(s), m_platform(s));
    m_rts(s);
}

/* ---- The mode at $C2:0000 ------------------------------------------------------------------------- */

// $C2:000F
/* Entering the sprite-object mode: interrupts, uploads and per-line effects
 * off, then forced blank at full brightness, which is the brightness the
 * mode's vblank handler goes on using; the cartridge read at its fast speed;
 * the mode's screen, its own state, its vblank handler and its upload queue
 * set up and its palette cleared. The world map it shows is set up next
 * (overworld/setup.c), where the original jumps ($C2:23A8). */
enum { WORLD_MAP_SET_UP = 0xC223A8, MENU_OBJECT_PAGE = 0x0000 };

static void set_up_the_mode(GameState *g, Platform *pf);

void menu_object_mode_starts(GameState *g, Platform *pf)
{
    tm_enable_interrupts(pf, PF_AT(0xC20016), 0);
    vid_stop_uploads(pf, PF_AT(0xC20019));
    vid_enable_line_effects(pf, PF_AT(0xC2001C), 0);
    vid_set_display(pf, PF_AT(0xC20021), &(VidDisplay){ .blank = true, .brightness = 0x0F });
    tm_fast_rom(pf, PF_AT(0xC20026), true);
    set_up_the_mode(g, pf);
}

/* On the machine the mode starts with the CPU's interrupts masked, an 8-bit
 * accumulator and 16-bit index registers, on its page with the data bank on
 * low RAM. */
void menu_object_mode_start(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    s->cpu.db = 0x00;
    s->cpu.d = MENU_OBJECT_PAGE;
    menu_object_mode_starts(state_of(s), m_platform(s));
    m_stop_at_own(s, WORLD_MAP_SET_UP, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

/* The mode's screen: the sprites' smaller size 16x16, their tiles at VRAM 0;
 * mode 1 with BG3 in front; BG1 and BG2's tilemaps two screens wide at $6000
 * and $6800 with their tiles at $2000, BG3's (and BG4's, the same) at $7800
 * with its tiles at $7000; nothing scrolled; the Mode 7 plane at its identity;
 * no windows; BG1-BG3 and the sprites on the main screen, BG1, BG2 and the
 * sprites on the sub screen, which color math adds to BG3 at half; and a black
 * fixed color. The display stays as the mode's start left it. */
static const VidScreen object_mode_screen = {
    .sprites = { .size_select = 3, .base = 0x0000 },
    .mode = { .mode = 1, .bg3_priority = true },
    .mosaic_size = 1,
    .bg = { { VID_TILEMAP(0x6000, 64, 32), .char_base = 0x2000 },
            { VID_TILEMAP(0x6800, 64, 32), .char_base = 0x2000 },
            { VID_TILEMAP(0x7800, 64, 32), .char_base = 0x7000 },
            { VID_TILEMAP(0x7800, 64, 32), .char_base = 0x7000 } },
    .mode7 = { .a = 0x0100, .d = 0x0100 },
    .main_layers = VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_OBJ_BIT,
    .sub_layers = VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT,
    .math = { .sub_screen = true, .half = true, .layers = VID_BG3_BIT },
    .vram_port = { .step = 1, .after_high = true },
};

/* The controller port's programmable lines, all high: nothing latched. */
enum { IO_LINES_RELEASED = 0xFF };

/* The multiplier and the divider start from nothing, which only the
 * comparison with the original sees: nothing reads them before setting them. */
static void clear_arithmetic(GameState *g)
{
    hw_set_multiplicand(g, 0);
    (void)hw_multiply_by(g, 0);
    hw_set_dividend(g, 0);
    hw_divide_by(g, 0);
}

// $C2:0043
/* Set the mode's screen up, and the console's own registers as the mode
 * starts them: the controller port's lines released, the arithmetic unit
 * cleared, the timers back at the frame's start. */
void menu_object_set_up_the_screen(GameState *g, Platform *pf)
{
    vid_apply_screen(pf, PF_AT(0xC20043), &object_mode_screen);
    in_set_io_lines(pf, PF_AT(0xC200F8), IO_LINES_RELEASED);
    ORIGINAL_EFFECT(clear_arithmetic(g));
    tm_reset_timers(pf, PF_AT(0xC2010C));
}

void menu_object_init_video(Snes *s)
{
    menu_object_set_up_the_screen(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:012F
/* The mode's own state starts from nothing: its direct page cleared, the
 * screen off, and the display it sends each frame as the screen starts --
 * mode 1 with BG3 in front, the Mode 7 plane at its identity, both windows
 * shut (each opening a pixel past where it closes), BG1-BG3 and the sprites
 * on the main screen, BG1, BG2 and the sprites on the sub screen. */
enum { MODE_PAGE_BYTES = 0xF0, SCREEN_OFF = 0x80, MODE_1_BG3_IN_FRONT = 0x09 };
enum { MATRIX_A_HIGH = 1, MATRIX_D_HIGH = 7, IDENTITY_HIGH = 0x01 };
enum { WINDOW_1_OPENS = 3, WINDOW_2_OPENS = 5, A_PIXEL_PAST_SHUT = 0x01 };
enum { MAIN_LAYERS = 0x17, SUB_LAYERS = 0x13 };

void menu_object_reset_the_mode(GameState *g)
{
    wram_fill8(g, RAM_MENU_OBJECT_SCRATCH, MODE_PAGE_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, MODE_PAGE_BYTES - 1));
    set_menu_object_display_blank(g, SCREEN_OFF);
    set_menu_object_shadows_bg_mode(g, MODE_1_BG3_IN_FRONT);
    set_menu_object_shadows_mode7_matrix_byte(g, MATRIX_A_HIGH, IDENTITY_HIGH);
    set_menu_object_shadows_mode7_matrix_byte(g, MATRIX_D_HIGH, IDENTITY_HIGH);
    set_menu_object_shadows_windows_byte(g, WINDOW_1_OPENS, A_PIXEL_PAST_SHUT);
    set_menu_object_shadows_windows_byte(g, WINDOW_2_OPENS, A_PIXEL_PAST_SHUT);
    set_menu_object_shadows_main_screen(g, MAIN_LAYERS);
    set_menu_object_shadows_sub_screen(g, SUB_LAYERS);
}

// $C2:0110
/* The mode's frame is its own vblank handler's; it has no line handler, so
 * the line interrupt (which it never enables) only returns. */


void menu_object_take_the_interrupts(GameState *g)
{
    program_install_vblank_handler(g, MENU_OBJECT_VBLANK_HANDLER);
    program_install_line_handler(g, MENU_OBJECT_LINE_HANDLER);
}

/* Where the uploads the mode builds are put together, in work RAM. */
enum { UPLOAD_BUFFER = 0x7EF000 };

// $C2:03EF
/* Empty the mode's upload queue, every record cleared, and start its
 * buffer over. */
void menu_object_reset_the_upload_queue(GameState *g)
{
    set_menu_object_vram_queue_busy(g, 0);
    set_menu_object_vram_queue_end(g, 0);
    set_menu_object_upload_buffer(g, UPLOAD_BUFFER);
    wram_fill8(g, RAM_MENU_OBJECT_VRAM_QUEUE, MENU_OBJECT_VRAM_QUEUE_COUNT * MENU_OBJECT_UPLOAD_BYTES, 0);
}

static void set_up_the_mode(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC20043, menu_object_set_up_the_screen(g, pf));
    TIMED_AS(g, 0xC2012F, menu_object_reset_the_mode(g));
    TIMED_AS(g, 0xC20110, menu_object_take_the_interrupts(g));
    TIMED_AS(g, 0xC203EF, menu_object_reset_the_upload_queue(g));
    TIMED_AS(g, 0xC21DB5, menu_clear_the_palette(g));
}

// $C2:012F
void menu_object_reset_the_mode_entry(Snes *s)
{
    menu_object_reset_the_mode(state_of(s));
    m_rts(s);
}

// $C2:0110
void menu_object_take_the_interrupts_entry(Snes *s)
{
    menu_object_take_the_interrupts(state_of(s));
    m_rts(s);
}

// $C2:03EF
/* It leaves the buffer's bank in A, which a caller may read. */
void menu_object_reset_the_upload_queue_entry(Snes *s)
{
    menu_object_reset_the_upload_queue(state_of(s));
    m_set_a8(s, (uint8_t)(UPLOAD_BUFFER >> 16));
    m_rts(s);
}

/* The mode's shadows, as its vblank handler sends them: the BG mode and
 * mosaic, the three layers' scroll, Mode 7's matrix and center, the windows,
 * screens and color math, and the fixed color one channel at a time. */
static void send_object_shadows(GameState *g, Platform *pf)
{
    VidMode mode = vid_mode_decode(menu_object_shadows_bg_mode(g));
    vid_set_mode(pf, PF_AT(0xC201D0), &mode);
    uint8_t mosaic = menu_object_shadows_mosaic(g);
    vid_set_mosaic(pf, PF_AT(0xC201D5), (uint8_t)(mosaic & 0x0F), (uint8_t)((mosaic >> 4) + 1));
    const uint16_t x[3] = { menu_object_shadows_bg1_scroll_x(g), menu_object_shadows_bg2_scroll_x(g),
                            menu_object_shadows_bg3_scroll_x(g) };
    const uint16_t y[3] = { menu_object_shadows_bg1_scroll_y(g), menu_object_shadows_bg2_scroll_y(g),
                            menu_object_shadows_bg3_scroll_y(g) };
    static const uint32_t scroll_sites[3][2] = {
        { 0xC201DA, 0xC201E4 }, { 0xC201EE, 0xC201F8 }, { 0xC20202, 0xC2020C },
    };
    for (int bg = 0; bg < 3; bg++) {
        vid_set_scroll(pf, PF_AT(scroll_sites[bg][0]), (VidLayer)bg, x[bg], x[bg], VID_SCROLL_H);
        vid_set_scroll(pf, PF_AT(scroll_sites[bg][1]), (VidLayer)bg, y[bg], y[bg], VID_SCROLL_V);
    }
    uint16_t m[4];
    for (int i = 0; i < 4; i++)
        m[i] = (uint16_t)(menu_object_shadows_mode7_matrix_byte(g, 2u * i)
                          | menu_object_shadows_mode7_matrix_byte(g, 2u * i + 1) << 8);
    VidMode7 m7 = { .a = (int16_t)m[0], .b = (int16_t)m[1], .c = (int16_t)m[2], .d = (int16_t)m[3] };
    vid_set_mode7(pf, PF_AT(0xC20216), &m7, VID_M7_MATRIX);
    uint32_t centre = menu_object_shadows_mode7_center(g);
    m7.cx = (int16_t)(uint16_t)centre;
    m7.cy = (int16_t)(uint16_t)(centre >> 16);
    vid_set_mode7(pf, PF_AT(0xC2023C), &m7, VID_M7_CENTER);
    ScreenShadows v = {
        { menu_object_shadows_windows_byte(g, 0), menu_object_shadows_windows_byte(g, 1),
          menu_object_shadows_windows_byte(g, 2) },
        { menu_object_shadows_windows_byte(g, 3), menu_object_shadows_windows_byte(g, 4),
          menu_object_shadows_windows_byte(g, 5), menu_object_shadows_windows_byte(g, 6) },
        { menu_object_shadows_windows_byte(g, 7), menu_object_shadows_windows_byte(g, 8) },
        menu_object_shadows_main_screen(g), menu_object_shadows_sub_screen(g),
        menu_object_shadows_main_window_mask(g), menu_object_shadows_sub_window_mask(g),
        menu_object_shadows_math_control(g), menu_object_shadows_math_layers(g),
    };
    static const ScreenSites at = { 0xC20250, 0xC2027D, 0xC20287, 0xC20291, 0xC20296 };
    send_screen_shadows(pf, &v, &at);
    const struct { uint8_t value, channel; uint32_t site; } color[3] = {
        { menu_object_shadows_fixed_blue(g), VID_BLUE, 0xC2029B },
        { menu_object_shadows_fixed_green(g), VID_GREEN, 0xC202A2 },
        { menu_object_shadows_fixed_red(g), VID_RED, 0xC202A9 },
    };
    for (int i = 0; i < 3; i++)
        vid_set_fixed_color(pf, PF_AT(color[i].site), (uint8_t)((color[i].value >> 5) | color[i].channel),
                            (uint8_t)(color[i].value & 0x1F));
}

/* The per-line tables' staged addresses into their entries, and the gate
 * screen's scroll start into every entry of its table. */
static void place_line_tables(GameState *g)
{
    enum { GATE_ENTRIES = 4 };
    for (unsigned slot = 0; slot < MENU_OBJECT_LINE_SLOTS_COUNT; slot++) {
        set_menu_object_line_slots_first(g, slot, menu_object_hdma_pointers(g, 2 * slot));
        set_menu_object_line_slots_second(g, slot, menu_object_hdma_pointers(g, 2 * slot + 1));
    }
    uint16_t at = menu_top_layer_wave_at(g);
    for (unsigned i = 0; i < GATE_ENTRIES; i++)
        set_menu_top_layer_wave_table(g, i, (menu_top_layer_wave_table(g, i) & 0xFF) | (uint32_t)at << 8);
}

/* The mode's frame state: the main loop has finished a frame to show, and the
 * palette changed in it. */
enum { FRAME_TO_SHOW = 0x01, PALETTE_TO_SEND = 0x02 };

static void send_object_frame(GameState *g, Platform *pf)
{
    vid_set_display(pf, PF_AT(0xC2017D), &BLANK);
    vid_enable_line_effects(pf, PF_AT(0xC20181), 0x00);
    vid_send_oam(pf, PF_AT(0xC20187), PF_GAME(RAM_ABS(RAM_OAM_BUFFER)));
    if (menu_object_frame_state(g) & PALETTE_TO_SEND)
        vid_send_palette(pf, PF_AT(0xC201AD), 0, PF_GAME(RAM_ABS(RAM_MENU_OBJECT_PALETTE)), 256);
    set_menu_object_frame_state(g, 0);
    send_object_shadows(g, pf);
    place_line_tables(g);
}

/* The mode's own work after its frame is sent, each a routine of its own,
 * run as C or on the machine (below), each false where it went no further:
 * the uploads it queued, the frame's input (the controller, the button
 * assignments and the play time, $C2:8002), and around the objects' turns
 * the sprite list emptied, the sprites hidden and the list drawn. */
typedef enum {
    SEND_THE_QUEUE, TAKE_THE_INPUT, EMPTY_THE_SPRITE_LIST, HIDE_THE_SPRITES, RUN_THE_OBJECTS, DRAW_THE_SPRITE_LIST,
    OBJECT_FRAME_STEPS,
} ObjectFrameStep;
typedef bool (*ObjectFrameRun)(void *ctx, ObjectFrameStep step);

// $C2:015E (the work, after the handler keeps the interrupted code's registers)
/* The sprite-object mode's vblank: counts the frame and, when the main loop
 * has finished one, sends it (the sprite table, the palette when it changed,
 * the shadows) and does the mode's own work: the uploads and the input, the
 * brightness and the per-line effects the frame left, and the objects'
 * turns, each showing its sprites on the list the frame starts empty, drawn
 * into the sprite table after them. */
static bool object_mode_vblank(GameState *g, Platform *pf, ObjectFrameRun run, void *ctx)
{
    (void)tm_ack_vblank(pf, PF_AT(0xC2016D));
    set_menu_object_frame_counter(g, (uint8_t)(menu_object_frame_counter(g) + 1));
    if (!(menu_object_frame_state(g) & FRAME_TO_SHOW))
        return true;
    send_object_frame(g, pf);
    if (!run(ctx, SEND_THE_QUEUE) || !run(ctx, TAKE_THE_INPUT))
        return false;
    VidDisplay display = vid_display_decode(menu_screen_display(g));
    vid_set_display(pf, PF_AT(0xC20323), &display);
    vid_enable_line_effects(pf, PF_AT(0xC20328), menu_object_line_effects(g));
    if (!run(ctx, EMPTY_THE_SPRITE_LIST))
        return false;
    /* the list's emptying comes back with a 16-bit accumulator, so the
     * sprites' count is cleared a word wide, over the running object's low
     * byte */
    set_menu_object_sprite_index(g, 0);
    set_menu_object_current(g, (uint16_t)(menu_object_current(g) & 0xFF00));
    for (ObjectFrameStep step = HIDE_THE_SPRITES; step < OBJECT_FRAME_STEPS; step++)
        if (!run(ctx, step))
            return false;
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | FRAME_TO_SHOW));
    return true;
}

/* ---- As C ---- */

static void send_object_queue(GameState *g, Platform *pf);

typedef struct {
    GameState *game;
    Platform *pf;
} ObjectVblankAsC;

/* Each step where its guard's state half takes it; the program goes no
 * further otherwise. */
static bool object_frame_step(void *ctx, ObjectFrameStep step)
{
    ObjectVblankAsC *c = ctx;
    GameState *g = c->game;
    Platform *pf = c->pf;
    bool done = true;
    switch (step) {
    case SEND_THE_QUEUE:
        if (!menu_object_queue_can_be_sent(g)) {
            program_needs_the_machine(g, 0xC20405, "an upload queue the port doesn't send");
            return false;
        }
        TIMED_AS(g, 0xC20405, send_object_queue(g, pf));
        return true;
    case TAKE_THE_INPUT:
        TIMED_AS(g, 0xC28002, done = input_take_frame(g, pf));
        if (!done)
            program_needs_the_machine(g, 0xC28002, "the controller not read, or the reset buttons held");
        return done;
    case EMPTY_THE_SPRITE_LIST:
        if (!menu_sprite_list_can_be_emptied(g)) {
            program_needs_the_machine(g, 0xC20C4D, "a sprite list whose links the mode's start didn't lay out");
            return false;
        }
        TIMED_AS(g, 0xC20C4D, menu_empty_the_sprite_list(g));
        return true;
    case HIDE_THE_SPRITES:
        TIMED_AS(g, 0xC2034D, menu_hide_the_sprites(g));
        return true;
    case RUN_THE_OBJECTS:
        TIMED_AS(g, 0xC2051D, done = menu_objects_run(g, pf));
        return done;
    case DRAW_THE_SPRITE_LIST:
        if (!menu_sprite_list_can_be_drawn(g)) {
            program_needs_the_machine(g, 0xC20CEA, "a sprite list the port doesn't draw");
            return false;
        }
        TIMED_AS(g, 0xC20CEA, menu_draw_the_sprite_list(g));
        return true;
    default:
        return false;
    }
}

void menu_object_vblank(GameState *g, Platform *pf)
{
    ObjectVblankAsC c = { g, pf };
    TIMED_AS(g, MENU_OBJECT_VBLANK_HANDLER, (void)object_mode_vblank(g, pf, object_frame_step, &c));
}

/* The mode has no line handler: the line interrupt only returns. */
void menu_object_line(GameState *g, Platform *pf) { (void)g, (void)pf; }

/* ---- On the machine ---- */

/* Each step is called from the original's call of it, and the work goes on
 * after it only where it came back as the handler runs. The list's emptying
 * comes back with a 16-bit accumulator. The objects' handlers may read Y and
 * the carry and overflow flags (the handlers are named in work RAM), which
 * the sprites' hiding leaves as the original does. */
static const struct {
    uint32_t site, routine;
    uint16_t ignores;
    bool long_call;
} object_frame_calls[OBJECT_FRAME_STEPS] = {
    [SEND_THE_QUEUE] = { 0xC20318, 0xC20405, NOTHING_READ, false },
    [TAKE_THE_INPUT] = { 0xC2031B, 0xC28002, 0, true },
    [EMPTY_THE_SPRITE_LIST] = { 0xC2032B, 0xC20C4D, NOTHING_READ, false },
    [HIDE_THE_SPRITES] = { 0xC20330, 0xC2034D, NOTHING_READ, false },
    [RUN_THE_OBJECTS] = { 0xC20333, 0xC2051D, DC_A | DC_X | DC_N | DC_Z, false },
    [DRAW_THE_SPRITE_LIST] = { 0xC20336, 0xC20CEA, NOTHING_READ, false },
};

static bool object_frame_step_on_the_machine(void *ctx, ObjectFrameStep step)
{
    Snes *s = ctx;
    if (object_frame_calls[step].long_call)
        return menu_run_long(s, object_frame_calls[step].site, object_frame_calls[step].routine,
                             menu_take_the_frames_input);
    m_jsr_dc(s, object_frame_calls[step].site, object_frame_calls[step].routine, object_frame_calls[step].ignores);
    if (step == EMPTY_THE_SPRITE_LIST)
        return !s->cpu.e && !m_m8(s) && !m_x8(s) && s->cpu.d == OBJECT_MODE_PAGE;
    return back_as_the_menus_run(s, OBJECT_MODE_PAGE);
}

// $C2:015E (installed at the $0501 NMI stub by $C2:0110)
/* The handler saves the registers on the stack as an interrupt handler
 * does, and runs on the mode's page with the data bank on low RAM. */
void menu_object_nmi(Snes *s)
{
    /* A, X, Y and D, saved a word each and put back the other way round */
    uint16_t *const saved[4] = { &s->cpu.a, &s->cpu.x, &s->cpu.y, &s->cpu.d };
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    for (int i = 0; i < 4; i++)
        m_push16(s, *saved[i]);
    m_push8(s, s->cpu.db);
    s->cpu.d = 0x0000;
    m_sep(s, CPU_FLAG_M);
    m_push8(s, 0x00);
    s->cpu.db = m_pull8(s);
    if (!object_mode_vblank(state_of(s), m_platform(s), object_frame_step_on_the_machine, s))
        return;
    m_rep(s, CPU_FLAG_M);
    s->cpu.db = m_pull8(s);
    for (int i = 3; i >= 0; i--)
        *saved[i] = m_pull16(s);
    m_set_nz16(s, *saved[0]);
    m_rti(s);
}

// $C2:0405
/* The sprite-object mode's VRAM upload queue, unless something is queueing
 * right now: each record from the source, bank and VRAM address, size and
 * port setting it names; then the queue is empty and the
 * upload buffer starts over. */
static void send_object_queue(GameState *g, Platform *pf)
{
    uint8_t end = menu_object_vram_queue_end(g);
    if (menu_object_vram_queue_busy(g) != 0 || end == 0)
        return;
    uint8_t at = 0;
    do {
        unsigned r = at / MENU_OBJECT_UPLOAD_BYTES;
        bool in_queue = r < MENU_OBJECT_VRAM_QUEUE_COUNT;
        uint16_t source = in_queue ? menu_object_vram_queue_source(g, r) : menu_object_vram_queue_source_unchecked(g, r);
        uint16_t vram = in_queue ? menu_object_vram_queue_vram(g, r) : menu_object_vram_queue_vram_unchecked(g, r);
        uint16_t size = in_queue ? menu_object_vram_queue_size(g, r) : menu_object_vram_queue_size_unchecked(g, r);
        uint8_t bank = in_queue ? menu_object_vram_queue_bank(g, r) : menu_object_vram_queue_bank_unchecked(g, r);
        VidVramPort port = vid_vram_port_decode(in_queue ? menu_object_vram_queue_port(g, r)
                                                         : menu_object_vram_queue_port_unchecked(g, r));
        vid_send_vram(pf, PF_AT(0xC20417), vram, PF_GAME((uint32_t)bank << 16 | source), size ? size : 0x10000u, &port);
        at = (uint8_t)(at + MENU_OBJECT_UPLOAD_BYTES);
    } while (at != end);
    set_menu_object_vram_queue_end(g, 0);
    set_menu_object_upload_buffer(g, UPLOAD_BUFFER);
}

void menu_object_upload_vram_queue(Snes *s)
{
    GameState *g = state_of(s);
    if (menu_object_vram_queue_busy(g) == 0 && menu_object_vram_queue_end(g) != 0)
        s->cpu.y &= 0xFF;   /* the queue is sent with 8-bit index registers */
    send_object_queue(g, m_platform(s));
    m_rts(s);
}

bool menu_object_queue_can_be_sent(const GameState *g)
{
    uint8_t end = menu_object_vram_queue_end(g);
    bool ok = menu_object_queue_is_whole(g);
    for (unsigned r = 0; ok && r < end / MENU_OBJECT_UPLOAD_BYTES; r++)
        ok = uploads_from_memory(r < MENU_OBJECT_VRAM_QUEUE_COUNT ? menu_object_vram_queue_bank(g, r)
                                                                   : menu_object_vram_queue_bank_unchecked(g, r));
    return ok;
}

bool menu_object_queue_is_sendable(Snes *s)
{
    if (m_direct_page(s) != 0 || !m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_object_queue_can_be_sent(state_of(s));
    vars_peek(false);
    return ok;
}

/* ---- Loading a screen's graphics ------------------------------------------------------------------ */

// $C2:26A8
/* Clear all of VRAM with the word at $C2:26D4, a whole 64 KiB from word 0. */
void menu_clear_all_of_vram(Platform *pf)
{
    vid_send_vram_fill(pf, PF_AT(0xC226AB), 0x0000, PF_GAME(MENU_VRAM_CLEARING_ZERO), 0x10000, &word_port);
}

/* It leaves X at 0. */
void menu_clear_vram(Snes *s)
{
    menu_clear_all_of_vram(m_platform(s));
    s->cpu.x = 0x0000;
    m_rts(s);
}

// $C2:2D70
/* The graphics loader's transfer: `bytes` from bank $7F at `from` to VRAM
 * word `vram`, a word at a time. A size of 0 is a whole 64 KiB, as the
 * registers read it. */
void menu_send_loaded_graphics(Platform *pf, uint16_t vram, uint16_t from, uint16_t bytes)
{
    vid_send_vram(pf, PF_AT(0xC22D70), vram, PF_GAME(0x7F0000 | from), bytes ? bytes : 0x10000, &word_port);
}

void menu_upload_vram(Snes *s)
{
    menu_send_loaded_graphics(m_platform(s), s->cpu.y, s->cpu.x, s->cpu.a);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:2D47
/* The palette the graphics loader decompressed (menu_loaded_palette), sent
 * to CGRAM. */
void menu_send_the_loaded_palette(Platform *pf)
{
    vid_send_palette(pf, PF_AT(0xC22D4B), 0, PF_GAME(RAM_MENU_LOADED_PALETTE), 256);
}


/* The screen the graphics loader's screens are built on: forced blank; the
 * sprites 8x8 and 16x16 with their tiles at VRAM $4000; mode 1 with BG3 in
 * front; BG1-BG3's tilemaps two screens high at $5800, $6000 and $6800, BG1
 * and BG2's tiles at 0 and BG3's at $7000, and BG4's all at 0; nothing
 * scrolled; the Mode 7 plane at its identity; no windows, no layer on either
 * screen and no color math; and a black fixed color. */
static const VidScreen loader_screen = {
    .display = &BLANK,
    .sprites = { .size_select = 0, .base = 0x4000 },
    .mode = { .mode = 1, .bg3_priority = true },
    .mosaic_size = 1,
    .bg = { { VID_TILEMAP(0x5800, 32, 64), .char_base = 0x0000 },
            { VID_TILEMAP(0x6000, 32, 64), .char_base = 0x0000 },
            { VID_TILEMAP(0x6800, 32, 64), .char_base = 0x7000 },
            { VID_TILEMAP(0x0000, 32, 32), .char_base = 0x0000 } },
    .mode7 = { .a = 0x0100, .d = 0x0100 },
    .vram_port = { .step = 1, .after_high = true },
};

/* The console stopped for the setup: no interrupts, timers, transfers or
 * per-line effects, and the cartridge read at its fast speed. */
static void stop_the_console(Platform *pf)
{
    tm_enable_interrupts(pf, PF_AT(0xC294C0), 0);
    tm_reset_timers(pf, PF_AT(0xC294C2));
    vid_stop_uploads(pf, PF_AT(0xC294C6));
    vid_enable_line_effects(pf, PF_AT(0xC294C8), 0x00);
    tm_fast_rom(pf, PF_AT(0xC294CC), true);
}

static void clear_the_screens_memory(GameState *g, Platform *pf);

// $C2:940D
/* Set the loader's screen up, with the console stopped, and clear all of
 * VRAM and the screen's buffers in work RAM ($C2:94D5). */
void menu_video_set_up(GameState *g, Platform *pf)
{
    vid_apply_screen(pf, PF_AT(0xC2940D), &loader_screen);
    stop_the_console(pf);
    clear_the_screens_memory(g, pf);
}

void menu_init_video(Snes *s)
{
    menu_video_set_up(state_of(s), m_platform(s));
    m_rts(s);
}

/* A stretch of work RAM cleared with the byte at $C2:9510 through the work
 * RAM port, with the per-line effects off first (the transfer runs on one of
 * their channels). The destination is memory, not video, so the transfer goes through
 * the SNES-only escape; the port's address is its input. */
void menu_clear_memory(GameState *g, Platform *pf, uint16_t at, uint16_t bytes)
{
    vid_enable_line_effects(pf, PF_AT(0xC2951B), 0x00);
    hw_port_seek(g, at, 0);
    const VidSnesTransfer t = { .dest = VID_SNES_WRAM_DATA, .source = MENU_CLEARING_ZERO, .size = bytes,
                                .wram_at = at, .repeats = true };
    vid_snes_copy(pf, PF_AT(0xC29526), &t);
}

// $C2:94D5
/* The rest of that setup: VRAM filled from word 0 with the word at $C2:9510,
 * then three stretches of work RAM cleared with it. */
static void clear_the_screens_memory(GameState *g, Platform *pf)
{
    static const struct { uint16_t at, bytes; } stretches[3] = { { 0x0C00, 0x1200 }, { 0x2E00, 0xD200 }, { 0x0000, 0x0100 } };
    vid_send_vram_fill(pf, PF_AT(0xC294D8), 0x0000, PF_GAME(MENU_CLEARING_ZERO), 0xFFFF, NULL);
    for (int i = 0; i < 3; i++)
        TIMED_AS(g, 0xC29512, menu_clear_memory(g, pf, stretches[i].at, stretches[i].bytes));
}

/* Its entry, where the setup goes on: the caller's flags, direct page and
 * data bank put back after. */
void menu_init_video_memory(Snes *s)
{
    clear_the_screens_memory(state_of(s), m_platform(s));
    m_pull_flags(s);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    m_rts(s);
}

/* The loader's stretches of work RAM are the screen's buffers, never the
 * stack the loader's own return and saved flags are on. */
bool menu_memory_stretch_misses_the_stack(Snes *s)
{
    uint16_t bytes = s->cpu.y, stack = s->cpu.s;
    uint32_t from = s->cpu.x, to = from + (bytes ? bytes : 0x10000u);
    uint32_t low = (uint16_t)(stack - 1), high = (uint32_t)stack + 3;
    return to <= low || from >= high;
}

// $C2:9512
/* Clear Y bytes of work RAM from $7E:X. */
void menu_fill_memory(Snes *s)
{
    menu_clear_memory(state_of(s), m_platform(s), s->cpu.x, s->cpu.y);
    m_rts(s);
}

// $C2:9542
/* Copy Y bytes from $FF:A into work RAM at $7E:X through the work RAM port:
 * memory again, so the SNES-only escape. */
void menu_copy_rom_to_memory(GameState *g, Platform *pf, uint16_t from, uint16_t at, uint16_t bytes)
{
    const VidSnesTransfer t = { .dest = VID_SNES_WRAM_DATA, .source = 0xFF0000u | from, .size = bytes, .wram_at = at };
    hw_port_seek(g, at, 0);
    vid_snes_copy(pf, PF_AT(0xC2954B), &t);
}

void menu_copy_to_memory(Snes *s)
{
    menu_copy_rom_to_memory(state_of(s), m_platform(s), s->cpu.a, s->cpu.x, s->cpu.y);
    m_rts(s);
}

/* ---- The window line table ----------------------------------------------------------------------- */

/* The window line table: 96 lines of one value, then sixteen groups of three
 * one-line entries, the first half at one level and the second at another,
 * each group's value from the ROM, then its end. */
enum { WINDOW_GROUPS = 16, GROUP_LINES = 3, ENTRY_BYTES = 3, FIRST_LEVEL = 0x02, SECOND_LEVEL = 0x82 };

static void put_window_byte(GameState *g, unsigned at, uint8_t v) { set_menu_window_line_table_byte(g, at, v); }

static void build_window_line_table(GameState *g)
{
    enum { HEADER_LINES = 0x60, HEADER_VALUE = 0xE0, FIRST_ENTRY = 3 };
    put_window_byte(g, 0, HEADER_LINES);
    put_window_byte(g, 1, 0);
    put_window_byte(g, 2, HEADER_VALUE);
    put_window_byte(g, 3, 0);
    unsigned end = FIRST_ENTRY + WINDOW_GROUPS * GROUP_LINES * ENTRY_BYTES;
    put_window_byte(g, end, 0);
    put_window_byte(g, end + 1, 0);
    unsigned at = FIRST_ENTRY;
    for (uint8_t group = 0; group < WINDOW_GROUPS; group++) {
        uint8_t level = group < WINDOW_GROUPS / 2 ? FIRST_LEVEL : SECOND_LEVEL;
        for (unsigned line = 0; line < GROUP_LINES; line++, at += ENTRY_BYTES) {
            put_window_byte(g, at, 1);
            put_window_byte(g, at + 1, level);
            put_window_byte(g, at + 2, menu_window_line_value(g, group));
        }
    }
}

// $FF:FC04
/* Build the indirect per-line table slot 1 reads for a window screen, and
 * point the slot at it. */
void menu_put_window_line_table(GameState *g, Platform *pf)
{
    vid_set_line_table(pf, PF_AT(0xFFFC0A), 1, (uint16_t)RAM_ABS(RAM_MENU_WINDOW_LINE_TABLE));
    build_window_line_table(g);
}

/* It leaves its loop's counts in the caller's first three cells: the second
 * level, a run of lines out, and the groups. */
void menu_leave_window_line_table_counts(GameState *g)
{
    set_menu_scratch_window_lines_level(g, SECOND_LEVEL);
    set_menu_scratch_window_lines_out(g, 0);
    set_menu_scratch_window_line_groups(g, WINDOW_GROUPS);
}

void menu_window_build_line_table(Snes *s)
{
    menu_put_window_line_table(state_of(s), m_platform(s));
    m_write8(s, m_dp(s, 0x00), SECOND_LEVEL);
    m_write8(s, m_dp(s, 0x01), 0);
    m_write8(s, m_dp(s, 0x02), WINDOW_GROUPS);
    m_rtl(s);
}

bool menu_window_line_table_is_buildable(Snes *s) { return s->cpu.db == 0x7E; }
