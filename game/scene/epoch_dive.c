/* Scenes: the Epoch's dive into Lavos.
 *
 * On the Day of Lavos, with its wings, the party flies the Epoch straight
 * into Lavos's shell (the scene program's mode 6). The scene cuts between
 * two views of the dive:
 *
 * - From the cockpit: the shell rushes past above and below on the Mode 7
 *   plane in perspective (perspective.c builds the two halves in turn), a
 *   band of light across the middle of the view ahead, the instrument panel
 *   at the bottom -- BG1 in mode 1 below the panel's top line, the line
 *   effects switching the screen from Mode 7 there -- with its sprites over
 *   it. The cockpit shakes, the panel jumping down by a random line or two
 *   each frame, harder as the dive goes on; the warning lights at the
 *   panel's sides and the needles of its two gauges flicker at random; and at
 *   the end the band of light opens up and the view goes white.
 * - From outside: the shell curves away down the screen, fogged where it
 *   meets the light at its top and bottom, and the Epoch streaks into it from
 *   the right as a trail of light whose round head is the Epoch, a small
 *   picture scaled to its distance; then, as the dive comes out of the
 *   shell's lining, the Epoch flies on as a small animated sprite.
 *
 * The two channels' scripts drive it. Channel 0 is the camera over the shell
 * (scene.h's SCENE_PLANE_*), which passes it at a speed of its own;
 * channel 1 is the Epoch seen from outside -- its zoom, its place and the
 * radius of its trail's head -- and, from the cockpit, the panel's place. The
 * scripts switch the views by giving the frame its tasks for one or the
 * other (look_from_outside, look_from_the_cockpit) and starting the one-off
 * vblank task that sets the screen up for it.
 *
 * The cockpit's random numbers are the program's stirred ones (random.c),
 * and what the original hands each draw to go on from is whatever its code
 * has at hand: the panel's place, the task's slot, a light's picture, where
 * the sprite table's cursor stands. The panel's lights draw with the data
 * bank on work RAM, which puts the palette's bytes in the place of the beam
 * (scene_draw_random_off_the_palette).
 *
 * The line effects' tables are the program's own bytes (re/ram.toml's
 * scene_dive_*): the tasks here move their counts and their sources as the
 * panel shakes, the trail moves and the light opens. */
#include "scene/scene.h"

#include "charge.h"
#include "core/palette.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The camera and the shell ----------------------------------------------------------------- */

/* Channel 0's register the scripts give the shell's speed past the camera in,
 * pixels a frame. */
enum { SHELL_SPEED = 8 };

/* The shell is a plane 1024 pixels round. */
enum { SHELL_ROUND = 0x400 };

static uint16_t camera(const GameState *g, unsigned reg) { return scene_channel_register(g, SCENE_CAMERA_CHANNEL, reg); }
static void set_camera(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, SCENE_CAMERA_CHANNEL, reg, v); }

/* A place on the shell kept round it: past its end it comes back to its
 * start, and before its start back to a pixel short of its end, where the
 * original's wrap adds the round with the carry its compare left set. */
static uint16_t round_the_shell(int16_t place)
{
    if (place >= SHELL_ROUND)
        return (uint16_t)(place - SHELL_ROUND);
    if (place < 0)
        return (uint16_t)(place + SHELL_ROUND + 1);
    return (uint16_t)place;
}

/* How the shell passes in each view: across the screen from outside, down it
 * from the cockpit; the plane turns about a point the camera keeps half a
 * screen on from its place across, or a line over half a screen back from
 * its place down. */
typedef struct {
    unsigned place, centre;
    int16_t centre_from_place;
} ShellPass;

static const ShellPass ACROSS = { SCENE_PLANE_X, SCENE_PLANE_CENTRE_X, 0x80 };
static const ShellPass DOWN = { SCENE_PLANE_Y, SCENE_PLANE_CENTRE_Y, -0x81 };

static int16_t shell_passed(const GameState *g, ShellPass pass)
{
    return (int16_t)(camera(g, pass.place) + camera(g, SHELL_SPEED));
}

// $7E:7F12, $7E:7F35
/* The shell passes the camera by its speed. */
static void pass_the_shell(GameState *g, ShellPass pass)
{
    uint16_t place = round_the_shell(shell_passed(g, pass));
    set_camera(g, pass.place, place);
    set_camera(g, pass.centre, (uint16_t)(place + pass.centre_from_place));
}

// $7E:81F1
/* As the Epoch comes out of the shell's lining, seen from outside, the shell
 * also falls away down the screen, three lines a frame. */
enum { FALLING_AWAY = 3 };

static void let_the_shell_fall_away(GameState *g)
{
    set_camera(g, SCENE_PLANE_Y, (uint16_t)(camera(g, SCENE_PLANE_Y) + FALLING_AWAY));
}

// $7E:82D1
/* The perspective is built again when the camera has moved since it was
 * last built (perspective.c): its height, its turn, or a build of the shell
 * above from the cockpit, which then has the shell below built next. */
static bool the_camera_moved(GameState *g) { return scene_keep_the_dive_camera(g); }

/* ---- The Epoch seen from outside ------------------------------------------------------------- */

/* Channel 1's registers: the Epoch's zoom (128ths of a row a scaled row, so
 * the larger, the smaller it is), its place on the screen, the cockpit's
 * panel's top line, and the radius of the trail's head. */
enum { EPOCH = 1 };
enum { EPOCH_ZOOM = 0, EPOCH_X = 1, EPOCH_Y = 2, PANEL_LINE = 5, TRAIL_RADIUS = 6 };

static uint16_t epoch(const GameState *g, unsigned reg) { return scene_channel_register(g, EPOCH, reg); }
static void set_epoch(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, EPOCH, reg, v); }

/* The Epoch's picture is scaled from its bitmap in the second bank into 33
 * rows of four pairs of 8-pixel groups, the scaling ending where its row
 * position reaches $400 (scaler.c). */
enum { EPOCH_BITMAP = 0x0A00, SCALING_ENDS_AT = 0x0400, EPOCH_ROWS = 0x21, EPOCH_PAIRS = 4 };

/* The picture's mark: whether a gathered picture waits for the vblank to
 * send it. While one does, the main loop scales none. */
enum { PICTURE_SENT = 0, PICTURE_WAITING = 1 };

static bool picture_waiting(const GameState *g) { return scene_dive_picture_waiting(g) != PICTURE_SENT; }
static void mark_the_picture(GameState *g, bool waiting) { set_scene_dive_picture_waiting(g, waiting ? PICTURE_WAITING : PICTURE_SENT); }

// $7E:8278
/* The main loop's call each pass: unless the last picture still waits to be
 * sent, the Epoch's picture is handed to the scaler at its zoom, a column's
 * step twice a row's. Returns whether it was. */
static bool hand_the_epoch_to_the_scaler(GameState *g)
{
    if (picture_waiting(g))
        return false;
    scene_scaler_start(g, (SceneScaling){ .source = EPOCH_BITMAP, .rows = EPOCH_ROWS, .pairs = EPOCH_PAIRS,
                                          .row_step = scene_scaler_row_step(g), .limit = SCALING_ENDS_AT });
    return true;
}

// $7E:82A4
/* Back from the scaler: the picture gathered into sprite tiles in the
 * tilemap buffer, and marked to be sent. */
static void gather_the_epoch(GameState *g)
{
    TIMED_AS(g, 0x7EB574, scene_gather_picture_into_tiles(g));
    mark_the_picture(g, true);
}

// $7E:824A
/* In the vblank: a gathered picture's tiles into the sprites' tiles, and the
 * picture marked sent. */
enum { EPOCH_TILES_VRAM = 0x4000, EPOCH_TILES_BYTES = 0x0400 };

static void send_the_epoch(GameState *g, Platform *pf)
{
    if (!picture_waiting(g))
        return;
    vid_send_vram(pf, PF_AT(0x7E8272), EPOCH_TILES_VRAM, PF_GAME(SCENE_TILEMAP_BUFFER), EPOCH_TILES_BYTES,
                  SCENE_VRAM_WHOLE_WORDS);
    mark_the_picture(g, false);
}

/* How far the Epoch's picture reaches out from its point: four over its zoom,
 * in 8.8. Its list is drawn from four pixels on from its point across and its
 * reach above it, with eight sprites, or only the first four from the zoom
 * of half its size on (where the zoom's low byte reaches it); and the trail's
 * head is four pixels wider than the reach. */
enum { FOUR = 0x0400, EPOCH_LIST = 0x82B0, LIST_FROM_THE_POINT = 4, TRAIL_BEYOND_THE_PICTURE = 4 };
enum { EPOCH_SPRITES = 8, SMALL_EPOCH_SPRITES = 4, HALF_SIZE = 0x80 };

// $7E:8205
/* Each frame from outside: the Epoch drawn at its point, at the zoom the main
 * loop scales its picture at, and its trail's head sized to it. */
static void draw_the_epoch(GameState *g)
{
    uint16_t zoom = epoch(g, EPOCH_ZOOM);
    set_scene_scaler_row_step(g, zoom);
    uint16_t reach = scene_divide_8_8_by(g, scene_program_own(), FOUR, zoom);
    uint16_t top = (uint16_t)(epoch(g, EPOCH_Y) - reach);
    set_scene_sprite_offset_y(g, (uint8_t)top);
    set_scene_sprite_offset_y_high(g, (uint8_t)(top >> 8));
    set_scene_dive_trail_radius(g, (uint16_t)(reach + TRAIL_BEYOND_THE_PICTURE));
    set_scene_sprite_offset_x(g, (uint16_t)(epoch(g, EPOCH_X) + LIST_FROM_THE_POINT));
    uint8_t sprites = (uint8_t)zoom >= HALF_SIZE ? SMALL_EPOCH_SPRITES : EPOCH_SPRITES;
    set_scene_dive_epoch_sprites(g, (uint8_t)(sprites + 1));
    TIMED_AS(g, 0xC30D5E, scene_draw_sprite_list(g, EPOCH_LIST, true));
}

// $7E:7FC4
static void size_the_trail(GameState *g) { set_epoch(g, TRAIL_RADIUS, scene_dive_trail_radius(g)); }

/* The trail of light is color math in window 1, which the line effects shape
 * line by line: shut above the trail's head, then on each line of the head
 * open from the left edge of a circle (the head's round front) to the
 * screen's right edge. The circle is drawn as far in from the table's top
 * left as its radius, so that its lines are the head's from its top. */
enum { REPEATING_LINES = 0x80, TO_THE_RIGHT_EDGE = 0xFF00, TRAIL_CIRCLE = 0xE060 };

/* The trail's left edge on a line of its head: the circle's, moved across to
 * where the Epoch is, or the screen's left edge where it would start before
 * it; past the right edge it comes round from the left (a byte). The original
 * adds the lines up without clearing the carry between them, so with the
 * Epoch left of the screen's edge every line after one whose edge is on the
 * screen starts a pixel further right. */
static int16_t trail_edge(uint8_t circle_edge, int16_t x, bool after_an_edge_on_the_screen)
{
    return (int16_t)(circle_edge + x + (x < 0 && after_an_edge_on_the_screen ? 1 : 0));
}

// $7E:7FCB
static void draw_the_trail(GameState *g)
{
    uint8_t radius = (uint8_t)epoch(g, TRAIL_RADIUS);
    set_scene_circle_radius(g, radius);
    set_scene_circle_center_x(g, radius);
    set_scene_circle_center_y(g, radius);
    TIMED_AS(g, 0xC30077, scene_circle_draw(g, SCENE_CIRCLE_ROUND | SCENE_CIRCLE_KEEPS_OUTSIDE));

    uint8_t above = (uint8_t)(epoch(g, EPOCH_Y) - radius - 1);
    uint8_t head = (uint8_t)(radius * 2);
    set_scene_dive_trail_lines_above(g, above);
    set_scene_dive_trail_math_lines_above(g, above);
    set_scene_dive_trail_math_lines(g, head);
    set_scene_dive_trail_lines(g, (uint8_t)(head | REPEATING_LINES));

    int16_t x = (int16_t)epoch(g, EPOCH_X);
    bool on_the_screen = false;
    for (unsigned line = 0; line < 2u * (epoch(g, TRAIL_RADIUS) + 1u); line++) {
        int16_t edge = trail_edge(scene_dive_trail_circle_left(g, line), x, on_the_screen);
        on_the_screen = edge >= 0;
        set_scene_dive_trail_edges(g, line, (uint16_t)(TO_THE_RIGHT_EDGE | (on_the_screen ? (uint8_t)edge : 0)));
    }
}

/* ---- The Epoch leaving, seen from outside ---------------------------------------------------- */

/* Its frames of animation in the program's table (scene_dive_epoch_frames):
 * each shows its sprite list for so many frames, and the table's end sends
 * the animation back to the first. */
enum { FIRST_FRAME = 0, END_OF_THE_FRAMES = 0, FRAMES_SHOWN = RAM_SCENE_DIVE_EPOCH_FRAMES_COUNT - 1 };

/* The program keeps the Epoch's frame as the address of its list in the
 * table. The setup leaves it a frame before the first, with a frame left, so
 * that the first frame is taken at once. */
enum { BEFORE_THE_FIRST_FRAME = -1, TAKE_THE_NEXT_FRAME = 1 };

static uint16_t frame_kept_as(int16_t frame)
{
    return (uint16_t)(RAM_SCENE_DIVE_EPOCH_FRAMES_LIST + frame * (int16_t)RAM_SCENE_DIVE_EPOCH_FRAMES_STRIDE);
}

static int16_t epoch_frame(const GameState *g)
{
    int16_t from_the_first = (int16_t)(uint16_t)(scene_dive_epoch_frame(g) - RAM_SCENE_DIVE_EPOCH_FRAMES_LIST);
    return (int16_t)(from_the_first / (int16_t)RAM_SCENE_DIVE_EPOCH_FRAMES_STRIDE);
}

static void set_epoch_frame(GameState *g, int16_t frame) { set_scene_dive_epoch_frame(g, frame_kept_as(frame)); }

static int16_t frame_after(const GameState *g, int16_t frame)
{
    int16_t next = (int16_t)(frame + 1);
    return scene_dive_epoch_frames_length(g, (unsigned)next) == END_OF_THE_FRAMES ? FIRST_FRAME : next;
}

// $7E:835B
/* Each frame: when its frame's time is up, the Epoch takes its next frame;
 * it is drawn at its place. */
static void fly_the_epoch_on(GameState *g)
{
    uint16_t frames_left = (uint16_t)(scene_dive_epoch_frames_left(g) - 1);
    int16_t frame = epoch_frame(g);
    if (frames_left == 0) {
        frame = frame_after(g, frame);
        frames_left = scene_dive_epoch_frames_length(g, (unsigned)frame);
        set_epoch_frame(g, frame);
    }
    set_scene_dive_epoch_frames_left(g, frames_left);
    set_scene_sprite_offset_x(g, epoch(g, EPOCH_X));
    uint16_t y = epoch(g, EPOCH_Y);
    set_scene_sprite_offset_y(g, (uint8_t)y);
    set_scene_sprite_offset_y_high(g, (uint8_t)(y >> 8));
    TIMED_AS(g, 0x7E3A97, scene_draw_signed_sprite_list(g, scene_dive_epoch_frames_list(g, (unsigned)frame)));
}

static uint16_t current_list(const GameState *g) { return scene_dive_epoch_frames_list(g, (unsigned)epoch_frame(g)); }

/* ---- The cockpit ------------------------------------------------------------------------------ */

/* The instrument panel's sprites, three lists of the program's, drawn from
 * half a screen across: the warning lights and the gauges' needles
 * (scene_dive_lights) and the panel's rim 40 lines above its top line, and
 * the yoke 18 lines below it. The panel's top line is the line the shell
 * below ends at (perspective.c), $A8 at rest. */
enum { LIGHTS = 0x8109, YOKE = 0x819B, RIM = 0x8122 };
enum { PANEL_ACROSS = 0x80, PANEL_AT_REST = 0xA8, RIM_ABOVE_THE_PANEL = 0x28, YOKE_BELOW_THE_PANEL = 0x12 };

static void draw_from(GameState *g, uint16_t x, uint16_t y)
{
    set_scene_sprite_offset_x(g, x);
    set_scene_sprite_offset_y(g, (uint8_t)y);
    set_scene_sprite_offset_y_high(g, (uint8_t)(y >> 8));
}

/* The yoke jumps a line further each time the draw that shakes it has its
 * low bit set, and another each time it spilled. */
static uint16_t yoke_shaken(SceneDraw d) { return (uint16_t)((d & 1) + scene_draw_spilled(d, TM_ENTROPY_WORDS)); }

// $7E:7F7D
/* Each frame from the cockpit: the panel shaken down by a random line or more
 * (as many as scene_dive_shake lets the draw reach), its sprites drawn at its
 * new line, the yoke shaken a little more on its own. The first draw goes on
 * from where the panel is across, salted with the task's slot; the second
 * from where the sprite table's cursor stands in its group of four once the
 * lights are drawn. */
static void shake_the_cockpit(GameState *g, Platform *pf, uint16_t slot)
{
    uint16_t across = (uint16_t)(epoch(g, EPOCH_X) + PANEL_ACROSS);
    SceneDraw d = scene_draw_random(g, pf, PF_AT(0x7E7F85), scene_draw_from(across, false, TM_ENTROPY_WORDS), slot,
                                    TM_ENTROPY_WORDS);
    uint16_t line = (uint16_t)((d & scene_dive_shake(g)) + PANEL_AT_REST);
    set_scene_shell_below_end(g, line);
    set_epoch(g, PANEL_LINE, line);
    draw_from(g, across, (uint16_t)(line - RIM_ABOVE_THE_PANEL));
    TIMED_AS(g, 0x7E3A97, scene_draw_signed_sprite_list(g, LIGHTS));
    d = scene_draw_random(g, pf, PF_AT(0x7E7FA0), scene_draw_from(scene_sprite_place_in_group(g), false, TM_ENTROPY_WORDS),
                          slot, TM_ENTROPY_WORDS);
    draw_from(g, across, (uint16_t)(line + YOKE_BELOW_THE_PANEL + yoke_shaken(d)));
    TIMED_AS(g, 0x7E3A97, scene_draw_signed_sprite_list(g, YOKE));
    draw_from(g, across, (uint16_t)(line - RIM_ABOVE_THE_PANEL));
    TIMED_AS(g, 0x7E3A97, scene_draw_signed_sprite_list(g, RIM));
}

/* The panel's lights: the two warning lights, then the two gauges' needles,
 * two sprites each, the left gauge's from 56 pixels right of the panel's
 * middle and the right's from 64 left of it. */
enum { LEFT_WARNING_LIGHT = 0, RIGHT_WARNING_LIGHT = 1, LEFT_NEEDLE = 2, RIGHT_NEEDLE = 4, NEEDLE_SPRITES = 2 };
enum { LEFT_GAUGE = 0x38, RIGHT_GAUGE = 0xC0 };
enum { WARNING_LIGHT_PICTURES = 4, NEEDLE_PICTURES = 4 };

/* A warning light's picture from a draw: one of its four, by the draw's
 * second and third bits. */
static unsigned warning_light_picture(SceneDraw d) { return (unsigned)(d >> 1) % WARNING_LIGHT_PICTURES; }
/* A needle's from a draw's fourth and fifth bits. */
static unsigned needle_picture(SceneDraw d) { return (unsigned)(d >> 3) % NEEDLE_PICTURES; }

/* The salt each draw takes is where the last one's picture lies in its
 * table, a byte offset, as the original's index has it; the right needle's,
 * where the picture after the left needle's lies. */
static uint16_t warning_light_place(unsigned picture) { return (uint16_t)(picture * 2); }
enum { SPRITE_BYTES = 4 };
static uint16_t needle_place(unsigned picture) { return (uint16_t)(picture * NEEDLE_SPRITES * SPRITE_BYTES); }

/* The program keeps a warning light's picture, and then a gauge's place,
 * where the next draw stirs them in. */
static void keep_the_light_s_picture(GameState *g, uint16_t looks) { scene_leave_for_the_draws(g, looks, TM_ENTROPY_WORDS); }
static void keep_the_gauge(GameState *g, uint8_t gauge) { scene_leave_for_the_draws(g, gauge, TM_ENTROPY_BYTES); }

static uint16_t set_warning_light(GameState *g, unsigned light, unsigned picture)
{
    uint16_t looks = scene_dive_warning_light_pictures(g, picture);
    set_scene_dive_lights_tile(g, light, (uint8_t)looks);
    set_scene_dive_lights_attributes(g, light, (uint8_t)(looks >> 8));
    keep_the_light_s_picture(g, looks);
    return looks;
}

static void set_needle(GameState *g, unsigned needle, unsigned picture, uint8_t gauge)
{
    for (unsigned n = 0; n < NEEDLE_SPRITES; n++) {
        unsigned from = picture * NEEDLE_SPRITES + n;
        set_scene_dive_lights_tile(g, needle + n, scene_dive_needle_pictures_tile(g, from));
        set_scene_dive_lights_attributes(g, needle + n, scene_dive_needle_pictures_attributes(g, from));
        set_scene_dive_lights_x(g, needle + n, (uint8_t)(scene_dive_needle_pictures_x(g, from) + gauge));
        set_scene_dive_lights_y(g, needle + n, scene_dive_needle_pictures_y(g, from));
    }
}

/* What a draw for a needle goes on from: the gauge's place, with what the
 * draw before spilled. */
static SceneDraw from_the_gauge(uint8_t gauge, bool spilled) { return scene_draw_from(gauge, spilled, TM_ENTROPY_BYTES); }

// $7E:8079, $7E:80D3, $7E:80A6, $7E:80AF
/* Each frame from the cockpit: the warning lights and the needles take a
 * picture each at random. The first draw goes on from what the list called
 * the task with -- its own address and the list's carry -- salted with its
 * slot; each draw after goes on from the last picture it took (a light's) or
 * the gauge's place (a needle's), salted with where the last picture lies.
 * The right needle's draw starts with nothing spilled: no needle's picture
 * reaches past the screen's edge from the left gauge. */
static void flicker_the_lights(GameState *g, SceneDraw called_with, uint16_t slot)
{
    SceneDraw d = scene_draw_random_off_the_palette(g, called_with, slot, TM_ENTROPY_WORDS);
    unsigned picture = warning_light_picture(d);
    uint16_t looks = set_warning_light(g, LEFT_WARNING_LIGHT, picture);
    d = scene_draw_random_off_the_palette(g, scene_draw_from(looks, scene_draw_spilled(d, TM_ENTROPY_WORDS), TM_ENTROPY_WORDS),
                                          warning_light_place(picture), TM_ENTROPY_WORDS);
    picture = warning_light_picture(d);
    set_warning_light(g, RIGHT_WARNING_LIGHT, picture);

    keep_the_gauge(g, LEFT_GAUGE);
    d = scene_draw_random_off_the_palette(g, from_the_gauge(LEFT_GAUGE, scene_draw_spilled(d, TM_ENTROPY_WORDS)),
                                          warning_light_place(picture), TM_ENTROPY_BYTES);
    unsigned needle = needle_picture(d);
    set_needle(g, LEFT_NEEDLE, needle, LEFT_GAUGE);

    keep_the_gauge(g, RIGHT_GAUGE);
    d = scene_draw_random_off_the_palette(g, from_the_gauge(RIGHT_GAUGE, false), needle_place(needle + 1), TM_ENTROPY_BYTES);
    set_needle(g, RIGHT_NEEDLE, needle_picture(d), RIGHT_GAUGE);
}

/* The program rewrites the needles through the work RAM port, which it
 * leaves past the right needle's sprites. */
enum { NEEDLES_END = 0x8122, WORK_RAM_PORT_BANK = SCENE_LOWER_WORK_RAM };

static void leave_the_port_past_the_needles(GameState *g) { hw_port_seek(g, NEEDLES_END, WORK_RAM_PORT_BANK); }

/* The line effects' tables run Mode 7 and the shell's scroll and color math
 * from the top of the view down to the panel's top line: two entries of 84
 * and 83 lines, then the lines from 167 on. The panel below is BG1, scrolled
 * so that its first row is on that line, and across against channel 1's
 * place. */
enum { LINES_BEFORE_THE_PANEL = 167, PANEL_FIRST_ROW = 0xFF, PANEL_SCROLL_ACROSS = 0x100 };

// $7E:7F58
/* In the vblank: the tables follow the panel as it shakes. */
static void follow_the_panel(GameState *g)
{
    uint8_t line = (uint8_t)scene_shell_below_end(g);
    set_scene_dive_panel_scroll_y(g, (uint8_t)(PANEL_FIRST_ROW - line));
    uint8_t lines = (uint8_t)(line - LINES_BEFORE_THE_PANEL);
    set_scene_dive_panel_mode_lines(g, lines);
    set_scene_dive_panel_scroll_lines(g, lines);
    set_scene_dive_panel_math_lines(g, lines);
    set_scene_dive_panel_scroll_x(g, (uint16_t)(PANEL_SCROLL_ACROSS - epoch(g, EPOCH_X)));
}

/* The band of light ahead, in the table of the fixed color: the dark above
 * it, the fog thickening into it (scene_dive_fog_thickening, a line a
 * byte), its brightest part in two halves, and the fog thinning out below.
 * The light opens a frame at a time: first the dark above it shrinks, two
 * lines a frame, and the brightest part grows three, until two lines of dark
 * are left; then the fog's start moves two lines brighter and the brightest
 * part grows two a frame, until the fog starts 30 lines in, the table
 * restarted past the dark each frame. */
enum { DARK_LEFT = 2, FOG_START_MOST = 0x1E, OPENING = 2 };
enum { COLOR_SLOT = 0, LIGHT_PAST_THE_DARK = 0x7BE5 };

// $7E:7BF2
/* Returns whether the table was restarted past the dark. */
static bool open_the_light(GameState *g, Platform *pf)
{
    if (scene_dive_light_above(g) != DARK_LEFT) {
        set_scene_dive_light_above(g, (uint8_t)(scene_dive_light_above(g) - OPENING));
        set_scene_dive_light_upper_middle(g, (uint8_t)(scene_dive_light_upper_middle(g) + OPENING));
        set_scene_dive_light_lower_middle(g, (uint8_t)(scene_dive_light_lower_middle(g) + 1));
        return false;
    }
    if (scene_dive_light_rising_from(g) == FOG_START_MOST)
        return false;
    set_scene_dive_light_rising_from(g, (uint8_t)(scene_dive_light_rising_from(g) + OPENING));
    set_scene_dive_light_rising(g, (uint8_t)(scene_dive_light_rising(g) - OPENING));
    set_scene_dive_light_upper_middle(g, (uint8_t)(scene_dive_light_upper_middle(g) + OPENING));
    vid_set_line_table(pf, PF_AT(0x7E7C2B), COLOR_SLOT, LIGHT_PAST_THE_DARK);
    return true;
}

/* ---- The views ------------------------------------------------------------------------------- */

/* The frame's tasks in each view. */
typedef enum {
    CHECK_THE_SHELL = 0x82D1,
    RUN_CHANNEL_0 = 0x3749,
    RUN_CHANNEL_1 = 0x3754,
    PASS_THE_SHELL_ACROSS = 0x7F12,
    PASS_THE_SHELL_DOWN = 0x7F35,
    DRAW_THE_EPOCH = 0x8205,
    SHAKE_THE_COCKPIT = 0x7F7D,
} DiveTask;

/* From the cockpit the shell is checked twice, so that a build of the half
 * above has the half below built after it in the same frame. */
static const DiveTask from_outside[] = { CHECK_THE_SHELL, RUN_CHANNEL_0, RUN_CHANNEL_1, PASS_THE_SHELL_ACROSS, DRAW_THE_EPOCH };
static const DiveTask from_the_cockpit[] = { CHECK_THE_SHELL,   CHECK_THE_SHELL, SHAKE_THE_COCKPIT,
                                             RUN_CHANNEL_0,     RUN_CHANNEL_1,   PASS_THE_SHELL_DOWN };
enum { OUTSIDE_TASKS = sizeof from_outside / sizeof from_outside[0],
       COCKPIT_TASKS = sizeof from_the_cockpit / sizeof from_the_cockpit[0] };

// $7E:7EF8
static void take_every_frame_task_off(GameState *g)
{
    for (unsigned slot = 0; slot < SCENE_TASKS_COUNT; slot++)
        set_scene_tasks(g, slot, NO_TASK);
}

static void give_the_frame(GameState *g, const DiveTask *tasks, unsigned count)
{
    take_every_frame_task_off(g);
    for (unsigned slot = 0; slot < count; slot++)
        set_scene_tasks(g, slot, (SceneTask)tasks[slot]);
}

// $7E:7ED1
static void look_from_outside(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    give_the_frame(g, from_outside, OUTSIDE_TASKS);
}

// $7E:7EE8
static void look_from_the_cockpit(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    give_the_frame(g, from_the_cockpit, COCKPIT_TASKS);
}

/* The camera from outside: high over the shell, its place down at the top,
 * the plane turning about the middle of the view. */
enum { HIGH_OVER_THE_SHELL = 0x00FF, OUTSIDE_TOP = 0, OUTSIDE_CENTRE_DOWN = 0x70 };
/* From the cockpit: nearer. */
enum { NEAR_THE_SHELL = 0x0080 };

/* The line effects each view's screen runs (their descriptors in the
 * program), and the slots they take. */
enum { OUTSIDE_LINE_EFFECTS = 0x7B9C, COCKPIT_LINE_EFFECTS = 0x7BB6 };
enum { OUTSIDE_LINE_SLOTS = 0x1F, COCKPIT_LINE_SLOTS = 0x3F };

// $7E:79F8
/* In the vblank, once: the screen from outside, the shell in Mode 7 in
 * perspective along its curve, the fixed color black, and the line effects:
 * the plane's matrix, the fog at the shell's top and bottom, the trail's
 * window and its color math. */
static void set_the_screen_outside(GameState *g, Platform *pf)
{
    set_camera(g, SCENE_PLANE_HEIGHT, HIGH_OVER_THE_SHELL);
    set_camera(g, SCENE_PLANE_Y, OUTSIDE_TOP);
    set_camera(g, SCENE_PLANE_CENTRE_Y, OUTSIDE_CENTRE_DOWN);
    scene_look_at_the_shell_from_outside(g);
    vid_set_mode(pf, PF_AT(0x7E7A1F), &(VidMode){ .mode = 7 });
    vid_set_fixed_color(pf, PF_AT(0x7E7A24), VID_ALL_CHANNELS, 0);
    TIMED_AS(g, 0x7E36CC, (void)scene_set_up_line_effects(g, pf, OUTSIDE_LINE_EFFECTS));
    set_scene_line_effects(g, OUTSIDE_LINE_SLOTS);
}

// $7E:7A35
/* In the vblank, once: the screen from the cockpit, BG1 and the sprites on
 * the main screen, nothing on the sub screen and no window masking it, the
 * fixed color black, mode 1 (the line effects switch it to Mode 7 above the
 * panel), and the line effects: the light ahead, the plane's matrix, BG1's
 * scroll, the mode and color math. */
static void set_the_screen_in_the_cockpit(GameState *g, Platform *pf)
{
    scene_look_at_the_shell_from_the_cockpit(g);
    set_camera(g, SCENE_PLANE_HEIGHT, NEAR_THE_SHELL);
    vid_set_fixed_color(pf, PF_AT(0x7E7A4C), VID_ALL_CHANNELS, 0);
    vid_set_screens(pf, PF_AT(0x7E7A4F), 0, 0, VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E7A51), &(VidWindows){ .win_main = 0 }, VID_WINDOWS_MAIN);
    vid_set_mode(pf, PF_AT(0x7E7A54), &(VidMode){ .mode = 1 });
    vid_set_screens(pf, PF_AT(0x7E7A58), VID_BG1_BIT | VID_OBJ_BIT, 0, VID_SCREEN_MAIN);
    TIMED_AS(g, 0x7E36CC, (void)scene_set_up_line_effects(g, pf, COCKPIT_LINE_EFFECTS));
    set_scene_line_effects(g, COCKPIT_LINE_SLOTS);
}

/* ---- The colors ------------------------------------------------------------------------------ */

/* As the dive starts, the view fades in from white, colors 32 to 63 a level a
 * frame; as it ends, it goes to white, the sprites' first four palettes and
 * colors 80 to 127 eight levels a frame. Each starts with a frame of the
 * animations at once. */
static const PaletteFade view_from_white = {
    .first_color = 0x20, .color_count = 0x20, .step = 1, .period = 1, .from_level = 0xFF, .to_level = 0,
};
static const PaletteFade sprites_to_white = {
    .first_color = 0x80, .color_count = 0x40, .step = 8, .period = 1, .from_level = 0, .to_level = 0xFF,
};
static const PaletteFade layers_to_white = {
    .first_color = 0x50, .color_count = 0x30, .step = 8, .period = 1, .from_level = 0, .to_level = 0xFF,
};
enum { FIRST_FADE_SLOT = 0, SECOND_FADE_SLOT = 1 };

// $7E:82F5
static void fade_the_shell_in(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    palette_start_fade_to_white(g, FIRST_FADE_SLOT, view_from_white);
    scene_animate_the_palettes_for(g, channel);
}

// $7E:831F
static void white_out(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    palette_start_fade_to_white(g, FIRST_FADE_SLOT, sprites_to_white);
    palette_start_fade_to_white(g, SECOND_FADE_SLOT, layers_to_white);
    scene_animate_the_palettes_for(g, channel);
}

/* ---- The setup -------------------------------------------------------------------------------- */

/* The scene's resources, by their place in the program's list (three bytes
 * an entry), as the unpacker takes them: the shell's pixels and its map, the
 * scene's colors, the Epoch's bitmap, the sprites' tiles and the panel's map;
 * and the sprites' colors, copied as they are by their number. */
enum {
    SHELL_PIXELS = 0x66, SHELL_MAP = 0x69, DIVE_COLOURS = 0x6C, EPOCH_PICTURE = 0x78,
    SPRITE_TILES = 0x72, MORE_SPRITE_TILES = 0x7B, PANEL_MAP = 0x75,
};
enum { SPRITE_COLOURS = 37 };

/* Where they go: the shell's pixels in VRAM's high bytes from $2000, its map in
 * the low bytes from 0; the scene's colors from color 16; the sprites' tiles
 * at $4400 and $5000; the panel's map, BG1's, at $6000. */
enum {
    SHELL_PIXELS_VRAM = 0x2000, SHELL_MAP_VRAM = 0x0000, DIVE_COLOURS_AT = 0x7E2020,
    SPRITE_TILES_VRAM = 0x4400, MORE_SPRITE_TILES_VRAM = 0x5000, PANEL_MAP_VRAM = 0x6000,
    SPRITE_COLOURS_AT = 0x2180, SPRITE_COLOURS_BYTES = 0x20,
    EPOCH_BITMAP_AT = 0x7F0A00,
};

/* The shell's map is half a plane, each of its rows doubled across the
 * plane's 128 tiles; the half is sent twice, so the shell repeats down the
 * plane. */
enum { PLANE_HALF_BYTES = 0x2000 };

static void load_the_shell(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    vid_set_vram_address(pf, PF_AT(0x7E78E4), SHELL_PIXELS_VRAM);
    vid_start_vram_uploads(pf, PF_AT(0x7E78E9), SCENE_VRAM_WHOLE_WORDS);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    scene_bridge_unpack(s, 0x7E78F2, SHELL_PIXELS);
    scene_choose_transfer(g, scene_pixels_into_high_bytes);
    scene_send_unpacked_by(g, pf, scene_pixels_into_high_bytes);
    scene_bridge_unpack(s, 0x7E7905, SHELL_MAP);
    scene_double_plane_rows(g);
    vid_set_vram_address(pf, PF_AT(0x7E790F), SHELL_MAP_VRAM);
    vid_start_vram_uploads(pf, PF_AT(0x7E7913), SCENE_VRAM_LOW_BYTES);
    for (unsigned half = 0; half < 2; half++)
        scene_send_from(g, pf, SCENE_TRANSFER_BYTES, VID_SNES_VRAM_DATA_LOW, SCENE_PLANE_MAP, PLANE_HALF_BYTES);
}

/* The colors: the scene's, colors 80 to 111 of them copied into the sprites'
 * third and fourth palettes (colors 160 to 191), the sprites' own, and the
 * clear color a pale cyan; then the palette made current, no animation
 * running. */
enum { COPIED_FROM = 0x50, COPIED_TO = 0xA0, COPIED_COLOURS = 0x20, PALE_CYAN = 0x6F69 };

static void load_the_colours(Snes *s)
{
    GameState *g = state_of(s);
    set_decompress_dest(g, DIVE_COLOURS_AT);
    scene_bridge_unpack(s, 0x7E7945, DIVE_COLOURS);
    for (unsigned n = 0; n < COPIED_COLOURS; n++)
        set_palette_base(g, COPIED_TO + n, palette_base(g, COPIED_FROM + n));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * COPIED_COLOURS));
    scene_copy_resource_to(g, scene_program_own(), SPRITE_COLOURS, SPRITE_COLOURS_AT, SPRITE_COLOURS_BYTES);
    set_palette_base(g, 0, PALE_CYAN);
    scene_use_loaded_palette(g);
    TIMED_AS(g, 0xFDE275, palette_stop_animations(g));
}

/* The shell's curve seen from outside: each screen line's distance onto the
 * shell, from the middle of the screen out, 255 less half of every third step
 * of the game's sine. */
enum { CURVE_LINES = 224, CURVE_STEP = 3, CURVE_FARTHEST = 0xFF };

static void make_the_curve(GameState *g)
{
    for (unsigned line = 0; line < CURVE_LINES; line++)
        set_scene_shell_curve(g, line, (uint8_t)(CURVE_FARTHEST - (scene_sine_magnitude(g, (uint16_t)(line * CURVE_STEP)) >> 1)));
}

/* The fog where the shell meets the light: the fixed color's intensity
 * rising a step a line from black to white over a band, and falling over
 * another. The program counts on from where the rising band ended, so the
 * falling band's first line is $00, which picks no channel and leaves the
 * color as it was. */
enum { FOG_LINES = 32, FIXED_COLOUR_BLACK = 0xE0 };

static void make_the_fog(GameState *g)
{
    uint8_t colour = FIXED_COLOUR_BLACK;
    for (unsigned line = 0; line < FOG_LINES; line++)
        set_scene_dive_fog_thickening(g, line, colour++);
    for (unsigned line = 0; line < FOG_LINES; line++)
        set_scene_dive_fog_thinning(g, line, colour--);
}

/* The panel's map: seven rows of 32 tiles, each row's left half its sixteen
 * tiles in turn from $90, sixteen on a row, and its right half the same
 * mirrored, each tile's attributes from the unpacked map in turn; then two
 * runs of three tiles in the middle of its fourth and fifth rows. */
enum { PANEL_ROWS = 7, HALF_ROW = 16, PANEL_FIRST_TILE = 0x90 };
enum { PANEL_MIDDLE = 0x6056, PANEL_MIDDLE_BELOW = 0x6076, MIDDLE_TILES = 3, MIDDLE_TILE = 0xB7, MIDDLE_TILE_BELOW = 0xC7 };

static void lay_out_the_panel(GameState *g, Platform *pf)
{
    uint32_t attributes = SCENE_UNPACK_BUFFER;
    for (unsigned row = 0; row < PANEL_ROWS; row++) {
        uint8_t first = (uint8_t)(PANEL_FIRST_TILE + row * HALF_ROW);
        for (unsigned n = 0; n < HALF_ROW; n++)
            vid_write_vram(pf, PF_AT(0x7E7B28), (uint16_t)(wram_read8(g, attributes++) << 8 | (uint8_t)(first + n)));
        for (unsigned n = HALF_ROW; n-- > 0;)
            vid_write_vram(pf, PF_AT(0x7E7B3D), (uint16_t)(wram_read8(g, attributes++) << 8 | (uint8_t)(first + n)));
    }
    ORIGINAL_EFFECT(hw_port_seek_address(g, (uint16_t)attributes));
    vid_start_vram_uploads(pf, PF_AT(0x7E7B55), SCENE_VRAM_LOW_BYTES);
    vid_set_vram_address(pf, PF_AT(0x7E7B5B), PANEL_MIDDLE);
    for (unsigned n = 0; n < MIDDLE_TILES; n++)
        vid_write_vram_byte(pf, PF_AT(0x7E7B60), false, (uint8_t)(MIDDLE_TILE + n));
    vid_set_vram_address(pf, PF_AT(0x7E7B6E), PANEL_MIDDLE_BELOW);
    for (unsigned n = 0; n < MIDDLE_TILES; n++)
        vid_write_vram_byte(pf, PF_AT(0x7E7B73), false, (uint8_t)(MIDDLE_TILE_BELOW + n));
}

/* What the original leaves in the scene's cells as it makes the curve: the
 * last half step of the sine, and its count of lines run down. */
static void leave_the_curve_s_cells(GameState *g)
{
    SceneProgramPage scene = scene_program_own();
    set_scene_call_dive_curve_step(g, scene, (uint8_t)(scene_sine_magnitude(g, (uint16_t)((CURVE_LINES - 1) * CURVE_STEP)) >> 1));
    set_scene_call_dive_curve_lines_left(g, scene, 0);
}

/* The program writes the fog through the work RAM port, which it leaves past
 * the falling band. */
enum { FOG_END = 0xE050 };

static void leave_the_port_past_the_fog(GameState *g) { hw_port_seek(g, FOG_END, SCENE_LOWER_WORK_RAM); }

/* The panel's tiles count up in the scene's first cell, which ends past the
 * last row. */
static void leave_the_panel_s_count(GameState *g)
{
    set_scene_call_dive_panel_tile(g, scene_program_own(), (uint8_t)(PANEL_FIRST_TILE + PANEL_ROWS * HALF_ROW));
}

/* The scaled picture's output in the second bank cleared, and where the
 * scaler's last output ended. */
enum { SCALER_OUTPUT = 0x7F0200, SCALER_OUTPUT_BYTES = 0x0800, SCALER_OUTPUT_END = 0x0A00 };

// $7E:7A68
/* The rest of the scene's pieces: the trail's circle's table, the Epoch's
 * first frame of animation to take at once, the shell's curve and the fog,
 * the sprites' tiles and the panel's map. */
static void make_the_pieces(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    set_scene_scaler_output_end(g, SCALER_OUTPUT_END);
    set_scene_circle_table(g, TRAIL_CIRCLE);
    set_scene_circle_table_bank(g, SCENE_LOWER_WORK_RAM);
    set_scene_dive_epoch_frames_left(g, TAKE_THE_NEXT_FRAME);
    set_epoch_frame(g, BEFORE_THE_FIRST_FRAME);
    make_the_curve(g);
    ORIGINAL_EFFECT(leave_the_curve_s_cells(g));
    make_the_fog(g);
    ORIGINAL_EFFECT(leave_the_port_past_the_fog(g));

    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    scene_bridge_unpack(s, 0x7E7ADE, SPRITE_TILES);
    vid_start_vram_uploads(pf, PF_AT(0x7E7AE3), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E7AE9), SPRITE_TILES_VRAM);
    scene_choose_transfer(g, scene_whole_words);
    scene_send_unpacked_by(g, pf, scene_whole_words);
    scene_bridge_unpack(s, 0x7E7AFD, MORE_SPRITE_TILES);
    vid_set_vram_address(pf, PF_AT(0x7E7B03), MORE_SPRITE_TILES_VRAM);
    scene_send_unpacked_by(g, pf, scene_whole_words);
    scene_bridge_unpack(s, 0x7E7B0D, PANEL_MAP);
    vid_set_vram_address(pf, PF_AT(0x7E7B13), PANEL_MAP_VRAM);
    lay_out_the_panel(g, pf);
    ORIGINAL_EFFECT(leave_the_panel_s_count(g));
    scene_clear_buffer(pf, SCALER_OUTPUT, SCALER_OUTPUT_BYTES, 0x7E7B98);
}

/* The Epoch's bitmap, and the routine that regroups its scaled picture into
 * sprite tiles, written into the program for a picture from $0200 of the
 * second bank into the tilemap buffer. */
enum { MATRIX_DESTINATION = 0x0C00, MATRIX_SOURCE = 0x0200 };

static void load_the_epoch(Snes *s)
{
    GameState *g = state_of(s);
    set_decompress_dest(g, EPOCH_BITMAP_AT);
    scene_bridge_unpack(s, 0x7E7987, EPOCH_PICTURE);
    TIMED_AS(g, 0xC309E9, scene_write_matrix_code_for(g, MATRIX_DESTINATION, MATRIX_SOURCE));
    s->cpu.d = 0x0000;   /* the writer works on the scene's page, through bank 0 */
    s->cpu.db = 0x00;
}

/* The two channels' scripts: channel 0's moves the camera, channel 1's runs
 * the dive. Channel 0's first step is taken at once, with 16-bit index
 * registers; it reads Y, which the regrouping routine's writer left past its
 * table, and the overflow it left clear. The setup goes on in 16-bit A. */
enum { CAMERA_SCRIPT = 0x7C73, DIVE_SCRIPT = 0x7CF3 };

static void first_script_step(Snes *s)
{
    m_set_flag(s, CPU_FLAG_X | CPU_FLAG_V, false);
    scene_bridge_take_the_first_script_step(s, 0x7E79AA, scene_matrix_writer_leaves_y(), s->cpu.db);
}

/* The main loop's call each pass scales the Epoch (scene_scale_the_epoch). */
enum { MAIN_LOOP_SCALES_THE_EPOCH = 0x8278, PROGRAM_BANK = 0x7E0000 };

/* The setup ends drawing the Epoch from outside and passing the main loop's
 * call once, through their entries where they hold and the original's
 * otherwise, with a picture marked waiting so the call scales none yet. */
enum { DRAW_THE_EPOCH_CALL = 0x7E79CA, SCALE_THE_EPOCH_CALL = 0x7E79CD };
enum { THE_DRAWING_READS_THE_CARRY = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z, THE_CALL_PASSES_V_AND_C_ON = DC_A | DC_X | DC_Y | DC_N | DC_Z };

static bool draw_and_scale_once(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.d = 0x0000;
    mark_the_picture(g, true);
    return scene_run(s, DRAW_THE_EPOCH_CALL, PROGRAM_BANK | DRAW_THE_EPOCH, THE_DRAWING_READS_THE_CARRY, scene_draw_the_epoch) &&
           scene_run(s, SCALE_THE_EPOCH_CALL, PROGRAM_BANK | MAIN_LOOP_SCALES_THE_EPOCH, THE_CALL_PASSES_V_AND_C_ON,
                     scene_scale_the_epoch);
}

/* The screen: BG1, BG2 and the sprites on both screens and masked by the
 * windows on the main one, window 1 over BG1, BG2, the sprites and color math,
 * the fixed color black, Mode 7's BG2 carrying priorities (and interlace on),
 * BG1's map at $6000 and its tiles at $4000. */
enum { LAYERS = VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT, SHELL_TILES_VRAM = 0x4000 };

static const VidBg panel_map = { VID_TILEMAP(PANEL_MAP_VRAM, 32, 32) };

static void set_up_the_screen(Platform *pf)
{
    vid_set_screens(pf, PF_AT(0x7E79DB), LAYERS, 0, VID_SCREEN_MAIN);
    vid_set_windows(pf, PF_AT(0x7E79DD), &(VidWindows){ .win_main = LAYERS }, VID_WINDOWS_MAIN);
    vid_set_screens(pf, PF_AT(0x7E79DF), 0, LAYERS, VID_SCREEN_SUB);
    vid_set_fixed_color(pf, PF_AT(0x7E79E3), VID_ALL_CHANNELS, 0);
    vid_set_video_config(pf, PF_AT(0x7E79E7), &(VidVideoConfig){ .interlace = true, .extbg = true });
    VidWindows w1 = { 0 };
    w1.w1[VID_BG1] = w1.w1[VID_BG2] = w1.w1[VID_OBJ] = w1.w1[VID_BACK] = VID_WIN_IN;
    vid_set_windows(pf, PF_AT(0x7E79EB), &w1, VID_WINDOWS_BG1_BG2);
    vid_set_windows(pf, PF_AT(0x7E79ED), &w1, VID_WINDOWS_OBJ_BACK);
    vid_set_bg_map(pf, PF_AT(0x7E79F1), VID_BG1, &panel_map);
    vid_set_bg_chars(pf, PF_AT(0x7E79F5), VID_BG1, SHELL_TILES_VRAM, 0);
}

/* The setup calls its pieces' routine ($7E:7A68) from here, whose unpacking
 * calls the original's unpacker under that call's return. */
enum { PIECES_CALL = 0x7E7976 };
/* The routine leaves the direct page on the DMA registers', where it
 * programmed the clear, and the unpacker after it is called on that page. */
enum { PIECES_LEAVE_THE_PAGE = M_DP_DMA };

// $7E:78DD
/* The scene's setup, from the program's mode table (mode 6). False where a
 * call of the original's stopped. */
static bool set_up_the_dive(Snes *s)
{
    GameState *g = state_of(s);
    load_the_shell(s);
    load_the_colours(s);
    scene_bridge_enter(s, PIECES_CALL);
    make_the_pieces(s);
    scene_bridge_leave(s);
    s->cpu.d = PIECES_LEAVE_THE_PAGE;
    load_the_epoch(s);
    set_scene_channels_script(g, SCENE_CAMERA_CHANNEL, CAMERA_SCRIPT);
    first_script_step(s);
    set_scene_channels_script(g, EPOCH, DIVE_SCRIPT);
    set_scene_main_loop_call(g, MAIN_LOOP_SCALES_THE_EPOCH);
    set_scene_shell_below_end(g, PANEL_AT_REST);
    set_epoch(g, PANEL_LINE, PANEL_AT_REST);
    if (!draw_and_scale_once(s))
        return false;
    set_up_the_screen(m_platform(s));
    return true;
}

/* ---- Entries ---------------------------------------------------------------------------------- */

/* The routines the scripts call, as C (scene/script.c). */
const SceneScriptRoutine scene_dive_routines[] = {
    [SCENE_ROW_LOOK_FROM_OUTSIDE] = { SCENE_ROUTINE_LOOK_FROM_OUTSIDE, look_from_outside, scene_look_from_outside_present, true },
    [SCENE_ROW_LOOK_FROM_THE_COCKPIT] = { SCENE_ROUTINE_LOOK_FROM_THE_COCKPIT, look_from_the_cockpit,
                                          scene_look_from_the_cockpit_present, true },
    [SCENE_ROW_FADE_THE_SHELL_IN] = { SCENE_ROUTINE_FADE_THE_SHELL_IN, fade_the_shell_in, scene_fade_the_shell_in_present, true },
    [SCENE_ROW_WHITE_OUT] = { SCENE_ROUTINE_WHITE_OUT, white_out, scene_white_out_present, true },
};
const unsigned scene_dive_routine_count = sizeof scene_dive_routines / sizeof scene_dive_routines[0];

#if ORIGINAL_EFFECTS
/* The views' switches copy their list with a block move, which leaves A past
 * its count and Y past the tasks it copied. */
enum { BLOCK_MOVE_DONE = 0xFFFF, TASK_BYTES = 2 };

static uint16_t leaves_the_move_done(uint16_t called_with)
{
    (void)called_with;
    return BLOCK_MOVE_DONE;
}

static void leaves_y_past_the_outside_tasks(Snes *s) { s->cpu.y = (uint16_t)(RAM_SCENE_TASKS + OUTSIDE_TASKS * TASK_BYTES); }
static void leaves_y_past_the_cockpit_tasks(Snes *s) { s->cpu.y = (uint16_t)(RAM_SCENE_TASKS + COCKPIT_TASKS * TASK_BYTES); }

/* The fades leave A and Y as the animations leave them, past their slots, and
 * the overflow clear. */
static uint16_t leaves_the_animations(uint16_t called_with)
{
    (void)called_with;
    return palette_animations_leave();
}

static void leaves_the_animations_registers(Snes *s)
{
    s->cpu.y = palette_animations_leave();
    m_set_flag(s, CPU_FLAG_V, false);
}

const SceneRoutineLeaves scene_dive_routine_leaves[] = {
    [SCENE_ROW_LOOK_FROM_OUTSIDE] = { leaves_the_move_done, leaves_y_past_the_outside_tasks },
    [SCENE_ROW_LOOK_FROM_THE_COCKPIT] = { leaves_the_move_done, leaves_y_past_the_cockpit_tasks },
    [SCENE_ROW_FADE_THE_SHELL_IN] = { leaves_the_animations, leaves_the_animations_registers },
    [SCENE_ROW_WHITE_OUT] = { leaves_the_animations, leaves_the_animations_registers },
};
#endif

void scene_look_from_outside(Snes *s) { scene_script_routine_alone(s, &scene_dive_routines[SCENE_ROW_LOOK_FROM_OUTSIDE]); }
void scene_look_from_the_cockpit(Snes *s) { scene_script_routine_alone(s, &scene_dive_routines[SCENE_ROW_LOOK_FROM_THE_COCKPIT]); }
void scene_fade_the_shell_in(Snes *s) { scene_script_routine_alone(s, &scene_dive_routines[SCENE_ROW_FADE_THE_SHELL_IN]); }
void scene_white_out(Snes *s) { scene_script_routine_alone(s, &scene_dive_routines[SCENE_ROW_WHITE_OUT]); }

/* The frame's tasks, 16-bit, each leaving A as its last load left it. */
void scene_pass_the_shell_across(Snes *s)
{
    GameState *g = state_of(s);
    pass_the_shell(g, ACROSS);
    ORIGINAL_EFFECT(s->cpu.a = camera(g, ACROSS.centre));
    m_rts(s);
}

void scene_pass_the_shell_down(Snes *s)
{
    GameState *g = state_of(s);
    pass_the_shell(g, DOWN);
    ORIGINAL_EFFECT(s->cpu.a = camera(g, DOWN.centre));
    m_rts(s);
}

void scene_let_the_shell_fall_away(Snes *s)
{
    GameState *g = state_of(s);
    let_the_shell_fall_away(g);
    ORIGINAL_EFFECT(s->cpu.a = camera(g, SCENE_PLANE_Y));
    m_rts(s);
}

/* Unchanged, A holds the last register compared. A new perspective is built
 * on channel 0's page, where the original jumps on to it, and where the port
 * doesn't build it the original does. */
enum { BUILD_PERSPECTIVE = 0x7E357B, CAMERA_PAGE = 0x0A00 };

static void leave_the_shell_checked(Snes *s, const GameState *g, bool built)
{
    if (built)
        scene_leave_perspective_registers(s, SCENE_CAMERA_CHANNEL);
    else
        s->cpu.a = camera(g, SCENE_PLANE_SHELL_FRAMES);
}

void scene_check_the_shell(Snes *s)
{
    GameState *g = state_of(s);
    bool moved = the_camera_moved(g);
    s->cpu.d = CAMERA_PAGE;
    if (moved && !scene_perspective_as_ported(s, CAMERA_PAGE)) {
        m_stop_at_own(s, BUILD_PERSPECTIVE, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    if (moved)
        TIMED_AS(g, BUILD_PERSPECTIVE, scene_build_perspective(g, SCENE_CAMERA_CHANNEL));
    ORIGINAL_EFFECT(leave_the_shell_checked(s, g, moved));
    m_rts(s);
}

/* The main loop's call each pass, with 16-bit A. With the last picture still
 * to be sent it is back at once; otherwise the scaler's rows go on in the
 * original's code, which comes back to the gathering. */
void scene_scale_the_epoch(Snes *s)
{
    GameState *g = state_of(s);
    if (!hand_the_epoch_to_the_scaler(g)) {
        ORIGINAL_EFFECT(m_lda16(s, scene_dive_picture_waiting(g)));
        m_rts(s);
        return;
    }
    scene_bridge_scale_scene_picture(s, SCALING_THE_EPOCH);
}

/* Back from the scaler with 16-bit A; back to the main loop with the data
 * bank the scaling kept on the stack. */
void scene_gather_the_epoch(Snes *s)
{
    GameState *g = state_of(s);
    gather_the_epoch(g);
    s->cpu.db = m_pull8(s);
    ORIGINAL_EFFECT(m_lda16(s, PICTURE_WAITING));
    m_rts(s);
}

/* A vblank task; nothing it leaves is read. */
void scene_send_the_epoch(Snes *s)
{
    send_the_epoch(state_of(s), m_platform(s));
    m_rts(s);
}

/* Y is the next free sprite entry, and A and V as the clipped list leaves
 * them. */
void scene_draw_the_epoch(Snes *s)
{
    GameState *g = state_of(s);
    draw_the_epoch(g);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_clipped_list_leaves_a(g, EPOCH_LIST, EPOCH_LIST));
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, scene_clipped_list_leaves_overflow(g, EPOCH_LIST)));
    m_rts(s);
}

void scene_size_the_trail(Snes *s)
{
    GameState *g = state_of(s);
    size_the_trail(g);
    ORIGINAL_EFFECT(s->cpu.a = epoch(g, TRAIL_RADIUS));
    m_rts(s);
}

/* Back with 16-bit A, which holds the last edge (or the lines' count, where
 * there were none), and Y run down to 0. */
void scene_draw_the_trail(Snes *s)
{
    GameState *g = state_of(s);
    draw_the_trail(g);
    unsigned lines = 2u * (epoch(g, TRAIL_RADIUS) + 1u);
    ORIGINAL_EFFECT(s->cpu.a = (uint16_t)lines ? scene_dive_trail_edges(g, lines - 1) : 0);
    s->cpu.y = 0;
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

/* Y is the next free sprite entry and A as the signed list leaves it. */
void scene_fly_the_epoch_on(Snes *s)
{
    GameState *g = state_of(s);
    fly_the_epoch_on(g);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_signed_list_leaves_a(g, current_list(g)));
    m_rts(s);
}

/* A frame task: the slot it runs in salts the draws. Y is the next free
 * sprite entry. */
void scene_shake_the_cockpit(Snes *s)
{
    GameState *g = state_of(s);
    shake_the_cockpit(g, m_platform(s), s->cpu.x);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_signed_list_leaves_a(g, RIM));
    m_rts(s);
}

/* A frame task, called with its own address in A, its slot in X and the
 * list's carry; back with 8-bit A, as its last byte store left it over the
 * high byte its cleared before. */
void scene_flicker_the_lights(Snes *s)
{
    GameState *g = state_of(s);
    flicker_the_lights(g, scene_draw_from(s->cpu.a, m_flag(s, CPU_FLAG_C), TM_ENTROPY_WORDS), s->cpu.x);
    ORIGINAL_EFFECT(leave_the_port_past_the_needles(g));
    ORIGINAL_EFFECT(s->cpu.a = scene_dive_lights_y(g, RIGHT_NEEDLE + NEEDLE_SPRITES - 1));
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* A vblank task, called with 8-bit A; back with it 16-bit. */
void scene_follow_the_panel(Snes *s)
{
    GameState *g = state_of(s);
    follow_the_panel(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_dive_panel_scroll_x(g));
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

/* A frame task; back with 8-bit A, or 16-bit where it restarted the table. */
void scene_open_the_light(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t above_before = scene_dive_light_above(g);
    bool restarted = open_the_light(g, m_platform(s));
    s->cpu.p |= CPU_FLAG_M;
    if (restarted) {
        ORIGINAL_EFFECT(s->cpu.a = LIGHT_PAST_THE_DARK);
        s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    } else {
        ORIGINAL_EFFECT(s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) |
                                              (above_before == DARK_LEFT ? scene_dive_light_rising_from(g) : above_before)));
    }
    m_rts(s);
}

/* Entered from the program's mode table with 8-bit A; back on the registers'
 * page with the data bank 0, as its last stores leave them, and the overflow
 * the Epoch's drawing left. */
enum { REGISTERS_PAGE = M_DP_PPU };

bool scene_dive_set_up_scene(Snes *s)
{
    if (!set_up_the_dive(s))
        return false;
    s->cpu.d = REGISTERS_PAGE;
    s->cpu.db = 0x00;
    s->cpu.p |= CPU_FLAG_M;
    return true;
}

void scene_dive_set_up(Snes *s)
{
    if (scene_dive_set_up_scene(s))
        m_rts(s);
}

/* The one-off vblank tasks take themselves off their list through its exit,
 * which reads none of the registers. */
enum { REMOVE_THIS_VBLANK_TASK = 0x7E39DE, NOTHING_READ = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

void scene_set_the_screen_outside(Snes *s)
{
    set_the_screen_outside(state_of(s), m_platform(s));
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

void scene_set_the_screen_in_the_cockpit(Snes *s)
{
    set_the_screen_in_the_cockpit(state_of(s), m_platform(s));
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* The program's routines the ports here run as C: its draw, the signed
 * sprite list, the 8.8 divide, the sine and the multiply (for the
 * perspective), the per-line effects' setup. */
static bool draw_present(Snes *s) { return m_code_matches(s, 0x7E3443, 0x5, 0x5E005F3C3E12B1C5ull); }
static bool divide_8_8_present(Snes *s) { return m_code_matches(s, 0x7E32E2, 0xBD, 0xD04B8C1274AF93C8ull); }
static bool line_effects_present(Snes *s) { return m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull); }

/* A frame task that adds the carry the list hands it: the list's loop leaves
 * it clear at every slot but the first, and the guards turn away a task
 * entered with it set. */
static bool list_carry_clear(Snes *s) { return !m_flag(s, CPU_FLAG_C); }

/* The shell's passing: the place past the camera inside the range the port
 * keeps round the shell (from 32767 pixels before its start to 32767 past),
 * which every speed a script gives keeps it in. */
enum { FAR_PAST_THE_END = 0x8000, FAR_BEFORE_THE_START = 0x8400 };

static bool passes_within_reach(Snes *s, ShellPass pass)
{
    vars_peek(true);
    uint16_t place = (uint16_t)shell_passed(state_of(s), pass);
    vars_peek(false);
    return place < FAR_PAST_THE_END || place >= FAR_BEFORE_THE_START;
}

bool scene_pass_the_shell_across_present(Snes *s)
{
    return list_carry_clear(s) && m_code_matches(s, 0x7E7F12, 0x23, 0x0C8B8BEF689AD883ull) && passes_within_reach(s, ACROSS);
}

bool scene_pass_the_shell_down_present(Snes *s)
{
    return list_carry_clear(s) && m_code_matches(s, 0x7E7F35, 0x23, 0x6B90C11F7A58022Dull) && passes_within_reach(s, DOWN);
}

bool scene_let_the_shell_fall_away_present(Snes *s)
{
    return list_carry_clear(s) && m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E81F1, 0xA, 0xEDB327407962EFF1ull);
}

bool scene_check_the_shell_present(Snes *s) { return m_code_matches(s, 0x7E82D1, 0x24, 0xD2DAEB1BF76D92EDull); }

/* The picture's mark is a byte the original tests and stores as a word with
 * the byte after it, which nothing in the dive sets: the guards of the mark's
 * word-wide test and stores hold that byte at 0, where the mark is the word. */
static bool mark_whole(const GameState *g) { return scene_dive_picture_mark_high(g) == 0; }

static bool mark_whole_at(Snes *s)
{
    vars_peek(true);
    bool ok = mark_whole(state_of(s));
    vars_peek(false);
    return ok;
}

/* The scaling: its own code and the gathering's after the scaler, on the
 * scene's page; a picture handed to the scaler at a zoom under $8000, whose
 * first row then starts with the carry the zoom's doubling leaves clear. */
enum { ZOOM_TOP_BIT = 0x8000 };

bool scene_scale_the_epoch_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_code_matches(s, 0x7E8278, 0x38, 0x4D4B756D9878E542ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = mark_whole(g) && (picture_waiting(g) || scene_scaler_row_step(g) < ZOOM_TOP_BIT);
    vars_peek(false);
    return ok;
}

bool scene_gather_the_epoch_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E82A4, 0xC, 0x817FD8C231E3ABB3ull) && scene_matrix_code_present(s) &&
           mark_whole_at(s);
}

bool scene_send_the_epoch_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E824A, 0x2E, 0xC36B3E87121CA7F4ull);
}

/* The Epoch's drawing: the list's carry clear, which the divide's leftover
 * takes in; a point no higher up the screen than the picture reaches; room
 * for its sprites. */
bool scene_draw_the_epoch_present(Snes *s)
{
    if (!list_carry_clear(s) || !scene_on_scene_page_low_bank(s) || !m_code_matches(s, 0x7E8205, 0x45, 0xAB9655DF25833B40ull) ||
        !divide_8_8_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t zoom = epoch(g, EPOCH_ZOOM);
    uint16_t reach = zoom ? (uint16_t)(FOUR / zoom) : 0;
    bool ok = epoch(g, EPOCH_Y) >= reach && scene_sprites_fit(g, EPOCH_SPRITES);
    vars_peek(false);
    return ok;
}

bool scene_size_the_trail_present(Snes *s) { return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E7FC4, 0x7, 0x3BAC732DDA6CB91Bull); }

/* The trail: the circle drawn into the dive's table, a head small enough for
 * it (under 32 lines' radius), its cells reached through the data bank. */
enum { LARGEST_TRAIL_RADIUS = 31 };

bool scene_draw_the_trail_present(Snes *s)
{
    if (!m_bank_reaches_low_ram(s) || !m_code_matches(s, 0x7E7FCB, 0x54, 0xB54091A50E168F11ull) || !scene_circle_table_is_clear(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = epoch(g, TRAIL_RADIUS) <= LARGEST_TRAIL_RADIUS && scene_circle_table(g) == TRAIL_CIRCLE &&
              !(scene_circle_table_bank(g) & 1);
    vars_peek(false);
    return ok;
}

/* The Epoch leaving: its frame one of the table's, or before the first where
 * the setup leaves it to take the first at once. */
static bool at_a_frame(const GameState *g)
{
    uint16_t at = scene_dive_epoch_frame(g);
    for (int16_t frame = BEFORE_THE_FIRST_FRAME; frame < FRAMES_SHOWN; frame++)
        if (at == frame_kept_as(frame))
            return frame != BEFORE_THE_FIRST_FRAME || scene_dive_epoch_frames_left(g) == TAKE_THE_NEXT_FRAME;
    return false;
}

static uint16_t list_to_draw(const GameState *g)
{
    int16_t frame = epoch_frame(g);
    if ((uint16_t)(scene_dive_epoch_frames_left(g) - 1) == 0)
        frame = frame_after(g, frame);
    return scene_dive_epoch_frames_list(g, (unsigned)frame);
}

bool scene_fly_the_epoch_on_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_code_matches(s, 0x7E835B, 0x30, 0x4B129A509997A196ull) ||
        !m_code_matches(s, 0x7E84F8, 0x40, 0x6F5E56370F667484ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = at_a_frame(g) && scene_sprites_fit(g, scene_sprite_list_length(g, list_to_draw(g)));
    vars_peek(false);
    return fits && scene_signed_sprite_list_code_present(s);
}

/* The cockpit's shaking: the panel across from a place that leaves no carry,
 * room for the three lists' sprites, and the lights' last sprite on the
 * picture however the panel shakes, which is where the drawing leaves the
 * cursor's place in its group for the second draw to go on from. */
enum { LAST_LIGHT = 5, LIGHTS_COUNT = 6, PICTURE_LINES = 0xE0, SCREEN_WIDTH = 0x100 };

static bool last_light_on_the_picture(const GameState *g)
{
    int16_t x = (int16_t)(int8_t)scene_dive_lights_x(g, LAST_LIGHT) + (int16_t)(epoch(g, EPOCH_X) + PANEL_ACROSS);
    int16_t y_top = (int16_t)((int8_t)scene_dive_lights_y(g, LAST_LIGHT) + PANEL_AT_REST - RIM_ABOVE_THE_PANEL);
    int16_t y_bottom = (int16_t)(y_top + scene_dive_shake(g));
    return x >= 0 && x < SCREEN_WIDTH && y_top >= 0 && y_bottom < PICTURE_LINES && scene_dive_shake(g) < PICTURE_LINES;
}

bool scene_shake_the_cockpit_present(Snes *s)
{
    if (!list_carry_clear(s) || !scene_on_scene_page_io_bank(s) || !m_code_matches(s, 0x7E7F7D, 0x47, 0x1A85BF4B8E243A23ull) ||
        !draw_present(s) || !scene_signed_sprite_list_code_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = epoch(g, EPOCH_X) < (uint16_t)(0x10000 - PANEL_ACROSS) && scene_sprite_list_length(g, LIGHTS) == LIGHTS_COUNT &&
              last_light_on_the_picture(g) &&
              scene_sprites_fit(g, (unsigned)(LIGHTS_COUNT + scene_sprite_list_length(g, YOKE) + scene_sprite_list_length(g, RIM)));
    vars_peek(false);
    return ok;
}

/* The lights' flickering: its code with the pictures' tables after it, on
 * the scene's page. */
bool scene_flicker_the_lights_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E8079, 0x90, 0x106397908BCA5DD8ull) && draw_present(s);
}

/* The panel's lines: a panel at or below line 167, the tables' third entries'
 * line. */
bool scene_follow_the_panel_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_code_matches(s, 0x7E7F58, 0x25, 0xB19803F4A639B912ull))
        return false;
    vars_peek(true);
    bool ok = (uint8_t)scene_shell_below_end(state_of(s)) >= LINES_BEFORE_THE_PANEL;
    vars_peek(false);
    return ok;
}

bool scene_open_the_light_present(Snes *s) { return m_code_matches(s, 0x7E7BF2, 0x3E, 0x51B7064CC84DD68Bull); }

/* The views' screens: their code, their line effects' descriptors and the
 * setup's code, reaching the registers through the data bank; from outside,
 * the step along the curve the program holds. */
bool scene_set_the_screen_outside_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E79F8, 0x3D, 0x0A7FB37C9164F422ull) &&
           m_code_matches(s, 0x7E7B9C, 0x1A, 0xEEBCBFE16BA5063Aull) && m_code_matches(s, 0x7E368D, 0x2, 0x09E59C07B65D2669ull) && line_effects_present(s);
}

bool scene_set_the_screen_in_the_cockpit_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E7A35, 0x33, 0x3CBD0BBE8A56DFA0ull) &&
           m_code_matches(s, 0x7E7BB6, 0x1F, 0x80F13F719E6EF701ull) && line_effects_present(s);
}

/* The script's routines: the frame's tasks for each view (the code and the
 * two lists it copies), and the colors' fades with the animations' frame. */
static bool views_present(Snes *s) { return m_code_matches(s, 0x7E7EC7, 0x3E, 0xE9413537E04A45A6ull); }
bool scene_look_from_outside_present(Snes *s) { return views_present(s); }
bool scene_look_from_the_cockpit_present(Snes *s) { return views_present(s); }

bool scene_fade_the_shell_in_present(Snes *s)
{
    return m_code_matches(s, 0x7E82F5, 0x2A, 0xBAF3E9D3FA7D4D5Cull) && scene_animate_the_palettes_present(s);
}

bool scene_white_out_present(Snes *s)
{
    return m_code_matches(s, 0x7E831F, 0x3C, 0xD565FC3F7E988E8Bull) && scene_animate_the_palettes_present(s);
}

/* The setup's code, and the program's routines it runs as C inline: the
 * doubling of the plane's rows, the palette made current, the two transfers,
 * the copy of a resource, the unpacker's destination set; and no picture
 * waiting yet, as the scene's clear leaves the mark, which the setup's word-wide
 * increment of it then sets. */
static bool no_picture_yet(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = !picture_waiting(g) && mark_whole(g);
    vars_peek(false);
    return ok;
}

bool scene_dive_set_up_present(Snes *s)
{
    return scene_on_scene_page(s) && m_bank_reaches_io(s) && m_code_matches(s, 0x7E78DD, 0x11B, 0xF2886D2399F990F4ull) &&
           m_code_matches(s, 0x7E7A68, 0x134, 0x4FCD71723D76F63Dull) && m_code_matches(s, 0x7E37A6, 0x24, 0x5FBEEF82E0975340ull) &&
           m_code_matches(s, 0x7E38AF, 0x14, 0xD31DAC893D4743DCull) && m_code_matches(s, 0x7E38C3, 0x48, 0x77070614CC728768ull) &&
           m_code_matches(s, 0x7E36AA, 0x22, 0xCCA1DE26086A09CBull) && m_code_matches(s, 0x7E89ED, 0x14, 0x22A581F747CC4E58ull) &&
           no_picture_yet(s);
}
