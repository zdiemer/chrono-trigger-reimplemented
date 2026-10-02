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
enum { SCENE_FLAG_NO_PANEL = 0x40 };

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

/* The arithmetic cells the drawing works in, which it leaves as they end:
 * the first (a bike's frame, the numbers drawn), the second (the distance to
 * go's high word) and the pose. */
typedef struct {
    bool first_set, second_set;
    uint16_t first, second;
} WorkCells;

/* ---- The racers' pictures --------------------------------------------------------------------- */

/* A racer's picture request (scene_race_picture_requests' state). */
enum { PICTURE_NONE = 0, PICTURE_WAITING = 1, PICTURE_GATHERED = 2 };

/* The racers' sprite lists, in the program's data. */
enum { CRONO_SPRITES = 0x531B, JOHNNY_SPRITES = 0x535C };
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
    uint16_t state = scene_race_picture_requests_state(g, r);
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

/* The smallest a racer is drawn at, and its frames of animation: a bike
 * changes frame every other count of the race's animation, Crono's on the odd
 * counts and Johnny's on the even. */
enum { CRONO_SMALLEST = 0x80, JOHNNY_SMALLEST = 0x40, SIZE_MASK = 0xFFF, OTHER_FRAME = 8, LEANS = 16 };

static unsigned picture_index(unsigned pose, bool other_frame) { return pose + (other_frame ? OTHER_FRAME : 0); }

/* Crono's picture for this frame, his size from channel 1 (and the count of
 * sprites from the size he was asked at last time): his pose, his lean, his
 * frame. Returns the frame's place in his pictures, which the original leaves
 * in the first arithmetic cell. */
enum { CRONO_PAIRS = 4 };

static uint16_t ask_crono_picture(GameState *g, unsigned pose)
{
    set_scene_race_picture_requests_source_limit(g, RACER_CRONO, scene_race_crono_frame_ends(g, pose));
    set_scene_race_picture_requests_rows(g, RACER_CRONO, scene_race_crono_frame_rows(g, pose));
    uint8_t far_count = (uint8_t)scene_race_crono_sprite_counts(g, pose);
    set_scene_race_sprite_list_counts(g, RACER_CRONO,
                                      sprites_at(scene_race_picture_requests_row_step(g, RACER_CRONO), far_count));
    uint16_t size = race_channel_1(g, RACE_CRONO_SCALE);
    if (!at_least(size, CRONO_SMALLEST)) {
        size = CRONO_SMALLEST;
        set_race_channel_1(g, RACE_CRONO_SCALE, size);
    }
    set_scene_race_picture_requests_row_step(g, RACER_CRONO, size >> 1);
    uint16_t counts = (uint16_t)(race_camera(g, RACE_ANIMATION) + 1);
    set_race_camera(g, RACE_ANIMATION, counts);
    unsigned frame = picture_index(pose, counts & 1);
    unsigned index = scene_race_crono_lean(g) * LEANS + frame;
    set_scene_race_picture_requests_source(g, RACER_CRONO, scene_race_crono_frames(g, index));
    set_scene_race_picture_requests_pairs(g, RACER_CRONO, CRONO_PAIRS);
    set_scene_race_picture_requests_state(g, RACER_CRONO, PICTURE_WAITING);
    return (uint16_t)(frame * 2);
}

/* Johnny's picture: his size from channel 1, never below JOHNNY_SMALLEST;
 * 3 pairs wide, $30 rows, ending at $600. */
enum { JOHNNY_PAIRS = 3, JOHNNY_ROWS = 0x30, JOHNNY_FRAME_END = 0x600 };

static void ask_johnny_picture(GameState *g, unsigned pose)
{
    uint16_t size = (race_channel_1(g, RACE_JOHNNY_SCALE) >> 1) & SIZE_MASK;
    if (!at_least(size, JOHNNY_SMALLEST)) {
        size = JOHNNY_SMALLEST;
        set_race_channel_1(g, RACE_JOHNNY_SCALE, size);
    }
    set_scene_race_picture_requests_row_step(g, RACER_JOHNNY, size);
    set_scene_race_sprite_list_counts(g, RACER_JOHNNY, sprites_at(size, JOHNNY_NEAR_SPRITES));
    unsigned frame = picture_index(pose, !(race_camera(g, RACE_ANIMATION) & 1));
    set_scene_race_picture_requests_source(g, RACER_JOHNNY, scene_race_johnny_frames(g, frame));
    set_scene_race_picture_requests_source_limit(g, RACER_JOHNNY, JOHNNY_FRAME_END);
    set_scene_race_picture_requests_rows(g, RACER_JOHNNY, JOHNNY_ROWS);
    set_scene_race_picture_requests_pairs(g, RACER_JOHNNY, JOHNNY_PAIRS);
    set_scene_race_picture_requests_state(g, RACER_JOHNNY, PICTURE_WAITING);
}

// $7E:4FAD, $7E:5047
/* A racer: its next picture asked for once the last has been scaled, and its
 * sprites drawn where it is on the screen, unless it is out of sight. */
static void draw_racer(GameState *g, Racer r, unsigned pose, WorkCells *cells)
{
    if (picture_taken(g, r)) {
        if (r == RACER_CRONO) {
            cells->first = ask_crono_picture(g, pose);
            cells->first_set = true;
        } else {
            ask_johnny_picture(g, pose);
        }
    }
    if (scene_race_racers_out_of_sight(g, r) != 0)
        return;
    set_sprite_offsets(g, scene_race_racers_screen_x(g, r), scene_race_racers_screen_y(g, r));
    draw_clipped_list(g, racer_sprites[r]);
}

/* ---- The panel -------------------------------------------------------------------------------- */

/* The distance bar: its length is from its start at $21 to its right end (one
 * less, unless the pose's sum reached the top of its word: the original
 * subtracts with the carry that sum shifted out), which goes into the
 * multiplier as the first operand for both markers. */
enum { BAR_START = 0x21, MARKER_SHIFT = 3, MARKER_BACK = 6, MARKER_DOWN = 0xC0 };

static void set_bar_scale(GameState *g, bool turned_over)
{
    uint16_t length = (uint16_t)(scene_race_bar_right(g) - BAR_START - !turned_over);
    hw_set_multiplicand(g, (uint8_t)length);
    ORIGINAL_EFFECT(hw_multiply_by(g, (uint8_t)(length >> 8)));   /* the word store's high byte */
}

/* A bike's marker on the bar: back from the bar's end by its distance times
 * the bar's length, and down from the bar by its lane. The original reads the
 * product's high byte as a word, so the byte after it -- the controller's A,
 * X, L and R as the console last read them -- comes along as the high byte of
 * what it takes off. While the race runs, and until Johnny's win stops them,
 * the marker is drawn. */
enum { MARKERS_STOPPED = 0x60 };
static const uint16_t marker_sprites[] = { 0x5391, 0x538D };

static void draw_marker(GameState *g, Platform *pf, Racer r)
{
    uint16_t far = (uint16_t)(scene_race_racers_distance(g, r) >> 8) >> MARKER_SHIFT;
    uint16_t product = hw_multiply_by(g, (uint8_t)far);
    ORIGINAL_EFFECT(hw_set_dividend(g, (uint16_t)((hw_dividend(g) & 0xFF00) | far >> 8)));   /* the word store's high byte */
    uint16_t taken = (uint16_t)(in_read_low(pf, PF_AT(0x7E50E1), 0) << 8 | product >> 8);
    uint32_t back = (uint32_t)scene_race_markers_right(g) + MARKER_BACK;
    uint16_t x = (uint16_t)(back - taken - !(back >> 16));
    uint16_t y = (uint16_t)((race_lane_high(g, r) >> MARKER_SHIFT) + MARKER_DOWN);
    set_sprite_offsets(g, x, y);
    if (scene_race_markers_shown(g) != MARKERS_STOPPED && scene_race_stage(g) == RACE_RUNNING)
        draw_sprite(g, marker_sprites[r]);
}

/* The boosts Crono has left, from the panel's right leftward; the last is
 * shown ready while no boost is on. */
enum { PANEL_X = 0xEA, PANEL_Y = 0xD0, BOOST_STEP = 8, BOOST_ICON = 0x5389, BOOST_READY_ICON = 0x5385 };

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
enum { PANEL_FRAME = 0x5395, PANEL_CORNERS = 0x53AE };

// $7E:39F5
/* A number, drawn as up to five digits from its leading one, each sprite a
 * digit's tile ($20 and the digit) in the program's number list, whose count
 * says how many show. The tens and ones come out of the CPU's divider in the
 * original, which it leaves dividing by 10. */
enum { NUMBER_LIST = 0x3A7D, DIGIT_TILES = 0x20, DIGIT_BYTES = 4, DIGITS = 5 };

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

static void draw_numbers(GameState *g, WorkCells *cells)
{
    uint16_t to_go = 0;
    if (scene_race_stage(g) == RACE_RUNNING) {
        uint32_t remaining = (uint32_t)(RACE_FINISH - scene_race_racers_distance(g, RACER_CRONO));
        cells->second = (uint16_t)(remaining >> 16);
        cells->second_set = true;
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
    cells->first = scene_race_score(g) >> 1;
    cells->first_set = true;
    draw_number(g, cells->first);
}

// $7E:5116
static void draw_panel(GameState *g, WorkCells *cells)
{
    draw_boosts(g);
    set_sprite_offsets(g, 0, 0);
    draw_clipped_list(g, PANEL_FRAME);
    draw_list(g, PANEL_CORNERS);
    draw_numbers(g, cells);
}

/* ---- A frame's drawing ---------------------------------------------------------------------- */

/* The pose both racers are drawn in: the way the camera faces, in eighths of a
 * turn, with channel 0's lean added; and whether that sum reached the top of
 * its word, which the bar's scale takes one off for. */
typedef struct {
    unsigned pose;
    bool turned_over;
} Pose;

enum { POSE_SHIFT = 7, HALF_POSE = 0x40, POSES = 8 };

static Pose pose_of(const GameState *g)
{
    uint16_t eighths = (uint16_t)(((uint16_t)(race_camera(g, CAMERA_ANGLE) + HALF_POSE) >> POSE_SHIFT) +
                                  race_camera(g, CAMERA_LEAN));
    return (Pose){ eighths & (POSES - 1), (eighths & 0x8000) != 0 };
}

static bool crono_nearer(const GameState *g)
{
    return at_least(scene_race_racers_bottom(g, RACER_CRONO), scene_race_racers_bottom(g, RACER_JOHNNY));
}

static void leave_cells(GameState *g, const WorkCells *cells, unsigned pose)
{
    SceneProgramPage own = scene_program_own();
    if (cells->first_set)
        set_scene_call_first(g, own, cells->first);
    if (cells->second_set)
        set_scene_call_second(g, own, cells->second);
    set_scene_call_race_pose(g, own, (uint16_t)(pose * 2));
}

// $7E:4F54
/* The racers and the panel: before the countdown ends the bikes' animation
 * stays at its start; the markers and the panel are drawn first, then the
 * nearer racer and the other. */
static void draw(GameState *g, Platform *pf)
{
    Pose p = pose_of(g);
    WorkCells cells = { false, false, 0, 0 };
    if (race_channel_1(g, RACE_STARTED) == 0)
        set_race_camera(g, RACE_ANIMATION, 0);
    set_bar_scale(g, p.turned_over);
    bool crono_first = crono_nearer(g);
    if (panel_shown(g)) {
        draw_marker(g, pf, crono_first ? RACER_CRONO : RACER_JOHNNY);
        draw_marker(g, pf, crono_first ? RACER_JOHNNY : RACER_CRONO);
        draw_panel(g, &cells);
    }
    draw_racer(g, crono_first ? RACER_CRONO : RACER_JOHNNY, p.pose, &cells);
    draw_racer(g, crono_first ? RACER_JOHNNY : RACER_CRONO, p.pose, &cells);
    ORIGINAL_EFFECT(leave_cells(g, &cells, p.pose));
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
    draw(g, m_platform(s));
    ORIGINAL_EFFECT(leave_registers(s, crono_first));
    m_rts(s);
}
