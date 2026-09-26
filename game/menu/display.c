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
#include "cpu_io.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "original_effects.h"
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

/* PLP, narrowing X and Y when the pulled P makes them 8-bit. */
static void plp(Snes *s)
{
    uint8_t p = m_pull8(s);
    s->cpu.p = p;
    if (p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

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

// $C2:800E
/* Entering the menus: the caller's direct page, data bank and flags kept on
 * its stack, the menus' data bank, and the mode and argument the caller
 * entered with kept for the menus to read; then the screen blanked while it is
 * set up. Stops at the stack switch at $C2:8039, which the original carries on
 * with. */
void menu_mode_start(Snes *s)
{
    static const uint32_t sites[4] = { 0xC28025, 0xC2802B, 0xC28031, 0xC28035 };
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_I, true);
    m_set_flag(s, CPU_FLAG_C, false);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.db);
    m_push8(s, s->cpu.p);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    s->cpu.db = 0x7E;
    set_menu_entry_a(g, (uint8_t)s->cpu.a);
    set_menu_entry_x(g, s->cpu.x);
    blank_and_stop(m_platform(s), sites);
    m_stop_at(s, 0xC28039, 0);
}

// $C2:82C1
/* Leaving the menus: the screen blanked, the caller's stack and what the
 * entry kept on it put back. It leaves the stack pointer in X. */
void menu_mode_return(Snes *s)
{
    static const uint32_t sites[4] = { 0xC282C3, 0xC282C9, 0xC282CF, 0xC282D3 };
    blank_and_stop(m_platform(s), sites);
    uint16_t stack = menu_caller_stack(state_of(s));
    s->cpu.x = stack;
    s->cpu.s = stack;
    plp(s);
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
    static const uint32_t sites[4] = { 0xC28331, 0xC28337, 0xC2833D, 0xC28341 };
    blank_and_stop(m_platform(s), sites);
    s->cpu.s = 0x06FF;
    m_stop_at(s, 0xC00000, 0);
}

/* The mode's frame: the screen blanked while it sends what the frame built,
 * unless the main loop hasn't finished one. */
enum { FRAME_SENT = 0xFF, FRAME_READY_BIT = 0x80, LINE_EFFECTS = 0x7F, BLOCK_MOVE_ASKED = 0x80 };

// $C2:842A (the body of the NMI handler at $C2:840E, installed at the $0500
// stub from the table at $C2:8406)
/* The menus' vblank: counts the frame, sends the frame's work ($C2:85D6) with
 * the per-line effects off and turns them back on after, reads the input and
 * the timers ($C2:8473), and sets the BG mode and the brightness (0 is forced
 * blank). Stops at $C2:8466, where the handler starts putting the registers
 * back; the entry at $C2:840E, which saves them and moves the stack, is the
 * original's. */
void menu_nmi_work(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    (void)tm_ack_vblank(pf, PF_AT(0xC2842A));
    set_menu_frame_counter(g, (uint8_t)(menu_frame_counter(g) + 1));
    vid_set_display(pf, PF_AT(0xC28431), &BLANK);
    if (!(menu_vblank_done(g) & FRAME_READY_BIT)) {
        vid_enable_line_effects(pf, PF_AT(0xC2843C), 0x00);
        m_jsr_dc(s, 0xC2843F, 0xC285D6, NOTHING_READ);
        if (!back_as_the_menus_run(s, MENUS_PAGE)) {
            m_stop_dont_care(s, 0xC28442, 0);
            return;
        }
        vid_enable_line_effects(pf, PF_AT(0xC28446), (uint8_t)(menu_line_effects(g) & LINE_EFFECTS));
        set_menu_vblank_done(g, FRAME_SENT);
    }
    m_jsr_dc(s, 0xC2844D, 0xC28473, INPUT_IGNORES);
    if (!back_as_the_menus_run(s, MENUS_PAGE)) {
        m_stop_dont_care(s, 0xC28450, 0);
        return;
    }
    vid_set_mode(pf, PF_AT(0xC28452), &(VidMode){ .mode = 1, .bg3_priority = true });
    uint8_t brightness = menu_brightness(g);
    VidDisplay display = brightness != 0 ? vid_display_decode(brightness) : BLANK;
    vid_set_display(pf, PF_AT(0xC2845B), &display);
    if (menu_wait_frames(g) != 0)
        set_menu_wait_frames(g, (uint8_t)(menu_wait_frames(g) - 1));
    m_stop_at(s, 0xC28466, NOTHING_READ);
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
 * the color math table the frame built (menu_build_math_line_table). Falls
 * through into the VRAM upload queue at $C2:8663, where this port stops; the
 * block move the shadows went out through leaves Y past the register block,
 * and a block move of the frame's past its destination. */
void menu_vblank_upload(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    send_menu_scroll(g, pf);
    send_menu_screen_shadows(g, pf);
    uint16_t y = M_DP_PPU + 0x33;
    vid_send_oam(pf, PF_AT(0xC28607), PF_GAME(RAM_OAM_BUFFER));
    if (menu_line_effects(g) & BLOCK_MOVE_ASKED) {
        set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) & ~BLOCK_MOVE_ASKED));
        y = move_the_block(g);
    }
    NATIVE_TIME_AS(s, 0xC286A9, menu_send_palette(g, pf));
    uint16_t table = (uint16_t)RAM_ABS(RAM_MENU_MATH_LINE_TABLES)
                     + ((menu_math_line_table_offset(g) >> 8) ? RAM_MENU_MATH_LINE_TABLES_STRIDE : 0);
    vid_set_line_table(pf, PF_AT(0xC28660), 0, table);
    s->cpu.y = y;
    s->cpu.db = 0x00;
    m_stop_at(s, 0xC28663, 0);
}

/* The vblank's work runs on the menus' vblank page, with the data bank on the
 * I/O registers; a block move the game asks for moves a buffer, not the code
 * the original runs it through nor the stack it returns on (which the
 * original's move would also read its own return address from). */
bool menu_vblank_work_runs(Snes *s)
{
    enum { VBLANK_PAGE = 0x0D00, STUB = 0x9690, STUB_END = 0x9694, STACK_REACH = 0x10 };
    if (m_direct_page(s) != VBLANK_PAGE || !m_io_bank(s->cpu.db))
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    bool asked = (menu_line_effects(g) & BLOCK_MOVE_ASKED) != 0;
    uint32_t from = menu_block_move_source(g), to = menu_block_move_dest(g), size = menu_block_move_size(g);
    vars_peek(false);
    if (!asked)
        return true;
    /* the move's destination runs round its bank, as a block move does */
    uint32_t bytes = size ? size : 0x10000u;
    for (uint16_t at = STUB; at < STUB_END; at++)
        if ((uint16_t)(at - to) < bytes)
            return false;
    uint16_t stack = s->cpu.s;
    for (uint16_t at = (uint16_t)(stack - STACK_REACH); at != (uint16_t)(stack + STACK_REACH); at++)
        if ((uint16_t)(at - to) < bytes || (uint16_t)(at - from) < bytes)
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
bool menu_vram_queue_is_whole(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    uint8_t bytes = menu_vram_queue_bytes(g);
    bool ok = bytes % MENU_UPLOAD_BYTES == 0 && bytes / MENU_UPLOAD_BYTES <= MENU_VRAM_QUEUE_COUNT;
    for (unsigned i = 0; ok && i < bytes / MENU_UPLOAD_BYTES; i++)
        ok = uploads_from_memory(menu_vram_queue_bank(g, i))
             && uploads_off_the_stack(menu_vram_queue_bank(g, i), menu_vram_queue_source(g, i),
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
 * mode's vblank handler goes on using. Stops at $C2:0024, where the routine
 * moves on to the CPU's memory speed, which has no service, and its own
 * state. */
void menu_object_mode_start(Snes *s)
{
    Platform *pf = m_platform(s);
    m_set_flag(s, CPU_FLAG_I, true);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    tm_enable_interrupts(pf, PF_AT(0xC20016), 0);
    vid_stop_uploads(pf, PF_AT(0xC20019));
    vid_enable_line_effects(pf, PF_AT(0xC2001C), 0);
    vid_set_display(pf, PF_AT(0xC20021), &(VidDisplay){ .blank = true, .brightness = 0x0F });
    m_stop_at(s, 0xC20024, 0);
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

// $C2:0043
/* Set the mode's screen up. Stops at $C2:00F2, where the routine moves on to
 * the CPU registers; the caller's direct page is kept on the stack. */
void menu_object_init_video(Snes *s)
{
    m_push16(s, s->cpu.d);
    vid_apply_screen(m_platform(s), PF_AT(0xC20043), &object_mode_screen);
    m_stop_at(s, 0xC200F2, 0);
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

/* The mode's own work after its frame is sent, each a routine of its own
 * called as the original calls it: the uploads it queued, the frame's
 * routine at $C2:8002, then (after the brightness and the per-line effects)
 * its sprites and objects. */
static bool run_frame_work(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    m_jsr_dc(s, 0xC20318, 0xC20405, NOTHING_READ);
    if (!back_as_the_menus_run(s, OBJECT_MODE_PAGE))
        return false;
    m_jsl_dc(s, 0xC2031B, 0xC28002, DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C);
    if (!back_as_the_menus_run(s, OBJECT_MODE_PAGE))
        return false;
    VidDisplay display = vid_display_decode((uint8_t)menu_object_display(g) | (uint8_t)(menu_object_display(g) >> 8));
    vid_set_display(pf, PF_AT(0xC20323), &display);
    vid_enable_line_effects(pf, PF_AT(0xC20328), menu_object_line_effects(g));
    m_jsr_dc(s, 0xC2032B, 0xC20C4D, NOTHING_READ);
    if (s->cpu.e || m_m8(s) || m_x8(s) || s->cpu.d != OBJECT_MODE_PAGE)
        return false;
    /* the list's reset comes back with a 16-bit accumulator, so the sprites'
     * count is cleared a word wide, over the running object's low byte */
    set_menu_object_sprite_index(g, 0);
    set_menu_object_current(g, (uint16_t)(menu_object_current(g) & 0xFF00));
    /* The objects' handlers may read Y and the carry and overflow flags (the
     * handlers are named in work RAM), which the sprites' hiding leaves as the
     * original does. */
    static const struct { uint32_t site, routine; uint16_t ignores; } work[3] = {
        { 0xC20330, 0xC2034D, NOTHING_READ },
        { 0xC20333, 0xC2051D, DC_A | DC_X | DC_N | DC_Z },
        { 0xC20336, 0xC20CEA, NOTHING_READ },
    };
    for (int i = 0; i < 3; i++) {
        m_jsr_dc(s, work[i].site, work[i].routine, work[i].ignores);
        if (!back_as_the_menus_run(s, OBJECT_MODE_PAGE))
            return false;
    }
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | FRAME_TO_SHOW));
    return true;
}

// $C2:015E (installed at the $0501 NMI stub by $C2:0110)
/* The sprite-object mode's vblank: counts the frame and, when the main loop
 * has finished one, sends it (the sprite table, the palette when it changed,
 * the shadows) and runs the mode's own work. It saves the registers on the
 * stack as an interrupt handler does, and runs on the mode's page with the
 * data bank on low RAM. */
void menu_object_nmi(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
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
    (void)tm_ack_vblank(pf, PF_AT(0xC2016D));
    set_menu_object_frame_counter(g, (uint8_t)(menu_object_frame_counter(g) + 1));
    if (menu_object_frame_state(g) & FRAME_TO_SHOW) {
        send_object_frame(g, pf);
        if (!run_frame_work(s))
            return;
    }
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
    enum { UPLOAD_BUFFER = 0x7EF000 };
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

bool menu_object_queue_is_sendable(Snes *s)
{
    if (m_direct_page(s) != 0 || !m_bank_reaches_low_ram(s))
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    uint8_t end = menu_object_vram_queue_end(g);
    bool ok = menu_object_queue_is_whole(g);
    for (unsigned r = 0; ok && r < end / MENU_OBJECT_UPLOAD_BYTES; r++)
        ok = uploads_from_memory(r < MENU_OBJECT_VRAM_QUEUE_COUNT ? menu_object_vram_queue_bank(g, r)
                                                                   : menu_object_vram_queue_bank_unchecked(g, r));
    vars_peek(false);
    return ok;
}

/* ---- Loading a screen's graphics ------------------------------------------------------------------ */

// $C2:26A8
/* Clear all of VRAM with the word at $C2:26D4, a whole 64 KiB from word 0.
 * It leaves X at 0. */
void menu_clear_vram(Snes *s)
{
    vid_send_vram_fill(m_platform(s), PF_AT(0xC226AB), 0x0000, PF_GAME(0xC226D4), 0x10000, &word_port);
    s->cpu.x = 0x0000;
    m_rts(s);
}

// $C2:2D70
/* The graphics loader's transfer: A bytes from $7F:X to VRAM word Y, a word at
 * a time. A size of 0 is a whole 64 KiB, as the registers read it. */
void menu_upload_vram(Snes *s)
{
    uint32_t size = s->cpu.a;
    vid_send_vram(m_platform(s), PF_AT(0xC22D70), s->cpu.y, PF_GAME(0x7F0000 | s->cpu.x), size ? size : 0x10000,
                  &word_port);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:2D47
/* The palette the graphics loader decompressed to $7F:8C00, sent to CGRAM.
 * Reached by falling out of the loader's last block move; stops at the call to
 * $C2:2D91, which the original carries on with. */
void menu_upload_loaded_palette(Snes *s)
{
    vid_send_palette(m_platform(s), PF_AT(0xC22D4B), 0, PF_GAME(0x7F8C00), 256);
    m_sep(s, CPU_FLAG_M);
    m_stop_at(s, 0xC22D6A, 0);
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

// $C2:940D
/* Set the loader's screen up. Stops at $C2:94B8, where the routine moves on
 * to the CPU registers; the caller's data bank, direct page and flags are kept
 * on the stack for $C2:94D5 to put back. */
void menu_init_video(Snes *s)
{
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.p);
    m_rep(s, CPU_FLAG_X);
    m_sep(s, CPU_FLAG_M);
    s->cpu.db = 0x00;
    vid_apply_screen(m_platform(s), PF_AT(0xC2940D), &loader_screen);
    m_stop_at(s, 0xC294B8, 0);
}

/* A stretch of work RAM cleared with the byte at $C2:9510 through the work
 * RAM port, with the per-line effects off first (the transfer runs on one of
 * their channels). The destination is memory, not video, so the transfer goes through
 * the SNES-only escape; the port's address is its input. */
void menu_clear_memory(GameState *g, Platform *pf, uint16_t at, uint16_t bytes)
{
    vid_enable_line_effects(pf, PF_AT(0xC2951B), 0x00);
    hw_port_seek(g, at, 0);
    const VidSnesTransfer t = { .dest = 0x80, .source = 0xC29510, .size = bytes };
    vid_snes_copy(pf, PF_AT(0xC29526), &t);
}

// $C2:94D5
/* The rest of that setup: VRAM filled from word 0 with the word at $C2:9510,
 * then three stretches of work RAM cleared with it; and the caller's flags,
 * direct page and data bank put back. */
void menu_init_video_memory(Snes *s)
{
    static const struct { uint16_t at, bytes; } stretches[3] = { { 0x0C00, 0x1200 }, { 0x2E00, 0xD200 }, { 0x0000, 0x0100 } };
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    vid_send_vram_fill(pf, PF_AT(0xC294D8), 0x0000, PF_GAME(0xC29510), 0xFFFF, NULL);
    for (int i = 0; i < 3; i++)
        NATIVE_TIME_AS(s, 0xC29512, menu_clear_memory(g, pf, stretches[i].at, stretches[i].bytes));
    plp(s);
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
    const VidSnesTransfer t = { .dest = 0x80, .source = 0xFF0000u | from, .size = bytes };
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
 * point the slot at it. It leaves its loop's counts in the caller's first
 * three cells: the second level, a run of lines out, and the groups. */
void menu_window_build_line_table(Snes *s)
{
    vid_set_line_table(m_platform(s), PF_AT(0xFFFC0A), 1, (uint16_t)RAM_ABS(RAM_MENU_WINDOW_LINE_TABLE));
    build_window_line_table(state_of(s));
    m_write8(s, m_dp(s, 0x00), SECOND_LEVEL);
    m_write8(s, m_dp(s, 0x01), 0);
    m_write8(s, m_dp(s, 0x02), WINDOW_GROUPS);
    m_rtl(s);
}

bool menu_window_line_table_is_buildable(Snes *s) { return s->cpu.db == 0x7E; }
