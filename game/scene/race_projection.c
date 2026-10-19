/* Scenes: the jet bike race's bikes placed on the screen.
 *
 * The camera stands between the two bikes and turns with the track: Crono is
 * a lead ahead of it along the track and Johnny a lead behind (the margin
 * Crono crossed the line by, once he has), each across the track by its lane.
 * Every frame the race turns those two places by the camera's angle, onto the
 * plane as the camera faces it, and then puts each in perspective: how far
 * ahead of the camera it is picks the line under the horizon it stands on, and
 * the line how big it is drawn -- its picture's size, its height and width --
 * and how far across the screen its place comes to. The farther bike is placed
 * first. Once Johnny has dropped out of the picture after Crono's win, he isn't
 * placed.
 *
 * The perspective goes through a divisor for each distance, kept in a table
 * (scene_race_divisors) that the camera's check forgets whenever the camera's
 * height changes: the smallest that brings the height over it under the
 * distance, searched for from the one found last.
 *
 * The original never clears the carry between its sums here either, and some
 * of them move what the player sees by a pixel or a step of size: Johnny's
 * place across the plane takes a unit where Crono's place down it wrapped, and
 * his place down one where his own across did; a bike looks a step farther off
 * where the sum before its distance carried; and Johnny is a pixel further
 * right at his nearest. Each rule has one term for its unit, and "The units
 * the placing hands on" works them out from the values the sums before used.
 * The sums whose carry is the same wherever the horizon is above the plane's
 * top are plain arithmetic, and the entry's guard keeps the horizon there. */
#include "scene/race.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

static SceneProgramPage own(void) { return scene_program_own(); }

/* ---- The camera's turn ------------------------------------------------------------------------ */

/* The bikes are turned by the camera's angle back from a quarter turn. */

typedef struct {
    int16_t sine, cosine;
} TurnBack;

static TurnBack turn_back(const GameState *g)
{
    uint16_t back = (uint16_t)(SCENE_QUARTER_TURN - race_camera(g, CAMERA_ANGLE));
    return (TurnBack){ scene_sine(g, back), scene_cosine(g, back) };
}

/* The turn is kept in scene_race_view with its negatives, where the
 * program's sums take them from. */
static void keep_turn(GameState *g, TurnBack t)
{
    set_scene_race_view(g, 0, (uint16_t)t.sine);
    set_scene_race_view(g, 1, (uint16_t)t.cosine);
    set_scene_race_view(g, 2, (uint16_t)-t.sine);
    set_scene_race_view(g, 3, (uint16_t)-t.cosine);
}

/* ---- The bikes' places on the plane --------------------------------------------------------------- */

/* The track's middle, in a lane's 256ths. */
enum { LANE_MIDDLE = 0x40 };

/* How far along the track from the camera a bike is: the lead, or once Crono
 * has crossed the line, the margin he crossed it by (Johnny's only in the
 * demo). */
static uint16_t along_of(const GameState *g, Racer r)
{
    bool finished = scene_race_stage(g) != RACE_RUNNING;
    if (r == RACER_JOHNNY)
        finished = finished && scene_race_demo(g) != 0;
    if (!finished)
        return scene_race_lead(g);
    return (uint16_t)(scene_race_finish_margin(g) | scene_race_finish_margin_high(g) << 8);
}

/* How far across the track from its middle a bike is. */
static uint16_t across_of(const GameState *g, Racer r) { return (uint16_t)(race_lane_high(g, r) - LANE_MIDDLE); }

/* A place turned: the turn times the place's low byte, signed, as the program
 * multiplies it; the places add the products' low words. */
static uint16_t turned(int16_t factor, uint16_t place) { return (uint16_t)((int32_t)factor * (int8_t)(uint8_t)place); }

typedef struct {
    uint16_t across, down;  /* across the plane as the camera faces it, and down it, away from the camera */
} OnPlane;

/* Crono a lead ahead along the track, across it at his lane. */
static OnPlane crono_on_plane(const GameState *g, TurnBack t)
{
    uint16_t along = along_of(g, RACER_CRONO), across = across_of(g, RACER_CRONO);
    return (OnPlane){ (uint16_t)(turned(t.sine, along) + turned(t.cosine, across)),
                      (uint16_t)(turned(t.cosine, along) + turned((int16_t)-t.sine, across)) };
}

/* The units Johnny's turn takes (johnny_units, below). */
typedef struct {
    bool into_across_lane, into_across, into_down_lane, into_down;
} JohnnyUnits;

static JohnnyUnits johnny_units(const GameState *g, TurnBack t);

/* Johnny a lead behind (the turn's negatives), across at his lane. */
static OnPlane johnny_on_plane(const GameState *g, TurnBack t)
{
    JohnnyUnits u = johnny_units(g, t);
    uint16_t along = along_of(g, RACER_JOHNNY), lane = across_of(g, RACER_JOHNNY);
    uint16_t across = (uint16_t)(turned((int16_t)-t.sine, along) + turned(t.cosine, (uint16_t)(lane + u.into_across_lane)) +
                                 u.into_across);
    uint16_t down = (uint16_t)(turned((int16_t)-t.cosine, along) + turned((int16_t)-t.sine, (uint16_t)(lane + u.into_down_lane)) +
                               u.into_down);
    return (OnPlane){ across, down };
}

/* ---- Perspective -------------------------------------------------------------------------------- */

/* The lines below the horizon. */
enum { SCREEN_LINES = 0xE0 };

static uint8_t camera_height(const GameState *g) { return (uint8_t)race_camera(g, CAMERA_HEIGHT); }

/* The units a bike's distance takes (distance_units, below). */
typedef struct {
    bool into_ahead, farther;
} DistanceUnits;

/* How far ahead of the camera a place down the plane is: the camera's
 * distance less how far down the plane the place is (its high byte). */
static uint8_t ahead_of_camera(const GameState *g, uint16_t down, DistanceUnits u)
{
    return (uint8_t)(((uint16_t)-down >> 8) + race_camera(g, CAMERA_DISTANCE) + u.into_ahead);
}

/* The distance from the camera a place is at: how far ahead it is, and the
 * camera's height. */
static uint16_t distance_of(const GameState *g, uint16_t down, DistanceUnits u)
{
    return (uint16_t)(ahead_of_camera(g, down, u) + camera_height(g) + u.farther);
}

/* A divisor is kept less one as the next search's start. */
static void keep_divisor(GameState *g, uint8_t divisor) { set_scene_race_divisor_start(g, (uint16_t)(divisor - 1)); }

static uint8_t last_divisor(const GameState *g) { return (uint8_t)(scene_race_divisor_start(g) + 1); }

// $7E:4EEB
/* The divisor kept for a distance, or the first that brings the camera's height
 * under the distance, from the one found last. */
static uint8_t divisor_for(GameState *g, uint16_t distance)
{
    uint8_t kept = scene_race_divisors(g, (uint8_t)distance);
    if (kept == 0) {
        uint16_t height = (uint16_t)(camera_height(g) << 8);
        uint8_t from = (uint8_t)scene_race_divisor_start(g);
        kept = (uint8_t)(scene_find_divisor_from(g, height, from, distance) + 1);
        set_scene_race_divisors(g, (uint8_t)distance, kept);
    }
    keep_divisor(g, kept);
    return kept;
}

/* The lines from the horizon down to the plane's top, less one (the guard
 * keeps the horizon above the plane's top, and the gap on the screen); for a
 * bike at the camera whose place down the plane wrapped (DistanceUnits'
 * farther), not less one. */
static uint16_t horizon_gap(const GameState *g, bool wrapped_at_the_camera)
{
    return (uint16_t)(race_camera(g, CAMERA_PITCH) - race_camera(g, CAMERA_PLANE_TOP) - !wrapped_at_the_camera);
}

/* The line under the plane's top a divisor stands for: the lines below the
 * gap shared out by it. The multiply carries the top bit of the cell after its
 * product into it, and the program keeps the gap there. */
static uint16_t line_at(GameState *g, uint8_t divisor, uint16_t gap)
{
    set_scene_call_divisor_less_one(g, own(), gap);
    return (uint16_t)(scene_multiply16(g, own(), (uint16_t)(divisor << 8), (uint16_t)(SCREEN_LINES - gap)) >> 16);
}

/* A bike's top on the screen: its line, from the horizon (the gap and the
 * line under it, where the gap is on the screen), above it by its last
 * picture's height. */
static uint16_t top_on_screen(const GameState *g, Racer r, uint16_t line, uint16_t gap)
{
    return (uint16_t)(line + gap + (gap <= SCREEN_LINES) - scene_race_racers_height(g, r));
}

/* The camera's height over the line, through the CPU's divider as the program
 * divides it: the bike's size, $FFF at most. */
enum { SIZE_MASK = 0x0FFF };

static uint16_t size_at(const GameState *g, uint16_t line)
{
    return scene_divider_quotient((uint16_t)(camera_height(g) << 8), (uint8_t)line) & SIZE_MASK;
}

/* The divider as the program leaves it: the height divided by the line, stored
 * as a word, its high byte in the next register. */
static void leave_divider(GameState *g, uint16_t line)
{
    hw_set_dividend(g, (uint16_t)(camera_height(g) << 8));
    hw_divide_by_word(g, line);
}

/* Half the size, but no less than $40: what the picture's height and width are
 * worked out over. */
enum { SMALLEST_HALF = 0x40 };

static uint16_t half_of(uint16_t size)
{
    uint16_t half = size >> 1;
    return half < SMALLEST_HALF ? SMALLEST_HALF : half;
}

/* The picture's height and width, as 8.8 fractions over the half size. */
enum { PICTURE_HEIGHT = 0x0C00, CRONO_WIDTH = 0x1000, ACROSS_SCALE = 0x8000 };

static uint16_t over_half(GameState *g, uint16_t value, uint16_t half) { return scene_divide_8_8_by(g, own(), value, half); }

/* How far across the screen a place across the plane comes to: the screen's
 * middle and the place times the scale the size gives (the product's high
 * word). */
enum { SCREEN_MIDDLE = 0x80 };

static uint16_t middle_on_screen(GameState *g, uint16_t across, uint16_t size)
{
    uint16_t scale = over_half(g, ACROSS_SCALE, size);
    uint32_t product = scene_multiply_signed16(g, own(), (uint16_t)(scale << 1), across);
    return (uint16_t)((product >> 16) + SCREEN_MIDDLE);
}

/* A picture centered on `middle`: one of an even width a pixel further left. */
static uint16_t left_edge(uint16_t middle, uint16_t width) { return (uint16_t)(middle - (width >> 1) - !(width & 1)); }
static uint16_t middle_of(uint16_t left, uint16_t width) { return (uint16_t)(left + (width >> 1) + !(width & 1)); }

/* The units Johnny's picture takes on the screen (johnny_screen_units, below). */
typedef struct {
    bool nearest, into_right, into_bottom;
} ScreenUnits;

static ScreenUnits johnny_screen_units(uint16_t size, uint16_t middle, uint16_t side);

// $7E:4E06
/* Crono in perspective: his top on the screen, his picture's size for the
 * scaler, its height and width, its edges, and where across the screen he is. */
static void place_crono(GameState *g, OnPlane p, uint16_t line, uint16_t gap)
{
    uint16_t size = size_at(g, line);
    uint16_t y = top_on_screen(g, RACER_CRONO, line, gap);
    set_race_channel_1(g, RACE_CRONO_SCALE, size);
    uint16_t half = half_of(size);
    uint16_t height = over_half(g, PICTURE_HEIGHT, half);
    uint16_t width = over_half(g, CRONO_WIDTH, half) & 0xFF;
    uint16_t x = left_edge(middle_on_screen(g, p.across, size), width);
    set_scene_race_racers_screen_y(g, RACER_CRONO, y);
    set_scene_race_racers_height(g, RACER_CRONO, height);
    set_scene_race_racers_bottom(g, RACER_CRONO, (uint16_t)(height + y));
    set_scene_race_racers_width(g, RACER_CRONO, width);
    set_scene_race_racers_screen_x(g, RACER_CRONO, x);
    set_scene_race_racers_right(g, RACER_CRONO, (uint16_t)(x + width));
}

// $7E:4E7D
/* Johnny the same way: his pictures are square. */
static void place_johnny(GameState *g, OnPlane p, uint16_t line, uint16_t gap)
{
    uint16_t size = size_at(g, line);
    uint16_t y = top_on_screen(g, RACER_JOHNNY, line, gap);
    set_race_channel_1(g, RACE_JOHNNY_SCALE, size);
    uint16_t side = over_half(g, PICTURE_HEIGHT, half_of(size)) & 0xFF;
    uint16_t middle = middle_on_screen(g, p.across, size);
    ScreenUnits u = johnny_screen_units(size, middle, side);
    uint16_t x = left_edge((uint16_t)(middle + u.nearest), side);
    set_scene_race_racers_screen_y(g, RACER_JOHNNY, y);
    set_scene_race_racers_width(g, RACER_JOHNNY, side);
    set_scene_race_racers_height(g, RACER_JOHNNY, side);
    set_scene_race_racers_screen_x(g, RACER_JOHNNY, x);
    set_scene_race_racers_right(g, RACER_JOHNNY, (uint16_t)(x + side + u.into_right));
    set_scene_race_racers_bottom(g, RACER_JOHNNY, (uint16_t)(y + side + u.into_bottom));
}

/* ---- The bikes' places, in order ----------------------------------------------------------------- */

static bool johnny_shown(const GameState *g) { return scene_race_johnny_gone(g) == 0; }

/* Where the bikes are on the plane. The program keeps a bike's place down the
 * plane in its screen_y until the bike is placed, so once Johnny is gone, the
 * last top his picture had on the screen stands for his place, and only which
 * bike goes first looks at it. */
typedef struct {
    OnPlane crono, johnny;
} Places;

static uint16_t gone_johnny_place(const GameState *g) { return scene_race_racers_screen_y(g, RACER_JOHNNY); }

static Places places_of(const GameState *g)
{
    TurnBack t = turn_back(g);
    Places p = { crono_on_plane(g, t), { 0, gone_johnny_place(g) } };
    if (johnny_shown(g))
        p.johnny = johnny_on_plane(g, t);
    return p;
}

/* Compared as the race compares: by the sign of the difference. */
static bool at_least(uint16_t a, uint16_t b) { return (int16_t)(a - b) >= 0; }

/* The farther bike first: Johnny where Crono is as far down the plane or
 * farther. */
static Racer first_placed(Places p) { return at_least(p.crono.down, p.johnny.down) ? RACER_JOHNNY : RACER_CRONO; }
static Racer second_placed(Places p) { return first_placed(p) == RACER_CRONO ? RACER_JOHNNY : RACER_CRONO; }

static OnPlane place_of(Places p, Racer r) { return r == RACER_CRONO ? p.crono : p.johnny; }

/* ---- The units the placing hands on ------------------------------------------------------------- */

/* Johnny's turn: into his first lane sum, whether Crono's place down the
 * plane wrapped; into his place across, whether that lane sum reached the
 * track's middle; into his second lane sum, whether his place across wrapped;
 * into his place down, whether that one reached the middle. Each moves Johnny
 * a step across or down the plane. */
static JohnnyUnits johnny_units(const GameState *g, TurnBack t)
{
    uint16_t crono_along = along_of(g, RACER_CRONO), crono_across = across_of(g, RACER_CRONO);
    uint16_t johnny_along = along_of(g, RACER_JOHNNY), lane = race_lane_high(g, RACER_JOHNNY);
    uint16_t johnny_across = across_of(g, RACER_JOHNNY);
    JohnnyUnits u;
    u.into_across_lane = (uint32_t)turned(t.cosine, crono_along) + turned((int16_t)-t.sine, crono_across) > 0xFFFF;
    u.into_across = (uint32_t)lane + u.into_across_lane >= LANE_MIDDLE;
    uint32_t across = (uint32_t)turned((int16_t)-t.sine, johnny_along) +
                      turned(t.cosine, (uint16_t)(johnny_across + u.into_across_lane)) + u.into_across;
    u.into_down_lane = across > 0xFFFF;
    u.into_down = (uint32_t)lane + u.into_down_lane >= LANE_MIDDLE;
    return u;
}

static ScreenUnits johnny_units_as_placed(const GameState *g);

/* A bike's distance: into how far ahead of the camera it is, a unit -- for
 * the bike placed first, where Crono's place down the plane is at or past
 * Johnny's as unsigned numbers (the order's comparison, which reads the sign
 * of the difference, agrees but where the two are on opposite sides of the
 * camera's line); for the second, where the first's last edge ran past the
 * end of the screen's coordinates (Crono's right edge, Johnny's bottom). And
 * the distance is a step farther where the camera's distance less the place
 * wrapped. Each makes the bike look a step farther off. */
static bool unit_after(const GameState *g, Racer first)
{
    uint16_t x = scene_race_racers_screen_x(g, first), width = scene_race_racers_width(g, first);
    if (first == RACER_CRONO)
        return (uint32_t)x + width > 0xFFFF;
    return (uint32_t)scene_race_racers_screen_y(g, RACER_JOHNNY) + width + johnny_units_as_placed(g).into_bottom > 0xFFFF;
}

static DistanceUnits units_with(const GameState *g, uint16_t down, bool into_ahead)
{
    uint16_t back = (uint16_t)-down;
    DistanceUnits u = { into_ahead, false };
    u.farther = (uint32_t)(uint16_t)(back >> 8 | back << 8) + race_camera(g, CAMERA_DISTANCE) + into_ahead > 0xFFFF;
    return u;
}

/* Whether no bike is placed before this one this frame: it is the first, or
 * the first is Johnny and he is gone. */
static bool placed_first(const GameState *g, Places p, Racer r)
{
    Racer first = first_placed(p);
    return r == first || !(first == RACER_CRONO || johnny_shown(g));
}

static DistanceUnits distance_units(const GameState *g, Places p, Racer r)
{
    bool into_ahead = placed_first(g, p, r) ? p.crono.down >= p.johnny.down : unit_after(g, first_placed(p));
    return units_with(g, place_of(p, r).down, into_ahead);
}

/* Johnny's picture on the screen: a pixel further right at his nearest (a
 * size of 1, where the scale's doubling wraps); his left edge's sum leaves a
 * unit unless it borrowed, which his right edge takes, and the right edge's
 * wrapping goes into his bottom. */
enum { NEAREST_SIZE = 1 };

static ScreenUnits johnny_screen_units(uint16_t size, uint16_t middle, uint16_t side)
{
    ScreenUnits u;
    u.nearest = size == NEAREST_SIZE;
    uint16_t moved = (uint16_t)(middle + u.nearest);
    u.into_right = moved >= (uint32_t)(side >> 1) + !(side & 1);
    u.into_bottom = (uint32_t)left_edge(moved, side) + side + u.into_right > 0xFFFF;
    return u;
}

/* The same, from Johnny's picture as it was placed, whose middle has the
 * nearest pixel in it already. */
enum { NOT_NEAREST = 0 };

static ScreenUnits johnny_units_as_placed(const GameState *g)
{
    uint16_t side = scene_race_racers_width(g, RACER_JOHNNY);
    uint16_t middle = middle_of(scene_race_racers_screen_x(g, RACER_JOHNNY), side);
    return johnny_screen_units(NOT_NEAREST, middle, side);
}

/* ---- The bikes placed ----------------------------------------------------------------------------- */

/* The placing's working cells, as the last bike placed leaves them: its left
 * edge's half width and middle, Crono's half size and the horizon's gap. The
 * scene's cells share their page with the controller's on the field's side,
 * which takes them over at its first read after the race. */
static void hand_over_working_cells(GameState *g, Racer last, uint16_t gap)
{
    uint16_t width = scene_race_racers_width(g, last);
    set_scene_call_race_edge_half_width(g, own(), width >> 1);
    set_scene_call_race_edge_middle(g, own(), middle_of(scene_race_racers_screen_x(g, last), width));
    set_scene_call_race_half_size(g, own(), half_of(race_channel_1(g, RACE_CRONO_SCALE)));
    set_scene_call_race_gap(g, own(), gap);
}

/* The distance the last bike placed was looked up at, which the program
 * leaves in Y: the frame task after the placing, the drawing, hands Y on where
 * it draws nothing (scene_race_draw), and the task list passes it to the next. */
static struct {
    uint8_t distance;
} last_lookup;

static void note_lookup(uint16_t distance) { last_lookup.distance = (uint8_t)distance; }

/* A bike right at the camera (nothing of it ahead) has no distance to look a
 * divisor up for, and stands where the bike placed before it this frame does
 * (the guard turns away a first bike at the camera). Returns the horizon's gap
 * the bike was placed over. */
static uint16_t place_one(GameState *g, Places p, Racer r)
{
    OnPlane at = place_of(p, r);
    DistanceUnits u = distance_units(g, p, r);
    bool at_the_camera = ahead_of_camera(g, at.down, u) == 0;
    uint8_t divisor;
    if (at_the_camera) {
        divisor = last_divisor(g);
        keep_divisor(g, divisor);
    } else {
        uint16_t distance = distance_of(g, at.down, u);
        ORIGINAL_EFFECT(note_lookup(distance));
        divisor = divisor_for(g, distance);
    }
    uint16_t gap = horizon_gap(g, at_the_camera && u.farther);
    uint16_t line = line_at(g, divisor, gap);
    ORIGINAL_EFFECT(leave_divider(g, line));
    if (r == RACER_CRONO)
        place_crono(g, at, line, gap);
    else
        place_johnny(g, at, line, gap);
    return gap;
}

// $7E:4D0D
/* Each frame: the bikes turned onto the plane, and the farther placed first.
 * The program's own multiplies that turn them are timed with the placing. */
static void place_bikes(GameState *g)
{
    keep_turn(g, turn_back(g));
    Places p = places_of(g);
    set_scene_race_divisor_start(g, race_camera(g, CAMERA_PITCH));
    Racer first = first_placed(p), second = second_placed(p), last = RACER_CRONO;
    uint16_t gap = 0;
    if (first == RACER_CRONO || johnny_shown(g)) {
        gap = place_one(g, p, first);
        last = first;
    }
    if (second == RACER_CRONO || johnny_shown(g)) {
        gap = place_one(g, p, second);
        last = second;
    }
    hand_over_working_cells(g, last, gap);
}

/* ---- Entry ------------------------------------------------------------------------------------- */

/* The placing's code, and the sine, the multiplies and the divide it calls. */
static bool placing_code_present(Snes *s)
{
    return m_code_matches(s, 0x7E4D0D, 0x247, 0x211E2D7F00447EA5ull) && m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull) &&
           m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull) && m_code_matches(s, 0x7E33D6, 0x37, 0xCB3F0FAD1C9F8EA9ull) &&
           m_code_matches(s, 0x7E32E2, 189, 0xD04B8C1274AF93C8ull);
}

/* The states the port places the bikes in: the horizon above the plane's top,
 * with the gap between them on the screen (otherwise the original's sums
 * borrow and wrap into the bikes' lines); and each bike either at a distance
 * whose search for a divisor ends (one the program has kept, or one some
 * divisor brings the camera's height under; with none the original searches
 * forever), or right at the camera after another bike was placed (a first
 * bike there takes whatever the turn's arithmetic left as its divisor). The
 * data bank reaches the CPU's divider, which the program reaches through
 * it. */
enum { LARGEST_DIVISOR = 0xFF };

static bool horizon_fits(const GameState *g)
{
    uint16_t pitch = race_camera(g, CAMERA_PITCH), top = race_camera(g, CAMERA_PLANE_TOP);
    return pitch > top && (uint16_t)(pitch - top - 1) <= SCREEN_LINES;
}

/* The largest divisor gives the smallest quotient, so a search ends where it
 * brings the height under the distance. */
static bool divisor_found_for(const GameState *g, uint16_t distance)
{
    if (scene_race_divisors(g, (uint8_t)distance) != 0)
        return true;
    return (uint16_t)(camera_height(g) << 8) / LARGEST_DIVISOR < distance;
}

static bool placeable(const GameState *g, uint16_t down, DistanceUnits u, bool after_another)
{
    if (ahead_of_camera(g, down, u) == 0)
        return after_another;
    return divisor_found_for(g, distance_of(g, down, u));
}

/* A bike placed after another takes a unit from how the first was placed,
 * which the guard can't know before either is: it takes both. */
static bool placeable_whatever_the_unit(const GameState *g, uint16_t down)
{
    return placeable(g, down, units_with(g, down, false), true) && placeable(g, down, units_with(g, down, true), true);
}

bool scene_race_place_bikes_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || !placing_code_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    Places p = places_of(g);
    bool fits = horizon_fits(g);
    static const Racer racers[] = { RACER_CRONO, RACER_JOHNNY };
    for (unsigned i = 0; i < sizeof racers / sizeof racers[0]; i++) {
        Racer r = racers[i];
        if (r == RACER_JOHNNY && !johnny_shown(g))
            continue;
        uint16_t down = place_of(p, r).down;
        if (placed_first(g, p, r))
            fits = fits && placeable(g, down, distance_units(g, p, r), false);
        else
            fits = fits && placeable_whatever_the_unit(g, down);
    }
    vars_peek(false);
    return fits;
}

/* What the placing leaves in A and Y, which the list's code hands on: A the
 * last bike's last edge (Crono's right, Johnny's bottom; Johnny's mark where he
 * is gone and would have come last), Y the distance the last bike placed was
 * looked up at. */
static void leave_registers(Snes *s, Racer would_be_last)
{
    GameState *g = state_of(s);
    if (would_be_last == RACER_JOHNNY && !johnny_shown(g))
        s->cpu.a = scene_race_johnny_gone(g);
    else if (would_be_last == RACER_CRONO)
        s->cpu.a = scene_race_racers_right(g, RACER_CRONO);
    else
        s->cpu.a = scene_race_racers_bottom(g, RACER_JOHNNY);
    s->cpu.y = last_lookup.distance;
}

/* A frame task entered with 16-bit A and index registers and the carry clear
 * (the list's own compare, for any slot but the first). */
void scene_race_place_bikes(Snes *s)
{
    GameState *g = state_of(s);
    Racer would_be_last = second_placed(places_of(g));
    place_bikes(g);
    ORIGINAL_EFFECT(leave_registers(s, would_be_last));
    m_rts(s);
}
