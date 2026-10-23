/* The time warp: the tunnel the party falls through between eras.
 *
 * The field's event command $FF with a scene of $80-$83 ends the field for it
 * (event/screen.c); the warp is a mode of its own, with its own vblank handler,
 * and when it is over it hands back to the field's entry, which loads the
 * location the event set. Its four kinds differ in how long they last and in
 * whether the picture flickers (WarpKind).
 *
 * It starts with the screen dark (warp_set_up): the vblank handler installed,
 * the story flags kept aside (the plane's map is built over them), the picture
 * drawn (warp/picture.c), both of the circle's window tables built empty
 * (warp/circle.c) and the line effects started; then the screen comes on, dark
 * until the vblank handler brightens it. Each frame the main loop waits for the
 * vblank handler to take the frame it finished (warp_wait_for_frame), then
 * makes the next one (warp_frame): the plane moved on, its colours turned and
 * pulsed, and the circle built for where the warp is in its course:
 * - opening, its first 32 frames: the tunnel seen only inside a circle that
 *   grows from a dot;
 * - running: the tunnel whole;
 * - closing, from 96 frames before its end: a circle that grows from a dot
 *   with the tunnel gone inside it;
 * - gone, its last 64 frames: the light alone.
 * The vblank handler (warp_vblank) sends the palette and the sprites, shows
 * the circle's table built last and counts the frame. On its last frame the
 * warp goes dark, puts the story flags back and starts the field again. */
#include "warp/warp.h"
#include "warp/warp_data.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- The work cells ------------------------------------------------------------------------------ */

void warp_leave_work_byte(GameState *g, unsigned cell, uint8_t v) { set_warp_work(g, cell, v); }

void warp_leave_work_word(GameState *g, unsigned cell, uint16_t v)
{
    set_warp_work(g, cell, (uint8_t)v);
    set_warp_work(g, cell + 1, (uint8_t)(v >> 8));
}

/* ---- The kinds of warp --------------------------------------------------------------------------- */

typedef enum { WARP_LONG, WARP_SHORT, WARP_SHORTEST, WARP_FLICKERING } WarpKindId;

/* A kind of warp: its last frame, whether its brightness flickers, and, for
 * the original's machine, where its frame starts (the entry its time is
 * charged at) and where it sets the circle's window. */
typedef struct {
    uint16_t last_frame;
    bool flickers;
    uint32_t frame_entry;
    uint32_t window_site;
} WarpKind;

static const WarpKind kinds[] = {
    [WARP_LONG] = { 0x2FF, false, 0xFDDA06, 0xFDDA42 },
    [WARP_SHORT] = { 0x0FF, false, 0xFDDA65, 0xFDDAA1 },
    [WARP_SHORTEST] = { 0x09F, false, 0xFDDAC4, 0xFDDB00 },
    [WARP_FLICKERING] = { 0x2FF, true, 0xFDDB27, 0xFDDB7A },
};

/* ---- The story flags ----------------------------------------------------------------------------- */

enum { STORY_FLAG_BYTES = 0x200 };

static void keep_the_story_flags(GameState *g)
{
    for (unsigned at = 0; at < STORY_FLAG_BYTES; at++)
        set_warp_kept_event_memory_byte(g, at, event_memory_byte(g, at));
}

static void put_back_the_story_flags(GameState *g)
{
    for (unsigned at = 0; at < STORY_FLAG_BYTES; at++)
        set_event_memory_byte(g, at, warp_kept_event_memory_byte(g, at));
}

/* ---- The start ----------------------------------------------------------------------------------- */

enum { WARP_VBLANK_HANDLER = 0xFDDE15, WARP_LINE_HANDLER = 0xFDDE87, FULL_BRIGHTNESS = 15 };

// $FD:DB97 (up to its wait for the vblank, $FD:EADA)
/* Everything stopped and the screen dark, the warp's handlers installed, the
 * picture drawn and the line effects started. The circle's tables are both
 * built empty, or meant to be: the count of tables built is cleared and stepped
 * between the two builds, which puts both on the second table. */
static void start_the_warp(GameState *g, Platform *pf)
{
    vid_stop_uploads(pf, PF_AT(0xFDDBA5));
    vid_enable_line_effects(pf, PF_AT(0xFDDBA8), 0);
    vid_set_display(pf, PF_AT(0xFDDBAD), &(VidDisplay){ .brightness = FULL_BRIGHTNESS, .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xFDDBB0), 0);
    set_warp_zero(g, 0);
    program_install_vblank_handler(g, WARP_VBLANK_HANDLER);
    program_install_line_handler(g, WARP_LINE_HANDLER);
    keep_the_story_flags(g);
    warp_draw_picture(g, pf);
    set_warp_circle_tables_built(g, 0);
    set_warp_circle_radius(g, 0);
    warp_build_circle(g);
    set_warp_circle_tables_built(g, (uint8_t)(warp_circle_tables_built(g) + 1));
    warp_build_circle(g);
    warp_start_line_effects(g, pf);
}

/* ---- A frame ------------------------------------------------------------------------------------- */

typedef enum { WARP_OPENING, WARP_RUNNING, WARP_CLOSING, WARP_GONE } WarpPhase;

/* The warp closes over its last 96 frames: a circle for 32, then the light
 * alone for 64. */
enum { CLOSING_FRAMES = 0x60, GONE_FRAMES = 0x40 };

static WarpPhase phase_of(const WarpKind *k, uint16_t frame)
{
    uint16_t end = (uint16_t)(k->last_frame + 1);
    if (frame < WARP_CIRCLE_FRAMES)
        return WARP_OPENING;
    if (frame < end - CLOSING_FRAMES)
        return WARP_RUNNING;
    if (frame < end - GONE_FRAMES)
        return WARP_CLOSING;
    return WARP_GONE;
}

/* Where the tunnel shows in each phase: inside the circle as it opens, outside
 * it as it closes, everywhere while it runs, and nowhere once it has gone
 * (window 2, which spans the screen). */
static void show_the_tunnel(Platform *pf, const WarpKind *k, WarpPhase phase)
{
    VidWindows w = { 0 };
    if (phase == WARP_OPENING)
        w.w1[VID_BG1] = VID_WIN_OUT;
    else if (phase == WARP_CLOSING)
        w.w1[VID_BG1] = VID_WIN_IN;
    else if (phase == WARP_GONE)
        w.w2[VID_BG1] = VID_WIN_IN;
    vid_set_windows(pf, PF_AT(k->window_site), &w, VID_WINDOWS_BG1_BG2);
}

/* The flickering warp's brightness steps on through its pattern, a little
 * over half a step a frame. The pattern's place starts at 0, to which it comes
 * back only after 1024 frames, longer than a warp lasts. On the first frame,
 * the original reads the brightness from far past the pattern: its index takes
 * A's high byte along, which every frame leaves clear but the start leaves
 * holding the bottom line's scale (leave_the_last_scale_in_a, below). Kept as
 * the original's index arithmetic: it is what the first frame shows. */
enum { FLICKER_SPEED = 0x98, FLICKER_STEPS_MASK = (WARP_FLICKER_STEPS << 8) - 1, FLICKER_START = 0 };
enum { BOTTOM_TABLE_LINE = 255 };

static void flicker(GameState *g, Platform *pf)
{
    bool first = warp_flicker(g) == FLICKER_START;
    uint8_t past = first ? (uint8_t)warp_plane_matrix_second_row_scale(g, BOTTOM_TABLE_LINE) : 0;
    uint16_t at = (uint16_t)((warp_flicker(g) + FLICKER_SPEED) & FLICKER_STEPS_MASK);
    set_warp_flicker(g, at);
    VidDisplay d = vid_display_decode(warp_flicker_brightness(g, (uint8_t)(at >> 8), past));
    vid_set_display(pf, PF_AT(0xFDDB3B), &d);
}

/* The plane runs down through the top half of the picture and up through the
 * bottom half, four lines a frame, round its 512. Its centre stays in the
 * middle across and follows it down. */
enum { PLANE_SPEED = 4, PLANE_LINES = 0x200, CENTRE_ACROSS = 0x80, CENTRE_DOWN = 0x70, TOP = 0, BOTTOM = 1 };

static void move_the_plane(GameState *g, uint8_t frame)
{
    uint16_t top = (uint16_t)(frame * PLANE_SPEED % PLANE_LINES);
    uint16_t bottom = (uint16_t)((uint16_t)((uint8_t)~frame * PLANE_SPEED - CENTRE_DOWN) % PLANE_LINES);
    set_warp_plane_centres_x(g, TOP, CENTRE_ACROSS);
    set_warp_plane_centres_y(g, TOP, (uint16_t)(top + CENTRE_DOWN));
    set_warp_plane_centres_x(g, BOTTOM, CENTRE_ACROSS);
    set_warp_plane_centres_y(g, BOTTOM, (uint16_t)(bottom + CENTRE_DOWN));
    set_warp_plane_scrolls_across(g, TOP, 0);
    set_warp_plane_scrolls_down(g, TOP, top);
    set_warp_plane_scrolls_across(g, BOTTOM, 0);
    set_warp_plane_scrolls_down(g, BOTTOM, bottom);
}

/* The light's first sprite palette turns two colours a frame through its
 * fifteen, so its bands stream outwards. */
enum { TURNING_FIRST = 129, TURNING_COLOURS = 15, TURN = 2 };
enum { WORK_TURNED_FIRST = 2, WORK_TURNED_SECOND = 4 };

static void turn_the_light(GameState *g)
{
    uint16_t taken[TURN];
    for (unsigned i = 0; i < TURN; i++)
        taken[i] = palette_base(g, TURNING_FIRST + i);
    for (unsigned i = 0; i < TURNING_COLOURS - TURN; i++)
        set_palette_base(g, TURNING_FIRST + i, palette_base(g, TURNING_FIRST + TURN + i));
    for (unsigned i = 0; i < TURN; i++)
        set_palette_base(g, TURNING_FIRST + TURNING_COLOURS - TURN + i, taken[i]);
    ORIGINAL_EFFECT(warp_leave_work_word(g, WORK_TURNED_FIRST, taken[0]));
    ORIGINAL_EFFECT(warp_leave_work_word(g, WORK_TURNED_SECOND, taken[1]));
}

/* Colours 16-47 of the tunnel pulse: their red and green swing with the sine
 * of the frame, the green half a turn behind the red, each colour as far as
 * its reach; their blue stays. */
enum { FIRST_PULSING = 16, BLUE_BITS = 0x7C00, GREEN_SHIFT = 5, WORK_PULSED = 4 };

static uint8_t pulse(uint8_t swing, uint8_t reach) { return (uint8_t)(swing * reach >> 8); }

static void pulse_the_colours(GameState *g, uint8_t frame)
{
    uint8_t red_swing = (uint8_t)(warp_sine(g, frame) + 128);
    uint8_t green_swing = (uint8_t)(warp_sine(g, (uint8_t)~frame) + 128);
    uint16_t made = 0;
    uint8_t reach = 0;
    for (unsigned i = 0; i < WARP_PULSING_COLOURS; i++) {
        reach = warp_pulse_reach(g, i);
        made = (uint16_t)(pulse(green_swing, reach) << GREEN_SHIFT | pulse(red_swing, reach));
        unsigned colour = FIRST_PULSING + i;
        set_palette_base(g, colour, (uint16_t)((palette_base(g, colour) & BLUE_BITS) | made));
    }
    ORIGINAL_EFFECT(warp_leave_work_word(g, WORK_PULSED, made));
    ORIGINAL_EFFECT(hw_set_multiplicand(g, green_swing));
    ORIGINAL_EFFECT((void)hw_multiply_by(g, reach));
}

/* The original steps the circle's point with the multiplier and its waits,
 * where the port steps it in locals: a step takes the original about as long
 * as eight of the frame's own accesses at the frame's rate, measured over the
 * three movies the warp plays in, which is what a frame charges for each
 * (charge.h). */
enum { ACCESSES_A_CIRCLE_STEP = 8 };

/* One frame of the warp: true while it goes on, false on its last frame. */
static bool make_a_frame(GameState *g, Platform *pf, const WarpKind *k)
{
    if (k->flickers)
        flicker(g, pf);
    uint16_t frame = warp_frame(g);
    move_the_plane(g, (uint8_t)frame);
    turn_the_light(g);
    WarpPhase phase = phase_of(k, frame);
    bool circle = phase == WARP_OPENING || phase == WARP_CLOSING;
    set_warp_circle_radius(g, circle ? warp_circle_radius_on(g, (uint8_t)frame) : 0);
    show_the_tunnel(pf, k, phase);
    uint16_t circle_steps = warp_build_circle(g);
    ORIGINAL_EFFECT(charge_as(g, k->frame_entry, (uint32_t)circle_steps * ACCESSES_A_CIRCLE_STEP));
    pulse_the_colours(g, (uint8_t)frame);
    return frame != k->last_frame;
}

/* ---- The end ------------------------------------------------------------------------------------- */

/* The screen dark and everything stopped. */
static void go_dark(Platform *pf)
{
    vid_stop_uploads(pf, PF_AT(0xFDEAC8));
    vid_enable_line_effects(pf, PF_AT(0xFDEACB), 0);
    vid_set_display(pf, PF_AT(0xFDEAD0), &(VidDisplay){ .blank = true });
    tm_enable_interrupts(pf, PF_AT(0xFDEAD5), 0);
}

/* ---- The vblank ---------------------------------------------------------------------------------- */

/* The line effects the warp runs, slots 1-5: the plane's matrix in two, its
 * centre, its scroll and the circle. */
enum { WARP_LINE_SLOTS = 0x3E, PALETTE_COLOURS = 256 };

// $FD:DE15
/* The frame taken: with the picture blanked, the palette and the sprites sent;
 * the circle's table built last shown; the frame counted, and the picture back
 * on at full brightness with its line effects. */
static void take_the_frame(GameState *g, Platform *pf)
{
    (void)tm_ack_vblank(pf, PF_AT(0xFDDE28));
    vid_stop_uploads(pf, PF_AT(0xFDDE2B));
    vid_enable_line_effects(pf, PF_AT(0xFDDE2E), 0);
    vid_set_display(pf, PF_AT(0xFDDE33), &(VidDisplay){ .blank = true });
    vid_send_palette(pf, PF_AT(0xFDF266), 0, PF_GAME(RAM_PALETTE_BASE), PALETTE_COLOURS);
    vid_send_oam(pf, PF_AT(0xFDF222), PF_GAME((uint16_t)RAM_OAM_BUFFER));
    warp_show_circle_table(g, warp_circle_tables_built(g) % WARP_CIRCLE_TABLES);
    set_warp_frame(g, (uint16_t)(warp_frame(g) + 1));
    set_warp_frame_ended(g, (uint8_t)(warp_frame_ended(g) + 1));
    vid_enable_line_effects(pf, PF_AT(0xFDDE77), WARP_LINE_SLOTS);
    vid_set_display(pf, PF_AT(0xFDDE7C), &(VidDisplay){ .brightness = FULL_BRIGHTNESS });
}

/* ---- The entries --------------------------------------------------------------------------------- */

enum { WARP_PAGE = 0x0500, SCREEN_ON = 0xFDEADA, THE_SCREEN_ON_CALL = 0xFDDCB5 };
enum { JSR_SIZE = 3 };

/* What the original's code after each of these reads of the registers: none
 * of them at the main loop's wait, at its end or at the field's entry. */
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool came_back_on_the_warp_page(const Snes *s)
{
    return !s->cpu.e && m_m8(s) && !m_x8(s) && m_direct_page(s) == WARP_PAGE && s->cpu.db == 0;
}

/* The start's last write through the CPU's port to work RAM is the bottom
 * line's scale, a byte at a time, which leaves the scale's low byte in A's
 * high byte; the first frame's plane reads it (and shifts it out of what it
 * makes, and the flickering warp's brightness, flicker). */
static void leave_the_last_scale_in_a(Snes *s)
{
    uint16_t scale = warp_plane_matrix_second_row_scale(state_of(s), BOTTOM_TABLE_LINE);
    s->cpu.a = (uint16_t)(scale << 8 | (uint8_t)s->cpu.a);
}

// $FD:DB97
/* The warp's start, on its own page and with the data bank $00, the CPU's
 * interrupts masked. The wait for the vblank that turns the screen on is the
 * original's ($FD:EADA, below), called as the timing service's; the frame
 * count starts after it. */
void warp_set_up(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    m_set_flag(s, CPU_FLAG_I, true);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    s->cpu.d = WARP_PAGE;
    s->cpu.db = 0x00;
    start_the_warp(g, pf);
    tm_wait_frame_then_enable(pf, PF_CALL_DC(THE_SCREEN_ON_CALL, SCREEN_ON, READS_NOTHING),
                              TM_INT_VBLANK | TM_INT_AUTO_JOYPAD, TM_LINE_UNCHANGED);
    if (!came_back_on_the_warp_page(s)) {
        m_stop_at_own(s, THE_SCREEN_ON_CALL + JSR_SIZE, 0);
        return;
    }
    set_warp_frame(g, 0);
    leave_the_last_scale_in_a(s);
    m_rts(s);
}

/* The screen dark and everything stopped, with the CPU's interrupts masked
 * too, as the warp's end begins. */
static void go_dark_and_still(Snes *s)
{
    go_dark(m_platform(s));
    m_set_flag(s, CPU_FLAG_I, true);
}

enum { FIELD_STARTS = 0xC00000, GO_DARK = 0xFDEAC8, PUT_BACK_THE_STORY_FLAGS = 0xFDEAB7 };

// $FD:DA55, $FD:DAB4, $FD:DB13, $FD:DB8D (each kind's end, all four the same)
/* The warp over: the screen dark, the story flags put back over the plane's
 * map, and the field started again, which loads the location the event set
 * for the end of the trip. */
void warp_end(Snes *s)
{
    NATIVE_TIME_AS(s, GO_DARK, go_dark_and_still(s));
    TIMED_AS(state_of(s), PUT_BACK_THE_STORY_FLAGS, put_back_the_story_flags(state_of(s)));
    m_stop_at(s, FIELD_STARTS, READS_NOTHING);
}

/* A frame, then on to the main loop's wait for the next (`loop`); after the
 * last, to the warp's end (`ending`, the entry below). The end is an entry of
 * its own rather than the last frame's tail: it stops the picture's line
 * effects, and run at the last frame's estimated time they stop a line early
 * or late, where the end's own entry starts in step with the original. A
 * frame leaves A's high byte clear, from the colours' pulse, and the next
 * frame's plane reads it (and shifts it out of what it makes). */
static void run_a_frame(Snes *s, WarpKindId id, uint32_t loop, uint32_t ending)
{
    bool goes_on = make_a_frame(state_of(s), m_platform(s), &kinds[id]);
    s->cpu.a = (uint8_t)s->cpu.a;
    if (goes_on)
        m_stop_at(s, loop, DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    else
        m_stop_at(s, ending, DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

// $FD:DA06 (the frame, after the wait at $FD:DA03)
void warp_long_frame(Snes *s) { run_a_frame(s, WARP_LONG, 0xFDDA03, 0xFDDA55); }
// $FD:DA65 (after the wait at $FD:DA62)
void warp_short_frame(Snes *s) { run_a_frame(s, WARP_SHORT, 0xFDDA62, 0xFDDAB4); }
// $FD:DAC4 (after the wait at $FD:DAC1)
void warp_shortest_frame(Snes *s) { run_a_frame(s, WARP_SHORTEST, 0xFDDAC1, 0xFDDB13); }
// $FD:DB27 (after the wait at $FD:DB24)
void warp_flickering_frame(Snes *s) { run_a_frame(s, WARP_FLICKERING, 0xFDDB24, 0xFDDB8D); }

/* ---- Each kind's start, and its main loop's wait ------------------------------------------------- */

/* The warp's start and the main loop's wait for the next frame, which the
 * kinds' starts and loops call (warp/bridge.c runs them as the original's
 * JSRs do). The wait stops at its loop, which returns to the frame after it
 * (warp_long_frame and the others). */
enum { SET_UP = 0xFDDB97, WAIT_FOR_FRAME = 0xFDEAEB };

static void wait_for_the_frame_from(Snes *s, uint32_t site) { (void)warp_run_from(s, site, WAIT_FOR_FRAME, warp_wait_for_frame); }

/* The warp starts, the flickering one with its flicker at its pattern's
 * start, and its main loop goes round: a frame waited for, then made. */
static void start(Snes *s, WarpKindId id, uint32_t start, uint32_t wait)
{
    if (!warp_run_from(s, start, SET_UP, warp_set_up))
        return;
    if (kinds[id].flickers)
        set_warp_flicker(state_of(s), 0);
    wait_for_the_frame_from(s, wait);
}

// $FD:DA00
void warp_start_the_long_trip(Snes *s) { start(s, WARP_LONG, 0xFDDA00, 0xFDDA03); }
// $FD:DA5F
void warp_start_the_short_trip(Snes *s) { start(s, WARP_SHORT, 0xFDDA5F, 0xFDDA62); }
// $FD:DABE
void warp_start_the_shortest_trip(Snes *s) { start(s, WARP_SHORTEST, 0xFDDABE, 0xFDDAC1); }
// $FD:DB1D
void warp_start_the_flickering_trip(Snes *s) { start(s, WARP_FLICKERING, 0xFDDB1D, 0xFDDB24); }

// $FD:DA03, $FD:DA62, $FD:DAC1, $FD:DB24
/* Each kind's main loop going round, after a frame made. */
void warp_long_trip_waits(Snes *s) { wait_for_the_frame_from(s, 0xFDDA03); }
void warp_short_trip_waits(Snes *s) { wait_for_the_frame_from(s, 0xFDDA62); }
void warp_shortest_trip_waits(Snes *s) { wait_for_the_frame_from(s, 0xFDDAC1); }
void warp_flickering_trip_waits(Snes *s) { wait_for_the_frame_from(s, 0xFDDB24); }

// $FD:EAC8
/* The warp's end begins, a call of its own so that the picture's line effects
 * stop where the original stops them. What it leaves in A nobody reads. */
void warp_go_dark(Snes *s)
{
    go_dark_and_still(s);
    m_rts(s);
}

// $FD:EAB7
/* The story flags put back, as the warp ends; the field starts again after. */
void warp_put_back_the_story_flags(Snes *s)
{
    put_back_the_story_flags(state_of(s));
    m_rts(s);
}

enum { FRAME_WAIT_LOOP = 0xFDEAED, FRAME_WAIT_LOOP_READS = DC_AL | DC_N | DC_Z };

// $FD:EAEB
/* The main loop's wait for the next frame: the vblank handler's mark cleared,
 * then waited for (the idle entry below). */
void warp_wait_for_frame(Snes *s)
{
    set_warp_frame_ended(state_of(s), 0);
    m_stop_at(s, FRAME_WAIT_LOOP, FRAME_WAIT_LOOP_READS);
}

// $FD:EAED
/* One pass of the wait: on once the vblank handler has taken the frame, the
 * mark cleared again. Idle entry. */
void warp_wait_for_frame_loop(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t ended = m_lda8(s, warp_frame_ended(g));
    if (ended == 0)
        return;
    set_warp_frame_ended(g, 0);
    m_rts(s);
}

enum { SCREEN_ON_LOOP_EXIT = 0xEADF };

// $FD:EADA
/* One pass of the wait for the vblank before the screen comes on, the vblank
 * handler being off: on once the vblank's flag is up, or the picture is in its
 * vblank already. Waits on beam position (docs/TODO.md, "Frame waits still
 * made on the machine"). Idle entry. */
void warp_screen_on_wait_for_vblank_loop(Snes *s)
{
    Platform *pf = m_platform(s);
    uint8_t flag = m_lda8(s, tm_ack_vblank(pf, PF_AT(SCREEN_ON)));
    if ((flag & TM_VBLANK_PENDING) || (tm_snes_video_status(pf, PF_AT(SCREEN_ON)) & TM_SNES_IN_VBLANK))
        s->cpu.pc = SCREEN_ON_LOOP_EXIT;
}

// $FD:EADF
/* The warp's vblank handler and the controllers' reading on, the picture on
 * but dark until the handler brightens it, and the CPU taking interrupts. What
 * it leaves in A nobody reads. */
void warp_turn_the_screen_on(Snes *s)
{
    Platform *pf = m_platform(s);
    tm_enable_interrupts(pf, PF_AT(0xFDEAE1), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    vid_set_display(pf, PF_AT(0xFDEAE6), &(VidDisplay){ 0 });
    m_set_flag(s, CPU_FLAG_I, false);
    m_rts(s);
}

void warp_vblank(Snes *s)
{
    take_the_frame(state_of(s), m_platform(s));
    m_rti(s);
}

/* The warp's routines index from the zero it keeps (warp_zero), which the
 * start sets and nothing changes. */
bool warp_on_its_page(Snes *s)
{
    if (m_direct_page(s) != WARP_PAGE || !m_bank_reaches_io(s))
        return false;
    vars_peek(true);
    bool zero = warp_zero(state_of(s)) == 0;
    vars_peek(false);
    return zero;
}
bool warp_bank_reaches_io(Snes *s) { return m_bank_reaches_io(s); }
