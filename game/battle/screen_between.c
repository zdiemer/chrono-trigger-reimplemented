/* Battle: bands of window between animation positions.
 *
 * An attack that passes from one combatant to another (a beam fired, a
 * stream thrown) can draw a window along the way between two of the
 * animation positions, a pair of them: a band of even width from the pair's
 * first position to its second, as one of the window quadrilaterals
 * (commands $AC and $D9). The band's half width is the animation scripts'
 * variable that also sizes the circles.
 *
 * Its sides are worked out from the direction between two positions, which
 * the battle measures by the slope between them in eighths of the screen's
 * columns and lines, through the ROM's table of slopes' angles. The last
 * direction measured is kept (battle_screen_direction).
 *
 * Two commands that measure it stay the original's. $DA draws a wedge from a
 * pair's first position to the band's width across its second, through the
 * screen scripts' triangle rasterizer ($CD:367C), which it calls from inside
 * its handler rather than ending with it as the other triangles do, so it
 * waits for that rasterizer to be C. $CC measures a direction alone, along
 * the cells the dispatch's index for it reaches past the positions (the
 * battle's stats). No script of the game's has either. */
#include "battle/screen_script.h"

#include "battle/screen_data.h"
#include "original_effects.h"

/* ---- The direction between two positions ------------------------------------------------- */

/* The original measures in eighths: it brings both points down by the first
 * one's distance past an eighth, rounds each run to the nearest eighth, and
 * takes the angle of the slope from the table, turned into the quarter of
 * the turn the run's signs say. */
enum { EIGHTH = 8, PAST_AN_EIGHTH = EIGHTH - 1, HALF_AN_EIGHTH = EIGHTH / 2 };

typedef struct {
    uint8_t from, to;  /* the two coordinates, brought down */
    bool backward;     /* `to` is the greater: the run goes left, or up */
    uint8_t run;       /* how far apart they are */
} Run;

static Run run_between(uint8_t from, uint8_t to)
{
    uint8_t past = from & PAST_AN_EIGHTH;
    Run r = { (uint8_t)(from - past), (uint8_t)(to - past), false, 0 };
    r.backward = r.from < r.to;
    r.run = r.backward ? (uint8_t)(r.to - r.from) : (uint8_t)(r.from - r.to);
    return r;
}

static uint8_t in_eighths(uint8_t run) { return (uint8_t)((run + HALF_AN_EIGHTH) / EIGHTH); }

// $CE:E7CF
/* The direction from one point toward another, as an angle (256 a turn, 0
 * pointing right, a quarter turn down). */
static uint8_t direction_between(const GameState *g, Run across, Run down)
{
    uint8_t slope = slope_angle(g, in_eighths(across.run), in_eighths(down.run));
    if (!across.backward)
        return (uint8_t)(down.backward ? SCREEN_HALF_TURN - slope : SCREEN_HALF_TURN + slope);
    return (uint8_t)(down.backward ? slope : -slope);
}

/* The original works the direction out in the cells $23-$2A, which the
 * battle uses for other things between effects (battle_direction_work), and
 * leaves its last values there; the game keeps them. */
static void keep_direction_work(GameState *g, Run across, Run down)
{
    const uint8_t work[] = { across.from, down.from, across.to, down.to, (uint8_t)(across.from - across.to),
                             (uint8_t)(across.backward ? 0xFF : 0), (uint8_t)(down.from - down.to),
                             (uint8_t)(down.backward ? 0xFF : 0) };
    for (unsigned i = 0; i < sizeof work; i++)
        set_battle_direction_work(g, i, work[i]);
}

/* ... and the runs and the table's place in its scratch cells: each run with
 * half an eighth added where it rounds up, the runs in eighths, and the
 * place. */
static uint16_t rounded_up(uint8_t run) { return (uint16_t)(run + ((run & HALF_AN_EIGHTH) ? EIGHTH : 0)); }

static void leave_direction(GameState *g, Run across, Run down, uint8_t direction)
{
    screen_leave_cell(g, 0x45, direction);
    screen_leave_word(g, 0x46, in_eighths(across.run));
    screen_leave_word(g, 0x48, rounded_up(across.run));
    screen_leave_word(g, 0x4A, rounded_up(down.run));
    screen_leave_word(g, 0x4C, (uint16_t)(in_eighths(down.run) * SLOPE_STEPS + in_eighths(across.run)));
}

static uint8_t measure_direction(GameState *g, BattlePoint from, BattlePoint to)
{
    Run across = run_between(from.x, to.x), down = run_between(from.y, to.y);
    uint8_t direction = direction_between(g, across, down);
    keep_direction_work(g, across, down);
    ORIGINAL_EFFECT(leave_direction(g, across, down, direction));
    return direction;
}

/* ---- The pairs of positions ---------------------------------------------------------------- */

static BattlePoint position(const GameState *g, unsigned p)
{
    uint16_t at = battle_animation_positions(g, p);
    return (BattlePoint){ (uint8_t)at, (uint8_t)(at >> 8) };
}

static BattlePoint pair_first(const GameState *g, uint8_t pair) { return position(g, SCREEN_PAIR_POSITIONS * pair); }
static BattlePoint pair_second(const GameState *g, uint8_t pair) { return position(g, SCREEN_PAIR_POSITIONS * pair + 1u); }

static bool names_pairs_and_a_quad(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->pair < SCREEN_POSITION_PAIRS && c->aim < SCREEN_POSITION_PAIRS && c->kept < SCREEN_POSITION_PAIRS
           && c->quad < SCREEN_WINDOW_QUADS;
}

// $CD:1B22, $CD:1B1F
/* Keep a pair on the battlefield, then measure the direction along another,
 * from its first position to its second, and keep it. */
static uint8_t aim_along(GameState *g, uint8_t kept, uint8_t aim)
{
    screen_keep_pair_on_the_battlefield(g, kept);
    uint8_t direction = measure_direction(g, pair_first(g, aim), pair_second(g, aim));
    set_battle_screen_direction(g, direction);
    return direction;
}

/* ---- The band's sides ----------------------------------------------------------------------- */

/* The animation scripts' variable that sizes the circles is the band's half
 * width. */
enum { HALF_WIDTH_VARIABLE = 14 };

/* How far each side of the band lies from its middle line: half its width
 * across the direction, a quarter turn to either side. */
typedef struct {
    BattlePoint one, other;
} BandSides;

static BattlePoint offset_toward(const GameState *g, uint8_t angle, uint8_t distance)
{
    return (BattlePoint){ screen_signed_cosine(g, angle, distance), screen_signed_sine(g, angle, distance) };
}

static BandSides sides_across(const GameState *g, uint8_t direction, uint8_t half_width)
{
    uint8_t one = (uint8_t)(direction - SCREEN_QUARTER_TURN);
    return (BandSides){ offset_toward(g, one, half_width), offset_toward(g, (uint8_t)(one + SCREEN_HALF_TURN), half_width) };
}

static BattlePoint moved(BattlePoint p, BattlePoint by)
{
    return (BattlePoint){ (uint8_t)(p.x + by.x), (uint8_t)(p.y + by.y) };
}

/* What the original leaves of the sides: each offset in its cells, and in the
 * multiplier the last one's product. */
static void leave_sides(GameState *g, uint8_t direction, uint8_t half_width, BandSides s)
{
    uint8_t other = (uint8_t)(direction + SCREEN_QUARTER_TURN);
    screen_leave_cell(g, 0x45, s.other.x);
    screen_leave_cell(g, 0x47, s.other.y);
    screen_leave_cell(g, 0x49, s.one.x);
    screen_leave_cell(g, 0x4B, s.one.y);
    screen_leave_sine_product(g, other, half_width);
}

static BandSides band_sides(GameState *g, uint8_t direction)
{
    uint8_t half_width = battle_script_variables(g, HALF_WIDTH_VARIABLE);
    BandSides s = sides_across(g, direction, half_width);
    ORIGINAL_EFFECT(leave_sides(g, direction, half_width, s));
    return s;
}

/* ---- The commands ------------------------------------------------------------------------- */

/* The band's corners: each side of its middle line, at its first position
 * and at its second. */
static void band_corners(BattlePoint first, BattlePoint second, BandSides s, BattlePoint c[SCREEN_QUAD_CORNERS])
{
    c[0] = moved(first, s.one);
    c[1] = moved(first, s.other);
    c[2] = moved(second, s.one);
    c[3] = moved(second, s.other);
}

// $CD:2460, $CD:19B1, $CD:2466
/* Screen commands $AC and $D9: draw window quadrilateral `quad` as a band
 * along a pair of positions, in the direction measured along `aim` once
 * `kept` is kept on the battlefield. */
static ScreenFlow lay_band(GameState *g, uint8_t quad, uint8_t pair, uint8_t aim, uint8_t kept)
{
    BandSides s = band_sides(g, aim_along(g, kept, aim));
    BattlePoint c[SCREEN_QUAD_CORNERS];
    band_corners(pair_first(g, pair), pair_second(g, pair), s, c);
    for (unsigned corner = 0; corner < SCREEN_QUAD_CORNERS; corner++)
        screen_set_quad_corner(g, quad, corner, c[corner]);
    screen_draw_window_quad(g, quad);
    return screen_on();
}

/* Whether the band the command would lay stays in the window table
 * (screen_quad_fits_the_table): its corners, worked out from the positions
 * as they will be once `kept` is kept on the battlefield. */
static BattlePoint position_once_kept(const GameState *g, unsigned p, uint8_t kept)
{
    BattlePoint at = position(g, p);
    return p / SCREEN_PAIR_POSITIONS == kept ? screen_kept_on_the_battlefield(at) : at;
}

static bool band_fits(const GameState *g, const ScreenCommand *c)
{
    unsigned first = SCREEN_PAIR_POSITIONS * c->pair;
    BattlePoint from = position_once_kept(g, SCREEN_PAIR_POSITIONS * c->aim, c->kept);
    BattlePoint to = position_once_kept(g, SCREEN_PAIR_POSITIONS * c->aim + 1u, c->kept);
    uint8_t direction = direction_between(g, run_between(from.x, to.x), run_between(from.y, to.y));
    BattlePoint corners[SCREEN_QUAD_CORNERS];
    band_corners(position_once_kept(g, first, c->kept), position_once_kept(g, first + 1u, c->kept),
                 sides_across(g, direction, battle_script_variables(g, HALF_WIDTH_VARIABLE)), corners);
    return screen_quad_fits_the_table(g, corners);
}

static bool lays_a_band_that_fits(const GameState *g, const ScreenCommand *c)
{
    return names_pairs_and_a_quad(g, c) && band_fits(g, c);
}
SCREEN_OPERATION(lay_band, quad, pair, aim, kept)

SCREEN_OPCODES(screen_between_opcodes,
    { 0xAC, OPERATION(lay_band), .needs = lays_a_band_that_fits, .length = 2, .quad = BYTE(0), .pair = BYTE(1),
      .aim = CONSTANT(1), .kept = CONSTANT(0) },
    { 0xD9, OPERATION(lay_band), .needs = lays_a_band_that_fits, .length = 2, .quad = BYTE(0), .pair = BYTE(1),
      .aim = BYTE(1), .kept = BYTE(1) },
);

/* ---- The entry ------------------------------------------------------------------------------ */

/* $CE:E7CF, called with the two points in the cells $23-$26, the first
 * across and down, then the second; the direction comes back in A and in
 * the cell $45. */
void battle_measure_direction(Snes *s)
{
    GameState *g = state_of(s);
    BattlePoint from = { battle_direction_work(g, 0), battle_direction_work(g, 1) };
    BattlePoint to = { battle_direction_work(g, 2), battle_direction_work(g, 3) };
    s->cpu.a = measure_direction(g, from, to);
    m_rtl(s);
}
