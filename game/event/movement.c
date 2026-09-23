/* Scripts moving objects about the map: walking to a tile or up to another
 * object, a number of steps in a direction, a jump or a launch, following
 * someone about.
 *
 * A move is a number of steps (field_object_steps_left) the field's object
 * update makes one a frame, at the velocity field_start_step sets from the
 * direction in field_direction; a move command stays on itself while one runs.
 * Toward a tile or an object, a command makes one step's worth of move at a
 * time and looks again, until it stands on the tile or next to the object;
 * a counted move makes one move of a given number of steps. An object may
 * turn to face the way it moves, and runs the walking and standing
 * animations; the "keep facing" commands do neither. On arrival, the
 * object's arrival flags can ask for it to be lined up with the tile or the
 * object, which takes steps of its own.
 *
 * The motion itself is the field's, and still the original's code these
 * commands call out to: field_direction_to_target ($C0:ABA2), which turns the
 * own and target tiles into a direction; field_object_in_the_way ($C0:5B95),
 * carry set when another object blocks it (the command then turns by $30);
 * field_start_step ($C0:ACFD) and field_set_velocity ($C0:AC69);
 * field_update_walk_animation ($C0:5614) and field_stand_animation
 * ($C0:568B); and field_line_up_with_tile ($C0:305D) and
 * field_line_up_with_object ($C0:30B3), carry set while a step is still
 * needed. So these commands are bridges: the decisions are plain C over the
 * game's state, and the calls are made with the registers the original has
 * at each (the running object in X for the two that read it; the command's
 * offset in Y, which the callees carry back). The command's offset after a
 * call is the one the callee leaves in Y, as the original's is; and where
 * the original adds or subtracts between calls, the overflow it leaves is
 * put in V for the calls after, which carry it to the command's return
 * (scaffolding, event/leaves.h).
 *
 * This file is still the original's shape -- a bridge per opcode, operands
 * by position -- because every command calls out mid-way. When the field's
 * motion is C, these become rows of the language's table, and their families
 * (to a tile, toward an object or a member; turning or keeping the facing;
 * counted) one operation each (docs/TODO.md). */
#include "cpu_io.h"
#include "event/bridge.h"
#include "event/event_data.h"
#include "event/variables.h"

/* The command, as the bridges here read it: where it is, and its operands by
 * position (these commands are still the original's shape; see above). */
typedef struct {
    uint16_t at;
} Command;

static Command command_here(const Snes *s) { return (Command){ event_command_offset(s) }; }

static uint8_t operand(const GameState *g, const Command *c, unsigned n)
{
    return script_byte(g, (uint16_t)(c->at + n));
}

static EventObject member_operand(const GameState *g, const Command *c, unsigned n)
{
    return slot_of(field_party_objects(g, operand(g, c, n) >> 1));
}

enum { COUNTED_MOVE_RUNNING = 1, CROSS_TILES = 0x80 };
enum { LINE_UP_ON_TILE = 0x01, LINE_UP_ON_OBJECT = 0x02 };   /* field_object_arrival_flags */
enum { TURN_ASIDE = 0x30 };   /* what a blocked move turns by, a byte to a turn */

/* ---- The decisions --------------------------------------------------------------------- */

static bool disabled(const GameState *g, unsigned object) { return field_object_state_state(g, object) & 0x80; }

static bool still_moving(const GameState *g) { return field_object_steps_left(g, running_object(g)) != 0; }

typedef enum { MOVE_FRESH, MOVE_RUNNING, MOVE_DONE } CountedMove;

/* A counted move's progress: none started, one running, or one just over
 * (whose mark is then taken away). */
static CountedMove counted_move(GameState *g)
{
    unsigned self = running_object(g);
    if (field_object_counted_move(g, self) == 0)
        return MOVE_FRESH;
    if (field_object_steps_left(g, self) != 0)
        return MOVE_RUNNING;
    set_field_object_counted_move(g, self, 0);
    return MOVE_DONE;
}

/* The tiles field_direction_to_target works from: the running object's, and
 * the one it heads for. */
static void set_own_tile(GameState *g)
{
    unsigned self = running_object(g);
    set_field_own_tile_x(g, field_object_x_tile(g, self));
    set_field_own_tile_y(g, field_object_y_tile(g, self));
}

static void set_target_tile(GameState *g, uint8_t x, uint8_t y)
{
    set_field_target_tile_x(g, x);
    set_field_target_tile_y(g, y);
}

static bool on_target_tile(const GameState *g)
{
    return field_own_tile_y(g) == field_target_tile_y(g) && field_own_tile_x(g) == field_target_tile_x(g);
}

/* The carry the original's comparison of the two tiles leaves (row, then
 * column): the calls made next carry it into field_object_in_the_way's
 * answer, so the port hands it on. */
static bool tile_compare_carry(const GameState *g)
{
    if (field_own_tile_y(g) != field_target_tile_y(g))
        return field_own_tile_y(g) >= field_target_tile_y(g);
    return field_own_tile_x(g) >= field_target_tile_x(g);
}

/* Head for another object: its tile, and the object, kept as the target. */
static void aim_at_object(GameState *g, unsigned target)
{
    set_target_tile(g, field_object_x_tile(g, target), field_object_y_tile(g, target));
    set_field_target_object(g, (uint16_t)(target * 2));
    set_own_tile(g);
}

static bool within_one(uint8_t a, uint8_t b) { return a == b || (uint8_t)(a + 1) == b || (uint8_t)(a - 1) == b; }

static bool next_to_target(const GameState *g)
{
    return within_one(field_own_tile_y(g), field_target_tile_y(g)) &&
           within_one(field_own_tile_x(g), field_target_tile_x(g));
}

/* The carry its comparisons leave: set when next to the target, otherwise
 * that of the last comparison, a tile short of the target on the row or
 * column that isn't within one. */
static bool next_to_carry(const GameState *g)
{
    if (!within_one(field_own_tile_y(g), field_target_tile_y(g)))
        return (uint8_t)(field_own_tile_y(g) - 1) >= field_target_tile_y(g);
    if (!within_one(field_own_tile_x(g), field_target_tile_x(g)))
        return (uint8_t)(field_own_tile_x(g) - 1) >= field_target_tile_x(g);
    return true;
}

static bool lines_up(const GameState *g, uint8_t flag) { return field_object_arrival_flags(g, running_object(g)) & flag; }

/* A move of `steps` steps, the same many allowed across tiles. */
static void set_steps(GameState *g, uint8_t steps)
{
    unsigned self = running_object(g);
    set_field_object_steps_left(g, self, steps);
    set_field_object_move_state_tile_mode(g, self, steps);
}

static void count_move(GameState *g)
{
    unsigned self = running_object(g);
    set_field_object_counted_move(g, self, (uint8_t)(field_object_counted_move(g, self) + 1));
}

/* ---- The calls ----------------------------------------------------------------------- */

/* Back from a call-out, which returns to `returns_to`: true if the callee
 * left the machine as the command runs (event_machine_back); otherwise the
 * port stops there, declaring it, and the original carries on from the
 * callee's machine. */
static bool back(Snes *s, uint32_t returns_to, bool x8)
{
    if (event_machine_back(s, x8))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

/* Call the field's motion routine `target` from the JSR at `site`; false if
 * the port stopped. The routines read nothing of the registers but Y, which
 * is the command's offset where they are made, and V, which they carry. */
static bool call(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr_dc(s, site, target, DC_A | DC_X | DC_N | DC_Z | DC_C);
    return back(s, site + 3, false);
}

/* The same for the routines the command reads a carry from afterwards
 * (field_direction_to_target's is carried into field_object_in_the_way's,
 * which with the line-up routines answers in it): the carry at the call is
 * the original's there, so it comes back as the original's. */
static bool call_for_carry(Snes *s, uint32_t site, uint32_t target, bool carry)
{
    m_set_flag(s, CPU_FLAG_C, carry);
    m_jsr_dc(s, site, target, DC_A | DC_X | DC_N | DC_Z);
    return back(s, site + 3, false);
}

/* The same for field_start_step and field_set_velocity, which take the
 * running object in X. */
static bool call_with_object(Snes *s, uint32_t site, uint32_t target)
{
    s->cpu.x = field_current_object(state_of(s));
    m_jsr_dc(s, site, target, DC_A | DC_N | DC_Z | DC_C);
    return back(s, site + 3, false);
}

/* The flags an addition between calls leaves, for the calls after it. */
static void leave_flags(Snes *s, bool carry, bool overflow)
{
    m_set_flag(s, CPU_FLAG_C, carry);
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, overflow));
}

/* Turn the blocked direction aside, as the original's 8-bit ADC does. */
static uint8_t turn_aside(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t d = (uint8_t)field_direction(g);
    uint8_t turned = (uint8_t)(d + TURN_ASIDE);
    leave_flags(s, d + TURN_ASIDE > 0xFF, ORIGINAL_EFFECTS && overflows8(d, TURN_ASIDE, turned));
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | turned));
    return turned;
}

/* The way to the target (field_direction_to_target, called at `direction`),
 * faced; and if another object is in the way (field_object_in_the_way, at
 * `blocked`), turned aside and faced that way. The original looks the turned
 * facing up with A's high byte as the callee left it; the port goes on only
 * when that is clear (as the game always has it), and leaves the rest to the
 * original. `carry` is the one the command's comparison left. */
static bool face_the_way(Snes *s, uint32_t direction, uint32_t blocked, bool carry)
{
    GameState *g = state_of(s);
    if (!call_for_carry(s, direction, 0xC0ABA2, carry))
        return false;
    uint8_t facing = facing_for_direction(g, (uint8_t)field_direction(g));
    set_field_object_facing(g, running_object(g), facing);
    s->cpu.a = facing;   /* as the original's lookup leaves A */
    if (!call_for_carry(s, blocked, 0xC05B95, m_flag(s, CPU_FLAG_C)))
        return false;
    if (!m_flag(s, CPU_FLAG_C))
        return true;
    if (s->cpu.a >> 8)
        return false;
    uint8_t turned = turn_aside(s);
    set_field_object_facing(g, running_object(g), facing_for_direction(g, turned));
    return true;
}

/* The keep-facing moves' version: the way, turned aside if blocked, unfaced. */
static bool find_the_way(Snes *s, uint32_t direction, uint32_t blocked, bool carry)
{
    if (!call_for_carry(s, direction, 0xC0ABA2, carry) ||
        !call_for_carry(s, blocked, 0xC05B95, m_flag(s, CPU_FLAG_C)))
        return false;
    if (m_flag(s, CPU_FLAG_C))
        turn_aside(s);
    return true;
}

/* End the turn on the command, which the move keeps running (its offset kept
 * in event_saved_position across the calls). */
static void moving(Snes *s) { event_return(s, (EventStep){ event_saved_position(state_of(s)), false }); }

/* Start the move (field_start_step at `step`), and end the turn. */
static void start_move(Snes *s, uint32_t step)
{
    set_event_saved_position(state_of(s), s->cpu.y);
    if (call_with_object(s, step, 0xC0ACFD))
        moving(s);
}

/* The same, with the walking animation (at `walk`). */
static void start_walking(Snes *s, uint32_t step, uint32_t walk)
{
    set_event_saved_position(state_of(s), s->cpu.y);
    if (call_with_object(s, step, 0xC0ACFD) && call(s, walk, 0xC05614))
        moving(s);
}

/* Moves of the object's step length, and counted moves (marked). */
static void start_step_move(Snes *s, uint32_t step)
{
    GameState *g = state_of(s);
    set_steps(g, field_object_step_length(g, running_object(g)));
    start_move(s, step);
}

static void start_step_walk(Snes *s, uint32_t step, uint32_t walk)
{
    GameState *g = state_of(s);
    set_steps(g, field_object_step_length(g, running_object(g)));
    start_walking(s, step, walk);
}

static void start_counted_move(Snes *s, uint32_t step)
{
    count_move(state_of(s));
    start_move(s, step);
}

static void start_counted_walk(Snes *s, uint32_t step, uint32_t walk)
{
    count_move(state_of(s));
    start_walking(s, step, walk);
}

static void hold(Snes *s)
{
    Command c = command_here(s);
    event_return(s, (EventStep){ c.at, false });
}

static void go_on(Snes *s, uint16_t length)
{
    Command c = command_here(s);
    event_return(s, (EventStep){ (uint16_t)(c.at + length), true });
}

/* Arrived: the standing animation ($C0:568B at `stand`), and on after the
 * command's `length` bytes. */
static void arrive(Snes *s, uint32_t stand, uint16_t length)
{
    if (call(s, stand, 0xC0568B))
        go_on(s, length);
}

/* Still lining up: the walking animation (at `walk`), and stay. */
static void keep_walking(Snes *s, uint32_t walk)
{
    if (call(s, walk, 0xC05614))
        hold(s);
}

/* Line up on arrival (the routine at `target`, called at `at`, with the
 * carry of the comparison that found it there, set): false if the port
 * stopped. `lining` says whether a step is still needed. */
static bool line_up(Snes *s, uint32_t at, uint32_t target, bool *lining)
{
    if (!call_for_carry(s, at, target, true))
        return false;
    *lining = m_flag(s, CPU_FLAG_C);
    return true;
}

/* ---- Toward a tile -------------------------------------------------------------------- */

static void aim_at_tile(GameState *g, const Command *c, bool from_variables)
{
    if (from_variables) {
        set_field_target_tile_x(g, variable_byte(g, operand(g, c, 1)));
        set_field_target_tile_y(g, variable_byte(g, operand(g, c, 2)));
    } else {
        set_field_target_tile_x(g, operand(g, c, 1));
        set_field_target_tile_y(g, operand(g, c, 2));
    }
    set_own_tile(g);
}

/* The move-to-tile commands that turn, walk and stand, and where each one's
 * copy of the code has its calls. */
enum { TO_TILE, TO_TILE_FROM_VARIABLES };
static const struct {
    bool from_variables;   /* the tile from variables rather than operands */
    uint32_t line_up, walk_on_line, stand, direction, blocked, step, walk;
} turning_tile_moves[] = {
    [TO_TILE] = { false, 0xC04F97, 0xC04FA5, 0xC04F9C, 0xC04FAB, 0xC04FBD, 0xC04FE0, 0xC04FE3 },
    [TO_TILE_FROM_VARIABLES] = { true, 0xC050D6, 0xC050E4, 0xC050DB, 0xC050EA, 0xC050FC, 0xC0511F, 0xC05122 },
};

/* And those that keep their facing, with no animations. */
enum { TO_TILE_KEEP_FACING, TO_TILE_FROM_VARIABLES_KEEP_FACING };
static const struct {
    bool from_variables;
    uint32_t line_up, direction, blocked, step;
} keeping_tile_moves[] = {
    [TO_TILE_KEEP_FACING] = { false, 0xC0515F, 0xC0516D, 0xC05170, 0xC0518A },
    [TO_TILE_FROM_VARIABLES_KEEP_FACING] = { true, 0xC051E3, 0xC051F1, 0xC051F4, 0xC0520E },
};

typedef enum { ON_THE_WAY, ARRIVED, LINING_UP, STOPPED } TileArrival;

/* Where a move to a tile stands, having aimed at it: on the way, or on the
 * tile (lined up with it by `line_up` first when the object asks for that). */
static TileArrival tile_arrival(Snes *s, uint32_t line_up_at)
{
    GameState *g = state_of(s);
    if (!on_target_tile(g))
        return ON_THE_WAY;
    bool lining = false;
    if (lines_up(g, LINE_UP_ON_TILE) && !line_up(s, line_up_at, 0xC0305D, &lining))
        return STOPPED;
    return lining ? LINING_UP : ARRIVED;
}

/* Toward a tile, a step's worth at a time, until standing on it. */
static void move_to_tile(Snes *s, unsigned move)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    if (still_moving(g)) {
        hold(s);
        return;
    }
    aim_at_tile(g, &c, turning_tile_moves[move].from_variables);
    switch (tile_arrival(s, turning_tile_moves[move].line_up)) {
    case ON_THE_WAY:
        if (face_the_way(s, turning_tile_moves[move].direction, turning_tile_moves[move].blocked,
                         tile_compare_carry(g)))
            start_step_walk(s, turning_tile_moves[move].step, turning_tile_moves[move].walk);
        return;
    case LINING_UP: keep_walking(s, turning_tile_moves[move].walk_on_line); return;
    case ARRIVED: arrive(s, turning_tile_moves[move].stand, 3); return;
    default: return;
    }
}

static void move_to_tile_keeping_facing(Snes *s, unsigned move)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    if (still_moving(g)) {
        hold(s);
        return;
    }
    aim_at_tile(g, &c, keeping_tile_moves[move].from_variables);
    switch (tile_arrival(s, keeping_tile_moves[move].line_up)) {
    case ON_THE_WAY:
        if (find_the_way(s, keeping_tile_moves[move].direction, keeping_tile_moves[move].blocked,
                         tile_compare_carry(g)))
            start_step_move(s, keeping_tile_moves[move].step);
        return;
    case LINING_UP: hold(s); return;
    case ARRIVED: go_on(s, 3); return;
    default: return;
    }
}

// $C0:4F61
/* Walk to a tile, turning to face the way and stepping round what's in the
 * way, and wait until there. */
void event_cmd_move_to_tile(Snes *s) { move_to_tile(s, TO_TILE); }

// $C0:5084
/* The same, to a tile the script has worked out in variables. */
void event_cmd_move_to_tile_from_variables(Snes *s) { move_to_tile(s, TO_TILE_FROM_VARIABLES); }

// $C0:5129
/* Slide to a tile without turning or animating: a push, a float. */
void event_cmd_move_to_tile_keep_facing(Snes *s) { move_to_tile_keeping_facing(s, TO_TILE_KEEP_FACING); }

// $C0:5191
/* The same, to a tile the script has worked out in variables. */
void event_cmd_move_to_tile_from_variables_keep_facing(Snes *s)
{
    move_to_tile_keeping_facing(s, TO_TILE_FROM_VARIABLES_KEEP_FACING);
}

// $C0:4FEA
/* Walk toward a tile for a limited number of steps, and wait until they are
 * taken or the tile reached. */
void event_cmd_move_to_tile_counted(Snes *s)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    unsigned self = running_object(g);
    bool fresh = field_object_counted_move(g, self) == 0;
    if (!fresh && field_object_steps_left(g, self) != 0) {
        hold(s);
        return;
    }
    if (fresh) {
        aim_at_tile(g, &c, false);
        switch (tile_arrival(s, 0xC05025)) {
        case ON_THE_WAY:
            if (!face_the_way(s, 0xC0503D, 0xC0504F, tile_compare_carry(g)))
                return;
            c = command_here(s);
            set_steps(g, operand(g, &c, 3));
            start_counted_walk(s, 0xC0507A, 0xC0507D);
            return;
        case LINING_UP: keep_walking(s, 0xC05037); return;
        case STOPPED: return;
        default: break;
        }
    }
    set_field_object_counted_move(g, self, 0);
    arrive(s, 0xC0502D, 4);
}

/* ---- Toward an object ------------------------------------------------------------------ */

/* Next to the target: whether a step of lining up with it (by the routine
 * at `line_up_at`) is still needed, when the object asks for that. */
static TileArrival object_arrival(Snes *s, uint32_t line_up_at)
{
    bool lining = false;
    if (lines_up(state_of(s), LINE_UP_ON_OBJECT) && !line_up(s, line_up_at, 0xC030B3, &lining))
        return STOPPED;
    return lining ? LINING_UP : ARRIVED;
}

// $C0:5228
/* Toward the object `target` a step's worth at a time, until next to it. */
static void approach(Snes *s, unsigned target)
{
    GameState *g = state_of(s);
    if (disabled(g, target)) {
        arrive(s, 0xC05273, 2);
        return;
    }
    aim_at_object(g, target);
    if (!next_to_target(g)) {
        if (face_the_way(s, 0xC05281, 0xC05293, next_to_carry(g)))
            start_step_walk(s, 0xC052B6, 0xC052B9);
        return;
    }
    switch (object_arrival(s, 0xC0526E)) {
    case LINING_UP: keep_walking(s, 0xC0527B); return;
    case ARRIVED: arrive(s, 0xC05273, 2); return;
    default: return;
    }
}

// $C0:5215
/* Walk up to another object, and wait until next to it. */
void event_cmd_move_toward_object(Snes *s)
{
    Command c = command_here(s);
    if (still_moving(state_of(s)))
        hold(s);
    else
        approach(s, slot_of(operand(state_of(s), &c, 1)));
}

// $C0:5410
/* Walk up to a party member, and wait until next to them. */
void event_cmd_move_toward_party_member(Snes *s)
{
    Command c = command_here(s);
    if (still_moving(state_of(s)))
        hold(s);
    else
        approach(s, member_operand(state_of(s), &c, 1));
}

// $C0:52D3
/* As approach, keeping the object's facing, with no animations. */
static void approach_keeping_facing(Snes *s, unsigned target)
{
    GameState *g = state_of(s);
    if (disabled(g, target)) {
        go_on(s, 2);
        return;
    }
    aim_at_object(g, target);
    if (!next_to_target(g)) {
        if (find_the_way(s, 0xC05326, 0xC05329, next_to_carry(g)))
            start_step_move(s, 0xC05343);
        return;
    }
    switch (object_arrival(s, 0xC05319)) {
    case LINING_UP: hold(s); return;
    case ARRIVED: go_on(s, 2); return;
    default: return;
    }
}

// $C0:52C0
/* Slide up to another object without turning or animating. */
void event_cmd_move_toward_object_keep_facing(Snes *s)
{
    Command c = command_here(s);
    if (still_moving(state_of(s)))
        hold(s);
    else
        approach_keeping_facing(s, slot_of(operand(state_of(s), &c, 1)));
}

// $C0:54F5
/* Slide up to a party member without turning or animating. */
void event_cmd_move_toward_party_member_keep_facing(Snes *s)
{
    Command c = command_here(s);
    if (still_moving(state_of(s)))
        hold(s);
    else
        approach_keeping_facing(s, member_operand(state_of(s), &c, 1));
}

// $C0:536B
/* One move of operand 2 steps toward the object `target`; the command is over
 * when the move is, or on arrival. */
static void approach_counted(Snes *s, unsigned target)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    switch (counted_move(g)) {
    case MOVE_RUNNING: hold(s); return;
    case MOVE_DONE: go_on(s, 3); return;
    default: break;
    }
    if (!disabled(g, target)) {
        aim_at_object(g, target);
        if (!next_to_target(g)) {
            if (!face_the_way(s, 0xC053CA, 0xC053DC, next_to_carry(g)))
                return;
            c = command_here(s);
            set_steps(g, operand(g, &c, 2));
            start_counted_walk(s, 0xC05406, 0xC05409);
            return;
        }
        switch (object_arrival(s, 0xC053B1)) {
        case LINING_UP: keep_walking(s, 0xC053C4); return;
        case STOPPED: return;
        default: break;
        }
    }
    set_field_object_counted_move(g, running_object(g), 0);
    arrive(s, 0xC053BB, 3);
}

// $C0:534A
/* Walk a limited number of steps toward another object. */
void event_cmd_move_toward_object_counted(Snes *s)
{
    Command c = command_here(s);
    approach_counted(s, slot_of(operand(state_of(s), &c, 1)));
}

// $C0:550E
/* Walk a limited number of steps toward a party member. */
void event_cmd_move_toward_party_member_counted(Snes *s)
{
    Command c = command_here(s);
    approach_counted(s, member_operand(state_of(s), &c, 1));
}

/* Whether the running object stands 2 to 13 tiles right of the screen's left
 * edge and 2 to 12 below its top (the edges halved, into the scratch cells);
 * the subtractions' flags are the original's. */
static bool well_on_screen(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t left = (uint8_t)(field_screen_left(g) >> 1), top = (uint8_t)(field_screen_top(g) >> 1);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_SECOND, left));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_THIRD, top));
    uint8_t x = field_own_tile_x(g), dx = (uint8_t)(x - left);
    leave_flags(s, x >= left, overflows8(x, (uint8_t)~left, dx));
    if (dx < 2 || dx >= 0x0E)
        return false;
    uint8_t y = field_own_tile_y(g), dy = (uint8_t)(y - top);
    leave_flags(s, y >= top, overflows8(y, (uint8_t)~top, dy));
    return dy >= 2 && dy < 0x0D;
}

// $C0:5429
/* Walk toward a party member until next to them, or until well on screen:
 * someone running in to join a scene. */
void event_cmd_move_toward_party_member_until_on_screen(Snes *s)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    if (still_moving(g)) {
        hold(s);
        return;
    }
    unsigned target = member_operand(g, &c, 1);
    if (!disabled(g, target)) {
        aim_at_object(g, target);
        if (!next_to_target(g) && !well_on_screen(s)) {
            /* every way off the screen's middle leaves the carry set */
            if (face_the_way(s, 0xC054B6, 0xC054C8, true))
                start_step_walk(s, 0xC054EB, 0xC054EE);
            return;
        }
    }
    arrive(s, 0xC054A8, 2);
}

/* ---- In a direction ------------------------------------------------------------------ */

/* Head in direction `d` (a byte to a turn). */
static void head(GameState *g, uint8_t d) { set_field_direction(g, d); }

// $C0:4E73
/* Walk a number of steps in a direction, facing it. */
void event_cmd_move_in_direction(Snes *s)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    switch (counted_move(g)) {
    case MOVE_RUNNING: hold(s); return;
    case MOVE_DONE: arrive(s, 0xC04E85, 3); return;
    default: break;
    }
    unsigned self = running_object(g);
    uint8_t d = operand(g, &c, 1);
    head(g, d);
    set_field_object_facing(g, self, facing_for_direction(g, d));
    set_field_object_move_state_tile_mode(g, self, CROSS_TILES);
    set_field_object_steps_left(g, self, operand(g, &c, 2));
    start_counted_walk(s, 0xC04EBD, 0xC04EC0);
}

// $C0:4EC7
/* Slide a number of steps in a direction without turning: backing away,
 * being pushed. */
void event_cmd_move_in_direction_keep_facing(Snes *s)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    switch (counted_move(g)) {
    case MOVE_RUNNING: hold(s); return;
    case MOVE_DONE: go_on(s, 3); return;
    default: break;
    }
    unsigned self = running_object(g);
    head(g, operand(g, &c, 1));
    set_field_object_steps_left(g, self, operand(g, &c, 2));
    set_field_object_move_state_tile_mode(g, self, CROSS_TILES);
    start_counted_move(s, 0xC04F03);
}

// $C0:4F0A
/* Slide in a direction for a number of steps, both worked out in variables. */
void event_cmd_move_by_variables(Snes *s)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    switch (counted_move(g)) {
    case MOVE_RUNNING: hold(s); return;
    case MOVE_DONE: go_on(s, 3); return;
    default: break;
    }
    unsigned self = running_object(g);
    head(g, (uint8_t)variable_word(g, operand(g, &c, 1)));
    set_field_object_steps_left(g, self, variable_byte(g, operand(g, &c, 2)));
    set_field_object_move_state_tile_mode(g, self, CROSS_TILES);
    start_counted_move(s, 0xC04F5A);
}

/* ---- With a velocity ------------------------------------------------------------------ */

/* A velocity move running, or just over (its marks taken away, on after the
 * command's `length` bytes); false to start one. */
static bool velocity_move_decided(Snes *s, uint16_t length)
{
    GameState *g = state_of(s);
    unsigned self = running_object(g);
    if (field_object_counted_move(g, self) == 0)
        return false;
    if (field_object_steps_left(g, self) != 0) {
        hold(s);
        return true;
    }
    set_field_object_counted_move(g, self, 0);
    set_field_object_accelerating(g, self, 0);
    go_on(s, length);
    return true;
}

/* Mark the object moving with 16-bit velocities, and end the turn on the
 * command (its offset kept in event_saved_position). */
static EventStep begin_velocity_move(GameState *g, unsigned object)
{
    set_field_object_counted_move(g, object, COUNTED_MOVE_RUNNING);
    set_field_object_accelerating(g, object, 1);
    return (EventStep){ event_saved_position(g), false };
}

// $C0:4E25
/* Launch the object at a speed, with an acceleration, for a number of
 * steps: a fall, a throw, a leap. */
static EventStep move_with_velocity(GameState *g, const Command *c)
{
    set_event_saved_position(g, c->at);
    unsigned self = running_object(g);
    set_field_object_velocity_x(g, self, (uint16_t)((field_object_velocity_x(g, self) & 0xFF00) | operand(g, c, 1)));
    set_field_object_velocity_y(g, self, (uint16_t)((field_object_velocity_y(g, self) & 0xFF00) | operand(g, c, 2)));
    set_field_object_acceleration(g, self, operand(g, c, 3));
    set_field_object_steps_left(g, self, operand(g, c, 4));
    return begin_velocity_move(g, self);
}

void event_cmd_move_with_velocity(Snes *s)
{
    if (velocity_move_decided(s, 5))
        return;
    Command c = command_here(s);
    EventStep st = move_with_velocity(state_of(s), &c);
    ORIGINAL_EFFECT(event_leave_y(s, field_current_object(state_of(s))));   /* the original's parse ends on the object */
    event_return(s, st);
}

/* The move takes `steps` steps, with the height (event_count's low byte) as
 * its acceleration and its Y velocity lowered by half of steps * height to
 * start with, so the object rises and comes down on the tile. The addition's
 * overflow is the step's. */
static bool rise(GameState *g, unsigned self, uint8_t steps)
{
    uint8_t height = (uint8_t)event_count(g);
    set_field_object_steps_left(g, self, steps);
    uint16_t product = (uint16_t)(steps * height);
    ORIGINAL_EFFECT(hw_set_multiplicand(g, steps));
    ORIGINAL_EFFECT(hw_multiply_by(g, height));   /* the original multiplies in hardware */
    set_field_object_acceleration(g, self, height);
    uint16_t lift = (uint16_t)(0 - (product >> 1));
    uint16_t vy = field_object_velocity_y(g, self);
    uint16_t sum = (uint16_t)(lift + vy);
    set_field_object_velocity_y(g, self, sum);
    return ORIGINAL_EFFECTS && overflows16(lift, vy, sum);
}

/* The steps to cover `distance` along an axis (target - own, the borrow
 * saying which way) at the object's velocity byte there: distance * 256 /
 * |velocity|, by the divider. False if the velocity is zero. */
static bool steps_along(GameState *g, uint8_t distance, bool borrow, uint8_t velocity, uint8_t *steps)
{
    if (borrow)
        distance = (uint8_t)-distance;
    uint16_t dividend = (uint16_t)(distance << 8);
    ORIGINAL_EFFECT(hw_set_dividend(g, dividend));   /* the original divides in hardware */
    if (velocity == 0)
        return false;
    if (velocity & 0x80)
        velocity = (uint8_t)-velocity;
    ORIGINAL_EFFECT(hw_divide_by(g, velocity));
    *steps = (uint8_t)(dividend / velocity);
    return true;
}

// $C0:4D4E
/* After the calls: as many steps as the X distance takes at the X velocity,
 * or failing that the Y distance at the Y velocity, or failing both the
 * height's. `self` is the object the calls left in X. */
static EventStep plan_velocity_move(Snes *s, GameState *g, unsigned self)
{
    uint8_t steps = (uint8_t)event_count(g);
    uint8_t dx = (uint8_t)(field_target_tile_x(g) - field_own_tile_x(g));
    uint8_t dy = (uint8_t)(field_target_tile_y(g) - field_own_tile_y(g));
    if (!(dx != 0 && steps_along(g, dx, field_target_tile_x(g) < field_own_tile_x(g),
                                 (uint8_t)field_object_velocity_x(g, self), &steps)) &&
        !(dy != 0 && steps_along(g, dy, field_target_tile_y(g) < field_own_tile_y(g),
                                 (uint8_t)field_object_velocity_y(g, self), &steps)))
        steps = (uint8_t)event_count(g);
    bool overflow = rise(g, self, steps);
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, overflow));
    return begin_velocity_move(g, self);
}

// $C0:4D06
/* Jump to a tile: an arc as high as the command says, at the object's speed,
 * landing on the tile. The velocity is field_set_velocity's ($C0:AC69) for
 * the direction field_direction_to_target gives, both called out to. */
void event_cmd_move_to_tile_with_velocity(Snes *s)
{
    GameState *g = state_of(s);
    Command c = command_here(s);
    if (velocity_move_decided(s, 4))
        return;
    set_event_saved_position(g, c.at);
    set_target_tile(g, operand(g, &c, 1), operand(g, &c, 2));
    set_event_count(g, (uint16_t)((event_count(g) & 0xFF00) | operand(g, &c, 3)));
    set_own_tile(g);
    if (!call_for_carry(s, 0xC04D46, 0xC0ABA2, m_flag(s, CPU_FLAG_C)) || !call_with_object(s, 0xC04D4B, 0xC0AC69))
        return;
    /* the original goes on with the object field_set_velocity leaves in X */
    if (!event_names_object(s->cpu.x)) {
        m_stop_at_own(s, 0xC04D4E, 0);
        return;
    }
    event_return(s, plan_velocity_move(s, g, slot_of(s->cpu.x)));
}

/* ---- The object's state routine, and following ---------------------------------------- */

enum { STATE_ROUTINES = 3 };

// $C0:3002
/* Let the object behave as its state says, a step at a time, for as long as
 * the script stays here: the field's own routines for a wandering townsperson,
 * a party member following the leader (state 0 $C0:9E29, and then $C0:3154 if
 * $C0:5B8D accepts its facing; 1 $C0:A26B; 2 $C0:A2CE). Nothing happens while
 * field_state_routine_hold is set. The routines are the original's, called
 * with 8-bit index registers. */
void event_cmd_run_state_routine(Snes *s)
{
    static const uint32_t sites[STATE_ROUTINES] = { 0xC03047, 0xC03037, 0xC03027 };
    static const uint32_t routines[STATE_ROUTINES] = { 0xC09E29, 0xC0A26B, 0xC0A2CE };
    GameState *g = state_of(s);
    Command c = command_here(s);
    if (field_state_routine_hold(g) != 0) {
        hold(s);
        return;
    }
    set_event_saved_position(g, c.at);
    s->cpu.p |= CPU_FLAG_X;   /* 8-bit index registers, which clears their high bytes */
    s->cpu.x &= 0x00FF;
    s->cpu.y &= 0x00FF;
    unsigned self = running_object(g);
    uint8_t state = field_object_state_state(g, self);
    if (state < STATE_ROUTINES) {
        set_field_object_steps_left(g, self, 1);
        m_jsr_dc(s, sites[state], routines[state], DC_A | DC_X | DC_N | DC_Z);
        if (!back(s, sites[state] + 3, true))
            return;
        if (state == 0) {
            m_jsr_dc(s, 0xC0304F, 0xC05B8D, DC_A | DC_X | DC_N | DC_Z);
            if (!back(s, 0xC03052, true))
                return;
            if (m_flag(s, CPU_FLAG_C)) {
                m_jsr_dc(s, 0xC03054, 0xC03154, DC_A | DC_X | DC_N | DC_Z);
                if (!back(s, 0xC03057, true))
                    return;
            }
        }
    }
    s->cpu.p &= (uint8_t)~CPU_FLAG_X;
    event_return(s, (EventStep){ event_saved_position(g), false });
}

// $C0:3546
/* Follow another object about for good: walk up to it every turn. */
void event_cmd_follow_object(Snes *s)
{
    uint16_t at = event_command_offset(s);
    ORIGINAL_EFFECT(keep_word(state_of(s), KEPT_SIXTH, at));   /* where the original keeps it across the call */
    m_jsr_dc(s, 0xC03548, 0xC05215, DC_A | DC_X | DC_N | DC_Z);
    if (back(s, 0xC0354B, false))
        event_return(s, (EventStep){ at, false });
}

// $C0:354F
/* Follow a party member about for good: walk up to them every turn, going
 * on with the turn when that does. */
void event_cmd_follow_party_member(Snes *s)
{
    uint16_t at = event_command_offset(s);
    ORIGINAL_EFFECT(keep_word(state_of(s), KEPT_SIXTH, at));   /* where the original keeps it across the call */
    m_jsr_dc(s, 0xC03551, 0xC05410, DC_A | DC_X | DC_N | DC_Z);
    if (back(s, 0xC03554, false))
        event_return(s, (EventStep){ at, m_flag(s, CPU_FLAG_C) });
}
