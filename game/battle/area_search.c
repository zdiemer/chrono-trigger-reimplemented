/* Battle: who stands in an area of the battlefield (battle/choosing.h).
 *
 * Techs and items that hit an area find their targets here: the menu's
 * choosing of targets shows the player whom the area takes in
 * (target_choice.c), and the targeting decides it as the action is carried
 * out (aim.c). The area is described in battle_area_search: which side it
 * looks over, the combatant it is placed on (the centre), a second it reaches
 * toward, its size and a flag; and its shape in battle_area_shape.
 *
 * The shapes:
 *  - a beam: the band, the size times eight pixels to either side, running
 *    from the centre through the second combatant and on (with the flag, the
 *    band ends at the second combatant's side instead);
 *  - a circle: within a distance of the centre, measured in cells of the
 *    battlefield's grid, the size the square of that distance;
 *  - the party's triangle: the enemies standing inside the triangle the three
 *    party members make;
 *  - a row: those level with the centre, 32 pixels above or below it.
 * Geometry is the battlefield's: positions are bytes, sums wrap, and an
 * angle is in 256ths of a turn (positions.h's direction). A combatant is in
 * a beam or a triangle when the directions to it from the shape's corners
 * all fall inside the shape's angles.
 *
 * The finds go in battle_area_found in slot order, after a first place kept
 * for the combatant the area is placed on, where it is on the side searched;
 * where it isn't, the list closes up. The original works in the direct
 * page's cells; what it leaves there is scaffolding (leave_*), except the
 * direction's and the multiply's cells, which their own functions keep. */
#include "battle/choosing.h"

#include "battle/aim_data.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The list of finds -------------------------------------------------------------------------- */

enum { NOBODY_FOUND = 0xFF, AREA_LIST_PLACES = AREA_FOUND_PLACES + 1, HIDDEN = 0x80 };

/* The list cleared, and the targets with it. */
static void clear_finds(GameState *g)
{
    for (unsigned place = 0; place < AREA_LIST_PLACES; place++) {
        set_battle_area_found(g, place, NOBODY_FOUND);
        set_battle_targets(g, place, NOBODY_FOUND);
    }
}

typedef struct {
    uint8_t count;
} Finds;

static void add_find(GameState *g, Finds *f, uint8_t slot) { set_battle_area_found(g, 1 + f->count++, slot); }

void battle_close_up_finds(GameState *g)
{
    if (!menu_nobody(battle_area_found(g, 0)))
        return;
    for (unsigned place = 0; place < AREA_FOUND_PLACES; place++)
        set_battle_area_found(g, place, battle_area_found(g, place + 1));
}

/* The combatant the area is placed on goes first where it is on the side
 * searched (a party slot when the search is among the party). */
static void put_placed_first(GameState *g, bool among_party, uint8_t placed)
{
    if (battle_slot_is_party(placed) == among_party)
        set_battle_area_found(g, 0, placed);
    battle_close_up_finds(g);
}

/* ---- Who can be found --------------------------------------------------------------------------- */

bool battle_can_be_aimed_at(const GameState *g, unsigned slot, bool fallen_too)
{
    return battle_slot_present(g, slot) && !(battle_object_hidden(g, slot) & HIDDEN)
           && (fallen_too || !battle_combatant_down(g, slot)) && !battle_member_sitting_out(g, slot);
}

static bool targetable(const GameState *g, unsigned slot) { return battle_can_be_aimed_at(g, slot, false); }

typedef struct {
    uint8_t first, end;
} Side;

static Side side_searched(bool among_party)
{
    return among_party ? (Side){ 0, BATTLE_PARTY_SIZE } : (Side){ BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS };
}

static BattlePoint standing(const GameState *g, unsigned slot) { return (BattlePoint){ combatant_x(g, slot), combatant_y(g, slot) }; }

AreaAsked battle_area_asked(const GameState *g)
{
    return (AreaAsked){ battle_area_search_among_party(g) != 0, battle_area_search_centre(g), battle_area_search_toward(g),
                        battle_area_search_size(g), battle_area_search_flag(g) != 0 };
}

/* The cells keep what each kind of area looks at: its side, and where it is
 * placed, how far it reaches and toward whom, as far as the kind uses them. */
void battle_note_area_asked(GameState *g, AreaKind kind, const AreaAsked *a)
{
    set_battle_area_search_among_party(g, a->among_party);
    if (kind == AREA_PARTY_TRIANGLE)
        return;
    set_battle_area_search_centre(g, a->centre);
    if (kind == AREA_ROW)
        return;
    set_battle_area_search_size(g, a->size);
    if (kind == AREA_CIRCLE)
        return;
    set_battle_area_search_toward(g, a->toward);
    set_battle_area_search_flag(g, a->flag);
}

/* An angle within a span of angles that may run past a whole turn. */
static bool within_angles(uint8_t angle, uint8_t from, uint8_t to, bool past_a_turn)
{
    return past_a_turn ? angle <= to || angle >= from : angle >= from && angle <= to;
}

/* The cells the searches loop over the side in: its end and the next slot,
 * which the loop leaves at the end. The end's cell is the one a jump to the
 * script's point takes its lift from (battle.h), so the end is left there as
 * game state; the rest is for shadow mode. */
enum { CELL_END_HIGH = 0x91, CELL_SLOT = 0x8E, CELL_LEFT_OUT = 0x92 };

static void leave_side(GameState *g, Side side)
{
    battle_leave_jump_lift(g, side.end);
    ORIGINAL_EFFECT(battle_leave_work8(g, CELL_END_HIGH, 0));
    ORIGINAL_EFFECT(battle_leave_work16(g, CELL_SLOT, side.end));
}

/* ---- The beam ------------------------------------------------------------------------------------ */

enum { QUARTER = 0x40, HALF = 0x80, THREE_QUARTERS = 0xC0, PIXELS_A_SIZE = 8 };

/* A side of the beam: a point on its edge and the span of angles from it in
 * which the beam lies. */
typedef struct {
    BattlePoint at;
    uint8_t from, to;
    bool past_a_turn;
} BeamEdge;

static BeamEdge beam_edge(BattlePoint at, uint8_t from, uint8_t to)
{
    return (BeamEdge){ at, from, to, from >= THREE_QUARTERS };
}

static BattlePoint off_by(GameState *g, BattlePoint from, uint8_t angle, uint8_t radius)
{
    uint8_t x = (uint8_t)(from.x + battle_along(g, angle, radius));
    uint8_t y = (uint8_t)(from.y + battle_along(g, (uint8_t)(angle + THREE_QUARTERS), radius));
    return (BattlePoint){ x, y };
}

/* The beam's cells: the edges' points, their spans' angles, and whether
 * each span runs past a turn. */
enum { BEAM_POINTS = 0x80, BEAM_LEFT_SPAN = 0x84, BEAM_RIGHT_SPAN = 0x88, BEAM_PAST_A_TURN = 0x8C };

static void leave_beam(GameState *g, const BeamEdge *left, const BeamEdge *right, uint8_t toward)
{
    battle_leave_work8(g, BEAM_POINTS, left->at.x);
    battle_leave_work8(g, BEAM_POINTS + 1, left->at.y);
    battle_leave_work8(g, BEAM_POINTS + 2, right->at.x);
    battle_leave_work8(g, BEAM_POINTS + 3, right->at.y);
    battle_leave_work8(g, BEAM_LEFT_SPAN, left->from);
    battle_leave_work8(g, BEAM_LEFT_SPAN + 2, left->to);
    battle_leave_work8(g, BEAM_RIGHT_SPAN, right->from);
    battle_leave_work8(g, BEAM_RIGHT_SPAN + 2, right->to);
    battle_leave_work8(g, BEAM_PAST_A_TURN, left->past_a_turn);
    battle_leave_work8(g, BEAM_PAST_A_TURN + 1, right->past_a_turn);
    battle_leave_work16(g, CELL_LEFT_OUT, toward);
}

// $C1:25A3
static void find_in_beam(GameState *g, AreaAsked a)
{
    uint8_t half_width = (uint8_t)(a.size * PIXELS_A_SIZE);
    set_battle_multiply_factors(g, 1, half_width);
    BattlePoint centre = standing(g, a.centre), toward = standing(g, a.toward);
    uint8_t heading = battle_direction(g, centre, toward);
    /* the edges' points: across the heading from the centre to its left, and
     * to its right from the centre, or from the second combatant with the
     * flag; each looks along the beam from there */
    BattlePoint left_at = off_by(g, centre, heading, half_width);
    BattlePoint right_offset = off_by(g, (BattlePoint){ 0, 0 }, (uint8_t)(heading + HALF), half_width);
    BattlePoint right_from = a.flag ? toward : centre;
    BattlePoint right_at = { (uint8_t)(right_from.x + right_offset.x), (uint8_t)(right_from.y + right_offset.y) };
    BeamEdge left = beam_edge(left_at, heading, (uint8_t)(heading + QUARTER));
    BeamEdge right = a.flag ? beam_edge(right_at, (uint8_t)(heading + HALF), (uint8_t)(heading + THREE_QUARTERS))
                            : beam_edge(right_at, (uint8_t)(heading + THREE_QUARTERS), heading);
    clear_finds(g);
    Side side = side_searched(a.among_party);
    Finds f = { 0 };
    for (uint8_t slot = side.first; slot < side.end; slot++) {
        if (!targetable(g, slot) || slot == a.toward)
            continue;
        BattlePoint there = standing(g, slot);
        if (within_angles(battle_direction(g, left.at, there), left.from, left.to, left.past_a_turn)
            && within_angles(battle_direction(g, right.at, there), right.from, right.to, right.past_a_turn))
            add_find(g, &f, slot);
    }
    ORIGINAL_EFFECT(leave_beam(g, &left, &right, a.toward));
    leave_side(g, side);
    put_placed_first(g, a.among_party, a.toward);
}

/* ---- The circle ---------------------------------------------------------------------------------- */

/* A position in the battlefield's grid cells, sixteen pixels each. */
enum { GRID_CELL_SHIFT = 4 };

static uint8_t grid_cell(uint8_t pixels) { return (uint8_t)(pixels >> GRID_CELL_SHIFT); }
static uint8_t cells_apart(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }

typedef struct {
    bool measured;
    uint8_t across_squared, down;
} LastMeasured;

/* The circle's cells: its centre's cell across and down, and of the last
 * combatant it measured, the square across and the cells down. */
enum { CIRCLE_ACROSS = 0x80, CIRCLE_DOWN = 0x82, MEASURED_ACROSS_SQUARED = 0x84, MEASURED_DOWN = 0x86 };

static void leave_circle(GameState *g, uint8_t centre, uint8_t across, uint8_t down, LastMeasured last)
{
    battle_leave_work16(g, CIRCLE_ACROSS, across);
    battle_leave_work16(g, CIRCLE_DOWN, down);
    battle_leave_work16(g, CELL_LEFT_OUT, centre);
    if (last.measured) {
        battle_leave_work16(g, MEASURED_ACROSS_SQUARED, last.across_squared);
        battle_leave_work16(g, MEASURED_DOWN, last.down);
    }
}

// $C1:2701
static void find_in_circle(GameState *g, AreaAsked a)
{
    uint8_t across = grid_cell(combatant_x(g, a.centre)), down = grid_cell(combatant_y(g, a.centre));
    clear_finds(g);
    Side side = side_searched(a.among_party);
    Finds f = { 0 };
    LastMeasured last = { false, 0, 0 };
    for (uint8_t slot = side.first; slot < side.end; slot++) {
        if (!targetable(g, slot) || slot == a.centre)
            continue;
        uint8_t dx = cells_apart(grid_cell(combatant_x(g, slot)), across);
        uint8_t dy = cells_apart(grid_cell(combatant_y(g, slot)), down);
        last = (LastMeasured){ true, battle_grid_distance_squared(g, dx), dy };
        /* the squares are added in a byte, as the original adds them */
        if ((uint8_t)(last.across_squared + battle_grid_distance_squared(g, dy)) <= a.size)
            add_find(g, &f, slot);
    }
    ORIGINAL_EFFECT(leave_circle(g, a.centre, across, down, last));
    leave_side(g, side);
    put_placed_first(g, a.among_party, a.centre);
}

/* ---- The row ------------------------------------------------------------------------------------- */

enum { ROW_REACH = 0x20 };

/* The row's cells: its top and its bottom, and the combatant it is on. */
enum { ROW_TOP = 0x85, ROW_BOTTOM = 0x87, ROW_CENTRE = 0x88 };

static void leave_row(GameState *g, uint8_t centre, uint8_t top, uint8_t bottom)
{
    battle_leave_work16(g, ROW_CENTRE, centre);
    battle_leave_work8(g, ROW_TOP, top);
    battle_leave_work8(g, ROW_BOTTOM, bottom);
}

// $C1:2332
/* The combatant the row is placed on goes first whatever side it is on. */
static void find_in_row(GameState *g, AreaAsked a)
{
    uint8_t y = combatant_y(g, a.centre);
    uint8_t top = y >= ROW_REACH ? (uint8_t)(y - ROW_REACH) : 0;
    uint8_t bottom = y + ROW_REACH <= 0xFF ? (uint8_t)(y + ROW_REACH) : 0xFF;
    clear_finds(g);
    Side side = side_searched(a.among_party);
    Finds f = { 0 };
    for (uint8_t slot = side.first; slot < side.end; slot++) {
        if (slot == a.centre || !targetable(g, slot))
            continue;
        uint8_t there = combatant_y(g, slot);
        if (there >= top && there <= bottom)
            add_find(g, &f, slot);
    }
    ORIGINAL_EFFECT(leave_row(g, a.centre, top, bottom));
    leave_side(g, side);
    set_battle_area_found(g, 0, a.centre);
}

/* ---- The party's triangle ------------------------------------------------------------------------ */

/* A corner of the triangle and the span of angles from it the triangle lies
 * in: from the direction to the next corner to the direction to the other. */
typedef struct {
    BattlePoint at;
    uint8_t from, to;
    bool past_a_turn;
} Corner;

static void swap_points(BattlePoint *a, BattlePoint *b)
{
    BattlePoint t = *a;
    *a = *b;
    *b = t;
}

/* The three members' places in the order the original takes them as
 * corners, by its compares: the rightmost and leftmost by X, with its own
 * ties broken by Y. */
static void order_corners(BattlePoint p[3])
{
    if (p[2].x >= p[0].x)
        swap_points(&p[0], &p[2]);
    if (p[2].x >= p[1].x)
        swap_points(&p[1], &p[2]);
    if (p[0].x >= p[1].x)
        swap_points(&p[0], &p[1]);
    if (p[2].x == p[0].x) {
        if (p[2].y < p[0].y)
            swap_points(&p[0], &p[2]);
    } else if (p[0].x == p[1].x) {
        if (p[1].y < p[0].y)
            swap_points(&p[0], &p[1]);
    } else if (p[0].y > p[1].y || p[0].y > p[2].y) {
        swap_points(&p[1], &p[2]);
    }
}

static Corner corner(GameState *g, BattlePoint at, BattlePoint next, BattlePoint other)
{
    uint8_t from = battle_direction(g, at, next), to = battle_direction(g, at, other);
    return (Corner){ at, from, to, to < from };
}

static bool inside_from(GameState *g, const Corner *c, BattlePoint there)
{
    return within_angles(battle_direction(g, c->at, there), c->from, c->to, c->past_a_turn);
}

/* The triangle's cells: its corners, their spans' angles, and whether each
 * span runs past a turn (a word of three, its last byte clear). */
enum { CORNER_POINTS = 0x80, CORNER_SPANS = 0x86, CORNER_PAST_A_TURN = 0x92, CORNERS = 3 };

static void leave_triangle(GameState *g, const Corner c[CORNERS])
{
    for (unsigned i = 0; i < CORNERS; i++) {
        battle_leave_work8(g, (uint8_t)(CORNER_POINTS + 2 * i), c[i].at.x);
        battle_leave_work8(g, (uint8_t)(CORNER_POINTS + 2 * i + 1), c[i].at.y);
        battle_leave_work8(g, (uint8_t)(CORNER_SPANS + 2 * i), c[i].from);
        battle_leave_work8(g, (uint8_t)(CORNER_SPANS + 2 * i + 1), c[i].to);
        battle_leave_work8(g, (uint8_t)(CORNER_PAST_A_TURN + i), c[i].past_a_turn);
    }
    battle_leave_work8(g, CORNER_PAST_A_TURN + CORNERS, 0);
}

// $C1:23A4
static void find_in_party_triangle(GameState *g)
{
    BattlePoint p[3];
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        p[member] = standing(g, member);
    order_corners(p);
    Corner c[3];
    c[0] = corner(g, p[0], p[1], p[2]);
    c[1] = corner(g, p[1], p[2], p[0]);
    c[2] = corner(g, p[2], p[0], p[1]);
    clear_finds(g);
    Side side = side_searched(false);
    Finds f = { 0 };
    for (uint8_t slot = side.first; slot < side.end; slot++) {
        if (!targetable(g, slot))
            continue;
        BattlePoint there = standing(g, slot);
        if (inside_from(g, &c[0], there) && inside_from(g, &c[1], there) && inside_from(g, &c[2], there))
            add_find(g, &f, slot);
    }
    ORIGINAL_EFFECT(leave_triangle(g, c));
    leave_side(g, side);
    battle_close_up_finds(g);
}

/* ---- The search ---------------------------------------------------------------------------------- */

/* The area rules number seven shapes; some of the numbers name the same
 * kind of area. A number past them names none, and nobody is looked for. */
enum { AREA_SHAPES = 7 };
static const AreaKind SHAPE_KIND[AREA_SHAPES] = {
    AREA_BEAM, AREA_CIRCLE, AREA_CIRCLE, AREA_PARTY_TRIANGLE, AREA_BEAM, AREA_PARTY_TRIANGLE, AREA_ROW,
};

static bool shape_known(uint8_t shape) { return shape < AREA_SHAPES; }

void battle_find_in(GameState *g, AreaKind kind, const AreaAsked *a)
{
    switch (kind) {
    case AREA_BEAM: find_in_beam(g, *a); break;
    case AREA_CIRCLE: find_in_circle(g, *a); break;
    case AREA_PARTY_TRIANGLE: find_in_party_triangle(g); break;
    case AREA_ROW: find_in_row(g, *a); break;
    }
}

// $C1:1FDD
void battle_find_area(GameState *g, uint8_t shape, const AreaAsked *a)
{
    if (shape_known(shape))
        battle_find_in(g, SHAPE_KIND[shape], a);
}

/* ---- The entries --------------------------------------------------------------------------------- */

/* The carry the search leaves its callers (the targeting's multiply takes it,
 * aim.c): set, but where a beam or a circle among the party put the member it
 * is placed on first, the compare that put it there clear. */
bool battle_area_search_carry(uint8_t shape, bool among_party, uint8_t centre, uint8_t toward)
{
    if (!shape_known(shape) || !among_party)
        return true;
    switch (SHAPE_KIND[shape]) {
    case AREA_BEAM: return !battle_slot_is_party(toward);
    case AREA_CIRCLE: return !battle_slot_is_party(centre);
    default: return true;
    }
}

/* The area noted in the search's cells searched by the shape noted beside
 * it, and the carry the search leaves its callers: the battle's step 7, and
 * the targeting's search ($C1:C741), which notes the shape and asks for it. */
void battle_search_the_noted_area(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t shape = battle_area_shape(g);
    AreaAsked asked = battle_area_asked(g);
    battle_find_area(g, shape, &asked);
    m_set_flag(s, CPU_FLAG_C, battle_area_search_carry(shape, asked.among_party, asked.centre, asked.toward));
}

// $C1:1FDD
/* The battle's step 7, as the original's code asks for it through the steps'
 * dispatch (battle/steps.c). */
void battle_find_the_noted_area(Snes *s)
{
    battle_search_the_noted_area(s);
    m_rts(s);
}
