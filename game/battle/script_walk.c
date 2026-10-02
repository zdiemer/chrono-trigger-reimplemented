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
#include "charge.h"
#include "original_effects.h"
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

static bool on_the_way(uint8_t mark) { return (mark & THE_WAY) == THE_WAY; }

/* ---- Finding the way ------------------------------------------------------------------------ */

/* What the search leaves in its work cells: where its last ring scan came
 * to and the cell it looked round last, the next ring's size, the walker's
 * cell, where its way back came to, how many times the last ring reached the
 * goal (or that it gave up), and its steps. */
enum { LEFT_SCAN = 0x00, LEFT_LOOKED_ROUND = 0x02, LEFT_RING_SIZE = 0x04, LEFT_WALKER = 0x0E, LEFT_WAY_BACK = 0x10,
       LEFT_REACHED = 0x12, LEFT_STEPS = 0x14 };

typedef struct {
    uint8_t scan, looked_round, ring_size, walker, way_back, reached, steps;
    bool looked; /* a cell was looked round (the walker's cell 0 ends the first ring at once) */
} SearchCells;

static void leave_search(GameState *g, SearchCells c)
{
    script_leave(g, LEFT_SCAN, c.scan);
    if (c.looked)
        script_leave(g, LEFT_LOOKED_ROUND, c.looked_round);
    script_leave(g, LEFT_RING_SIZE, c.ring_size);
    script_leave(g, LEFT_WALKER, c.walker);
    script_leave(g, LEFT_WAY_BACK, c.way_back);
    script_leave(g, LEFT_REACHED, c.reached);
    script_leave_word(g, LEFT_STEPS, c.steps);
}

/* Mark a ring: every free cell next to the last ring's, with its steps,
 * making the next ring; counting how often the goal is next to one. */
static void mark_a_ring(GameState *g, SearchCells *c)
{
    static const uint8_t ways[] = { UP, DOWN, LEFT, RIGHT };
    c->ring_size = 0;
    for (c->scan = 0;; c->scan++) {
        uint8_t cell = battle_walk_frontier(g, c->scan);
        if (cell == 0)
            return;
        c->looked_round = cell;
        c->looked = true;
        for (unsigned w = 0; w < sizeof ways; w++) {
            uint8_t next = neighbour(cell, ways[w]), mark = battle_grid(g, next);
            if (mark == FREE) {
                set_battle_grid(g, next, c->steps);
                set_battle_walk_next_frontier(g, c->ring_size++, next);
            } else if (mark == THE_GOAL) {
                c->reached++;
            }
        }
    }
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

/* Mark the way back from the goal: each step a neighbour one step nearer
 * the walker (the first found, up, down, left, right; the right one if none
 * is), until the walker's own. */
static void mark_the_way_back(GameState *g, SearchCells *c)
{
    static const uint8_t ways[] = { UP, DOWN, LEFT, RIGHT };
    set_battle_grid(g, c->walker, FREE);
    for (c->steps--; !(c->steps & 0x80); c->steps--) {
        uint8_t next = 0, mark = 0;
        for (unsigned w = 0; w < sizeof ways; w++) {
            next = neighbour(c->way_back, ways[w]);
            mark = battle_grid(g, next);
            if (mark == c->steps)
                break;
        }
        set_battle_grid(g, next, (uint8_t)(mark | THE_WAY));
        c->way_back = next;
    }
}

// $C1:4A71
/* Find the way across the grid from one point to another (to the cell above
 * it, where it is the walker's own), marking it on the grid. */
static void find_the_way(GameState *g, BattlePoint from, BattlePoint to)
{
    SearchCells c = { .steps = 1 };
    c.walker = cell_of(from);
    set_battle_grid(g, c.walker, THE_WALKER);
    set_battle_walk_frontier(g, 0, c.walker);
    set_battle_walk_frontier(g, 1, 0);
    set_battle_walk_from_cell(g, c.walker);
    uint8_t goal = cell_of(to);
    if (goal == c.walker)
        goal = neighbour(goal, UP);
    set_battle_grid(g, goal, THE_GOAL);
    c.way_back = goal;
    set_battle_walk_goal_cell(g, goal);
    for (;;) {
        mark_a_ring(g, &c);
        if (c.reached)
            break;
        move_out_a_ring(g, c.ring_size);
        if (++c.steps == STEPS_SEARCHED) {
            c.reached = GAVE_UP;
            ORIGINAL_EFFECT(leave_search(g, c));
            return;
        }
    }
    mark_the_way_back(g, &c);
    ORIGINAL_EFFECT(leave_search(g, c));
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

static void take_out_the_grid(GameState *g, uint8_t slot)
{
    uint16_t store = battle_walk_grid_store(g, slot);
    for (unsigned cell = 0; cell < GRID_CELLS; cell++)
        set_battle_walk_grid(g, cell, battle_walk_saved_grids(g, (uint16_t)(store + cell)));
}

/* A walker's heading before its first step, and its scripted move's steps
 * as a step is due and as one starts. */
enum { NO_HEADING_YET = 0xFF, STEP_DUE = 0xFF, STEP_STARTS = 7, STEPS_OVER = 0x80 };

/* What starting a walk leaves: where it is from and to, and the walker. */
enum { LEFT_FROM = 0x00, LEFT_TO = 0x02, LEFT_WALKING = 0x00 };

static void leave_walk_ends(GameState *g, BattlePoint from, BattlePoint to)
{
    script_leave(g, LEFT_FROM, from.x);
    script_leave(g, LEFT_FROM + 1, from.y);
    script_leave(g, LEFT_TO, to.x);
    script_leave(g, LEFT_TO + 1, to.y);
}

// $C1:5454
static void start_walking(GameState *g, ScriptChannel ch, uint8_t slot, BattlePoint to, uint8_t length)
{
    BattlePoint from = { combatant_x(g, slot), combatant_y(g, slot) };
    set_battle_walk_start_x(g, slot, from.x);
    set_battle_walk_start_y(g, slot, from.y);
    ORIGINAL_EFFECT(leave_walk_ends(g, from, to));
    set_battle_script_command_cells(g, slot, (uint8_t)(battle_script_command_cells(g, slot) + 1));
    set_battle_script_walk_lengths(g, ch.channel, length);
    TIMED_AS(g, 0xC14A71, find_the_way(g, from, to));
    ORIGINAL_EFFECT(script_leave_word(g, LEFT_WALKING, slot));
    set_battle_walk_cell(g, slot, battle_walk_from_cell(g));
    keep_the_grid(g, slot);
    set_battle_path(g, slot, 0);
    set_battle_walk_cleared(g, slot, 0);
    set_battle_walk_heading(g, slot, NO_HEADING_YET);
    set_battle_path_steps(g, slot, STEP_DUE);
    set_battle_path_active(g, slot, 1);
}

/* ---- A step ---------------------------------------------------------------------------------- */

/* The headings, and the step a heading's move starts at (eight a heading). */
typedef enum { HEADING_UP, HEADING_DOWN, HEADING_LEFT, HEADING_RIGHT } WalkHeading;
enum { STEPS_A_HEADING = 8, TURNS_ONE_WAY = 1, TURNS_OTHER_WAY = 2, CUT_ONE_WAY = 3, CUT_OTHER_WAY = 4 };

typedef struct {
    bool ended;
    uint8_t step, cell;
} WalkStep;

/* What choosing a step leaves: the walker's cell, the cell it looked at
 * last or goes to, the step, and the diagonal it looked at. */
enum { LEFT_AT = 0x02, LEFT_NEXT_CELL = 0x04, LEFT_STEP = 0x05, LEFT_DIAGONAL = 0x06 };

typedef struct {
    uint8_t at, next_cell, step, diagonal;
    bool step_chosen, diagonal_looked;
} StepCells;

static void leave_step(GameState *g, StepCells c)
{
    script_leave(g, LEFT_AT, c.at);
    script_leave_word(g, LEFT_NEXT_CELL, c.next_cell);
    if (c.step_chosen)
        script_leave(g, LEFT_STEP, c.step);
    if (c.diagonal_looked)
        script_leave_word(g, LEFT_DIAGONAL, c.diagonal);
}

/* The heading of the first neighbour on the way, up, down, left, in that
 * order and never back along the last heading; right otherwise. */
static WalkHeading next_heading(const GameState *g, uint8_t at, uint8_t last, StepCells *c)
{
    static const uint8_t ways[] = { UP, DOWN, LEFT };
    for (unsigned h = HEADING_UP; h < HEADING_RIGHT; h++) {
        if (last == h)
            continue;
        c->next_cell = neighbour(at, ways[h]);
        if (on_the_way(battle_walk_grid(g, c->next_cell)))
            return (WalkHeading)h;
    }
    c->next_cell = neighbour(at, RIGHT);
    return HEADING_RIGHT;
}

/* Choose the walker's next step along the way, from its cell: the walk ends
 * where the next cell, or a cell the step looks at, is the goal. */
static WalkStep choose_step(GameState *g, uint8_t at, uint8_t last, StepCells *c)
{
    c->at = at;
    WalkHeading h = next_heading(g, at, last, c);
    uint8_t step = (uint8_t)(h * STEPS_A_HEADING);
    c->step = step;
    c->step_chosen = true;
    if (battle_walk_grid(g, c->next_cell) == THE_GOAL)
        return (WalkStep){ true, 0, 0 };
    uint8_t ahead = battle_walk_grid(g, neighbour(at, battle_walk_probe(g, step, TWO_AHEAD)));
    if (ahead == THE_GOAL)
        return (WalkStep){ true, 0, 0 };
    if (on_the_way(ahead))
        return (WalkStep){ false, step, c->next_cell };
    uint8_t one_way = neighbour(at, battle_walk_probe(g, step, DIAGONAL_ONE_WAY));
    uint8_t mark = battle_walk_grid(g, one_way);
    if (mark == THE_GOAL)
        return (WalkStep){ true, 0, 0 };
    c->diagonal_looked = true;
    if (on_the_way(mark)) {
        c->diagonal = c->next_cell = one_way;
        bool cut = battle_walk_grid(g, neighbour(at, battle_walk_probe(g, step, CORNER_ONE_WAY))) & THE_WAY;
        c->step = (uint8_t)(step + (cut ? CUT_ONE_WAY : TURNS_ONE_WAY));
        return (WalkStep){ false, c->step, one_way };
    }
    uint8_t other_way = neighbour(at, battle_walk_probe(g, step, DIAGONAL_OTHER_WAY));
    if (battle_walk_grid(g, other_way) == THE_GOAL) {
        c->diagonal_looked = false;
        return (WalkStep){ true, 0, 0 };
    }
    c->diagonal = c->next_cell = other_way;
    bool cut = battle_walk_grid(g, neighbour(at, battle_walk_probe(g, step, CORNER_OTHER_WAY))) & THE_WAY;
    c->step = (uint8_t)(step + (cut ? CUT_OTHER_WAY : TURNS_OTHER_WAY));
    return (WalkStep){ false, c->step, other_way };
}

// $C1:54C1
/* The walker takes its next step, once the last is over: a scripted move,
 * facing the way it goes. False where the walk has ended. */
static bool take_a_step(GameState *g, uint8_t slot)
{
    set_battle_path_steps(g, slot, STEP_STARTS);
    take_out_the_grid(g, slot);
    StepCells c = { 0 };
    WalkStep s = choose_step(g, battle_walk_cell(g, slot), battle_walk_heading(g, slot), &c);
    ORIGINAL_EFFECT(leave_step(g, c));
    if (s.ended) {
        set_battle_path_active(g, slot, 0);
        return false;
    }
    set_battle_path(g, slot, s.step);
    set_battle_walk_heading(g, slot, battle_walk_step_heading(g, s.step));
    set_battle_object_facing(g, slot, battle_walk_step_facing(g, s.step));
    set_battle_walk_cell(g, slot, s.cell);
    return true;
}

/* ---- The command ------------------------------------------------------------------------------ */

/* The original keeps the command's length in a work cell (where the search
 * then leaves the walker's cell). */
enum { LEFT_LENGTH = 0x0E };

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
    ORIGINAL_EFFECT(script_leave(g, LEFT_LENGTH, length));
    if (ch.kind != SCRIPT_ONE_COMBATANT)
        return past_the_walk(g, ch);
    uint8_t slot = script_combatant(g, ch);
    ORIGINAL_EFFECT(script_leave_word(g, LEFT_WALKING, slot));
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
void battle_find_the_way(Snes *s)
{
    GameState *g = state_of(s);
    find_the_way(g, (BattlePoint){ battle_work(g, 0), battle_work(g, 1) }, (BattlePoint){ battle_work(g, 2), battle_work(g, 3) });
    s->cpu.a = 0;
    m_rts(s);
}

/* ---- The table ------------------------------------------------------------------------- */

SCRIPT_OPCODES(script_walk_opcodes,
    { 0x16, OPERATION(walk), .aim = CONSTANT(AIM_AT_COORDINATES), .x = OPERAND(1), .y = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0x17, OPERATION(walk), .aim = CONSTANT(AIM_AT_THE_POINT), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x18, OPERATION(walk), .aim = CONSTANT(AIM_AT_A_PLACE), .place = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS });
