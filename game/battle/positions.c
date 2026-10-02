/* Battle: where the combatants stand to each other (battle/positions.h). */
#include "battle/positions.h"

#include "battle/animation.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/sprite_data.h"
#include "charge.h"
#include "original_effects.h"
#include "wram.h"
#include "vars.h"

/* ---- What the searches leave in their cells ---------------------------------------------- */

/* The direct-page cells the searches work in (battle_work, from $80). */
enum {
    LEFT_FIRST = 0x00,       /* the reach test's vertical distance, a word ($80); the walker, a word */
    LEFT_SECOND = 0x02,      /* the reach test's horizontal distance squared, a word ($82) */
    LEFT_THIRD = 0x04,       /* where a find's loop is, a word ($84); a measure's second square */
    LEFT_BEST = 0x06,        /* the nearest or farthest distance a find has, a word ($86) */
    LEFT_SEARCHER = 0x0A,    /* the searcher, as the nearest-enemy find keeps it ($8A) */
    LEFT_START = 0x0A,       /* where a walk starts, x and y ($8A, $8B) */
    LEFT_STEP = 0x0C,        /* how far down and across a walk's step reaches ($8C, $8E) */
};

static void leave_work_word(GameState *g, unsigned at, uint16_t v)
{
    set_battle_work(g, at, (uint8_t)v);
    set_battle_work(g, at + 1, (uint8_t)(v >> 8));
}

/* ---- Reach ------------------------------------------------------------------------------ */

/* How far apart two coordinates are, made positive. */
static uint8_t apart(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }

/* The square of the distance between two positions. The original adds the
 * two squares in 16 bits, so two combatants far enough apart come out near. */
static uint16_t distance_squared(GameState *g, BattlePoint a, BattlePoint b)
{
    uint8_t dx = apart(a.x, b.x), dy = apart(a.y, b.y);
    uint16_t dx_squared = battle_multiply(g, dx, dx);
    uint16_t sum = (uint16_t)(dx_squared + battle_multiply(g, dy, dy));
    ORIGINAL_EFFECT(leave_work_word(g, LEFT_FIRST, dy));
    ORIGINAL_EFFECT(leave_work_word(g, LEFT_SECOND, dx_squared));
    set_battle_product(g, sum); /* the reach tests' callers take their carry from it */
    return sum;
}

/* The position a combatant's box was last worked out from. */
/* Where a combatant's box is worked out around. A slot past the combatants
 * (a candidate that is none) reads and writes past the table, as the
 * original's indexed accesses do. */
static BattlePoint box_place(const GameState *g, uint8_t slot)
{
    if (slot < BATTLE_BOUNDS_X_COUNT)
        return (BattlePoint){ battle_bounds_x(g, slot), battle_bounds_y(g, slot) };
    return (BattlePoint){ battle_bounds_x_unchecked(g, slot), battle_bounds_y_unchecked(g, slot) };
}

static void place_box(GameState *g, uint8_t slot, BattlePoint at)
{
    if (slot < BATTLE_BOUNDS_X_COUNT) {
        set_battle_bounds_x(g, slot, at.x);
        set_battle_bounds_y(g, slot, at.y);
    } else {
        set_battle_bounds_x_unchecked(g, slot, at.x);
        set_battle_bounds_y_unchecked(g, slot, at.y);
    }
}

/* The reach test: how far apart the two are, squared, and whether that is
 * within reach, which it leaves as the search's answer. */
static uint16_t measure_reach(GameState *g, BattlePoint a, BattlePoint b, uint16_t reach)
{
    uint16_t distance = distance_squared(g, a, b);
    set_battle_search_range(g, reach);
    set_battle_search_failed(g, distance <= reach ? 0 : 0xFF);
    return distance;
}

bool battle_within_reach(GameState *g, uint8_t a, uint8_t b, uint16_t reach)
{
    return measure_reach(g, box_place(g, a), box_place(g, b), reach) <= reach;
}

bool battle_box_within_reach_of(GameState *g, uint8_t slot, BattlePoint at, uint16_t reach)
{
    return measure_reach(g, box_place(g, slot), at, reach) <= reach;
}

/* ---- Finding the nearest and the farthest ----------------------------------------------- */

/* Whether a combatant can be found: present, shown, and not knocked out. */
static bool findable(const GameState *g, uint8_t slot)
{
    return battle_slot_present(g, slot) && !(battle_object_hidden(g, slot) & 0x80) && !combatant_knocked_out(g, slot);
}

typedef enum { NEAREST, FARTHEST } Extreme;

/* Look among the combatants from `first` to before `end` for the one nearest
 * or farthest from the searcher; a tie goes to the later one. Each one looked
 * at is left as the search's other, and each measured leaves the reach test's
 * answer. */
static void find_extreme(GameState *g, uint8_t searcher, uint8_t first, uint8_t end, Extreme extreme, bool leave_out_self)
{
    uint16_t best = extreme == NEAREST ? 0xFFFF : 0;
    uint8_t slot = first;
    for (; slot != end; slot++) {
        if (leave_out_self && slot == searcher)
            continue;
        set_battle_search_other(g, slot);
        if (!findable(g, slot))
            continue;
        uint16_t distance = measure_reach(g, box_place(g, searcher), box_place(g, slot), REACH_SEARCH);
        if (extreme == NEAREST ? best >= distance : distance >= best) {
            best = distance;
            set_battle_target_found(g, slot);
            ORIGINAL_EFFECT(set_battle_target_found_high(g, 0));
        }
    }
    ORIGINAL_EFFECT(leave_work_word(g, LEFT_THIRD, slot));
    ORIGINAL_EFFECT(leave_work_word(g, LEFT_BEST, best));
    if (leave_out_self)
        ORIGINAL_EFFECT(set_battle_work(g, LEFT_SEARCHER, searcher));
}

/* ---- Where a combatant stands ------------------------------------------------------------- */

static BattlePoint standing(const GameState *g, uint8_t slot) { return (BattlePoint){ combatant_x(g, slot), combatant_y(g, slot) }; }

/* The battlefield's grid is in cells of 16 pixels. */
enum { CELL_SHIFT = 4, HALF_DOWN = 8, RIGHT_EDGE_CELL = 11, LEFT_EDGE_CELL = 5, LEVEL = 0x20 };

static uint8_t cell_of(uint8_t v) { return (uint8_t)(v >> CELL_SHIFT); }

/* How far apart two coordinates are, as the original measures it here: their
 * difference as a signed byte, made positive, so that two more than 128
 * pixels apart come out nearer. */
static uint8_t apart_wrapping(uint8_t a, uint8_t b)
{
    int8_t d = (int8_t)(uint8_t)(a - b);
    return (uint8_t)(d < 0 ? -d : d);
}

static uint16_t square(GameState *g, uint8_t d) { return battle_multiply(g, d, d); }

// $C1:2CF3
/* How much nearer the other is to the third than to the searcher: the
 * difference of the squares of the distances. */
static uint16_t nearer_to_third(GameState *g, uint8_t searcher, uint8_t other, uint8_t third)
{
    BattlePoint at = standing(g, other), s = standing(g, searcher), t = standing(g, third);
    uint16_t to_searcher = (uint16_t)(square(g, apart_wrapping(at.x, s.x)) + square(g, apart_wrapping(at.y, s.y)));
    uint16_t third_across = square(g, apart_wrapping(at.x, t.x));
    uint16_t to_third = (uint16_t)(third_across + square(g, apart_wrapping(at.y, t.y)));
    int16_t difference = (int16_t)(uint16_t)(to_third - to_searcher);
    uint16_t measure = (uint16_t)(difference < 0 ? -difference : difference);
    ORIGINAL_EFFECT(leave_work_word(g, LEFT_FIRST, (uint16_t)(measure >> 8)));
    ORIGINAL_EFFECT(leave_work_word(g, LEFT_SECOND, to_searcher));
    ORIGINAL_EFFECT(leave_work_word(g, LEFT_THIRD, third_across));
    return measure;
}

/* ---- The walk ---------------------------------------------------------------------------- */

/* A walk steps out from where the searcher stands in the direction of the
 * other, and holds as soon as a step comes within reach; it stops, failing,
 * where the battlefield's grid stops the searcher, or (with a third given)
 * another combatant's box is in the way.
 *
 * The original's walk is kept as it runs, though it can't be what was meant
 * (only a condition no enemy's script has asks for it; docs/TODO.md): each
 * step's reach is measured not to the other but to whatever box position lies
 * as far into the table of them as the multiplier's first product left by
 * the step's across distance (the radius times the low byte of the sine),
 * which reaches past the table into work RAM; and each step reaches farther
 * than the first by the searcher's radius (battle_orbit_radius) plus the last
 * vertical distance measured, which the original keeps its radius in the
 * cell of. */
static BattlePoint stray_box_place(const GameState *g, uint16_t index)
{
    return (BattlePoint){ wram_read8(g, 0x7EA039u + index), wram_read8(g, 0x7EA050u + index) };
}

static uint16_t leftover_index(const GameState *g, uint8_t radius, uint8_t angle)
{
    uint8_t magnitude = battle_sine_magnitude(g, angle);
    uint16_t sine = (uint16_t)(angle & 0x80 ? -magnitude : magnitude);
    return (uint16_t)(radius * (uint8_t)sine);
}

enum { QUARTER_TURN = 0x40 };

// $C1:2C02
static bool walk(GameState *g, uint8_t searcher, uint8_t other, bool keep_clear)
{
    BattlePoint start = standing(g, searcher);
    uint8_t angle = battle_direction(g, start, standing(g, other));
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_START, start.x));
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_START + 1, start.y));
    uint8_t reach_left = 0;
    bool held;
    for (;;) {
        uint8_t radius = (uint8_t)(reach_left + battle_orbit_radius(g, searcher));
        int8_t down = battle_along(g, angle, radius), across = battle_along(g, (uint8_t)(angle + QUARTER_TURN), radius);
        ORIGINAL_EFFECT(set_battle_work(g, LEFT_STEP, (uint8_t)down));
        ORIGINAL_EFFECT(set_battle_work(g, LEFT_STEP + 2, (uint8_t)across));
        BattlePoint step = { (uint8_t)(start.x + across), (uint8_t)(start.y + down) };
        set_battle_bounds_y(g, searcher, step.y);
        set_battle_bounds_x(g, searcher, step.x);
        BattlePoint stray = stray_box_place(g, leftover_index(g, radius, (uint8_t)(angle + QUARTER_TURN)));
        if (measure_reach(g, stray, step, REACH_SEARCH) <= REACH_SEARCH) {
            held = true;
            break;
        }
        reach_left = apart(stray.y, step.y);
        ORIGINAL_EFFECT(leave_work_word(g, LEFT_FIRST, searcher));
        battle_update_bounds(g, searcher);
        set_battle_search_through_marked(g, false);
        if (battle_grid_stops(g, searcher) || (keep_clear && battle_box_overlaps(g, searcher) != OVERLAPS_NOBODY)) {
            held = false;
            break;
        }
    }
    set_battle_search_failed(g, held ? 0 : 0xFF);
    battle_update_bounds(g, searcher);
    return held;
}

/* ---- The search -------------------------------------------------------------------------- */

static SearchAnswer answer(bool held) { return (SearchAnswer){ held, 0, 0 }; }

/* A test that fails counts its cell down from what the search left it at
 * (0, so to $FF). */
static SearchAnswer test(GameState *g, bool held)
{
    if (!held)
        set_battle_search_failed(g, (uint8_t)(battle_search_failed(g) - 1));
    return answer(held);
}

static SearchAnswer found(const GameState *g) { return (SearchAnswer){ battle_search_failed(g) == 0, battle_target_found(g), 0 }; }

/* Each search starts from the combatants' boxes worked out around where they
 * stand, and holding. */
static void set_up(GameState *g, uint8_t searcher, uint8_t other)
{
    place_box(g, searcher, standing(g, searcher));
    place_box(g, other, standing(g, other));
    set_battle_search_failed(g, 0);
}

static SearchAnswer find(GameState *g, uint8_t searcher, uint8_t first, uint8_t end, Extreme extreme, bool leave_out_self)
{
    find_extreme(g, searcher, first, end, extreme, leave_out_self);
    return found(g);
}

SearchAnswer battle_search_kind(GameState *g, SearchKind kind, uint8_t searcher, uint8_t other, uint8_t third)
{
    BattlePoint s = standing(g, searcher), o = standing(g, other);
    SearchAnswer a = answer(true);
    switch (kind) {
    case SEARCH_NEAREST_MEMBER:
        TIMED_AS(g, 0xC129B2, a = find(g, searcher, 0, BATTLE_PARTY_SIZE, NEAREST, false));
        break;
    case SEARCH_FARTHEST_MEMBER:
        TIMED_AS(g, 0xC129FB, a = find(g, searcher, 0, BATTLE_PARTY_SIZE, FARTHEST, false));
        break;
    case SEARCH_NEAREST_OTHER_ENEMY:
        TIMED_AS(g, 0xC12A43, a = find(g, searcher, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS, NEAREST, true));
        break;
    case SEARCH_FARTHEST_ENEMY:
        TIMED_AS(g, 0xC12A98, a = find(g, searcher, BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS, FARTHEST, false));
        break;
    case SEARCH_WITHIN_REACH:
        TIMED_AS(g, 0xC12B37, a = answer(battle_within_reach(g, searcher, other, REACH_SEARCH)));
        break;
    case SEARCH_LEVEL:
        TIMED_AS(g, 0xC12BBC, a = test(g, apart_wrapping(s.y, o.y) <= LEVEL));
        break;
    case SEARCH_OTHER_BELOW:
        TIMED_AS(g, 0xC12BDA, a = test(g, s.y < o.y));
        break;
    case SEARCH_OTHER_TO_THE_RIGHT:
        TIMED_AS(g, 0xC12BEE, a = test(g, s.x < o.x));
        break;
    case SEARCH_WALK:
        TIMED_AS(g, 0xC12C02, a = answer(walk(g, searcher, other, third != 0)));
        break;
    case SEARCH_IN_LOWER_HALF:
        TIMED_AS(g, 0xC12CA7, a = test(g, cell_of(s.y) >= HALF_DOWN));
        break;
    case SEARCH_IN_UPPER_HALF:
        TIMED_AS(g, 0xC12CBA, a = test(g, cell_of(s.y) < HALF_DOWN));
        break;
    case SEARCH_AT_RIGHT_EDGE:
        TIMED_AS(g, 0xC12CCD, a = test(g, cell_of(s.x) >= RIGHT_EDGE_CELL));
        break;
    case SEARCH_AT_LEFT_EDGE:
        TIMED_AS(g, 0xC12CE0, a = test(g, cell_of(s.x) < LEFT_EDGE_CELL));
        break;
    case SEARCH_NEARER_TO_THIRD: {
        uint16_t nearer = 0;
        TIMED_AS(g, 0xC12CF3, nearer = nearer_to_third(g, searcher, other, third));
        a = (SearchAnswer){ true, (uint8_t)(nearer >> 8), nearer };
        set_battle_target_found(g, a.found);
        break;
    }
    case SEARCH_WITHIN_WIDE_REACH:
        TIMED_AS(g, 0xC12B2B, a = answer(battle_within_reach(g, searcher, other, REACH_WIDE)));
        break;
    default:
        break;
    }
    return a;
}

SearchAnswer battle_search(GameState *g, SearchKind kind, uint8_t searcher, uint8_t other, uint8_t third)
{
    set_battle_target_search(g, (uint8_t)kind);
    set_battle_target_searcher(g, searcher);
    set_battle_search_other(g, other);
    set_battle_search_third(g, third);
    set_up(g, searcher, other);
    return battle_search_kind(g, kind, searcher, other, third);
}

/* ---- The grid and the other combatants ------------------------------------------------------ */

enum { CELL_BLOCKS = 0x40, CELL_MARKED = 0x80, GRID_COLUMNS = 16 };

/* The cells under the box, a row at a time from its top. The original looks
 * at the box's first cell whatever its size (its box can wrap past the
 * screen's edge), and the cell's place wraps in 8 bits as it adds it. */
bool battle_grid_stops(GameState *g, uint8_t slot)
{
    uint8_t top = cell_of(battle_bounds_top(g, slot)), left = cell_of(battle_bounds_left(g, slot));
    uint8_t bottom = (uint8_t)(cell_of(battle_bounds_bottom(g, slot)) + 1), right = (uint8_t)(cell_of(battle_bounds_right(g, slot)) + 1);
    bool through_marked = battle_search_through_marked(g) != 0;
    uint8_t row = top, column;
    bool stops = false;
    do {
        column = 0;
        do {
            uint8_t cell = battle_base_grid(g, (uint8_t)(row * GRID_COLUMNS + column + left));
            stops = (cell & CELL_BLOCKS) || (!through_marked && (cell & CELL_MARKED));
        } while (!stops && (uint8_t)(++column + left) < right);
    } while (!stops && ++row < bottom);
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_SECOND, row));
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_SECOND + 1, left));
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_THIRD, bottom));
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_THIRD + 1, right));
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_BEST, column));
    return stops;
}

/* Two boxes overlap where each reaches into the other along both axes. */
static bool spans_meet(uint8_t a_low, uint8_t a_high, uint8_t b_low, uint8_t b_high)
{
    return a_low >= b_low ? a_low <= b_high : a_high >= b_low;
}

static bool boxes_meet(const GameState *g, uint8_t a, uint8_t b)
{
    return spans_meet(battle_bounds_left(g, a), battle_bounds_right(g, a), battle_bounds_left(g, b), battle_bounds_right(g, b))
           && spans_meet(battle_bounds_top(g, a), battle_bounds_bottom(g, a), battle_bounds_top(g, b), battle_bounds_bottom(g, b));
}

/* Only combatants on the battlefield's plane count: present, shown, and not
 * drawn above everything. */
static bool in_the_way(const GameState *g, uint8_t slot)
{
    return battle_slot_present(g, slot) && !(battle_object_hidden(g, slot) & 0x80)
           && !(battle_object_extra_draw_on(g, slot) & 0x80);
}

Overlap battle_box_overlaps(GameState *g, uint8_t slot)
{
    for (uint8_t other = 0; other < BATTLE_COMBATANTS; other++) {
        if (other != slot && in_the_way(g, other) && boxes_meet(g, slot, other))
            return battle_slot_is_party(other) ? OVERLAPS_MEMBER : OVERLAPS_ENEMY;
    }
    return OVERLAPS_NOBODY;
}

/* ---- Directions ------------------------------------------------------------------------------ */

/* What the direction leaves in its cells: the two points, their differences
 * and signs, their sizes, the table's place and the angle ($D3-$E3). The
 * original's callers ask for the next direction from the points left there,
 * so they are game state, kept plainly. */
static void leave_direction(GameState *g, BattlePoint from, BattlePoint to, uint8_t across, uint8_t down, uint8_t angle)
{
    uint16_t index = battle_arctangent_index(across, down);
    uint8_t cells[BATTLE_DIRECTION_CELLS_COUNT] = {
        from.x, from.y, to.x, to.y,
        (uint8_t)(from.x - to.x), from.x < to.x ? 0xFF : 0x00,
        (uint8_t)(from.y - to.y), from.y < to.y ? 0xFF : 0x00,
        angle, (uint8_t)(across / BATTLE_ARCTANGENT_STEP), 0, across, 0, down, 0, (uint8_t)index, (uint8_t)(index >> 8),
    };
    for (unsigned i = 0; i < BATTLE_DIRECTION_CELLS_COUNT; i++)
        set_battle_direction_cells(g, i, cells[i]);
}

enum { HALF_TURN = 0x80 };

// $C1:0222
uint8_t battle_direction(GameState *g, BattlePoint from, BattlePoint to)
{
    bool right = from.x < to.x, below = from.y < to.y;
    uint8_t across = apart(from.x, to.x), down = apart(from.y, to.y);
    uint8_t angle = battle_arctangent(g, across, down);
    if (!right)
        angle = below ? (uint8_t)(HALF_TURN - angle) : (uint8_t)(HALF_TURN + angle);
    else if (!below)
        angle = (uint8_t)-angle;
    leave_direction(g, from, to, across, down, angle);
    return angle;
}

/* The radius goes to the multiply as a word, its high byte 0. */
static void leave_radius_word(GameState *g) { set_battle_long_multiply(g, 1, 0); }

// $C1:01F9
int8_t battle_along(GameState *g, uint8_t angle, uint8_t radius)
{
    uint8_t magnitude = battle_sine_magnitude(g, angle);
    uint16_t sine = (uint16_t)(angle & HALF_TURN ? -magnitude : magnitude);
    int8_t along = (int8_t)(uint8_t)(battle_multiply_long(g, radius, sine) >> 8);
    ORIGINAL_EFFECT(leave_radius_word(g));
    return along;
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The searches take their question and combatants from the cells the
 * original's callers fill (battle_target_search, battle_target_searcher,
 * battle_search_other, battle_search_third), and leave in A and the carry
 * what the kind's last compare left, which the callers of the battle's steps
 * are given back: worked out here from the question and the state after. */
static uint8_t searcher_of(const GameState *g) { return battle_target_searcher(g); }

static bool search_carry(const GameState *g, SearchKind kind, SearchAnswer a)
{
    uint8_t s = searcher_of(g), o = battle_search_other(g);
    BattlePoint sp = standing(g, s), op = standing(g, o);
    switch (kind) {
    case SEARCH_WITHIN_REACH: return battle_product(g) >= REACH_SEARCH;
    case SEARCH_WITHIN_WIDE_REACH: return battle_product(g) >= REACH_WIDE;
    case SEARCH_LEVEL: return apart_wrapping(sp.y, op.y) >= LEVEL;
    case SEARCH_OTHER_BELOW: return sp.y >= op.y;
    case SEARCH_OTHER_TO_THE_RIGHT: return sp.x >= op.x;
    case SEARCH_WALK: return battle_bounds_carry(g, s);
    case SEARCH_IN_LOWER_HALF:
    case SEARCH_IN_UPPER_HALF: return cell_of(sp.y) >= HALF_DOWN;
    case SEARCH_AT_RIGHT_EDGE: return cell_of(sp.x) >= RIGHT_EDGE_CELL;
    case SEARCH_AT_LEFT_EDGE: return cell_of(sp.x) >= LEFT_EDGE_CELL;
    case SEARCH_NEARER_TO_THIRD: return (a.nearer & 0x80) != 0;
    default: return true;
    }
}

static uint8_t search_a(const GameState *g, SearchKind kind, SearchAnswer a)
{
    uint8_t s = searcher_of(g), o = battle_search_other(g);
    BattlePoint sp = standing(g, s), op = standing(g, o);
    switch (kind) {
    case SEARCH_NEAREST_MEMBER:
    case SEARCH_FARTHEST_MEMBER: return BATTLE_PARTY_SIZE;
    case SEARCH_NEAREST_OTHER_ENEMY:
    case SEARCH_FARTHEST_ENEMY: return BATTLE_COMBATANTS;
    case SEARCH_WITHIN_REACH:
    case SEARCH_WITHIN_WIDE_REACH: return battle_search_failed(g);
    case SEARCH_LEVEL: return apart_wrapping(sp.y, op.y);
    case SEARCH_OTHER_BELOW: return sp.y;
    case SEARCH_OTHER_TO_THE_RIGHT: return sp.x;
    case SEARCH_WALK: return s;
    case SEARCH_IN_LOWER_HALF:
    case SEARCH_IN_UPPER_HALF: return cell_of(sp.y);
    case SEARCH_AT_RIGHT_EDGE:
    case SEARCH_AT_LEFT_EDGE: return cell_of(sp.x);
    case SEARCH_NEARER_TO_THIRD: return a.found;
    default: return 0;
    }
}

static void leave_search_registers(Snes *s, SearchKind kind, SearchAnswer a)
{
    GameState *g = state_of(s);
    s->cpu.a = search_a(g, kind, a);
    m_set_flag(s, CPU_FLAG_C, search_carry(g, kind, a));
}

static SearchKind kind_asked(const GameState *g) { return (SearchKind)battle_target_search(g); }

void battle_search_for_target(Snes *s)
{
    GameState *g = state_of(s);
    SearchKind kind = kind_asked(g);
    SearchAnswer a = battle_search(g, kind, searcher_of(g), battle_search_other(g), battle_search_third(g));
    leave_search_registers(s, kind, a);
    m_rts(s);
}

static void search_kind_entry(Snes *s, SearchKind kind)
{
    GameState *g = state_of(s);
    SearchAnswer a = battle_search_kind(g, kind, searcher_of(g), battle_search_other(g), battle_search_third(g));
    leave_search_registers(s, kind, a);
    m_rts(s);
}

void battle_search_nearest_member(Snes *s) { search_kind_entry(s, SEARCH_NEAREST_MEMBER); }
void battle_search_farthest_member(Snes *s) { search_kind_entry(s, SEARCH_FARTHEST_MEMBER); }
void battle_search_nearest_other_enemy(Snes *s) { search_kind_entry(s, SEARCH_NEAREST_OTHER_ENEMY); }
void battle_search_farthest_enemy(Snes *s) { search_kind_entry(s, SEARCH_FARTHEST_ENEMY); }
void battle_search_within_reach(Snes *s) { search_kind_entry(s, SEARCH_WITHIN_REACH); }
void battle_search_level(Snes *s) { search_kind_entry(s, SEARCH_LEVEL); }
void battle_search_other_below(Snes *s) { search_kind_entry(s, SEARCH_OTHER_BELOW); }
void battle_search_other_to_the_right(Snes *s) { search_kind_entry(s, SEARCH_OTHER_TO_THE_RIGHT); }
void battle_search_walk(Snes *s) { search_kind_entry(s, SEARCH_WALK); }
void battle_search_in_lower_half(Snes *s) { search_kind_entry(s, SEARCH_IN_LOWER_HALF); }
void battle_search_in_upper_half(Snes *s) { search_kind_entry(s, SEARCH_IN_UPPER_HALF); }
void battle_search_at_right_edge(Snes *s) { search_kind_entry(s, SEARCH_AT_RIGHT_EDGE); }
void battle_search_at_left_edge(Snes *s) { search_kind_entry(s, SEARCH_AT_LEFT_EDGE); }
void battle_search_nearer_to_third(Snes *s) { search_kind_entry(s, SEARCH_NEARER_TO_THIRD); }
void battle_search_within_wide_reach(Snes *s) { search_kind_entry(s, SEARCH_WITHIN_WIDE_REACH); }

/* The reach tests the enemies' animation effects make take the two
 * combatants in X and Y, and leave the answer in A. */
static void reach_entry(Snes *s, uint16_t reach)
{
    GameState *g = state_of(s);
    battle_within_reach(g, (uint8_t)s->cpu.x, (uint8_t)s->cpu.y, reach);
    s->cpu.a = battle_search_failed(g);
    m_set_flag(s, CPU_FLAG_C, battle_product(g) >= reach);
    m_rts(s);
}

void battle_within_long_reach(Snes *s) { reach_entry(s, REACH_LONG); }
void battle_within_search_reach(Snes *s) { reach_entry(s, REACH_SEARCH); }
void battle_within_far_reach(Snes *s) { reach_entry(s, REACH_FAR); }
void battle_touching(Snes *s) { reach_entry(s, REACH_TOUCHING); }
void battle_within_near_reach(Snes *s) { reach_entry(s, REACH_NEAR); }

/* The grid's and the boxes' tests take the combatant in a word at $80, and
 * say what they found in A and its sign. */
static uint8_t combatant_in_work(const GameState *g) { return battle_work(g, LEFT_FIRST); }

void battle_grid_stops_combatant(Snes *s)
{
    GameState *g = state_of(s);
    bool stops = battle_grid_stops(g, combatant_in_work(g));
    s->cpu.a = stops ? 0x00FF : 0;
    m_set_nz8(s, (uint8_t)s->cpu.a);
    m_rts(s);
}

enum { MEETS_MEMBER = 0x80, MEETS_ENEMY = 0x81 };

void battle_box_meets_another(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t slot = combatant_in_work(g);
    Overlap o = battle_box_overlaps(g, slot);
    s->cpu.a = o == OVERLAPS_NOBODY ? 0 : o == OVERLAPS_MEMBER ? MEETS_MEMBER : MEETS_ENEMY;
    s->cpu.x = slot;
    m_set_nz8(s, (uint8_t)s->cpu.a);
    m_rts(s);
}

/* The direction takes its two points in its cells and leaves the angle in A,
 * the table's place in X, and the carry of turning the quarter's angle
 * round. */
static bool direction_carry(BattlePoint from, BattlePoint to, uint8_t quarter_angle)
{
    bool right = from.x < to.x, below = from.y < to.y;
    if (right)
        return below ? false : quarter_angle == 0;
    return below ? quarter_angle <= HALF_TURN : quarter_angle >= HALF_TURN;
}

void battle_direction_between(Snes *s)
{
    GameState *g = state_of(s);
    BattlePoint from = { battle_direction_cells(g, 0), battle_direction_cells(g, 1) };
    BattlePoint to = { battle_direction_cells(g, 2), battle_direction_cells(g, 3) };
    uint8_t angle = battle_direction(g, from, to);
    uint8_t across = apart(from.x, to.x), down = apart(from.y, to.y);
    s->cpu.a = angle;
    s->cpu.x = battle_arctangent_index(across, down);
    m_set_flag(s, CPU_FLAG_C, direction_carry(from, to, battle_arctangent(g, across, down)));
    m_rts(s);
}

/* The distance along an axis takes the angle in A and the radius in its
 * multiply cell, and leaves the multiplier's first product in X. */
void battle_distance_along(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t angle = (uint8_t)s->cpu.a, radius = battle_multiply_factors(g, 1);
    int8_t along = battle_along(g, angle, radius);
    s->cpu.a = (uint8_t)along;
    s->cpu.x = leftover_index(g, radius, angle);
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------- */

/* The searches index the tables with the combatants' cells, and with A's
 * high byte as the original indexes with all of A. */
bool battle_search_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool named = battle_target_search(g) < SEARCH_KINDS && battle_names_combatant(battle_target_searcher(g))
                 && battle_names_combatant(battle_search_other(g)) && battle_names_combatant(battle_search_third(g));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && named;
}

/* The enemies' animation effects measure a combatant against the box past
 * the combatants', where they put the effect. */
enum { EFFECT_BOX = 11 };

bool battle_reach_test_ready(Snes *s)
{
    return battle_on_page(s) && battle_names_combatant(s->cpu.x) && s->cpu.y <= EFFECT_BOX;
}

bool battle_combatant_in_work_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool named = battle_work(g, LEFT_FIRST + 1) == 0 && battle_names_combatant(battle_work(g, LEFT_FIRST));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && named;
}
