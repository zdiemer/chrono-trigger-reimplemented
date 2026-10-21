/* Scenes: the Beyond Time ending's night over the kingdom.
 *
 * After Marle rings the new bell in Leene Square, the Beyond Time ending's
 * scripts (on the shared endings' setup, endings.c) show the night: Taban's
 * fireworks burst over the square while the town's lights twinkle, and the
 * staff roll by as the Epoch flies over the world's map in the dark, the
 * camera following it round, between the Mode 7 pictures of the places the
 * party saw. The scripts drive it all through their two channels' registers,
 * and start these as the frame's tasks:
 *
 * - the flight: channel 0 is the camera over the world's map -- its place on
 *   the map, its height, its turn, its horizon, the depth the plane starts at
 *   and how far behind its place it looks from -- and channel 1 the Epoch,
 *   which circles the middle of the screen at its angle, as far out as its
 *   reach, its frame of animation facing the way it flies. Each frame the map
 *   passes under the camera by the Epoch's reach; the camera's perspective is
 *   built again where it changed (perspective.c); in the vblank the view is
 *   worked out from the camera, the per-line tables split at the horizon
 *   between the sky and the ground, and the plane's centre and the scrolls
 *   sent. The map can be shown flat instead, turned and scaled by one matrix.
 * - the fireworks: two of them over the square, each bursting a burst picked
 *   at random, a frame at a time, drawn as a list of large sprites
 *   (sprites.c); the shared setup places them and picks their first bursts.
 * - the town's lights: every eighth frame four pairs of colors swap.
 * - the Mode 7 pictures: their scale and scroll, sent in the vblank.
 *
 * And the shared endings' orbit, a figure of eight every ending's scene keeps
 * going, which only another ending's picture follows.
 *
 * The work is C on the game's state, in the scene program's arithmetic
 * (math.c: its multiply, which leaves its cells as the original does). Each
 * task is an entry the lists call on the machine, as the program's routine;
 * the entries at the end say what each leaves for the list. */
#include "scene/race.h"

#include "charge.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The program's arithmetic, as the scenes here use it ------------------------------------- */

/* A sine `value` scaled by `by` 256ths through the program's signed
 * multiply, where the routine uses that: a negative value rounds away from
 * 0, a step further out than scene_scale_by_256ths's wherever the product has
 * a fraction. */
static uint16_t scaled_through_signed_multiply(GameState *g, SceneProgramPage page, int16_t value, uint16_t by)
{
    return (uint16_t)(scene_multiply_signed16(g, page, (uint16_t)value, by) >> 8);
}

/* ---- The shared endings' orbit -------------------------------------------------------------- */

// $7E:99B5
/* The orbit's place: across, the sine of four times the frame counter, and
 * down, the cosine of twice it, each swung out by the orbit's size -- a figure
 * of eight. */
static void orbit(GameState *g)
{
    uint16_t twice = (uint16_t)(scene_frame_counter(g) << 1);
    uint16_t size = scene_ending_orbit_size(g);
    ORIGINAL_EFFECT(set_scene_call_orbit_frame(g, scene_program_own(), twice));
    set_scene_ending_orbit_x(g, (uint16_t)scene_scale_by_256ths(g, scene_program_own(), scene_sine(g, (uint16_t)(twice << 1)), size));
    set_scene_ending_orbit_y(g, (uint16_t)scene_scale_by_256ths(g, scene_program_own(), scene_cosine(g, twice), size));
}

/* ---- The flight ------------------------------------------------------------------------------ */

enum { EPOCH = 1 };

/* Channel 0's registers: the camera. Its place on the map, its height, turn,
 * horizon and the plane's top, and, shown flat, the plane's centre and matrix
 * beside them, are the plane's (scene.h, SCENE_PLANE_*); and how far behind
 * its place it looks from. */
enum { FLIGHT_DISTANCE = 15 };
/* Channel 1's registers: the Epoch. Its zoom and place on the screen, the
 * angle it circles at, how far behind the camera stays, and its reach: how
 * far out it circles, and how fast the map passes under it. */
enum { EPOCH_ZOOM = 0, EPOCH_X = 1, EPOCH_Y = 2, EPOCH_ANGLE = 5, EPOCH_CAMERA_DISTANCE = 6, EPOCH_REACH = 8 };

static uint16_t camera(const GameState *g, unsigned reg) { return scene_channel_register(g, SCENE_CAMERA_CHANNEL, reg); }
static void set_camera(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, SCENE_CAMERA_CHANNEL, reg, v); }
static uint16_t epoch(const GameState *g, unsigned reg) { return scene_channel_register(g, EPOCH, reg); }
static void set_epoch(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, EPOCH, reg, v); }

static bool plane_flat(const GameState *g) { return scene_ending_plane_flat(g) != 0; }

// $7E:90C1
/* The map passes under the camera by the Epoch's reach, round its 1024 rows. */
enum { MAP_ROWS = 0x400 };

static void pass_the_map(GameState *g)
{
    set_camera(g, SCENE_PLANE_Y, (uint16_t)(camera(g, SCENE_PLANE_Y) + epoch(g, EPOCH_REACH)) & (MAP_ROWS - 1));
}

/* The flight's check compares the first four of the camera's registers the
 * perspective keeps (height, turn, horizon, depth). */
enum { FLIGHT_CAMERA_REGISTERS = 4 };

// $7E:9ABD
/* The camera's view brought up to date each frame: shown flat, the plane is
 * turned by the camera; in perspective, the camera is kept, and the result
 * is whether it changed since the perspective was built, so that the
 * perspective is to be built again. */
static bool update_the_view(GameState *g)
{
    if (plane_flat(g)) {
        scene_turn_the_flat_plane(g);
        return false;
    }
    return scene_keep_the_camera(g, FLIGHT_CAMERA_REGISTERS) != FLIGHT_CAMERA_REGISTERS;
}

/* The view's place: the plane's centre half a screen across and a screen's
 * height down from the view's scroll. */
enum { VIEW_CENTRE_ACROSS = 0x80, VIEW_CENTRE_DOWN = 0xE0 };

/* For shadow mode: the camera's distance turned, across and down, which the
 * original keeps beside the view on its way to it. */
static void leave_the_distance_turned(GameState *g, uint16_t across, uint16_t down)
{
    set_scene_flight_view_turned_x(g, across);
    set_scene_flight_view_turned_y(g, down);
}

// $7E:9AF3
/* In the vblank: the camera stays as far behind as the Epoch's script says,
 * and the view is its place on the map moved out along its turn by that
 * distance. */
static void place_the_view(GameState *g)
{
    SceneProgramPage page = scene_program_channel(SCENE_CAMERA_CHANNEL);
    uint16_t distance = epoch(g, EPOCH_CAMERA_DISTANCE), turn = camera(g, SCENE_PLANE_TURN);
    set_camera(g, FLIGHT_DISTANCE, distance);
    uint16_t down = scaled_through_signed_multiply(g, page, scene_cosine(g, turn), distance);
    uint16_t across = scaled_through_signed_multiply(g, page, scene_sine(g, turn), distance);
    ORIGINAL_EFFECT(leave_the_distance_turned(g, across, down));
    uint16_t x = (uint16_t)(camera(g, SCENE_PLANE_X) + across), y = (uint16_t)(camera(g, SCENE_PLANE_Y) + down);
    set_scene_flight_view_x(g, x);
    set_scene_flight_view_centre_x(g, (uint16_t)(x + VIEW_CENTRE_ACROSS));
    set_scene_flight_view_y(g, y);
    set_scene_flight_view_centre_y(g, (uint16_t)(y + VIEW_CENTRE_DOWN));
}

/* The flat plane's centre is the camera's place half a plane-screen in. */
enum { FLAT_CENTRE_IN = 0x80 };

/* The original works the centre down out with the carry of its sum across,
 * so a camera less than half a plane-screen left of the map's left edge has
 * its centre a line lower. The flight's flat view keeps its camera just
 * there, half a screen left of the edge, so its centre across is the map's
 * left edge and its centre down a line further on than its place. */
static bool left_of_the_map_by_less_than_the_centre(int16_t x) { return x < 0 && x >= -FLAT_CENTRE_IN; }

static void centre_the_flat_plane(GameState *g)
{
    uint16_t x = camera(g, SCENE_PLANE_X);
    unsigned lower = left_of_the_map_by_less_than_the_centre((int16_t)x) ? 1u : 0u;
    set_camera(g, SCENE_PLANE_CENTRE_X, (uint16_t)(x + FLAT_CENTRE_IN));
    set_camera(g, SCENE_PLANE_CENTRE_Y, (uint16_t)(camera(g, SCENE_PLANE_Y) + FLAT_CENTRE_IN + lower));
}

/* The sky's scroll down puts its last line at the horizon. */
enum { SKY_LAST_LINE = 0x27 };

// $7E:9DBC
/* In the vblank: the view sent. Flat, the plane (plane.c), its centre
 * the camera's place half a screen in, and its scroll the camera's place.
 * Otherwise the plane's centre from the view, and the per-line table's
 * scrolls: the ground's the view's, the sky's against the camera's turn, its
 * last line at the horizon. */
static void send_the_view(GameState *g, Platform *pf)
{
    if (plane_flat(g)) {
        centre_the_flat_plane(g);
        scene_send_the_flat_plane(g, pf);
        return;
    }
    VidMode7 centre = { .cx = (int16_t)scene_flight_view_centre_x(g), .cy = (int16_t)scene_flight_view_centre_y(g) };
    vid_set_mode7(pf, PF_AT(0x7E9DDE), &centre, VID_M7_CENTER);
    set_scene_flight_view_line_x(g, scene_flight_view_x(g));
    set_scene_flight_view_line_y(g, scene_flight_view_y(g));
    set_scene_flight_view_sky_x(g, (uint16_t)-camera(g, SCENE_PLANE_TURN));
    set_scene_flight_view_sky_y(g, (uint16_t)(SKY_LAST_LINE - camera(g, SCENE_PLANE_HORIZON)));
}

// $7E:9080
/* In the vblank: the per-line tables split at the horizon, the sky above it
 * and the ground from it, for the BG mode, the main screen, BG1's scroll and
 * the color math (the race's table). */
enum { SKY_PART = 0, GROUND_PART = 1 };

static void set_the_horizon(GameState *g)
{
    SceneHorizon h = scene_horizon_lines((uint8_t)camera(g, SCENE_PLANE_HORIZON));
    const uint8_t lines[] = { [SKY_PART] = h.above, [GROUND_PART] = h.below };
    for (unsigned part = SKY_PART; part <= GROUND_PART; part++) {
        set_scene_flight_mode_line_counts(g, part, lines[part]);
        set_scene_flight_screens_line_counts(g, part, lines[part]);
        set_scene_flight_scroll_line_counts(g, part, lines[part]);
        set_scene_race_math_line_counts(g, part, lines[part]);
    }
}

/* The Epoch circles the middle of the screen, a 16th of its reach across and
 * a 32nd of it down, which makes the circle the slant of the ground. */
enum { CIRCLE_MIDDLE_X = 0x80, CIRCLE_MIDDLE_Y = 0x70, CIRCLE_SHIFT_ACROSS = 4, CIRCLE_SHIFT_DOWN = 5 };
/* Its frame faces against its angle, turned three eighths and a step. */
enum { FACING_TURN = 0x301 };

// $7E:992A
/* The Epoch circles: its place round the middle of the screen at its angle,
 * and the frame of its animation that faces the way it flies. The swung
 * values are shifted as unsigned words, as the original shifts them. */
static void circle_the_epoch(GameState *g)
{
    SceneProgramPage page = scene_program_channel(EPOCH);
    uint16_t angle = epoch(g, EPOCH_ANGLE), reach = epoch(g, EPOCH_REACH);
    set_epoch(g, EPOCH_X, (uint16_t)((scaled_through_signed_multiply(g, page, scene_sine(g, angle), reach) >> CIRCLE_SHIFT_ACROSS) + CIRCLE_MIDDLE_X));
    set_epoch(g, EPOCH_Y, (uint16_t)((scaled_through_signed_multiply(g, page, scene_cosine(g, angle), reach) >> CIRCLE_SHIFT_DOWN) + CIRCLE_MIDDLE_Y));
    scene_ending_show_picture_frame(g, (uint16_t)(-angle - FACING_TURN));
}

/* A 64th of the sine, as an unsigned word. */
enum { SWAY_SHIFT = 6 };

// $7E:997D
/* The Epoch sways among the stars: its height and its zoom follow the sine
 * and the cosine of its angle, a 64th of each taken as an unsigned word, so
 * that a negative one comes out near 1024 (the zoom then draws it at its
 * smallest). */
static void sway_the_epoch(GameState *g)
{
    uint16_t angle = epoch(g, EPOCH_ANGLE);
    set_epoch(g, EPOCH_Y, (uint16_t)((uint16_t)scene_sine(g, angle) >> SWAY_SHIFT));
    set_epoch(g, EPOCH_ZOOM, (uint16_t)((uint16_t)scene_cosine(g, angle) >> SWAY_SHIFT));
}

/* ---- The town's lights ------------------------------------------------------------------------- */

/* The colors that swap, in pairs, every eighth frame. */
static const struct {
    uint8_t one, other;
} lights[] = { { 20, 21 }, { 17, 18 }, { 13, 45 }, { 14, 46 } };
enum { LIGHT_PAIRS = sizeof lights / sizeof lights[0], EVERY_EIGHTH_FRAME = 0x07 };

static bool lights_twinkle_now(const GameState *g) { return (scene_frame_counter(g) & EVERY_EIGHTH_FRAME) == 0; }

// $7E:9D48
/* The lights twinkle: on every eighth frame each pair swaps, in the colors
 * the frame sends and in the scene's own. */
static void twinkle_the_lights(GameState *g)
{
    if (!lights_twinkle_now(g))
        return;
    for (unsigned i = 0; i < LIGHT_PAIRS; i++) {
        uint16_t one = palette_working(g, lights[i].one);
        set_palette_working(g, lights[i].one, palette_working(g, lights[i].other));
        set_palette_working(g, lights[i].other, one);
    }
    for (unsigned i = 0; i < LIGHT_PAIRS; i++) {
        uint16_t one = palette_base(g, lights[i].one);
        set_palette_base(g, lights[i].one, palette_base(g, lights[i].other));
        set_palette_base(g, lights[i].other, one);
    }
}

// $7E:9D45
/* The same, with the camera turned a step back first. */
static void turn_back_and_twinkle(GameState *g)
{
    set_camera(g, SCENE_PLANE_TURN, (uint16_t)(camera(g, SCENE_PLANE_TURN) - 1));
    twinkle_the_lights(g);
}

/* ---- The fireworks --------------------------------------------------------------------------- */

/* A burst is a run of frames in the program (scene_firework_bursts), each
 * shown for a number of frames and drawn by its list of large sprites, and
 * ended by a frame of 0 frames. A frame is named by where it lies in the
 * program. */
enum { FIREWORKS = 2, FRAME_BYTES = 3, BURST_OVER = 0 };
static const uint16_t bursts[] = { 0xA5B0, 0xA5CC, 0xA5E8, 0xA610 };
enum { BURSTS = sizeof bursts / sizeof bursts[0], HELD_BURST = 0xA60C, FIRST_BURST_FRAME = 0xA5B0 };

typedef struct {
    uint8_t frames;   /* how many frames it shows; BURST_OVER past the burst's last */
    uint16_t list;    /* its list of large sprites (sprites.c) */
} BurstFrame;

/* A frame of a burst: how long it shows and what it draws. The frame that
 * ends a burst draws nothing. */
static BurstFrame burst_frame(const GameState *g, uint16_t frame)
{
    unsigned at = (unsigned)(frame - FIRST_BURST_FRAME);
    uint8_t frames = scene_firework_bursts(g, at);
    if (frames == BURST_OVER)
        return (BurstFrame){ BURST_OVER, 0 };
    return (BurstFrame){ frames, (uint16_t)(scene_firework_bursts(g, at + 1) | scene_firework_bursts(g, at + 2) << 8) };
}

static uint16_t next_frame(uint16_t frame) { return (uint16_t)(frame + FRAME_BYTES); }

/* A firework's frame of its burst. The program keeps it as the place of the
 * frame's sprite list, a byte into the frame. A burst is started a frame
 * before its first, so that stepping on finds the first. */
enum { FRAME_LIST = 1 };

static uint16_t firework_frame(const GameState *g, unsigned firework)
{
    return (uint16_t)(scene_fireworks_frame(g, firework) - FRAME_LIST);
}
static void set_firework_frame(GameState *g, unsigned firework, uint16_t frame)
{
    set_scene_fireworks_frame(g, firework, (uint16_t)(frame + FRAME_LIST));
}
static uint16_t before(uint16_t first_frame) { return (uint16_t)(first_frame - FRAME_BYTES); }

// $7E:9BD8
/* A new burst: the held one while the fireworks hold, otherwise one of four
 * at random, from the start. */
static void start_a_burst(GameState *g, unsigned firework)
{
    uint16_t burst = HELD_BURST;
    if (!scene_fireworks_held(g))
        burst = bursts[scene_next_random_byte(g) % BURSTS];
    set_firework_frame(g, firework, before(burst));
    set_scene_fireworks_countdown(g, firework, 1);
}

/* On to the burst's next frame, once its frames are shown; past its end, a
 * new burst. */
static void step_the_burst(GameState *g, unsigned firework)
{
    uint16_t left = (uint16_t)(scene_fireworks_countdown(g, firework) - 1);
    set_scene_fireworks_countdown(g, firework, left);
    if (left)
        return;
    uint16_t next;
    while (burst_frame(g, next = next_frame(firework_frame(g, firework))).frames == BURST_OVER)
        start_a_burst(g, firework);
    set_scene_fireworks_countdown(g, firework, burst_frame(g, next).frames);
    set_firework_frame(g, firework, next);
}

/* For the original's code: the list routine's test written back as it was
 * (kept on the scene's page meanwhile), and the load the program has in the
 * routine's spare bytes, which the fireworks write last. */
enum { SPARE_AS_LOADED = 0xE0A9 };

static void leave_the_list_routine(GameState *g)
{
    set_scene_call_kept_list_clip(g, scene_program_own(), scene_large_list_clip(g));
    set_scene_large_list_spare(g, SPARE_AS_LOADED);
}

/* For the original's code and shadow mode: the original hands a firework's
 * place to the list routine as two words read from the places, which leave
 * their high bytes in the scene's offset -- across, the firework's Y over its
 * X, and down, the byte after its Y (the next firework's X, or for the last
 * the byte past the places). Only the low bytes place the sprites; the last
 * firework's words are the ones that stay. */
static void leave_the_offset_as_words(GameState *g)
{
    unsigned last = FIREWORKS - 1;
    uint8_t x = scene_fireworks_x(g, last), y = scene_fireworks_y(g, last);
    scene_draw_lists_at(g, (uint16_t)(x | y << 8), (uint16_t)(y | scene_fireworks_x_unchecked(g, FIREWORKS) << 8));
}

// $7E:9BF9
/* The fireworks burst: each a frame on in its burst, drawn where it bursts as
 * large sprites, every one kept however low. */
static void burst_the_fireworks(GameState *g)
{
    for (unsigned firework = 0; firework < FIREWORKS; firework++) {
        step_the_burst(g, firework);
        scene_draw_lists_at(g, scene_fireworks_x(g, firework), scene_fireworks_y(g, firework));
        TIMED_AS(g, 0x7EA03D, scene_draw_large_sprite_list(g, burst_frame(g, firework_frame(g, firework)).list, LIST_KEEPS_EVERY_SPRITE));
    }
    ORIGINAL_EFFECT(leave_the_offset_as_words(g));
    ORIGINAL_EFFECT(leave_the_list_routine(g));
}

/* The two fireworks' places over the square. */
static const struct {
    uint8_t x, y;
} firework_places[FIREWORKS] = { { 0x80, 0x70 }, { 0xA0, 0x60 } };

// $7E:9BB8
/* The shared endings' setup places the two fireworks over the square, each
 * with a first burst picked, to start bursting from the next frame. */
void scene_set_the_fireworks_up(GameState *g)
{
    for (unsigned firework = 0; firework < FIREWORKS; firework++) {
        start_a_burst(g, firework);
        set_scene_fireworks_x(g, firework, firework_places[firework].x);
        set_scene_fireworks_y(g, firework, firework_places[firework].y);
    }
}

/* ---- The Mode 7 pictures ---------------------------------------------------------------------- */

/* Channel 0's registers as a picture's: its scale, its scroll and its
 * centre. */
enum { PICTURE_SCALE = 0, PICTURE_SCROLL_X = 1, PICTURE_SCROLL_Y = 2, PICTURE_CENTRE_X = 3, PICTURE_CENTRE_Y = 4,
       PICTURE_BG2_SCROLL = 7 };

// $7E:9E2B
/* In the vblank: the picture scaled, unturned, round its centre, and
 * scrolled. */
static void send_the_picture(const GameState *g, Platform *pf)
{
    int16_t scale = (int16_t)camera(g, PICTURE_SCALE);
    VidMode7 m7 = { .a = scale, .d = scale, .cx = (int16_t)camera(g, PICTURE_CENTRE_X),
                    .cy = (int16_t)camera(g, PICTURE_CENTRE_Y), .hofs = (int16_t)camera(g, PICTURE_SCROLL_X),
                    .vofs = (int16_t)camera(g, PICTURE_SCROLL_Y) };
    vid_set_mode7(pf, PF_AT(0x7E9E31), &m7, VID_M7_MATRIX | VID_M7_CENTER | VID_M7_SCROLL);
}

// $7E:9E19
/* The same, with BG2 scrolled across and the view's line scroll set from the
 * picture's. */
static void send_the_picture_and_bg2(GameState *g, Platform *pf)
{
    vid_set_scroll(pf, PF_AT(0x7E9E1C), VID_BG2, camera(g, PICTURE_BG2_SCROLL), 0, VID_SCROLL_H);
    set_scene_flight_view_line_x(g, camera(g, PICTURE_SCROLL_X));
    send_the_picture(g, pf);
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The frame's tasks: the list reads A's high byte after the last and Y in the
 * next; the vblank's read nothing. Each comes back as the original's does,
 * leaving A as its last load left it. */

void scene_keep_the_orbit(Snes *s)
{
    GameState *g = state_of(s);
    orbit(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_ending_orbit_y(g));
    m_rts(s);
}

void scene_pass_the_map(Snes *s)
{
    GameState *g = state_of(s);
    pass_the_map(g);
    ORIGINAL_EFFECT(s->cpu.a = camera(g, SCENE_PLANE_Y));
    m_rts(s);
}

/* Flat, A holds the matrix's last entry the turn wrote; unchanged, the last
 * register compared, the depth. A new perspective is built on channel 0's
 * page, where the original jumps on to it, and where the port doesn't build
 * it the original does, reading none of the registers but the page. */
enum { BUILD_PERSPECTIVE = 0x7E357B, CAMERA_PAGE = 0x0A00 };

static void leave_the_view_checked(Snes *s, const GameState *g, bool perspective_built)
{
    if (perspective_built)
        scene_leave_perspective_registers(s, SCENE_CAMERA_CHANNEL);
    else
        s->cpu.a = plane_flat(g) ? camera(g, SCENE_PLANE_C) : camera(g, SCENE_PLANE_TOP);
}

void scene_check_the_flight_view(Snes *s)
{
    GameState *g = state_of(s);
    bool rebuild = update_the_view(g);
    if (rebuild && !scene_perspective_as_ported(s, CAMERA_PAGE)) {
        s->cpu.d = CAMERA_PAGE;
        m_stop_at_own(s, BUILD_PERSPECTIVE, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    if (rebuild)
        TIMED_AS(g, BUILD_PERSPECTIVE, scene_build_perspective(g, SCENE_CAMERA_CHANNEL));
    ORIGINAL_EFFECT(leave_the_view_checked(s, g, rebuild));
    m_rts(s);
}

/* The view's placing works with a 16-bit accumulator, which it comes back
 * with; so does the view's sending in perspective. */
void scene_place_the_flight_view(Snes *s)
{
    place_the_view(state_of(s));
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

void scene_send_the_flight_view(Snes *s)
{
    GameState *g = state_of(s);
    send_the_view(g, m_platform(s));
    if (!plane_flat(g))
        s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

void scene_set_the_flight_horizon(Snes *s)
{
    set_the_horizon(state_of(s));
    m_rts(s);
}

void scene_circle_the_epoch(Snes *s)
{
    GameState *g = state_of(s);
    circle_the_epoch(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_ending_picture_source(g));
    m_rts(s);
}

void scene_sway_the_epoch(Snes *s)
{
    GameState *g = state_of(s);
    sway_the_epoch(g);
    ORIGINAL_EFFECT(s->cpu.a = epoch(g, EPOCH_ZOOM));
    m_rts(s);
}

/* Off the eighth frame A holds the frame counter's place in the eight; on
 * it, the last color the swap took, the scene's own color 14 as it was, which
 * is color 46 now. */
static void leave_the_lights(Snes *s, const GameState *g)
{
    s->cpu.a = lights_twinkle_now(g) ? palette_base(g, lights[LIGHT_PAIRS - 1].other)
                                     : (uint16_t)(scene_frame_counter(g) & EVERY_EIGHTH_FRAME);
}

void scene_twinkle_the_lights(Snes *s)
{
    GameState *g = state_of(s);
    twinkle_the_lights(g);
    ORIGINAL_EFFECT(leave_the_lights(s, g));
    m_rts(s);
}

void scene_turn_back_and_twinkle(Snes *s)
{
    GameState *g = state_of(s);
    turn_back_and_twinkle(g);
    ORIGINAL_EFFECT(leave_the_lights(s, g));
    m_rts(s);
}

/* A holds the spare bytes' load, and Y the next free sprite entry. */
void scene_burst_the_fireworks(Snes *s)
{
    GameState *g = state_of(s);
    burst_the_fireworks(g);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = SPARE_AS_LOADED);
    m_rts(s);
}

/* Called by the shared setup with 8-bit A, which it comes back with; the
 * setup reads none of the registers it leaves. */
void scene_set_the_fireworks_up_entry(Snes *s)
{
    scene_set_the_fireworks_up(state_of(s));
    m_rts(s);
}

void scene_send_the_picture(Snes *s)
{
    send_the_picture(state_of(s), m_platform(s));
    m_rts(s);
}

void scene_send_the_picture_and_bg2(Snes *s)
{
    send_the_picture_and_bg2(state_of(s), m_platform(s));
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* The program's sine and its two multiplies, which the scenes here run as C. */
static bool sine_present(Snes *s) { return m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull); }
static bool multiply_present(Snes *s) { return m_code_matches(s, 0x7E31BF, 0x4A, 0x479627EBF1311CAFull); }
static bool signed_multiply_present(Snes *s)
{
    return multiply_present(s) && m_code_matches(s, 0x7E33D6, 0x37, 0xCB3F0FAD1C9F8EA9ull);
}

/* Every task runs on the scene's page, reaching the channels' registers and
 * the program through a data bank that reaches low RAM; the vblank's that
 * send through one that reaches the registers. */
static bool frame_task_on_page(Snes *s) { return scene_on_scene_page_low_bank(s); }

bool scene_keep_the_orbit_present(Snes *s)
{
    return frame_task_on_page(s) && m_code_matches(s, 0x7E99B5, 0x45, 0x78FEC1F894A5BFF8ull) && sine_present(s) &&
           multiply_present(s);
}

bool scene_pass_the_map_present(Snes *s)
{
    return frame_task_on_page(s) && m_code_matches(s, 0x7E90C1, 0x0E, 0x881B13929F5DFB2Eull);
}

bool scene_check_the_flight_view_present(Snes *s)
{
    return frame_task_on_page(s) && m_code_matches(s, 0x7E9ABD, 0x36, 0x5952F84E74F8A1FDull) &&
           m_code_matches(s, 0x7E56B4, 0x37, 0xD7D22FBA648F6283ull) && sine_present(s) && multiply_present(s);
}

bool scene_place_the_flight_view_present(Snes *s)
{
    return frame_task_on_page(s) && m_code_matches(s, 0x7E9AF3, 0x53, 0x5224AFB038B83640ull) && sine_present(s) &&
           signed_multiply_present(s);
}

/* Flat, the original also adds the carry the list hands the task into the
 * centre across, which the list's loop leaves clear at the task's slot: a task
 * entered with the carry set is the original's. */
bool scene_send_the_flight_view_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || !m_code_matches(s, 0x7E9DBC, 0x5D, 0xEEE8467EDC5F8B2Aull) ||
        !m_code_matches(s, 0x7E5654, 0x55, 0x93E05BF3D717E712ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = !plane_flat(g) || !m_flag(s, CPU_FLAG_C);
    vars_peek(false);
    return ok;
}

bool scene_set_the_flight_horizon_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E9080, 0x27, 0x0EBC17FE2730FACFull);
}

bool scene_circle_the_epoch_present(Snes *s)
{
    return frame_task_on_page(s) && m_code_matches(s, 0x7E992A, 0x45, 0x781AD13F2E5F0F5Cull) &&
           m_code_matches(s, 0x7E8921, 0x0F, 0xDE82006F5167A2D9ull) && sine_present(s) && signed_multiply_present(s);
}

bool scene_sway_the_epoch_present(Snes *s)
{
    return frame_task_on_page(s) && m_code_matches(s, 0x7E997D, 0x1F, 0xECA1EDE83E4CF167ull) && sine_present(s);
}

bool scene_twinkle_the_lights_present(Snes *s)
{
    return frame_task_on_page(s) && m_code_matches(s, 0x7E9D48, 0x6A, 0x34669C13030AE9A8ull);
}

bool scene_turn_back_and_twinkle_present(Snes *s)
{
    return scene_twinkle_the_lights_present(s) && m_code_matches(s, 0x7E9D45, 0x03, 0x8C843A1C379B1891ull);
}

/* The fireworks' code, their bursts' table, the bursts and their sprite
 * lists, and the program's random bytes. */
static bool fireworks_code_present(Snes *s)
{
    return m_code_matches(s, 0x7E9BB8, 0x41, 0xFA1ECC12B4B643A5ull) && m_code_matches(s, 0x7E9C4E, 0x0A, 0xD820E174BD3B64E4ull) &&
           m_code_matches(s, 0x7EA203, 0x43D, 0xB232C3EBF5FD21D1ull) && m_code_matches(s, 0x7E3448, 0x19, 0x092856CFEC289766ull);
}

/* Whether a firework is at a frame of one of its bursts, or at a burst's
 * start (an entry before its first), where a burst is started on the start's
 * last count. */
enum { LONGEST_BURST = 16 };

static bool at_a_frame(const GameState *g, uint16_t frame)
{
    for (unsigned b = 0; b <= BURSTS; b++) {
        uint16_t at = b < BURSTS ? bursts[b] : HELD_BURST;
        for (unsigned n = 0; n < LONGEST_BURST && burst_frame(g, at).frames != BURST_OVER; n++, at = next_frame(at))
            if (at == frame)
                return true;
    }
    return false;
}

static bool at_a_start(uint16_t frame)
{
    for (unsigned b = 0; b < BURSTS; b++)
        if (before(bursts[b]) == frame)
            return true;
    return frame == before(HELD_BURST);
}

static bool in_its_burst(const GameState *g, unsigned firework)
{
    uint16_t frame = firework_frame(g, firework);
    return at_a_frame(g, frame) || (at_a_start(frame) && scene_fireworks_countdown(g, firework) == 1);
}

/* The sprites a firework draws this frame, where the tables must have room
 * for them: its frame's, or on its frame's last count the next frame's, or a
 * new burst's first (every burst starts with a frame of a sprite at most). */
enum { FIRST_FRAMES_MOST_SPRITES = 1 };

static unsigned sprites_drawn(const GameState *g, unsigned firework)
{
    uint16_t frame = firework_frame(g, firework);
    if (scene_fireworks_countdown(g, firework) != 1)
        return scene_sprite_list_length(g, burst_frame(g, frame).list);
    BurstFrame next = burst_frame(g, next_frame(frame));
    return next.frames == BURST_OVER ? FIRST_FRAMES_MOST_SPRITES : scene_sprite_list_length(g, next.list);
}

bool scene_burst_the_fireworks_present(Snes *s)
{
    if (!frame_task_on_page(s) || !m_code_matches(s, 0x7E9BF9, 0x55, 0x3E4B3458DBB1B804ull) || !fireworks_code_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = true;
    unsigned sprites = 0;
    for (unsigned firework = 0; firework < FIREWORKS; firework++) {
        ok = ok && in_its_burst(g, firework);
        sprites += ok ? sprites_drawn(g, firework) : 0;
    }
    ok = ok && scene_sprites_fit(g, sprites);
    vars_peek(false);
    return ok;
}

bool scene_set_the_fireworks_up_present(Snes *s) { return scene_on_scene_page(s) && fireworks_code_present(s); }

bool scene_send_the_picture_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E9E2B, 0x4F, 0x5EF71A03D74E88BDull);
}

bool scene_send_the_picture_and_bg2_present(Snes *s)
{
    return scene_send_the_picture_present(s) && m_code_matches(s, 0x7E9E19, 0x12, 0xC7B76F034B5D1EAEull);
}
