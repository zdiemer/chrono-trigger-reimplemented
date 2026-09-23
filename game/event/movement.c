/* Scripts moving objects about the map: walking to a tile or up to another
 * object, a number of steps in a direction, a jump or a launch, following
 * someone about, and letting an object behave as its state says.
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
 * carry set when another object blocks it (the command then turns aside);
 * field_start_step ($C0:ACFD) and field_set_velocity ($C0:AC69);
 * field_update_walk_animation ($C0:5614) and field_stand_animation
 * ($C0:568B); and field_line_up_with_tile ($C0:305D) and
 * field_line_up_with_object ($C0:30B3), carry set while a step is still
 * needed. So these commands are bridges, in three layers: what a command
 * says, decoded once; the decisions, plain C over the game's state; and the
 * calls, which set what each callee reads of the registers and say which
 * callee reads it. When the field's motion is C, the calls become C calls and
 * these commands rows of the language's table like the rest, their families
 * (to a tile, toward an object or a member; turning or keeping the facing;
 * counted) one operation each (docs/TODO.md). */
#include "cpu_io.h"
#include "event/bridge.h"
#include "event/event_data.h"
#include "event/party.h"
#include "event/variables.h"

enum { COUNTED_MOVE_RUNNING = 1, CROSS_TILES = 0x80 };
enum { LINE_UP_ON_TILE = 0x01, LINE_UP_ON_OBJECT = 0x02 };   /* field_object_arrival_flags */
enum { TURN_ASIDE = 0x30 };   /* what a blocked move turns by, directions being a byte to a turn */
enum { DISABLED = 0x80 };     /* field_object_state_state */

/* ---- The field's objects, as the moves see them ---------------------------------------
 *
 * The field's (game/field/), until its object module lands. */

static bool disabled(const GameState *g, EventObject object) { return field_object_state_state(g, object) & DISABLED; }

static bool still_moving(const GameState *g, EventObject self) { return field_object_steps_left(g, self) != 0; }

typedef enum { MOVE_FRESH, MOVE_RUNNING, MOVE_DONE } CountedMove;

/* A counted move's progress: none started, one running, or one just over
 * (whose mark is then taken away). */
static CountedMove counted_move(GameState *g, EventObject self)
{
    if (field_object_counted_move(g, self) == 0)
        return MOVE_FRESH;
    if (field_object_steps_left(g, self) != 0)
        return MOVE_RUNNING;
    set_field_object_counted_move(g, self, 0);
    return MOVE_DONE;
}

static void count_move(GameState *g, EventObject self)
{
    set_field_object_counted_move(g, self, (uint8_t)(field_object_counted_move(g, self) + 1));
}

/* A move of `steps` steps, the same many allowed across tiles. */
static void set_steps(GameState *g, EventObject self, uint8_t steps)
{
    set_field_object_steps_left(g, self, steps);
    set_field_object_move_state_tile_mode(g, self, steps);
}

/* A move in a direction, as many steps as it takes, across tiles. */
static void head(GameState *g, EventObject self, uint8_t direction, uint8_t steps)
{
    set_field_direction(g, direction);
    set_field_object_steps_left(g, self, steps);
    set_field_object_move_state_tile_mode(g, self, CROSS_TILES);
}

static bool lines_up(const GameState *g, EventObject self, uint8_t flag)
{
    return field_object_arrival_flags(g, self) & flag;
}

/* The tiles field_direction_to_target works from: the running object's, and
 * the one it heads for. */
static void aim(GameState *g, EventObject self, uint8_t x, uint8_t y)
{
    set_field_target_tile_x(g, x);
    set_field_target_tile_y(g, y);
    set_field_own_tile_x(g, field_object_x_tile(g, self));
    set_field_own_tile_y(g, field_object_y_tile(g, self));
}

/* Head for another object: its tile, and the object, kept as the target. */
static void aim_at_object(GameState *g, EventObject self, EventObject target)
{
    set_field_target_tile_x(g, field_object_x_tile(g, target));
    set_field_target_tile_y(g, field_object_y_tile(g, target));
    set_field_target_object(g, (uint16_t)(target * 2));
    set_field_own_tile_x(g, field_object_x_tile(g, self));
    set_field_own_tile_y(g, field_object_y_tile(g, self));
}

/* ---- What a move command says -------------------------------------------------------------- */

typedef struct {
    uint16_t at;       /* where the command is */
    uint8_t x, y;      /* the tile */
    uint8_t steps;     /* a counted move's */
} TileMove;

/* The move the command at Y says, decoded by the row of the opcode given
 * (the command's own, or the one whose handler the original calls). */
static TileMove tile_move(Snes *s, uint8_t opcode)
{
    EventCommand c = event_command_of(s, opcode);
    return (TileMove){ event_command_offset(s), c.tile_x, c.tile_y, c.steps };
}

typedef struct {
    uint16_t at;
    EventObject target;   /* the object, or the party member's */
    uint8_t steps;
} ObjectMove;

static ObjectMove object_move(Snes *s, uint8_t opcode)
{
    EventCommand c = event_command_of(s, opcode);
    return (ObjectMove){ event_command_offset(s), c.target, c.steps };
}

typedef struct {
    uint16_t at;
    uint8_t direction, steps;
} DirectionMove;

static DirectionMove direction_move(Snes *s, uint8_t opcode)
{
    EventCommand c = event_command_of(s, opcode);
    return (DirectionMove){ event_command_offset(s), c.direction, c.steps };
}

/* ---- The decisions --------------------------------------------------------------------- */

static bool on_target_tile(const GameState *g)
{
    return field_own_tile_y(g) == field_target_tile_y(g) && field_own_tile_x(g) == field_target_tile_x(g);
}

static bool within_one(uint8_t a, uint8_t b) { return a == b || (uint8_t)(a + 1) == b || (uint8_t)(a - 1) == b; }

static bool next_to_target(const GameState *g)
{
    return within_one(field_own_tile_y(g), field_target_tile_y(g)) &&
           within_one(field_own_tile_x(g), field_target_tile_x(g));
}

/* The screen's left and top edges, in the objects' tiles (the field keeps
 * them in 8-pixel tiles, objects stand on 16-pixel ones). */
static uint8_t screen_left(const GameState *g) { return (uint8_t)(field_screen_left(g) >> 1); }
static uint8_t screen_top(const GameState *g) { return (uint8_t)(field_screen_top(g) >> 1); }

/* How far in from the screen's edges an object must stand to be well on
 * screen: past the first two tiles, short of the last two or three. */
enum { MARGIN = 2, ACROSS = 14, DOWN = 13 };

static bool well_inside(uint8_t tile, uint8_t edge, uint8_t extent)
{
    uint8_t in = (uint8_t)(tile - edge);
    return in >= MARGIN && in < extent;
}

/* Whether the running object stands well on screen. */
static bool well_on_screen(const GameState *g)
{
    return well_inside(field_own_tile_x(g), screen_left(g), ACROSS) &&
           well_inside(field_own_tile_y(g), screen_top(g), DOWN);
}

/* A move that meets an object in the way turns aside from it. */
static uint8_t turned_aside(uint8_t direction) { return (uint8_t)(direction + TURN_ASIDE); }

/* A jump's rise: the move takes `steps` steps with the height as its
 * acceleration, and its vertical velocity starts lowered by half of steps
 * times height, so the object rises and comes down on the tile. Returns the
 * lift given. */
static uint16_t rise(GameState *g, EventObject self, uint8_t steps, uint8_t height)
{
    set_field_object_steps_left(g, self, steps);
    uint16_t product = (uint16_t)(steps * height);
    ORIGINAL_EFFECT(hw_set_multiplicand(g, steps));
    ORIGINAL_EFFECT(hw_multiply_by(g, height));   /* the original multiplies in hardware */
    set_field_object_acceleration(g, self, height);
    uint16_t lift = (uint16_t)(0 - (product >> 1));
    set_field_object_velocity_y(g, self, (uint16_t)(field_object_velocity_y(g, self) + lift));
    return lift;
}

/* The steps to cover the tiles between `own` and `target` along an axis at a
 * velocity (its low byte): distance * 256 / |velocity|. False if the velocity
 * is zero. */
static bool steps_along(GameState *g, uint8_t own, uint8_t target, uint8_t velocity, uint8_t *steps)
{
    uint8_t distance = target >= own ? (uint8_t)(target - own) : (uint8_t)(own - target);
    uint16_t dividend = (uint16_t)(distance << 8);
    ORIGINAL_EFFECT(hw_set_dividend(g, dividend));   /* the original divides in hardware */
    if (velocity == 0)
        return false;
    int8_t signed_velocity = (int8_t)velocity;   /* the way along the axis doesn't matter, the speed does */
    uint8_t speed = signed_velocity < 0 ? (uint8_t)-signed_velocity : (uint8_t)signed_velocity;
    ORIGINAL_EFFECT(hw_divide_by(g, speed));
    *steps = (uint8_t)(dividend / speed);
    return true;
}

/* A jump's steps: as many as the horizontal distance takes at the object's
 * horizontal velocity, or failing that the vertical, or failing both the
 * height's. */
static uint8_t jump_steps(GameState *g, EventObject self, uint8_t height)
{
    uint8_t steps = height;
    uint8_t own_x = field_own_tile_x(g), target_x = field_target_tile_x(g);
    uint8_t own_y = field_own_tile_y(g), target_y = field_target_tile_y(g);
    if (own_x != target_x && steps_along(g, own_x, target_x, (uint8_t)field_object_velocity_x(g, self), &steps))
        return steps;
    if (own_y != target_y && steps_along(g, own_y, target_y, (uint8_t)field_object_velocity_y(g, self), &steps))
        return steps;
    return height;
}

/* ---- The calls ------------------------------------------------------------------------- */

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
 * the port stopped. The routines read nothing of the registers but Y (the
 * command's offset, which they hand back) and V, which they carry to the
 * command's return. */
static bool call(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr_dc(s, site, target, DC_A | DC_X | DC_N | DC_Z | DC_C);
    return back(s, site + 3, false);
}

/* The same for the routines whose carry is read: field_direction_to_target
 * passes on the carry it is called with, and field_object_in_the_way and the
 * line-up routines answer in it. */
static bool call_with_carry(Snes *s, uint32_t site, uint32_t target, bool carry)
{
    m_set_flag(s, CPU_FLAG_C, carry);
    m_jsr_dc(s, site, target, DC_A | DC_X | DC_N | DC_Z);
    return back(s, site + 3, false);
}

/* The same for field_start_step and field_set_velocity, which take the
 * running object (its slot doubled) in X. */
static bool call_with_object(Snes *s, uint32_t site, uint32_t target)
{
    s->cpu.x = field_current_object(state_of(s));
    m_jsr_dc(s, site, target, DC_A | DC_N | DC_Z | DC_C);
    return back(s, site + 3, false);
}

/* The carry field_direction_to_target ($C0:ABA2) passes on to
 * field_object_in_the_way ($C0:5B95) when the command heads for a tile: that
 * of the original's comparison of the two tiles, row then column. */
static bool carry_after_tile_test(const GameState *g)
{
    if (field_own_tile_y(g) != field_target_tile_y(g))
        return field_own_tile_y(g) >= field_target_tile_y(g);
    return field_own_tile_x(g) >= field_target_tile_x(g);
}

/* ... and when it heads for an object: set when next to it, otherwise that of
 * the last comparison made, a tile short of the target on the row or column
 * that isn't within one. */
static bool carry_after_next_to_test(const GameState *g)
{
    if (!within_one(field_own_tile_y(g), field_target_tile_y(g)))
        return (uint8_t)(field_own_tile_y(g) - 1) >= field_target_tile_y(g);
    if (!within_one(field_own_tile_x(g), field_target_tile_x(g)))
        return (uint8_t)(field_own_tile_x(g) - 1) >= field_target_tile_x(g);
    return true;
}

/* The V flag an addition or a subtraction between the calls leaves, which
 * the calls after carry to the command's return: compared at those calls,
 * and read by nothing (event/leaves.h). */
static void leave_v(Snes *s, bool overflow) { m_set_flag(s, CPU_FLAG_V, overflow); }

/* ... after the screen test: that of its last subtraction. */
static void leave_v_of_screen_test(Snes *s, const GameState *g)
{
    uint8_t x = field_own_tile_x(g), left = screen_left(g);
    if (!well_inside(x, left, ACROSS)) {
        leave_v(s, overflows8(x, (uint8_t)~left, (uint8_t)(x - left)));
        return;
    }
    uint8_t y = field_own_tile_y(g), top = screen_top(g);
    leave_v(s, overflows8(y, (uint8_t)~top, (uint8_t)(y - top)));
}

/* Turn the way aside from the object in it. */
static uint8_t turn_aside(Snes *s, GameState *g)
{
    uint8_t d = (uint8_t)field_direction(g);
    uint8_t turned = turned_aside(d);
    ORIGINAL_EFFECT(leave_v(s, overflows8(d, TURN_ASIDE, turned)));
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | turned));
    return turned;
}

/* The way to the target (field_direction_to_target, called at `direction`),
 * faced; and if another object is in the way (field_object_in_the_way, at
 * `blocked`), turned aside and faced that way. The original looks the turned
 * facing up with A as field_object_in_the_way hands it back; the port goes on
 * only when its high byte is clear (as the game always has it), and leaves
 * the rest to the original. A is what the original's first lookup leaves,
 * which the callee hands back. */
static bool face_the_way(Snes *s, EventObject self, uint32_t direction, uint32_t blocked, bool carry)
{
    GameState *g = state_of(s);
    if (!call_with_carry(s, direction, 0xC0ABA2, carry))
        return false;
    uint8_t facing = facing_for_direction(g, (uint8_t)field_direction(g));
    set_field_object_facing(g, self, facing);
    s->cpu.a = facing;
    if (!call_with_carry(s, blocked, 0xC05B95, m_flag(s, CPU_FLAG_C)))
        return false;
    if (!m_flag(s, CPU_FLAG_C))
        return true;
    if (s->cpu.a >> 8)
        return false;
    set_field_object_facing(g, self, facing_for_direction(g, turn_aside(s, g)));
    return true;
}

/* The keep-facing moves' version: the way, turned aside if blocked, unfaced. */
static bool find_the_way(Snes *s, uint32_t direction, uint32_t blocked, bool carry)
{
    if (!call_with_carry(s, direction, 0xC0ABA2, carry) ||
        !call_with_carry(s, blocked, 0xC05B95, m_flag(s, CPU_FLAG_C)))
        return false;
    if (m_flag(s, CPU_FLAG_C))
        turn_aside(s, state_of(s));
    return true;
}

/* The original keeps the command's offset in event_saved_position across its
 * calls, and reads it back itself. */
static void keep_offset_across_calls(GameState *g, uint16_t at) { set_event_saved_position(g, at); }

/* The move is started; the object's turn ends on the command, which the move
 * keeps running. */
static void moving(Snes *s, uint16_t at) { event_return(s, (EventStep){ at, false }); }
static void hold(Snes *s, uint16_t at) { event_return(s, (EventStep){ at, false }); }
static void go_on(Snes *s, uint16_t next) { event_return(s, (EventStep){ next, true }); }

/* Start the move (field_start_step at `step`), with the walking animation
 * (at `walk`) or not, and end the turn. */
static void start_move(Snes *s, uint16_t at, uint32_t step)
{
    ORIGINAL_EFFECT(keep_offset_across_calls(state_of(s), at));
    if (call_with_object(s, step, 0xC0ACFD))
        moving(s, at);
}

static void start_walking(Snes *s, uint16_t at, uint32_t step, uint32_t walk)
{
    ORIGINAL_EFFECT(keep_offset_across_calls(state_of(s), at));
    if (call_with_object(s, step, 0xC0ACFD) && call(s, walk, 0xC05614))
        moving(s, at);
}

/* Arrived: the standing animation ($C0:568B at `stand`), and on after the
 * command. */
static void arrive(Snes *s, uint32_t stand, uint16_t next)
{
    if (call(s, stand, 0xC0568B))
        go_on(s, next);
}

/* Still lining up: the walking animation (at `walk`), and stay. */
static void keep_walking(Snes *s, uint16_t at, uint32_t walk)
{
    if (call(s, walk, 0xC05614))
        hold(s, at);
}

typedef enum { ON_THE_WAY, ARRIVED, LINING_UP, STOPPED } Arrival;

/* Line up on arrival (the routine at `target`, called at `at`, with the carry
 * the test that found it there leaves set). */
static Arrival line_up(Snes *s, uint32_t at, uint32_t target)
{
    if (!call_with_carry(s, at, target, true))
        return STOPPED;
    return m_flag(s, CPU_FLAG_C) ? LINING_UP : ARRIVED;
}

/* Where a move to a tile stands, having aimed at it: on the way, or on the
 * tile (lined up with it first, by `line_up_at`, if the object asks). */
static Arrival tile_arrival(Snes *s, EventObject self, uint32_t line_up_at)
{
    GameState *g = state_of(s);
    if (!on_target_tile(g))
        return ON_THE_WAY;
    return lines_up(g, self, LINE_UP_ON_TILE) ? line_up(s, line_up_at, 0xC0305D) : ARRIVED;
}

/* Next to the target object: lined up with it first, if the object asks. */
static Arrival object_arrival(Snes *s, EventObject self, uint32_t line_up_at)
{
    return lines_up(state_of(s), self, LINE_UP_ON_OBJECT) ? line_up(s, line_up_at, 0xC030B3) : ARRIVED;
}

/* ---- Toward a tile -------------------------------------------------------------------- */

/* The move-to-tile commands, and where each one's copy of the code has its
 * calls: those that turn, walk and stand, and those that keep their facing. */
enum { TO_TILE, TO_TILE_FROM_VARIABLES };
static const struct {
    uint8_t opcode;   /* whose row decodes the tile: from operands, or from variables */
    uint32_t line_up, walk_on_line, stand, direction, blocked, step, walk;
} turning_tile_moves[] = {
    [TO_TILE] = { 0x96, 0xC04F97, 0xC04FA5, 0xC04F9C, 0xC04FAB, 0xC04FBD, 0xC04FE0, 0xC04FE3 },
    [TO_TILE_FROM_VARIABLES] = { 0x97, 0xC050D6, 0xC050E4, 0xC050DB, 0xC050EA, 0xC050FC, 0xC0511F, 0xC05122 },
};

enum { TO_TILE_KEEP_FACING, TO_TILE_FROM_VARIABLES_KEEP_FACING };
static const struct {
    uint8_t opcode;
    uint32_t line_up, direction, blocked, step;
} keeping_tile_moves[] = {
    [TO_TILE_KEEP_FACING] = { 0xA0, 0xC0515F, 0xC0516D, 0xC05170, 0xC0518A },
    [TO_TILE_FROM_VARIABLES_KEEP_FACING] = { 0xA1, 0xC051E3, 0xC051F1, 0xC051F4, 0xC0520E },
};

enum { TILE_MOVE_LENGTH = 3, COUNTED_TILE_MOVE_LENGTH = 4, OBJECT_MOVE_LENGTH = 2,
       COUNTED_OBJECT_MOVE_LENGTH = 3, DIRECTION_MOVE_LENGTH = 3 };

/* Toward a tile, a step's worth at a time, until standing on it. */
static void move_to_tile(Snes *s, unsigned move)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    TileMove m = tile_move(s, turning_tile_moves[move].opcode);
    if (still_moving(g, self)) {
        hold(s, m.at);
        return;
    }
    aim(g, self, m.x, m.y);
    switch (tile_arrival(s, self, turning_tile_moves[move].line_up)) {
    case ON_THE_WAY:
        if (face_the_way(s, self, turning_tile_moves[move].direction, turning_tile_moves[move].blocked,
                         carry_after_tile_test(g))) {
            set_steps(g, self, field_object_step_length(g, self));
            start_walking(s, m.at, turning_tile_moves[move].step, turning_tile_moves[move].walk);
        }
        return;
    case LINING_UP: keep_walking(s, m.at, turning_tile_moves[move].walk_on_line); return;
    case ARRIVED: arrive(s, turning_tile_moves[move].stand, (uint16_t)(m.at + TILE_MOVE_LENGTH)); return;
    default: return;
    }
}

static void move_to_tile_keeping_facing(Snes *s, unsigned move)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    TileMove m = tile_move(s, keeping_tile_moves[move].opcode);
    if (still_moving(g, self)) {
        hold(s, m.at);
        return;
    }
    aim(g, self, m.x, m.y);
    switch (tile_arrival(s, self, keeping_tile_moves[move].line_up)) {
    case ON_THE_WAY:
        if (find_the_way(s, keeping_tile_moves[move].direction, keeping_tile_moves[move].blocked,
                         carry_after_tile_test(g))) {
            set_steps(g, self, field_object_step_length(g, self));
            start_move(s, m.at, keeping_tile_moves[move].step);
        }
        return;
    case LINING_UP: hold(s, m.at); return;
    case ARRIVED: go_on(s, (uint16_t)(m.at + TILE_MOVE_LENGTH)); return;
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
    EventObject self = running_object(g);
    TileMove m = tile_move(s, 0x9A);
    bool fresh = field_object_counted_move(g, self) == 0;
    if (!fresh && still_moving(g, self)) {
        hold(s, m.at);
        return;
    }
    if (fresh) {
        aim(g, self, m.x, m.y);
        switch (tile_arrival(s, self, 0xC05025)) {
        case ON_THE_WAY:
            if (!face_the_way(s, self, 0xC0503D, 0xC0504F, carry_after_tile_test(g)))
                return;
            set_steps(g, self, m.steps);
            count_move(g, self);
            start_walking(s, m.at, 0xC0507A, 0xC0507D);
            return;
        case LINING_UP: keep_walking(s, m.at, 0xC05037); return;
        case STOPPED: return;
        default: break;
        }
    }
    set_field_object_counted_move(g, self, 0);
    arrive(s, 0xC0502D, (uint16_t)(m.at + COUNTED_TILE_MOVE_LENGTH));
}

/* ---- Toward an object ------------------------------------------------------------------ */

// $C0:5228
/* Toward the object a step's worth at a time, until next to it. */
static void approach(Snes *s, EventObject self, ObjectMove m)
{
    GameState *g = state_of(s);
    uint16_t after = (uint16_t)(m.at + OBJECT_MOVE_LENGTH);
    if (disabled(g, m.target)) {
        arrive(s, 0xC05273, after);
        return;
    }
    aim_at_object(g, self, m.target);
    if (!next_to_target(g)) {
        if (face_the_way(s, self, 0xC05281, 0xC05293, carry_after_next_to_test(g))) {
            set_steps(g, self, field_object_step_length(g, self));
            start_walking(s, m.at, 0xC052B6, 0xC052B9);
        }
        return;
    }
    switch (object_arrival(s, self, 0xC0526E)) {
    case LINING_UP: keep_walking(s, m.at, 0xC0527B); return;
    case ARRIVED: arrive(s, 0xC05273, after); return;
    default: return;
    }
}

static void approach_unless_moving(Snes *s, uint8_t opcode)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    ObjectMove m = object_move(s, opcode);
    if (still_moving(g, self))
        hold(s, m.at);
    else
        approach(s, self, m);
}

// $C0:5215
/* Walk up to another object, and wait until next to it. */
void event_cmd_move_toward_object(Snes *s) { approach_unless_moving(s, 0x94); }

// $C0:5410
/* Walk up to a party member, and wait until next to them. */
void event_cmd_move_toward_party_member(Snes *s) { approach_unless_moving(s, 0x95); }

// $C0:52D3
/* As approach, keeping the object's facing, with no animations. */
static void approach_keeping_facing(Snes *s, uint8_t opcode)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    ObjectMove m = object_move(s, opcode);
    uint16_t after = (uint16_t)(m.at + OBJECT_MOVE_LENGTH);
    if (still_moving(g, self)) {
        hold(s, m.at);
        return;
    }
    if (disabled(g, m.target)) {
        go_on(s, after);
        return;
    }
    aim_at_object(g, self, m.target);
    if (!next_to_target(g)) {
        if (find_the_way(s, 0xC05326, 0xC05329, carry_after_next_to_test(g))) {
            set_steps(g, self, field_object_step_length(g, self));
            start_move(s, m.at, 0xC05343);
        }
        return;
    }
    switch (object_arrival(s, self, 0xC05319)) {
    case LINING_UP: hold(s, m.at); return;
    case ARRIVED: go_on(s, after); return;
    default: return;
    }
}

// $C0:52C0
/* Slide up to another object without turning or animating. */
void event_cmd_move_toward_object_keep_facing(Snes *s) { approach_keeping_facing(s, 0x9E); }

// $C0:54F5
/* Slide up to a party member without turning or animating. */
void event_cmd_move_toward_party_member_keep_facing(Snes *s) { approach_keeping_facing(s, 0x9F); }

// $C0:536B
/* One move of a limited number of steps toward the object; the command is
 * over when the move is, or on arrival. */
static void approach_counted(Snes *s, uint8_t opcode)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    ObjectMove m = object_move(s, opcode);
    uint16_t after = (uint16_t)(m.at + COUNTED_OBJECT_MOVE_LENGTH);
    switch (counted_move(g, self)) {
    case MOVE_RUNNING: hold(s, m.at); return;
    case MOVE_DONE: go_on(s, after); return;
    default: break;
    }
    if (!disabled(g, m.target)) {
        aim_at_object(g, self, m.target);
        if (!next_to_target(g)) {
            if (!face_the_way(s, self, 0xC053CA, 0xC053DC, carry_after_next_to_test(g)))
                return;
            set_steps(g, self, m.steps);
            count_move(g, self);
            start_walking(s, m.at, 0xC05406, 0xC05409);
            return;
        }
        switch (object_arrival(s, self, 0xC053B1)) {
        case LINING_UP: keep_walking(s, m.at, 0xC053C4); return;
        case STOPPED: return;
        default: break;
        }
    }
    set_field_object_counted_move(g, self, 0);
    arrive(s, 0xC053BB, after);
}

// $C0:534A
/* Walk a limited number of steps toward another object. */
void event_cmd_move_toward_object_counted(Snes *s) { approach_counted(s, 0x98); }

// $C0:550E
/* Walk a limited number of steps toward a party member. */
void event_cmd_move_toward_party_member_counted(Snes *s) { approach_counted(s, 0x99); }

/* What the original keeps of the screen test: the screen's edges. */
static void keep_screen_edges(GameState *g)
{
    keep_byte(g, KEPT_SECOND, screen_left(g));
    keep_byte(g, KEPT_THIRD, screen_top(g));
}

// $C0:5429
/* Walk toward a party member until next to them, or until well on screen:
 * someone running in to join a scene. */
void event_cmd_move_toward_party_member_until_on_screen(Snes *s)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    ObjectMove m = object_move(s, 0x8F);
    if (still_moving(g, self)) {
        hold(s, m.at);
        return;
    }
    if (!disabled(g, m.target)) {
        aim_at_object(g, self, m.target);
        if (!next_to_target(g)) {
            ORIGINAL_EFFECT(keep_screen_edges(g));
            ORIGINAL_EFFECT(leave_v_of_screen_test(s, g));
            if (!well_on_screen(g)) {
                /* off the screen's middle, every test leaves the carry
                 * field_direction_to_target passes on set */
                if (face_the_way(s, self, 0xC054B6, 0xC054C8, true)) {
                    set_steps(g, self, field_object_step_length(g, self));
                    start_walking(s, m.at, 0xC054EB, 0xC054EE);
                }
                return;
            }
        }
    }
    arrive(s, 0xC054A8, (uint16_t)(m.at + OBJECT_MOVE_LENGTH));
}

/* ---- In a direction ------------------------------------------------------------------ */

// $C0:4E73
/* Walk a number of steps in a direction, facing it. */
void event_cmd_move_in_direction(Snes *s)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    DirectionMove m = direction_move(s, 0x92);
    switch (counted_move(g, self)) {
    case MOVE_RUNNING: hold(s, m.at); return;
    case MOVE_DONE: arrive(s, 0xC04E85, (uint16_t)(m.at + DIRECTION_MOVE_LENGTH)); return;
    default: break;
    }
    head(g, self, m.direction, m.steps);
    set_field_object_facing(g, self, facing_for_direction(g, m.direction));
    count_move(g, self);
    start_walking(s, m.at, 0xC04EBD, 0xC04EC0);
}

/* Slide a number of steps in a direction without turning. */
static void slide(Snes *s, uint8_t opcode, uint32_t step)
{
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    DirectionMove m = direction_move(s, opcode);
    switch (counted_move(g, self)) {
    case MOVE_RUNNING: hold(s, m.at); return;
    case MOVE_DONE: go_on(s, (uint16_t)(m.at + DIRECTION_MOVE_LENGTH)); return;
    default: break;
    }
    head(g, self, m.direction, m.steps);
    count_move(g, self);
    start_move(s, m.at, step);
}

// $C0:4EC7
/* Slide a number of steps in a direction without turning: backing away,
 * being pushed. */
void event_cmd_move_in_direction_keep_facing(Snes *s) { slide(s, 0x9C, 0xC04F03); }

// $C0:4F0A
/* The same, the direction and the steps worked out in variables. */
void event_cmd_move_by_variables(Snes *s) { slide(s, 0x9D, 0xC04F5A); }

/* ---- With a velocity ------------------------------------------------------------------ */

/* A move with its own velocity is under way, or just over (its marks taken
 * away); false to start one. */
typedef enum { NO_LAUNCH, LAUNCHED, LANDED } Launch;

static Launch launch_state(GameState *g, EventObject self)
{
    if (field_object_counted_move(g, self) == 0)
        return NO_LAUNCH;
    if (still_moving(g, self))
        return LAUNCHED;
    set_field_object_counted_move(g, self, 0);
    set_field_object_accelerating(g, self, 0);
    return LANDED;
}

/* The object moves with its own velocity from now: a fall, a throw, a jump. */
static void mark_launched(GameState *g, EventObject self)
{
    set_field_object_counted_move(g, self, COUNTED_MOVE_RUNNING);
    set_field_object_accelerating(g, self, 1);
}

// $C0:4E25
/* Launch the object at a speed (the low bytes of its velocities), with an
 * acceleration, for a number of steps: a fall, a throw, a leap. Waits until
 * it lands. */
static EventFlow launch(GameState *g, const EventCommand *c)
{
    switch (launch_state(g, c->self)) {
    case LAUNCHED: return stay();
    case LANDED: return next_command();
    default: break;
    }
    ORIGINAL_EFFECT(keep_resume_point(g, 0));
    set_field_object_velocity_x(g, c->self, (uint16_t)((field_object_velocity_x(g, c->self) & 0xFF00) | c->velocity_x));
    set_field_object_velocity_y(g, c->self, (uint16_t)((field_object_velocity_y(g, c->self) & 0xFF00) | c->velocity_y));
    set_field_object_acceleration(g, c->self, c->acceleration);
    set_field_object_steps_left(g, c->self, c->steps);
    mark_launched(g, c->self);
    ORIGINAL_EFFECT(leave_y(field_current_object(g)));   /* the original's parse ends on the object */
    return stay();
}

/* The original keeps the jump's height in event_count's low byte across its
 * calls, and reads it back itself. */
static void keep_height(GameState *g, uint8_t height)
{
    set_event_count(g, (uint16_t)((event_count(g) & 0xFF00) | height));
}

// $C0:4D06
/* Jump to a tile: an arc as high as the command says, at the object's speed,
 * landing on the tile. The velocity is field_set_velocity's ($C0:AC69) for
 * the direction field_direction_to_target gives, both called out to. */
void event_cmd_jump_to_tile(Snes *s)
{
    enum { LENGTH = 4 };
    GameState *g = state_of(s);
    EventObject self = running_object(g);
    TileMove m = tile_move(s, 0x7A);
    uint8_t height = event_command_of(s, 0x7A).height;
    switch (launch_state(g, self)) {
    case LAUNCHED: hold(s, m.at); return;
    case LANDED: go_on(s, (uint16_t)(m.at + LENGTH)); return;
    default: break;
    }
    ORIGINAL_EFFECT(keep_offset_across_calls(g, m.at));
    aim(g, self, m.x, m.y);
    ORIGINAL_EFFECT(keep_height(g, height));
    if (!call_with_carry(s, 0xC04D46, 0xC0ABA2, m_flag(s, CPU_FLAG_C)) || !call_with_object(s, 0xC04D4B, 0xC0AC69))
        return;
    /* the original goes on with the object field_set_velocity hands back in X */
    if (!event_names_object(s->cpu.x)) {
        m_stop_at_own(s, 0xC04D4E, 0);
        return;
    }
    EventObject jumper = slot_of(s->cpu.x);
    uint16_t lift = rise(g, jumper, jump_steps(g, jumper, height), height);
    uint16_t velocity = field_object_velocity_y(g, jumper);
    ORIGINAL_EFFECT(leave_v(s, overflows16(lift, (uint16_t)(velocity - lift), velocity)));
    mark_launched(g, jumper);
    hold(s, m.at);
}

/* ---- Behaving as the state says, and following ---------------------------------------- */

enum { STATE_ROUTINES = 3 };

// $C0:3002
/* Let the object behave as its state says, a step at a time, for as long as
 * the script stays here: the field's own routines for a wandering townsperson,
 * a party member following the leader (state 0 $C0:9E29, and then $C0:3154 if
 * $C0:5B8D accepts its facing; 1 $C0:A26B; 2 $C0:A2CE). Nothing happens while
 * field_state_routine_hold is set. The routines are the original's, called
 * with 8-bit index registers. */
void event_cmd_behave(Snes *s)
{
    static const uint32_t sites[STATE_ROUTINES] = { 0xC03047, 0xC03037, 0xC03027 };
    static const uint32_t routines[STATE_ROUTINES] = { 0xC09E29, 0xC0A26B, 0xC0A2CE };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    if (field_state_routine_hold(g) != 0) {
        hold(s, at);
        return;
    }
    ORIGINAL_EFFECT(keep_offset_across_calls(g, at));
    s->cpu.p |= CPU_FLAG_X;   /* 8-bit index registers, which clears their high bytes */
    s->cpu.x &= 0x00FF;
    s->cpu.y &= 0x00FF;
    EventObject self = running_object(g);
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
    hold(s, at);
}

/* The original keeps the command's offset in its last scratch cell across the
 * call, and reads it back itself. */
static void keep_following(GameState *g, uint16_t at) { keep_word(g, KEPT_SIXTH, at); }

// $C0:3546
/* Follow another object about for good: walk up to it every turn (the walk
 * is event_cmd_move_toward_object's, which the original calls). */
void event_cmd_follow_object(Snes *s)
{
    uint16_t at = event_command_offset(s);
    ORIGINAL_EFFECT(keep_following(state_of(s), at));
    if (event_call_bridge(s, 0xC03548, 0xC05215, event_cmd_move_toward_object) && back(s, 0xC0354B, false))
        hold(s, at);
}

// $C0:354F
/* Follow a party member about for good: walk up to them every turn, going
 * on with the turn when that does. */
void event_cmd_follow_party_member(Snes *s)
{
    uint16_t at = event_command_offset(s);
    ORIGINAL_EFFECT(keep_following(state_of(s), at));
    if (event_call_bridge(s, 0xC03551, 0xC05410, event_cmd_move_toward_party_member) &&
        back(s, 0xC03554, false))
        event_return(s, (EventStep){ at, m_flag(s, CPU_FLAG_C) });
}

/* The moves' guards are their operands': an object, or a party member whose
 * object is one. */
EVENT_OPCODES(event_movement_opcodes,
    { 0x96, .bridge = event_cmd_move_to_tile, .tile_x = OPERAND(1), .tile_y = OPERAND(2), .length = 3,
      .routine = 0xC04F61 },
    { 0x97, .bridge = event_cmd_move_to_tile_from_variables, .tile_x = VARIABLE_BYTE(1),
      .tile_y = VARIABLE_BYTE(2), .length = 3, .routine = 0xC05084 },
    { 0xA0, .bridge = event_cmd_move_to_tile_keep_facing, .tile_x = OPERAND(1), .tile_y = OPERAND(2),
      .length = 3, .routine = 0xC05129 },
    { 0xA1, .bridge = event_cmd_move_to_tile_from_variables_keep_facing, .tile_x = VARIABLE_BYTE(1),
      .tile_y = VARIABLE_BYTE(2), .length = 3, .routine = 0xC05191 },
    { 0x9A, .bridge = event_cmd_move_to_tile_counted, .tile_x = OPERAND(1), .tile_y = OPERAND(2),
      .steps = OPERAND(3), .length = 4, .routine = 0xC04FEA },
    { 0x94, .bridge = event_cmd_move_toward_object, .target = OBJECT(1), .length = 2, .routine = 0xC05215 },
    { 0x95, .bridge = event_cmd_move_toward_party_member, .target = MEMBER_OBJECT(1), .length = 2,
      .routine = 0xC05410 },
    { 0x9E, .bridge = event_cmd_move_toward_object_keep_facing, .target = OBJECT(1), .length = 2,
      .routine = 0xC052C0 },
    { 0x9F, .bridge = event_cmd_move_toward_party_member_keep_facing, .target = MEMBER_OBJECT(1), .length = 2,
      .routine = 0xC054F5 },
    { 0x98, .bridge = event_cmd_move_toward_object_counted, .target = OBJECT(1), .steps = OPERAND(2), .length = 3,
      .routine = 0xC0534A },
    { 0x99, .bridge = event_cmd_move_toward_party_member_counted, .target = MEMBER_OBJECT(1),
      .steps = OPERAND(2), .length = 3, .routine = 0xC0550E },
    { 0x8F, .bridge = event_cmd_move_toward_party_member_until_on_screen, .target = MEMBER_OBJECT(1),
      .length = 2, .routine = 0xC05429 },
    { 0x92, .bridge = event_cmd_move_in_direction, .direction = OPERAND(1), .steps = OPERAND(2), .length = 3,
      .routine = 0xC04E73 },
    { 0x9C, .bridge = event_cmd_move_in_direction_keep_facing, .direction = OPERAND(1), .steps = OPERAND(2),
      .length = 3, .routine = 0xC04EC7 },
    { 0x9D, .bridge = event_cmd_move_by_variables, .direction = VARIABLE_BYTE(1), .steps = VARIABLE_BYTE(2),
      .length = 3, .routine = 0xC04F0A },
    { 0x7A, .bridge = event_cmd_jump_to_tile, .tile_x = OPERAND(1), .tile_y = OPERAND(2), .height = OPERAND(3),
      .length = 4, .routine = 0xC04D06 },
    { 0x7B, launch, .velocity_x = OPERAND(1), .velocity_y = OPERAND(2), .acceleration = OPERAND(3),
      .steps = OPERAND(4), .length = 5, .routine = 0xC04E25 },
    { 0xB0, .bridge = event_cmd_behave, .length = 1, .routine = 0xC03002 },
    { 0xB5, .bridge = event_cmd_follow_object, .target = OBJECT(1), .length = 2, .routine = 0xC03546 },
    { 0xB6, .bridge = event_cmd_follow_party_member, .target = MEMBER_OBJECT(1), .length = 2,
      .routine = 0xC0354F });
