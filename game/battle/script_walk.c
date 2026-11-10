/* Battle: the animation scripts' walk -- a combatant walking to a point by
 * the battlefield's grid, round what stands in its way.
 *
 * The battlefield is a grid of 16 by 16 cells, 16 pixels each (battle_grid:
 * walls and the cells combatants stand on marked, the rest 0). A walk first
 * finds its way: a search out from the walker's cell, a ring of cells a
 * step, each cell marked with how many steps away it is, until a ring
 * reaches the goal (or after 30 rings, when it gives up); then back from the
 * goal along cells one step nearer each time, marking them as the way. The
 * walker keeps its own copy of the marked grid, and takes the way a step at
 * a time: each step is one of the scripted moves (battle_path, moved by
 * battle/object_motion.c), two cells straight on where the way runs on,
 * otherwise a diagonal round one side or the other, cut short where the
 * corner is in the way, and it never turns straight back. The walk ends at
 * the goal, or where the way runs out.
 *
 * Only a combatant walks; on the other targets' channel and an effect's,
 * the command goes on at once. The original works in the battle's first
 * work cells as it goes; what it leaves there is scaffolding. */
#include "battle/script_language.h"

#include "battle/battle.h"
#include "battle/script_data.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "charge.h"
#include "vars.h"

/* ---- The grid --------------------------------------------------------------------------- */

/* A cell is its row (the top four bits) and its column (the bottom four);
 * a pixel's cell is its Y's top bits and its X's. Neighbours are a row up
 * or down, a column left or right, wrapping round the grid as a byte. */
enum { CELL_PIXELS_SHIFT = 4, ROW_BITS = 0xF0, UP = 0xF0, DOWN = 0x10, LEFT = 0xFF, RIGHT = 0x01 };

static uint8_t cell_of(BattlePoint p) { return (uint8_t)((p.y & ROW_BITS) | p.x >> CELL_PIXELS_SHIFT); }
static uint8_t neighbour(uint8_t cell, uint8_t way) { return (uint8_t)(cell + way); }

/* What the search marks: a free cell (0) with its steps from the walker; the
 * goal; the walker's own cell while the search runs; and the way back,
 * whose marks keep the steps in their low bits. */
enum { FREE = 0x00, THE_GOAL = 0xFF, THE_WALKER = 0x20, THE_WAY = 0xE0 };

/* The search gives up when its rings reach 30 steps. */
enum { STEPS_SEARCHED = 0x1F, GAVE_UP = 0x80 };

/* ---- Finding the way ------------------------------------------------------------------------ */

/* A ring of the search, marked: how many of its cells it looked round (it
 * ends at its first cell 0, which is also how the ring's end is marked), the
 * size of the next ring it marked out, and how often the goal was next to
 * one of its cells. */
typedef struct {
    uint8_t looked_round, next_ring_size, goal_seen;
} Ring;

static const uint8_t ways_round[] = { UP, DOWN, LEFT, RIGHT };

/* Mark a ring: every free cell next to the last ring's, with its steps,
 * making the next ring; counting how often the goal is next to one. */
static bool on_the_way(uint8_t mark) { return (mark & THE_WAY) == THE_WAY; }

static Ring mark_a_ring(GameState *g, uint8_t steps)
{
    Ring ring = { 0, 0, 0 };
    uint8_t k = 0;
    for (uint8_t cell; (cell = battle_walk_frontier(g, k)) != 0; k++) {
        for (unsigned w = 0; w < sizeof ways_round; w++) {
            uint8_t next = neighbour(cell, ways_round[w]), mark = battle_grid(g, next);
            if (mark == FREE) {
                set_battle_grid(g, next, steps);
                set_battle_walk_next_frontier(g, ring.next_ring_size++, next);
            } else if (mark == THE_GOAL) {
                ring.goal_seen++;
            }
        }
    }
    ring.looked_round = k;
    return ring;
}

/* The next ring becomes the one to search from, its end marked. */
static void move_out_a_ring(GameState *g, uint8_t ring_size)
{
    set_battle_walk_next_frontier(g, ring_size, 0);
    unsigned i = 0;
    for (; battle_walk_next_frontier(g, i) != 0; i++)
        set_battle_walk_frontier(g, i, battle_walk_next_frontier(g, i));
    set_battle_walk_frontier(g, i, 0);
}

/* Mark the way back from the goal, `steps` from the walker: each step a
 * neighbour one step nearer the walker (the first found, up, down, left,
 * right; the right one if none is), until the walker's own, which is marked
 * free for it. Returns the cell the way back ends at. */
static uint8_t mark_the_way_back(GameState *g, uint8_t walker, uint8_t goal, uint8_t steps)
{
    set_battle_grid(g, walker, FREE);
    uint8_t at = goal;
    for (int8_t nearer = (int8_t)(steps - 1); nearer >= 0; nearer--) {
        uint8_t next = 0, mark = 0;
        for (unsigned w = 0; w < sizeof ways_round; w++) {
            next = neighbour(at, ways_round[w]);
            mark = battle_grid(g, next);
            if (mark == (uint8_t)nearer)
                break;
        }
        set_battle_grid(g, next, (uint8_t)(mark | THE_WAY));
        at = next;
    }
    return at;
}

/* What the search leaves in its work cells: where its last ring's scan came
 * to and the last cell any ring looked round (none, for a walker at cell 0,
 * whose rings all end at once), the next ring's size, the walker's cell, how
 * many times the last ring saw the goal (or that it gave up), and its steps
 * (run out past the walker, once the way back is marked). Where the way back
 * came to (the goal, when it gave up) lands on the cell the next jump to the
 * script's point lifts by, and is left as game state (battle.h). */
enum { LEFT_SCAN = 0x00, LEFT_LOOKED_ROUND = 0x02, LEFT_RING_SIZE = 0x04, LEFT_WALKER = 0x0E,
       LEFT_REACHED = 0x12, LEFT_STEPS = 0x14, STEPS_RUN_OUT = 0xFF };

typedef struct {
    Ring last_ring;
    uint8_t last_looked_round, walker, steps;
    bool gave_up;
} SearchEnd;

/* The original leaves the last cell a ring looked round, which is gone from
 * the ring once the next is moved out: kept as the rings go. */

// $C1:4A71
/* Find the way across the grid from one point to another (to the cell above
 * it, where it is the walker's own), marking it on the grid. */
bool battle_find_way(GameState *g, BattlePoint from, BattlePoint to)
{
    uint8_t walker = cell_of(from);
    set_battle_grid(g, walker, THE_WALKER);
    set_battle_walk_frontier(g, 0, walker);
    set_battle_walk_frontier(g, 1, 0);
    set_battle_walk_from_cell(g, walker);
    uint8_t goal = cell_of(to);
    if (goal == walker)
        goal = neighbour(goal, UP);
    set_battle_grid(g, goal, THE_GOAL);
    set_battle_walk_goal_cell(g, goal);
    uint8_t steps = 1;
    Ring ring;
    for (;;) {
        ring = mark_a_ring(g, steps);
        charge_machine_accesses(g, ring.looked_round ? 1 : 0);
        if (ring.goal_seen)
            break;
        move_out_a_ring(g, ring.next_ring_size);
        if (++steps == STEPS_SEARCHED) {
            charge_machine_accesses(g, 6 + (walker != 0 ? 1 : 0));
            battle_leave_jump_lift(g, goal);
            return false;
        }
    }
    uint8_t way_back = mark_the_way_back(g, walker, goal, steps);
    charge_machine_accesses(g, 6 + (walker != 0 ? 1 : 0));
    battle_leave_jump_lift(g, way_back);
    return true;
}

/* ---- Starting the walk ---------------------------------------------------------------------- */

/* The walker's own grid: its copy, kept in the store while it walks, and
 * copied back out for each step. */
enum { GRID_CELLS = 256 };

static void keep_the_grid(GameState *g, uint8_t slot)
{
    uint16_t store = battle_walk_grid_store(g, slot);
    for (unsigned cell = 0; cell < GRID_CELLS; cell++)
        set_battle_walk_saved_grids(g, (uint16_t)(store + cell), battle_grid(g, cell));
}

/* A walker's heading before its first step, and its scripted move's steps
 * as a step is due and as one starts. */
enum { NO_HEADING_YET = 0xFF, STEP_DUE = 0xFF, STEP_STARTS = 7, STEPS_OVER = 0x80 };

/* The battle counts the walks each combatant starts, which nothing reads
 * (re/ram.toml, battle_script_command_cells). */

/* What starting a walk leaves: where it is from and to, and the walker. */
enum { LEFT_FROM = 0x00, LEFT_TO = 0x02, LEFT_WALKING = 0x00 };

// $C1:5454
static void take_out_the_grid(GameState *g, uint8_t slot)
{
    uint16_t store = battle_walk_grid_store(g, slot);
    for (unsigned cell = 0; cell < GRID_CELLS; cell++)
        set_battle_walk_grid(g, cell, battle_walk_saved_grids(g, (uint16_t)(store + cell)));
}

static void start_walking(GameState *g, ScriptChannel ch, uint8_t slot, BattlePoint to, uint8_t length)
{
    BattlePoint from = { combatant_x(g, slot), combatant_y(g, slot) };
    set_battle_walk_start_x(g, slot, from.x);
    set_battle_walk_start_y(g, slot, from.y);
    charge_machine_accesses(g, 4);
    charge_machine_accesses(g, 2);
    set_battle_script_walk_lengths(g, ch.channel, length);
    TIMED_AS(g, 0xC14A71, battle_find_way(g, from, to));
    charge_machine_accesses(g, 2);
    set_battle_walk_cell(g, slot, battle_walk_from_cell(g));
    keep_the_grid(g, slot);
    set_battle_path(g, slot, 0);
    set_battle_walk_cleared(g, slot, 0);
    set_battle_walk_heading(g, slot, NO_HEADING_YET);
    set_battle_path_steps(g, slot, STEP_DUE);
    set_battle_path_active(g, slot, 1);
}

/* ---- A step ---------------------------------------------------------------------------------- */

/* The headings, the way each goes on the grid, and the step a heading's move
 * starts at (eight a heading). */
typedef enum { HEADING_UP, HEADING_DOWN, HEADING_LEFT, HEADING_RIGHT } WalkHeading;
static const uint8_t heading_ways[] = { UP, DOWN, LEFT, RIGHT };
enum { STEPS_A_HEADING = 8, TURNS_ONE_WAY = 1, TURNS_OTHER_WAY = 2, CUT_ONE_WAY = 3, CUT_OTHER_WAY = 4 };

/* Where a step saw the goal, which ends the walk: the next cell along the
 * way, two cells straight on, or the cell diagonally ahead one way or the
 * other; or nowhere, and the walker steps. */
typedef enum { GOAL_NOT_SEEN, GOAL_NEXT, GOAL_TWO_AHEAD, GOAL_ONE_WAY, GOAL_OTHER_WAY } GoalSeen;

/* A step: where it saw the goal, and otherwise the scripted move it takes
 * and the cell it goes to. Where the goal was seen, the move and the cell are
 * the heading's. */
typedef struct {
    GoalSeen goal;
    WalkHeading heading;
    uint8_t step, cell;
} WalkStep;

typedef struct {
    WalkHeading heading;
    uint8_t cell;
} NextCell;

/* The heading of the first neighbour on the way, up, down, left, in that
 * order and never back along the last heading; right otherwise. */

/* A cell a step looks at, and whether a step round a corner is cut short
 * there (the corner is on the way). */

/* Choose the walker's next step along the way, from its cell: two cells
 * straight on where the way runs on, otherwise a diagonal round one side or
 * the other, cut short where the corner is in the way. */

/* What choosing a step leaves: the walker's cell, the cell it goes to (the
 * heading's, where it saw the goal), the step, and the diagonal it goes
 * round. */
enum { LEFT_AT = 0x02, LEFT_NEXT_CELL = 0x04, LEFT_STEP = 0x05, LEFT_DIAGONAL = 0x06 };

// $C1:54C1
/* The walker takes its next step, once the last is over: a scripted move,
 * facing the way it goes. False where the walk has ended. */

/* ---- The command ------------------------------------------------------------------------------ */

/* The original keeps the command's length in a work cell (where the search
 * then leaves the walker's cell). */
enum { LEFT_LENGTH = 0x0E };

static NextCell next_cell(const GameState *g, uint8_t at, uint8_t last)
{
    for (unsigned h = HEADING_UP; h < HEADING_RIGHT; h++) {
        if (last == h)
            continue;
        uint8_t cell = neighbour(at, heading_ways[h]);
        if (on_the_way(battle_walk_grid(g, cell)))
            return (NextCell){ (WalkHeading)h, cell };
    }
    return (NextCell){ HEADING_RIGHT, neighbour(at, RIGHT) };
}

static uint8_t probed(const GameState *g, uint8_t at, uint8_t step, WalkProbe probe)
{
    return neighbour(at, battle_walk_probe(g, step, probe));
}

static bool cut_short(const GameState *g, uint8_t at, uint8_t step, WalkProbe corner)
{
    return (battle_walk_grid(g, probed(g, at, step, corner)) & THE_WAY) != 0;
}

static WalkStep choose_step(const GameState *g, uint8_t at, uint8_t last)
{
    NextCell next = next_cell(g, at, last);
    uint8_t step = (uint8_t)(next.heading * STEPS_A_HEADING);
    WalkStep s = { GOAL_NOT_SEEN, next.heading, step, next.cell };
    if (battle_walk_grid(g, next.cell) == THE_GOAL) {
        s.goal = GOAL_NEXT;
        return s;
    }
    uint8_t ahead = battle_walk_grid(g, probed(g, at, step, TWO_AHEAD));
    if (ahead == THE_GOAL)
        s.goal = GOAL_TWO_AHEAD;
    if (ahead == THE_GOAL || on_the_way(ahead))
        return s;
    uint8_t one_way = probed(g, at, step, DIAGONAL_ONE_WAY), one_way_mark = battle_walk_grid(g, one_way);
    if (one_way_mark == THE_GOAL) {
        s.goal = GOAL_ONE_WAY;
        return s;
    }
    if (on_the_way(one_way_mark)) {
        s.cell = one_way;
        s.step = (uint8_t)(step + (cut_short(g, at, step, CORNER_ONE_WAY) ? CUT_ONE_WAY : TURNS_ONE_WAY));
        return s;
    }
    uint8_t other_way = probed(g, at, step, DIAGONAL_OTHER_WAY);
    if (battle_walk_grid(g, other_way) == THE_GOAL) {
        s.goal = GOAL_OTHER_WAY;
        return s;
    }
    s.cell = other_way;
    s.step = (uint8_t)(step + (cut_short(g, at, step, CORNER_OTHER_WAY) ? CUT_OTHER_WAY : TURNS_OTHER_WAY));
    return s;
}

static bool take_a_step(GameState *g, uint8_t slot)
{
    set_battle_path_steps(g, slot, STEP_STARTS);
    take_out_the_grid(g, slot);
    uint8_t at = battle_walk_cell(g, slot);
    WalkStep s = choose_step(g, at, battle_walk_heading(g, slot));
    charge_machine_accesses(g, 4 + (s.goal == GOAL_NOT_SEEN && s.step != s.heading * STEPS_A_HEADING ? 2 : 0));
    if (s.goal != GOAL_NOT_SEEN) {
        set_battle_path_active(g, slot, 0);
        return false;
    }
    set_battle_path(g, slot, s.step);
    set_battle_walk_heading(g, slot, battle_walk_step_heading(g, s.step));
    set_battle_object_facing(g, slot, battle_walk_step_facing(g, s.step));
    set_battle_walk_cell(g, slot, s.cell);
    return true;
}

static ScriptFlow past_the_walk(GameState *g, ScriptChannel ch)
{
    script_go_past(g, battle_script_walk_lengths(g, ch.channel));
    return SCRIPT_MOVED_NEXT_FRAME;
}

// $C1:5409, $C1:541B, $C1:542D
/* The channel's combatant walks to where the command aims: its way is found
 * as it sets off, and it steps along it until the walk ends. */
static ScriptFlow walk(GameState *g, ScriptChannel ch, uint8_t length, ScriptAim aim, uint8_t x, uint8_t y, uint8_t place)
{
    BattlePoint to = script_aim(g, aim, x, y, place);
    charge_machine_accesses(g, 1);
    if (ch.kind != SCRIPT_ONE_COMBATANT)
        return past_the_walk(g, ch);
    uint8_t slot = script_combatant(g, ch);
    charge_machine_accesses(g, 2);
    if (!battle_path_active(g, slot)) {
        start_walking(g, ch, slot, to, length);
        return SCRIPT_STAY;
    }
    if (!(battle_path_steps(g, slot) & STEPS_OVER))
        return SCRIPT_STAY;
    return take_a_step(g, slot) ? SCRIPT_STAY : past_the_walk(g, ch);
}
SCRIPT_OPERATION(walk, channel, length, aim, x, y, place)

/* ---- The entry -------------------------------------------------------------------------- */

/* The search as the original's walk calls it: from the point in the first two
 * work cells to the point in the next two. It returns with A's high byte
 * clear, as its first instruction leaves it. */

/* ---- The table ------------------------------------------------------------------------- */

SCRIPT_OPCODES(script_walk_opcodes,
    { 0x16, OPERATION(walk), .aim = CONSTANT(AIM_AT_COORDINATES), .x = OPERAND(1), .y = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x17, OPERATION(walk), .aim = CONSTANT(AIM_AT_THE_POINT), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x18, OPERATION(walk), .aim = CONSTANT(AIM_AT_A_PLACE), .place = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS });
