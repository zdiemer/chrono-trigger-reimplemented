/* The time warp: the tunnel the party falls through between eras.
 *
 * The field's event command $FF with a scene of $80-$83 ends the field for it
 * (event/screen.c); the warp is a mode of its own, with its own vblank handler,
 * and when it is over it returns to the field, which loads the
 * location the event set. Its four kinds differ in how long they last and in
 * whether the picture flickers (WarpKind).
 *
 * It starts with the screen dark (start_the_warp): the vblank handler installed,
 * the story flags kept aside (the plane's map is built over them), the picture
 * drawn (warp/picture.c), both of the circle's window tables built empty
 * (warp/circle.c) and the line effects started; then the screen comes on, dark
 * until the vblank handler brightens it. Each frame the main loop waits for the
 * vblank handler to take the frame it finished through the timing service, then
 * makes the next one (make_a_frame): the plane moved on, its colours turned and
 * pulsed, and the circle built for where the warp is in its course:
 * - opening, its first 32 frames: the tunnel seen only inside a circle that
 *   grows from a dot;
 * - running: the tunnel whole;
 * - closing, from 96 frames before its end: a circle that grows from a dot
 *   with the tunnel gone inside it;
 * - gone, its last 64 frames: the light alone.
 * The vblank handler (take_the_frame) sends the palette and the sprites, shows
 * the circle's table built last and counts the frame. On its last frame the
 * warp goes dark, puts the story flags back and starts the field again. */
#include "warp/warp.h"
#include "warp/warp_data.h"

#include "charge.h"
#include "core/program.h"
#include "field/main_loop.h"
#include "scene/scene.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "ram.h"
#include "vars.h"

/* ---- The kinds of warp --------------------------------------------------------------------------- */

typedef enum { WARP_LONG, WARP_SHORT, WARP_SHORTEST, WARP_FLICKERING } WarpKindId;

/* Duration and flicker behavior, with timing and video provenance. These
 * identities select work estimates; they are never callable addresses. */
typedef struct {
    uint16_t last_frame;
    bool flickers;
    uint32_t frame_rate;
    uint32_t window_site;
} WarpKind;

static const WarpKind kinds[] = {
    [WARP_LONG] = { 0x2FF, false, 0xFDDA06, 0xFDDA42 },
    [WARP_SHORT] = { 0x0FF, false, 0xFDDA65, 0xFDDAA1 },
    [WARP_SHORTEST] = { 0x09F, false, 0xFDDAC4, 0xFDDB00 },
    [WARP_FLICKERING] = { 0x2FF, true, 0xFDDB27, 0xFDDB7A },
};
enum { WARP_KINDS = sizeof kinds / sizeof kinds[0] };

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

enum { FULL_BRIGHTNESS = 15 };

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
    program_install_vblank_handler(g, warp_vblank);
    charge_legacy_accesses(g, 4);
    program_install_line_handler(g, warp_line);
    charge_legacy_accesses(g, 4);
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
 * holding the bottom line's scale, the last word it writes through the CPU's
 * port to work RAM, a byte at a time. Kept as the original's index
 * arithmetic: it is what the first frame shows. */
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

static void turn_the_light(GameState *g)
{
    uint16_t taken[TURN];
    for (unsigned i = 0; i < TURN; i++)
        taken[i] = palette_base(g, TURNING_FIRST + i);
    for (unsigned i = 0; i < TURNING_COLOURS - TURN; i++)
        set_palette_base(g, TURNING_FIRST + i, palette_base(g, TURNING_FIRST + TURN + i));
    for (unsigned i = 0; i < TURN; i++)
        set_palette_base(g, TURNING_FIRST + TURNING_COLOURS - TURN + i, taken[i]);
}

/* Colours 16-47 of the tunnel pulse: their red and green swing with the sine
 * of the frame, the green half a turn behind the red, each colour as far as
 * its reach; their blue stays. */
enum { FIRST_PULSING = 16, BLUE_BITS = 0x7C00, GREEN_SHIFT = 5 };

static uint8_t pulse(uint8_t swing, uint8_t reach) { return (uint8_t)(swing * reach >> 8); }

static void pulse_the_colours(GameState *g, uint8_t frame)
{
    uint8_t red_swing = (uint8_t)(warp_sine(g, frame) + 128);
    uint8_t green_swing = (uint8_t)(warp_sine(g, (uint8_t)~frame) + 128);
    for (unsigned i = 0; i < WARP_PULSING_COLOURS; i++) {
        uint8_t reach = warp_pulse_reach(g, i);
        uint16_t made = (uint16_t)(pulse(green_swing, reach) << GREEN_SHIFT | pulse(red_swing, reach));
        unsigned colour = FIRST_PULSING + i;
        set_palette_base(g, colour, (uint16_t)((palette_base(g, colour) & BLUE_BITS) | made));
    }
}

/* The original steps the circle's point with the multiplier and its waits,
 * where the C steps it in locals: a step takes the original about as long as
 * eight of the frame's own accesses at the frame's rate, measured over the
 * three movies the warp plays in, which is what a frame charges for each
 * (charge.h): the native frame's retained work estimate. */
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
    charge_as(g, k->frame_rate, (uint32_t)circle_steps * ACCESSES_A_CIRCLE_STEP);
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

/* ---- The trip ----------------------------------------------------------------------------------- */

/* The kind of trip a field's event asks for by its scene's number (from
 * WARP_FIRST_SCENE): the longest first, then shorter, and the flickering one
 * for the rest. */
static WarpKindId kind_of(uint8_t scene)
{
    switch (scene) {
    case WARP_FIRST_SCENE: return WARP_LONG;
    case WARP_FIRST_SCENE + 1: return WARP_SHORT;
    case WARP_FIRST_SCENE + 2: return WARP_SHORTEST;
    default: return WARP_FLICKERING;
    }
}

/* The trip started: the warp set up, the screen turned on after the next
 * vblank, the frame count at 0 for the vblank handler to step and the
 * flickering warp's flicker at its pattern's start. The screen's wait is the
 * original's routine ($FD:EADA, called at $FD:DCB5), which the timing service
 * stands for. */
enum { SET_UP = 0xFDDB97, SCREEN_ON = 0xFDEADA, THE_SCREEN_ON_CALL = 0xFDDCB5 };

static void start_the_trip(GameState *g, Platform *pf, const WarpKind *k)
{
    TIMED_AS(g, SET_UP, start_the_warp(g, pf));
    tm_wait_frame_then_enable(pf, PF_CALL(THE_SCREEN_ON_CALL, SCREEN_ON),
                              TM_INT_VBLANK | TM_INT_AUTO_JOYPAD, TM_LINE_UNCHANGED);
    set_warp_frame(g, 0);
    if (k->flickers)
        set_warp_flicker(g, 0);
}

/* The warp's routines index from the zero it keeps (warp_zero), which the
 * start sets and nothing changes. */
static bool keeps_its_zero(const GameState *g) { return warp_zero(g) == 0; }

enum { FRAME_WAIT_LOOP = 0xFDEAED, GO_DARK = 0xFDEAC8, PUT_BACK_THE_STORY_FLAGS = 0xFDEAB7 };

/* The main loop's wait for the vblank handler to take the frame made. */
static void wait_for_the_frame_taken(GameState *g, Platform *pf)
{
    set_warp_frame_ended(g, 0);
    while (warp_frame_ended(g) == 0)
        tm_wait_frame(pf, PF_AT(FRAME_WAIT_LOOP));
    set_warp_frame_ended(g, 0);
}

/* Run the trip,
 * from the field's end to the field's start again: the warp started and the
 * screen turned on, a frame made after each the vblank handler takes, and on
 * the last the screen dark and the story flags put back. False where the
 * warp's zero has moved, which the game never does. */
bool warp_runs(GameState *g, Platform *pf, uint8_t scene)
{
    WarpKindId id = kind_of(scene);
    const WarpKind *k = &kinds[id];
    start_the_trip(g, pf, k);
    bool goes_on = true;
    while (goes_on) {
        wait_for_the_frame_taken(g, pf);
        if (!keeps_its_zero(g)) {
            program_fail(g, k->frame_rate, "a warp whose zero has moved");
            return false;
        }
        TIMED_AS(g, k->frame_rate, goes_on = make_a_frame(g, pf, k));
    }
    TIMED_AS(g, GO_DARK, go_dark(pf));
    TIMED_AS(g, PUT_BACK_THE_STORY_FLAGS, put_back_the_story_flags(g));
    return true;
}

/* Publish the frame through the callback installed at the start of the trip. */
void warp_vblank(GameState *g, Platform *pf) { take_the_frame(g, pf); }

// $FD:DE87
/* The warp's line handler, which the warp installs but never enables the
 * line interrupt for: it does nothing. */
void warp_line(GameState *g, Platform *pf) { (void)g, (void)pf; }
