/* Menu mode: the routines that drive the display.
 *
 * Two modes live in bank C2 and each has its own vblank handler. The mode at
 * $C2:800E (the menus proper, and the load screen every save movie starts in)
 * keeps its display state as shadows in work RAM -- the layers' scroll, the
 * windows, screens and color math, the sprite table, the palette -- and sends
 * them in vblank, with the uploads it has queued and a block move a frame may
 * ask for. The mode at $C2:0000 (the Epoch's screens, the End of Time's gate,
 * the menus' map scenes) keeps the same state in its own cells and sends it
 * the same way, then runs its objects (menu/objects.c).
 *
 * Both set the screen up by a fixed recipe as they start, and a graphics
 * loader shared by the screens fills VRAM, the palette and work RAM for them.
 *
 * Everything reaches the hardware through the video and timing services. */
#include "menu/display.h"

#include "charge.h"
#include "field/main_loop.h"
#include "core/program.h"
#include "core/sprite_table.h"
#include "cpu_io.h"
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
#include "overworld/setup.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* The setting every upload here uses unless it says otherwise: a word at a
 * time, stepping after the high byte. */
static const VidVramPort word_port = { .step = 1, .after_high = true };

/* What a screen shows while it is set up: nothing, at full brightness. */
static const VidDisplay BLANK = { .blank = true };

/* ---- The windows, the screens and color math, as both modes keep them --------------------------- */

/* The sixteen values in the order the console takes them: the window masks, edges and
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

/* The openings the menus' callers ask for as they enter: the screen
 * each opens on is the ROM's (menu_screen_opened_on). One opens on a screen
 * whose only step closes the menus again, for what opening and closing them
 * do to the party's records and the field's sprites. The first entry for the
 * load screen at power-on doesn't open the menus at all (starts_afresh). */
typedef enum {
    MENU_OPEN_LOAD_SCREEN, MENU_OPEN_MAIN_MENU, MENU_OPEN_SHOP, MENU_OPEN_NAMING, MENU_OPEN_ONLY_TO_CLOSE,
    MENU_OPEN_GATE_EXCHANGE, MENU_OPEN_SAVE_POINT,
} MenuOpening;

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

/* The blanks the mode's start, its return and its starting the game again
 * make, each from the original's own sites. */
static const uint32_t start_sites[4] = { 0xC28025, 0xC2802B, 0xC28031, 0xC28035 };
static const uint32_t return_sites[4] = { 0xC282C3, 0xC282C9, 0xC282CF, 0xC282D3 };
static const uint32_t restart_sites[4] = { 0xC28331, 0xC28337, 0xC2833D, 0xC28341 };

/* The mode running, and how it ended: the task closing the menus says, back
 * to their caller or the game started again from its reset. */
struct MenuModeContext {
    MenuModeEnd end;
};

/* Each frame's turns once the vblank handler has sent what the last frame
 * queued, until a task closes the menus or goes no further. */
static void run_the_tasks(GameState *g, Platform *pf, MenuModeContext *mode)
{
    menu_tasks_open(g, pf);
    MenuTurns turns;
    do {
        while (!menu_vblank_done(g))
            tm_wait_frame(pf, PF_AT(0xC281CA));
        TIMED_AS(g, 0xC281CF, turns = menu_tasks_take_the_turns(g));
    } while (turns == MENU_TURNS_TAKEN);
    menu_tasks_end(g);
    if (turns != MENU_TURNS_CLOSED)
        mode->end = MENUS_WENT_NO_FURTHER;
}

// $C2:800E
/* Entering the menus: what the caller asked for kept, and the screen blanked
 * while it is set up. Then the menus open on the screen asked for ($C2:8096)
 * and start their tasks ($C2:8190), which run until a screen closes them --
 * or the game starts afresh from its reset ($C2:832F). */
static void run_the_mode(GameState *g, Platform *pf, uint8_t asked_for, uint16_t asked_with, MenuModeContext *mode)
{
    set_menu_asked_for(g, asked_for);
    set_menu_asked_with(g, asked_with);
    blank_and_stop(pf, start_sites);
    if (starts_afresh(g)) {
        start_a_game_afresh(g, pf);
        TIMED_AS(g, 0xC2832F, blank_and_stop(pf, restart_sites));
        mode->end = MENUS_START_THE_GAME_AGAIN;
        return;
    }
    bool opened = false;
    TIMED_AS(g, 0xC28096, opened = menu_opens(g, pf));
    if (!opened)
        return;
    open_on_the_screen(g, pf);
    run_the_tasks(g, pf, mode);
}

MenuModeEnd menu_mode_runs(GameState *g, Platform *pf, uint8_t asked_for, uint16_t asked_with)
{
    MenuModeContext mode = { MENUS_WENT_NO_FURTHER };
    menu_coroutines(g)->running = &mode;
    run_the_mode(g, pf, asked_for, asked_with, &mode);
    menu_coroutines(g)->running = NULL;
    return mode.end;
}

/* Closing the menus, from whichever screen closes them: every task stopped
 * but the one closing them, which goes on; what the party wears shown on the
 * field's sprites again; and the screen faded out. Then either back to the
 * caller or, from the load screen, on to the game, which starts again from
 * its reset with the game loaded. The menus' mode returns to its caller, the
 * program, saying how they closed, and the closing task never goes on. */
enum { EVERY_TASK = 0xFF };

// $C2:82B2, $C2:8320
void menu_close(GameState *g, Platform *pf, bool for_the_game)
{
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, EVERY_TASK));
    TIMED_AS(g, 0xC2834D, party_wear_it_shows(g));
    TIMED_AS(g, 0xC283CA, menu_fade_to_black(g));
    if (for_the_game)
        TIMED_AS(g, 0xC2832F, blank_and_stop(pf, restart_sites));
    else
        TIMED_AS(g, 0xC282C1, blank_and_stop(pf, return_sites));
    MenuModeContext *running = menu_coroutines(g)->running;
    if (running)
        running->end = for_the_game ? MENUS_START_THE_GAME_AGAIN : MENUS_CLOSED;
    menu_tasks_closed(g);
}

/* The mode's frame: the screen blanked while it sends what the frame built,
 * unless the main loop hasn't finished one. */
enum { FRAME_SENT = 0xFF, FRAME_READY_BIT = 0x80, LINE_EFFECTS = 0x7F, BLOCK_MOVE_ASKED = 0x80 };

void menu_ask_for_the_block_move(GameState *g) { set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | BLOCK_MOVE_ASKED)); }

/* ---- The menus' vblank handler ------------------------------------------------------------------ */

static void send_the_frame(GameState *g, Platform *pf);

/* The frame's work and the upload queue, each sent when it holds what the
 * port can send; the program goes no further otherwise. */
static bool frame_sent(GameState *g, Platform *pf)
{
    if (!menu_block_move_is_a_buffer(g)) {
        program_breaks_a_contract(g, 0xC285D6, "a block move over the code the menus' vblank runs it through");
        return false;
    }
    TIMED_AS(g, 0xC285D6, (void)send_the_frame(g, pf));
    return menu_upload_queue_sent(g, pf);
}

// $C2:842A
/* The menus' vblank: counts the frame, sends the frame's work ($C2:85D6) with
 * the per-line effects off and turns them back on after, reads the input and
 * the timers ($C2:8473), and sets the BG mode and the brightness (0 is forced
 * blank). Each of the two calls is false where it went no further. */
static void menus_vblank(GameState *g, Platform *pf)
{
    (void)tm_ack_vblank(pf, PF_AT(0xC2842A));
    set_menu_frame_counter(g, (uint8_t)(menu_frame_counter(g) + 1));
    vid_set_display(pf, PF_AT(0xC28431), &BLANK);
    if (!(menu_vblank_done(g) & FRAME_READY_BIT)) {
        vid_enable_line_effects(pf, PF_AT(0xC2843C), 0x00);
        if (!frame_sent(g, pf))
            return;
        vid_enable_line_effects(pf, PF_AT(0xC28446), (uint8_t)(menu_line_effects(g) & LINE_EFFECTS));
        set_menu_vblank_done(g, FRAME_SENT);
    }
    bool taken = false;
    TIMED_AS(g, 0xC28473, taken = menu_frame_input_taken(g, pf));
    if (!taken)
        return;
    vid_set_mode(pf, PF_AT(0xC28452), &(VidMode){ .mode = 1, .bg3_priority = true });
    uint8_t brightness = menu_brightness(g);
    VidDisplay display = brightness != 0 ? vid_display_decode(brightness) : BLANK;
    vid_set_display(pf, PF_AT(0xC2845B), &display);
    if (menu_wait_frames(g) != 0)
        set_menu_wait_frames(g, (uint8_t)(menu_wait_frames(g) - 1));
}

void menu_vblank(GameState *g, Platform *pf) { TIMED_AS(g, 0xC2842A, menus_vblank(g, pf)); }

/* The menus' line handler returns at once. */
void menu_line(GameState *g, Platform *pf) { (void)g, (void)pf; }

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
}

static void send_menu_screen_shadows(GameState *g, Platform *pf)
{
    uint32_t masks = menu_window_shadows_window_masks(g);
    uint16_t window1 = menu_window_shadows_window1_edges(g), window2 = menu_window_shadows_window2_edges(g);
    uint16_t logic = menu_window_shadows_window_logic(g);
    ScreenShadows v = {
        { (uint8_t)masks, (uint8_t)(masks >> 8), (uint8_t)(masks >> 16) },
        { (uint8_t)window1, (uint8_t)(window1 >> 8), (uint8_t)window2, (uint8_t)(window2 >> 8) },
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

/* The block move a frame asks for, within work RAM's first bank. */
static void move_the_block(GameState *g)
{
    uint16_t from = menu_block_move_source(g), to = menu_block_move_dest(g), size = menu_block_move_size(g);
    uint32_t bytes = size ? size : 0x10000u;
    wram_copy(g, 0x7E0000u | from, 0x7E0000u | to, bytes);
    charge_block_move(g, bytes);
}

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
 * through into the VRAM upload queue at $C2:8663. */
static void send_the_frame(GameState *g, Platform *pf)
{
    send_menu_scroll(g, pf);
    send_menu_screen_shadows(g, pf);
    vid_send_oam(pf, PF_AT(0xC28607), PF_GAME(RAM_OAM_BUFFER), sprite_table_places(g));
    if (menu_line_effects(g) & BLOCK_MOVE_ASKED) {
        set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) & ~BLOCK_MOVE_ASKED));
        move_the_block(g);
    }
    TIMED_AS(g, 0xC286A9, menu_send_palette(g, pf));
    uint16_t table = (uint16_t)RAM_ABS(RAM_MENU_MATH_LINE_TABLES)
                     + ((menu_math_line_table_offset(g) >> 8) ? RAM_MENU_MATH_LINE_TABLES_STRIDE : 0);
    vid_set_line_table(pf, PF_AT(0xC28660), 0, table);
}

/* A block move the game asks for moves a buffer, never the four bytes in low
 * RAM the original runs the move through. */
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

// $C2:8663
/* The queued VRAM uploads, the last queued first, which empties the queue.
 * A size of 0 is a whole 64 KiB. */
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
                      menu_upload_data((uint32_t)menu_vram_queue_bank(g, i) << 16 | menu_vram_queue_source(g, i), size ? size : 0x10000u),
                      size ? size : 0x10000u, NULL);
    }
}

bool menu_upload_queue_sent(GameState *g, Platform *pf)
{
    if (!menu_upload_queue_is_whole(g)) {
        program_breaks_a_contract(g, 0xC28663, "an upload queue the port doesn't send");
        return false;
    }
    TIMED_AS(g, 0xC28663, menu_send_upload_queue(g, pf));
    return true;
}

/* What the game uploads comes from work RAM's banks or the ROM's, never
 * through the banks that mirror the console's I/O, where what a transfer
 * reads depends on when it reads it. */
static bool uploads_from_memory(uint8_t bank) { return bank == 0x7E || bank == 0x7F || (bank & 0x7F) >= 0x40; }

/* The queue's entries are whole, no more than it holds, and from memory. */
bool menu_upload_queue_is_whole(const GameState *g)
{
    uint8_t bytes = menu_vram_queue_bytes(g);
    bool ok = bytes % MENU_UPLOAD_BYTES == 0 && bytes / MENU_UPLOAD_BYTES <= MENU_VRAM_QUEUE_COUNT;
    for (unsigned i = 0; ok && i < bytes / MENU_UPLOAD_BYTES; i++)
        ok = uploads_from_memory(menu_vram_queue_bank(g, i));
    return ok;
}

// $C2:86A9

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
    charge_block_move(g, 2 * MENU_MATH_RAMP_COLORS_COUNT);
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

/* ---- The mode at $C2:0000 ------------------------------------------------------------------------- */

// $C2:000F

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

// $C2:0043
/* Set the mode's screen up, and the console's own state as the mode starts
 * it: the controller port's lines released, the arithmetic unit
 * cleared, the timers back at the frame's start. */
void menu_object_set_up_the_screen(GameState *g, Platform *pf)
{
    (void)g; /* Shared mode-operation signature. */
    vid_apply_screen(pf, PF_AT(0xC20043), &object_mode_screen);
    in_set_io_lines(pf, PF_AT(0xC200F8), IO_LINES_RELEASED);
    tm_reset_timers(pf, PF_AT(0xC2010C));
}

/* The mode's own state starts from nothing: its scratch cleared, the
 * screen off, and the display it sends each frame as the screen starts --
 * mode 1 with BG3 in front, the Mode 7 plane at its identity, both windows
 * shut (each opening a pixel past where it closes), BG1-BG3 and the sprites
 * on the main screen, BG1, BG2 and the sprites on the sub screen. */
enum { MODE_PAGE_BYTES = 0xF0, SCREEN_OFF = 0x80, MODE_1_BG3_IN_FRONT = 0x09 };
enum { MATRIX_A_HIGH = 1, MATRIX_D_HIGH = 7, IDENTITY_HIGH = 0x01 };
enum { WINDOW_1_LEFT = 0, WINDOW_2_LEFT = 2, A_PIXEL_PAST_SHUT = 0x01 };
enum { MAIN_LAYERS = 0x17, SUB_LAYERS = 0x13 };

void menu_object_reset_the_mode(GameState *g)
{
    wram_fill8(g, RAM_MENU_OBJECT_SCRATCH, MODE_PAGE_BYTES, 0);
    charge_block_move(g, MODE_PAGE_BYTES - 1);
    set_menu_object_display_blank(g, SCREEN_OFF);
    set_menu_object_shadows_bg_mode(g, MODE_1_BG3_IN_FRONT);
    set_menu_object_shadows_mode7_matrix_byte(g, MATRIX_A_HIGH, IDENTITY_HIGH);
    set_menu_object_shadows_mode7_matrix_byte(g, MATRIX_D_HIGH, IDENTITY_HIGH);
    set_menu_object_shadows_window_edges_byte(g, WINDOW_1_LEFT, A_PIXEL_PAST_SHUT);
    set_menu_object_shadows_window_edges_byte(g, WINDOW_2_LEFT, A_PIXEL_PAST_SHUT);
    set_menu_object_shadows_main_screen(g, MAIN_LAYERS);
    set_menu_object_shadows_sub_screen(g, SUB_LAYERS);
}

/* The mode's frame is its own vblank handler's; it has no line handler, so
 * the line interrupt (which it never enables) only returns. */

void menu_object_take_the_interrupts(GameState *g)
{
    program_install_vblank_handler(g, menu_object_vblank);
    charge_accesses(g, 4);
    program_install_line_handler(g, menu_object_line);
    charge_accesses(g, 4);
}

/* Where the uploads the mode builds are put together, in work RAM. */
enum { UPLOAD_BUFFER = 0x7EF000 };

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

// $C2:03EF

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
        { menu_object_shadows_window_masks_byte(g, 0), menu_object_shadows_window_masks_byte(g, 1),
          menu_object_shadows_window_masks_byte(g, 2) },
        { menu_object_shadows_window_edges_byte(g, 0), menu_object_shadows_window_edges_byte(g, 1),
          menu_object_shadows_window_edges_byte(g, 2), menu_object_shadows_window_edges_byte(g, 3) },
        { menu_object_shadows_window_logic_byte(g, 0), menu_object_shadows_window_logic_byte(g, 1) },
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
    vid_send_oam(pf, PF_AT(0xC20187), PF_GAME(RAM_ABS(RAM_OAM_BUFFER)), sprite_table_places(g));
    if (menu_object_frame_state(g) & PALETTE_TO_SEND)
        vid_send_palette(pf, PF_AT(0xC201AD), 0, PF_GAME(RAM_ABS(RAM_MENU_OBJECT_PALETTE)), 256);
    set_menu_object_frame_state(g, 0);
    send_object_shadows(g, pf);
    place_line_tables(g);
}

/* The mode's own work after its frame is sent, each a routine of its own,
 * each false where it went no further:
 * the uploads it queued, the frame's input (the controller, the button
 * assignments and the play time, $C2:8002), and around the objects' turns
 * the sprite list emptied, the sprites hidden and the list drawn. */
typedef enum {
    SEND_THE_QUEUE, TAKE_THE_INPUT, EMPTY_THE_SPRITE_LIST, HIDE_THE_SPRITES, RUN_THE_OBJECTS, DRAW_THE_SPRITE_LIST,
    OBJECT_FRAME_STEPS,
} ObjectFrameStep;
static bool object_frame_step(GameState *g, Platform *pf, ObjectFrameStep step);

// $C2:015E
/* The sprite-object mode's vblank: counts the frame and, when the main loop
 * has finished one, sends it (the sprite table, the palette when it changed,
 * the shadows) and does the mode's own work: the uploads and the input, the
 * brightness and the per-line effects the frame left, and the objects'
 * turns, each showing its sprites on the list the frame starts empty, drawn
 * into the sprite table after them. */
static bool object_mode_vblank(GameState *g, Platform *pf)
{
    (void)tm_ack_vblank(pf, PF_AT(0xC2016D));
    set_menu_object_frame_counter(g, (uint8_t)(menu_object_frame_counter(g) + 1));
    if (!(menu_object_frame_state(g) & FRAME_TO_SHOW))
        return true;
    send_object_frame(g, pf);
    if (!object_frame_step(g, pf, SEND_THE_QUEUE) || !object_frame_step(g, pf, TAKE_THE_INPUT))
        return false;
    VidDisplay display = vid_display_decode(menu_screen_display(g));
    vid_set_display(pf, PF_AT(0xC20323), &display);
    vid_enable_line_effects(pf, PF_AT(0xC20328), menu_object_line_effects(g));
    if (!object_frame_step(g, pf, EMPTY_THE_SPRITE_LIST))
        return false;
    /* the sprites' count is cleared a word wide, as the original clears it,
     * over the running object's low byte */
    set_menu_object_sprite_index(g, 0);
    set_menu_object_current(g, (uint16_t)(menu_object_current(g) & 0xFF00));
    for (ObjectFrameStep step = HIDE_THE_SPRITES; step < OBJECT_FRAME_STEPS; step++)
        if (!object_frame_step(g, pf, step))
            return false;
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | FRAME_TO_SHOW));
    return true;
}

static void send_object_queue(GameState *g, Platform *pf);

/* Each step, when what it works on is one the port handles; the program goes
 * no further otherwise. */
static bool object_frame_step(GameState *g, Platform *pf, ObjectFrameStep step)
{
    bool done = true;
    switch (step) {
    case SEND_THE_QUEUE:
        if (!menu_object_queue_can_be_sent(g)) {
            program_breaks_a_contract(g, 0xC20405, "an upload queue the port doesn't send");
            return false;
        }
        TIMED_AS(g, 0xC20405, send_object_queue(g, pf));
        return true;
    case TAKE_THE_INPUT:
        TIMED_AS(g, 0xC28002, done = input_take_frame(g, pf));
        if (!done)
            program_breaks_a_contract(g, 0xC28002, "the controller not read, or the reset buttons held");
        return done;
    case EMPTY_THE_SPRITE_LIST:
        if (!menu_sprite_list_can_be_emptied(g)) {
            program_breaks_a_contract(g, 0xC20C4D, "a sprite list whose links the mode's start didn't lay out");
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
            program_breaks_a_contract(g, 0xC20CEA, "a sprite list the port doesn't draw");
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
    TIMED_AS(g, MENU_OBJECT_VBLANK_HANDLER, (void)object_mode_vblank(g, pf));
}

/* The mode has no line handler: the line interrupt only returns. */
void menu_object_line(GameState *g, Platform *pf) { (void)g, (void)pf; }

/* ---- The mode's upload queue -------------------------------------------------------------------- */

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
        vid_send_vram(pf, PF_AT(0xC20417), vram, menu_upload_data((uint32_t)bank << 16 | source, size ? size : 0x10000u), size ? size : 0x10000u, &port);
        at = (uint8_t)(at + MENU_OBJECT_UPLOAD_BYTES);
    } while (at != end);
    set_menu_object_vram_queue_end(g, 0);
    set_menu_object_upload_buffer(g, UPLOAD_BUFFER);
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

/* ---- Loading a screen's graphics ------------------------------------------------------------------ */

// $C2:26A8
/* Clear all 64 KiB of VRAM, starting at word 0. */
void menu_clear_all_of_vram(Platform *pf)
{
    vid_send_vram_fill(pf, PF_AT(0xC226AB), 0x0000, PF_VALUE(0), 0x10000, &word_port);
}

// $C2:2D70
/* The graphics loader's transfer: `bytes` from bank $7F at `from` to VRAM
 * word `vram`, a word at a time. A size of 0 is a whole 64 KiB. */
void menu_send_loaded_graphics(Platform *pf, uint16_t vram, uint16_t from, uint16_t bytes)
{
    vid_send_vram(pf, PF_AT(0xC22D70), vram, PF_GAME(0x7F0000 | from), bytes ? bytes : 0x10000, &word_port);
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

/* A stretch of work RAM cleared through the work RAM port (from `at` in
 * bank $7E, running on into $7F), with the per-line effects off first: the
 * original's transfer runs on one of their channels. */
// $C2:9512
void menu_clear_memory(GameState *g, Platform *pf, uint16_t at, uint16_t bytes)
{
    uint32_t count = bytes ? bytes : 0x10000u;
    vid_enable_line_effects(pf, PF_AT(0xC2951B), 0x00);
    hw_port_seek(g, at, 0);
    wram_port_fill(g, at, count, 0);
    tm_charge_transfer(pf, PF_AT(0xC29526), count);
}

// $C2:94D5
/* Clear VRAM from word 0, then the screen's three work-RAM buffers. */
static void clear_the_screens_memory(GameState *g, Platform *pf)
{
    static const struct { uint16_t at, bytes; } stretches[3] = { { 0x0C00, 0x1200 }, { 0x2E00, 0xD200 }, { 0x0000, 0x0100 } };
    vid_send_vram_fill(pf, PF_AT(0xC294D8), 0x0000, PF_VALUE(0), 0xFFFF, NULL);
    for (int i = 0; i < 3; i++)
        TIMED_AS(g, 0xC29512, menu_clear_memory(g, pf, stretches[i].at, stretches[i].bytes));
}

// $C2:9542
/* Copy `bytes` of the menus' screen graphics, from `from` in their bank,
 * into work RAM at `at` through the work RAM port. */
void menu_copy_rom_to_memory(GameState *g, Platform *pf, uint16_t from, uint16_t at, uint16_t bytes)
{
    uint32_t count = bytes ? bytes : 0x10000u;
    hw_port_seek(g, at, 0);
    wram_port_copy(g, menu_screen_graphics_data(from, count), at, count);
    tm_charge_transfer(pf, PF_AT(0xC2954B), count);
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
