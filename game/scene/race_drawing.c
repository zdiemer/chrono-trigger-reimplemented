/* Scenes: the jet bike race drawn.
 *
 * Each racer is a picture scaled to its distance from the camera: the race
 * asks the program's main loop for a scaled picture (scene_race_picture_
 * requests: which picture, by the way the camera faces and the bike's frame
 * of animation, and Crono's lean, and how big), the main loop scales it and
 * gathers it into tiles (race_matrix.c) and the vblank sends them to the
 * racer's half of VRAM, and every frame the racer's sprite list is drawn over
 * them where the bike is on the screen, fewer of its sprites the farther it
 * is. The nearer bike is drawn first, so its sprites cover the other's.
 *
 * Unless the race runs without it, the panel goes round the screen's edge: the
 * distance bar with a marker for each bike, the boosts Crono has left, the
 * distance still to go and his score. The bar is drawn by the multiplier: its
 * length times a bike's distance gives the marker's place back from its end. */
#include "scene/race.h"

#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/input.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The race is drawn without its panel when the scene's flags say so. */
static bool panel_shown(const GameState *g) { return !(scene_flags(g) & SCENE_FLAG_NO_PANEL); }

/* Compared as the race compares: by the sign of the difference. */
static bool at_least(uint16_t a, uint16_t b) { return (int16_t)(a - b) >= 0; }

static void set_sprite_offsets(GameState *g, uint16_t x, uint16_t y)
{
    set_scene_sprite_offset_x(g, x);
    set_scene_sprite_offset_y(g, (uint8_t)y);
    set_scene_sprite_offset_y_high(g, (uint8_t)(y >> 8));
}

static void draw_sprite(GameState *g, uint16_t source) { TIMED_AS(g, 0xC30DBC, scene_draw_sprite(g, source)); }
static void draw_list(GameState *g, uint16_t list) { TIMED_AS(g, 0xC30D0E, scene_draw_sprite_list(g, list, false)); }
static void draw_clipped_list(GameState *g, uint16_t list) { TIMED_AS(g, 0xC30D5E, scene_draw_sprite_list(g, list, true)); }

/* ---- The racers' pictures --------------------------------------------------------------------- */


/* The racers' sprite lists, in the program's data. */
enum { CRONO_SPRITES = (uint16_t)0x7E531B, JOHNNY_SPRITES = (uint16_t)0x7E535C };
static const uint16_t racer_sprites[] = { CRONO_SPRITES, JOHNNY_SPRITES };

/* A gathered picture is marked ready for the vblank to send to the racer's
 * half of VRAM; the race counts the mark up as a word. */
static void mark_tiles_ready(GameState *g, Racer r)
{
    uint8_t mark = r == RACER_CRONO ? scene_race_tilemap_a_ready(g) : scene_race_tilemap_b_ready(g);
    uint16_t counted = (uint16_t)((mark | scene_race_tilemap_ready_high(g, r) << 8) + 1);
    if (r == RACER_CRONO)
        set_scene_race_tilemap_a_ready(g, (uint8_t)counted);
    else
        set_scene_race_tilemap_b_ready(g, (uint8_t)counted);
    set_scene_race_tilemap_ready_high(g, r, (uint8_t)(counted >> 8));
}

/* Whether the racer's last request has been scaled; if it was gathered, its
 * tiles are marked ready and the request done. */
static bool picture_taken(GameState *g, Racer r)
{
    PictureState state = race_picture_state(g, r);
    if (state == PICTURE_WAITING)
        return false;
    if (state == PICTURE_GATHERED) {
        set_scene_race_picture_requests_state(g, r, PICTURE_NONE);
        mark_tiles_ready(g, r);
    }
    return true;
}

/* How many of a racer's sprites show, by its size: all its pose's when near,
 * fewer farther away. */
enum { NEAR = 0x80, MIDDLE = 0x100, MIDDLE_SPRITES = 5, FAR_SPRITES = 2, JOHNNY_NEAR_SPRITES = 10 };

static uint8_t sprites_at(uint16_t size, uint8_t near_sprites)
{
    if (!at_least(size, NEAR))
        return near_sprites;
    return at_least(size, MIDDLE) ? FAR_SPRITES : MIDDLE_SPRITES;
}

/* The nearest a racer is drawn at (its scale: the smaller, the bigger its
 * picture), and its frames of animation: a bike
 * changes frame every other count of the race's animation, Crono's on the odd
 * counts and Johnny's on the even. */
enum { CRONO_NEAREST = 0x80, JOHNNY_NEAREST = 0x40, SIZE_MASK = 0xFFF, OTHER_FRAME = 8, LEANS = 16 };

static unsigned picture_index(unsigned pose, bool other_frame) { return pose + (other_frame ? OTHER_FRAME : 0); }

/* A picture asked of the main loop: where it is in the bank of pictures, how
 * many rows and how wide, where its rows end, and how far down each scaled
 * row steps (half the racer's size). */
typedef struct {
    uint16_t source, rows, pairs, source_limit, row_step;
} PictureRequest;

static void ask_for_picture(GameState *g, Racer r, const PictureRequest *p)
{
    set_scene_race_picture_requests_source(g, r, p->source);
    set_scene_race_picture_requests_rows(g, r, p->rows);
    set_scene_race_picture_requests_pairs(g, r, p->pairs);
    set_scene_race_picture_requests_source_limit(g, r, p->source_limit);
    set_scene_race_picture_requests_row_step(g, r, p->row_step);
    set_scene_race_picture_requests_state(g, r, PICTURE_WAITING);
}

/* The race's animation counts on each time Crono's picture is asked for. */
static uint16_t count_animation(GameState *g)
{
    uint16_t counts = (uint16_t)(race_camera(g, RACE_ANIMATION) + 1);
    set_race_camera(g, RACE_ANIMATION, counts);
    return counts;
}

/* Crono's picture for this frame: his pose's, by his lean and his frame of
 * animation, at his size from channel 1, never nearer than CRONO_NEAREST. How
 * many of his sprites show follows the size he was asked at last time. */
enum { CRONO_PAIRS = 4 };

static unsigned crono_frame(unsigned pose, uint16_t animation) { return picture_index(pose, animation & 1); }

static void ask_crono_picture(GameState *g, unsigned pose)
{
    uint8_t far_count = (uint8_t)scene_race_crono_sprite_counts(g, pose);
    set_scene_race_sprite_list_counts(g, RACER_CRONO,
                                      sprites_at(scene_race_picture_requests_row_step(g, RACER_CRONO), far_count));
    uint16_t size = race_channel_1(g, RACE_CRONO_SCALE);
    if (!at_least(size, CRONO_NEAREST)) {
        size = CRONO_NEAREST;
        set_race_channel_1(g, RACE_CRONO_SCALE, size);
    }
    unsigned frame = crono_frame(pose, count_animation(g));
    PictureRequest p = {
        .source = scene_race_crono_frames(g, scene_race_crono_lean(g) * LEANS + frame),
        .rows = scene_race_crono_frame_rows(g, pose),
        .pairs = CRONO_PAIRS,
        .source_limit = scene_race_crono_frame_ends(g, pose),
        .row_step = size >> 1,
    };
    ask_for_picture(g, RACER_CRONO, &p);
}

/* Johnny's picture: his pose's, by his frame of animation, at his size from
 * channel 1, never nearer than JOHNNY_NEAREST; all his pictures are 3 pairs wide
 * and $30 rows, ending at $600. */
enum { JOHNNY_PAIRS = 3, JOHNNY_ROWS = 0x30, JOHNNY_FRAME_END = 0x600 };

static void ask_johnny_picture(GameState *g, unsigned pose)
{
    uint16_t size = (race_channel_1(g, RACE_JOHNNY_SCALE) >> 1) & SIZE_MASK;
    if (!at_least(size, JOHNNY_NEAREST)) {
        size = JOHNNY_NEAREST;
        set_race_channel_1(g, RACE_JOHNNY_SCALE, size);
    }
    set_scene_race_sprite_list_counts(g, RACER_JOHNNY, sprites_at(size, JOHNNY_NEAR_SPRITES));
    unsigned frame = picture_index(pose, !(race_camera(g, RACE_ANIMATION) & 1));
    PictureRequest p = {
        .source = scene_race_johnny_frames(g, frame),
        .rows = JOHNNY_ROWS,
        .pairs = JOHNNY_PAIRS,
        .source_limit = JOHNNY_FRAME_END,
        .row_step = size,
    };
    ask_for_picture(g, RACER_JOHNNY, &p);
}

// $7E:4FAD, $7E:5047
/* A racer: its next picture asked for once the last has been scaled, and its
 * sprites drawn where it is on the screen, unless it is out of sight. */
static void draw_racer(GameState *g, Racer r, unsigned pose)
{
    if (picture_taken(g, r)) {
        if (r == RACER_CRONO)
            ask_crono_picture(g, pose);
        else
            ask_johnny_picture(g, pose);
    }
    if (scene_race_racers_out_of_sight(g, r) != 0)
        return;
    set_sprite_offsets(g, scene_race_racers_screen_x(g, r), scene_race_racers_screen_y(g, r));
    draw_clipped_list(g, racer_sprites[r]);
}

/* ---- The panel -------------------------------------------------------------------------------- */

/* The distance bar, from its start at $22 to its right end, and each bike's
 * marker on it: back from the bar's end by the bar's length times how far the
 * bike has come (in $800ths, the product's high byte), down from the bar by
 * its lane. The original reads the product's high byte as a word, so the byte
 * after it -- the controller's A, X, L and R as the console last read them --
 * comes along as the high byte of what it takes off. While the race runs, and
 * until Johnny's win stops them, the markers are drawn. */
enum { BAR_START = 0x22, MARKER_SHIFT = 3, MARKER_BACK = 5, MARKER_DOWN = 0xC0 };
enum { MARKERS_STOPPED = 0x60 };
static const uint16_t marker_sprites[] = { (uint16_t)0x7E5391, (uint16_t)0x7E538D };

/* The bar is a pixel longer where channel 0's lean turns the pose back past
 * the first: the original's sum hands a unit on into the bar's length. */
static uint16_t bar_length(const GameState *g, bool leaned_back)
{
    return (uint16_t)(scene_race_bar_right(g) - BAR_START + leaned_back);
}

static uint8_t marker_far(const GameState *g, Racer r) { return (uint8_t)((scene_race_racers_distance(g, r) >> 8) >> MARKER_SHIFT); }

static void draw_marker(GameState *g, Platform *pf, Racer r, uint16_t length)
{
    uint16_t product = (uint16_t)((uint8_t)length * marker_far(g, r));
    uint16_t taken = (uint16_t)(in_read_low(pf, PF_AT(0x7E50E1), 0) << 8 | product >> 8);
    uint16_t x = (uint16_t)(scene_race_markers_right(g) + MARKER_BACK - taken);
    uint16_t y = (uint16_t)((race_lane_high(g, r) >> MARKER_SHIFT) + MARKER_DOWN);
    set_sprite_offsets(g, x, y);
    if (scene_race_markers_shown(g) != MARKERS_STOPPED && scene_race_stage(g) == RACE_RUNNING)
        draw_sprite(g, marker_sprites[r]);
}

/* The CPU's multiplier as the bar leaves it: the bar's length as the first
 * operand, and the product of the last marker placed, or, with no panel, the
 * length times its own high byte (the original stores the length as a word).
 * The panel's numbers then use the divider, whose remainder shares the
 * product's register. */
static void leave_multiplier(GameState *g, uint16_t length, bool markers, Racer last)
{
    hw_set_multiplicand(g, (uint8_t)length);
    hw_multiply_by(g, markers ? marker_far(g, last) : (uint8_t)(length >> 8));
}

/* The boosts Crono has left, from the panel's right leftward; the last is
 * shown ready while no boost is on. */
enum { PANEL_X = 0xEA, PANEL_Y = 0xD0, BOOST_STEP = 8, BOOST_ICON = (uint16_t)0x7E5389, BOOST_READY_ICON = (uint16_t)0x7E5385 };

static void draw_boosts(GameState *g)
{
    set_sprite_offsets(g, PANEL_X, PANEL_Y);
    for (uint16_t left = scene_race_boosts(g); left != 0; left--) {
        bool ready = left == 1 && scene_race_boost_timer(g) == 0;
        draw_sprite(g, ready ? BOOST_READY_ICON : BOOST_ICON);
        set_scene_sprite_offset_x(g, (uint16_t)(scene_sprite_offset_x(g) - BOOST_STEP));
    }
}

/* The panel's frame: two lists of sprites round the screen. */
enum { PANEL_FRAME = (uint16_t)0x7E5395, PANEL_CORNERS = (uint16_t)0x7E53AE };

// $7E:39F5
/* A number, drawn as up to five digits from its leading one, each sprite a
 * digit's tile ($20 and the digit) in the program's number list, whose count
 * says how many show. The tens and ones come out of the CPU's divider in the
 * original, which it leaves dividing by 10. */
enum { NUMBER_LIST = (uint16_t)0x7E3A7D, DIGIT_TILES = 0x20, DIGIT_BYTES = 4, DIGITS = 5 };

static void draw_number(GameState *g, uint16_t value)
{
    static const uint16_t places[DIGITS] = { 1, 10, 100, 1000, 10000 };
    uint8_t digits[DIGITS];
    uint16_t left = value;
    for (unsigned d = DIGITS; d-- > 0;) {
        digits[d] = (uint8_t)(left / places[d]);
        left = (uint16_t)(left % places[d]);
    }
    for (unsigned d = DIGITS; d-- > 0;)
        set_scene_number_digits(g, 1 + d * DIGIT_BYTES, (uint8_t)(DIGIT_TILES + digits[d]));
    unsigned shown = DIGITS;
    while (shown > 1 && digits[shown - 1] == 0)
        shown--;
    set_scene_number_digits(g, 0, (uint8_t)(shown + 1));
    ORIGINAL_EFFECT(hw_set_dividend(g, (uint16_t)(value % 100)));
    ORIGINAL_EFFECT(hw_divide_by_word(g, 10));
    draw_list(g, NUMBER_LIST);
}

/* The distance to go, shown in 512ths of the race less a lag that counts down
 * a step every 64 frames from the start (none past 1000, nor after the
 * finish), and Crono's score, never below 0, shown at half. */
enum { DISTANCE_X = 0x10, DISTANCE_Y = 0x0E, SCORE_Y = 0x18, SHOWN_LIMIT = 1001, LAG_EVERY = 0x40 };

static uint32_t distance_to_go(const GameState *g)
{
    return (uint32_t)(RACE_FINISH - scene_race_racers_distance(g, RACER_CRONO));
}

static void draw_numbers(GameState *g)
{
    uint16_t to_go = 0;
    if (scene_race_stage(g) == RACE_RUNNING) {
        uint32_t remaining = distance_to_go(g);
        uint16_t shown = (uint16_t)(((uint16_t)(remaining >> 8) >> 1) - scene_race_distance_shown_less(g));
        to_go = at_least(shown, SHOWN_LIMIT) ? 0 : shown;
    }
    set_sprite_offsets(g, DISTANCE_X, DISTANCE_Y);
    draw_number(g, to_go);

    if (((scene_race_frames(g) + 1) & (LAG_EVERY - 1)) == 0 && scene_race_distance_shown_less(g) != 0)
        set_scene_race_distance_shown_less(g, (uint16_t)(scene_race_distance_shown_less(g) - 1));

    set_sprite_offsets(g, scene_sprite_offset_x(g), SCORE_Y);
    if ((int16_t)scene_race_score(g) < 0)
        set_scene_race_score(g, 0);
    draw_number(g, scene_race_score(g) >> 1);
}

// $7E:5116
static void draw_panel(GameState *g)
{
    draw_boosts(g);
    set_sprite_offsets(g, 0, 0);
    draw_clipped_list(g, PANEL_FRAME);
    draw_list(g, PANEL_CORNERS);
    draw_numbers(g);
}

/* ---- A frame's drawing ---------------------------------------------------------------------- */

/* The pose both racers are drawn in: the way the camera faces, in eighths of a
 * turn, with channel 0's lean added. */
enum { POSE_SHIFT = 7, HALF_POSE = 0x40, POSES = 8 };

static uint16_t pose_sum(const GameState *g)
{
    return (uint16_t)(((uint16_t)(race_camera(g, CAMERA_ANGLE) + HALF_POSE) >> POSE_SHIFT) + race_camera(g, CAMERA_LEAN));
}

static unsigned pose_of(const GameState *g) { return pose_sum(g) & (POSES - 1); }

/* Whether the lean turned the pose back past the first. */
static bool leaned_back_past_first(const GameState *g) { return (int16_t)pose_sum(g) < 0; }

static bool crono_nearer(const GameState *g)
{
    return at_least(scene_race_racers_bottom(g, RACER_CRONO), scene_race_racers_bottom(g, RACER_JOHNNY));
}

// $7E:4F54
/* The racers and the panel: before the countdown ends the bikes' animation
 * stays at its start; the markers and the panel are drawn first, then the
 * nearer racer and the other. Accepted divergence: the markers are placed
 * back from the bar's end without the unit the original's sum would carry
 * past $FFFF, which the bar's ends the setup writes ($B8, $D8) never do. */
static void draw(GameState *g, Platform *pf)
{
    unsigned pose = pose_of(g);
    if (race_channel_1(g, RACE_STARTED) == 0)
        set_race_camera(g, RACE_ANIMATION, 0);
    uint16_t length = bar_length(g, leaned_back_past_first(g));
    bool crono_first = crono_nearer(g);
    Racer nearer = crono_first ? RACER_CRONO : RACER_JOHNNY, farther = crono_first ? RACER_JOHNNY : RACER_CRONO;
    bool panel = panel_shown(g);
    if (panel) {
        draw_marker(g, pf, nearer, length);
        draw_marker(g, pf, farther, length);
    }
    ORIGINAL_EFFECT(leave_multiplier(g, length, panel, farther));
    if (panel)
        draw_panel(g);
    draw_racer(g, nearer, pose);
    draw_racer(g, farther, pose);
}

/* What the drawing leaves in the arithmetic's working cells, from the state
 * after it: the pose; the first cell Crono's picture's place where one was
 * asked for (`crono_asked`), else the score shown, where the panel is; the
 * second the distance to go's high word, where the panel shows it. */
static void leave_working_cells(GameState *g, bool crono_asked)
{
    SceneProgramPage own = scene_program_own();
    unsigned pose = pose_of(g);
    set_scene_call_race_pose(g, own, (uint16_t)(pose * 2));
    bool panel = panel_shown(g);
    if (panel && scene_race_stage(g) == RACE_RUNNING)
        set_scene_call_second(g, own, (uint16_t)(distance_to_go(g) >> 16));
    if (crono_asked)
        set_scene_call_first(g, own, (uint16_t)(crono_frame(pose, race_camera(g, RACE_ANIMATION)) * 2));
    else if (panel)
        set_scene_call_first(g, own, scene_race_score(g) >> 1);
}

/* ---- Entry ------------------------------------------------------------------------------------- */

/* The most sprites a frame of the race draws: both racers' lists, the
 * markers, three boosts, the panel's lists and two numbers. */
enum { MOST_SPRITES = 16 + 9 + 2 + 3 + 6 + 3 + 2 * DIGITS, MOST_BOOSTS = 3, MOST_LEAN = 2 };

/* The drawing's code, guarded by hashes that leave out the operands the race
 * and its setup write (the bar's ends, the markers' stop, the distance's lag);
 * the race keeps those as variables. It takes the states the race makes: its
 * boosts at most three, Crono leaning one of his three ways, a sprite
 * table with room, and a markers' stop that is either the original's load or
 * the return Johnny's win writes. */
enum { MARKERS_DRAWN = 0xA5 };

bool scene_race_draw_present(Snes *s)
{
    if (!scene_on_scene_page_low_bank(s) || !m_code_matches(s, 0x7E4F54, 0x20, 0x968623895890694Eull) ||
        !m_code_matches(s, 0x7E4F76, 0x164, 0xC045241A96FE6193ull) ||
        !m_code_matches(s, 0x7E50DC, 0x31, 0x2D1D3F4109AB1CBEull) ||
        !m_code_matches(s, 0x7E510E, 0x63, 0xD196A45821235900ull) ||
        !m_code_matches(s, 0x7E5173, 0x3E, 0x17E12C9F40FEC15Eull) ||
        !m_code_matches(s, 0x7E39F5, 0x88, 0x6EE9408DE106F647ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t stop = scene_race_markers_shown(g);
    bool fits = scene_race_boosts(g) <= MOST_BOOSTS && (stop == MARKERS_DRAWN || stop == MARKERS_STOPPED) &&
                scene_race_crono_lean(g) <= MOST_LEAN && scene_sprites_fit(g, MOST_SPRITES);
    vars_peek(false);
    return fits;
}

/* What the drawing leaves in A and Y, which the list's code hands on: the
 * last sprite routine's, from the farther racer's list or, out of sight, its
 * mark; Y the next free sprite, where anything was drawn. */
static void leave_registers(Snes *s, bool crono_first)
{
    GameState *g = state_of(s);
    Racer last = crono_first ? RACER_JOHNNY : RACER_CRONO;
    vars_peek(true);
    uint16_t out = scene_race_racers_out_of_sight(g, last);
    s->cpu.a = out != 0 ? out : scene_clipped_list_leaves_a(g, racer_sprites[last], racer_sprites[last]);
    bool drew = panel_shown(g) || out == 0 || scene_race_racers_out_of_sight(g, (Racer)(1 - last)) == 0;
    if (drew)
        s->cpu.y = scene_sprite_index(g);
    vars_peek(false);
}

/* A frame task entered with 16-bit A and the carry clear (the list's own
 * compare, for any slot but the first: the pose's sum takes it in). */
void scene_race_draw(Snes *s)
{
    GameState *g = state_of(s);
    bool crono_first = crono_nearer(g);
    bool crono_asked = scene_race_picture_requests_state(g, RACER_CRONO) != PICTURE_WAITING;
    draw(g, m_platform(s));
    ORIGINAL_EFFECT(leave_working_cells(g, crono_asked));
    ORIGINAL_EFFECT(leave_registers(s, crono_first));
    m_rts(s);
}
